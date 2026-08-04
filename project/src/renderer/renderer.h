#ifndef RENDERER_H
#define RENDERER_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include <bgfx/bgfx.h>

#include "window.h"
#include "transform.h"
#include "gpu_resources_handle.h"
#include "rendering_datatypes.h"
#include "camera.h"
#include "model.h"
#include "lights.h"

namespace fs = std::filesystem;

namespace krn
{
    class Registry;
    class Scene;
    class SceneManager;
    struct EngineCtx;

    // GPU-side data
    struct MeshGPU
    {
        bgfx::VertexBufferHandle vbh { bgfx::kInvalidHandle };
        bgfx::IndexBufferHandle ibh { bgfx::kInvalidHandle };
    };

    struct MaterialGPU
    {
        glm::vec4 albedo { 1.0f, 1.0f, 1.0f, 1.0f };
        glm::vec4 metallicRoughness { 0.0f, 1.0f, 0.0f, 0.0f }; // x = metallic, y = roughness
        glm::vec4 emissive { 0.0f, 0.0f, 0.0f, 0.0f };

        bgfx::TextureHandle albedoTex { bgfx::kInvalidHandle };
        bgfx::TextureHandle normalTex { bgfx::kInvalidHandle };
        bgfx::TextureHandle metallicRoughnessTex { bgfx::kInvalidHandle };
        bgfx::TextureHandle emissiveTex { bgfx::kInvalidHandle };
    };

    // Every GPU resource one loaded scene's meshes/materials own. The
    // Renderer keeps one of these per currently-loaded scene, keyed by
    // GPU_ResourcesHandle (see gpu_resources_handle.h) so multiple scenes can
    // be loaded at once; MeshHandle/MaterialHandle index within a single
    // scene's own GPU_Resources, not across scenes.
    struct GPU_Resources
    {
        std::vector<MeshGPU> meshes;
        std::vector<MaterialGPU> materials;

        // Deduplicated by resolved path so materials in this scene sharing a
        // texture file don't load/upload it twice
        std::unordered_map<std::string, bgfx::TextureHandle> textureCache;
    };

    // Output of loading a gltf scene file
    struct GLTF_LoadResult
    {
        // { mesh name, cpu mesh data }
        std::unordered_map<std::string, MeshDesc> meshes { };

        // { material name, cpu material data }
        std::unordered_map<std::string, MaterialDesc> materials { };

        struct Node
        {
            std::vector<Node> childNodes;

            glm::vec3 position { };
            glm::quat rotation { };
            glm::vec3 scale { 1.0f, 1.0f, 1.0f };

            std::string meshName { };
            std::string materialName { };
        };

        std::vector<Node> rootNodes { };

        std::unordered_map<std::string, Node&> nodeMap;
    };

    // Forward PBR renderer: one directional light, up to MAX_POINT_LIGHTS
    // point lights, no shadows. Draws every Model in the current
    // scene's registry from the primary Camera's point of view. Owns every
    // GPU resource it draws with - meshes, textures, materials, the shared
    // PBR shader program - one GPU_Resources per loaded scene, plus
    // permanent resources (program, uniforms, default textures) that live
    // for the Renderer's whole lifetime.
    class Renderer final
    {
        public:

        static constexpr std::size_t MAX_POINT_LIGHTS { 4 };

        // dataPath: engine's data root, used to locate compiled shaders
        // (dataPath/shaders/bin/<profile>/...) and, per-scene, any textures
        // MeshRenderers reference by relative path.
        explicit Renderer(Window& window, SceneManager& scenes, const fs::path& dataPath);
        ~Renderer();

        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;
        Renderer(Renderer&&) noexcept = delete;
        Renderer& operator=(Renderer&&) noexcept = delete;

        void DrawEmptyFrame();
        void DrawFrame(EngineCtx& ctx, Scene& scene, Registry& reg);

        // Scene-scoped: valid only for the GPU_ResourcesHandle of a
        // currently loaded scene (see Scene::GetResourcesHandle()); freed
        // when that scene unloads.
        MeshHandle CreateMesh(GPU_ResourcesHandle handle, const MeshDesc& data);
        MaterialHandle CreateMaterial(GPU_ResourcesHandle handle, const MaterialDesc& desc);

        private:

        // Called by the SceneManager on scene load
        void OnLoadCallback(const EngineCtx& ctx, Scene& scene, Registry& reg, std::vector<fs::path> resToLoad);

        // Called by the SceneManager on scene unload
        void OnUnloadCallback(const EngineCtx& ctx, Scene& scene, Registry& reg);

        // First entity with a Camera, preferring one with primary == true;
        // NULL_ENTITY if the scene has none
        Entity FindPrimaryCamera(Registry& reg);

        // Gathers every DirectionalLight/PointLight in the scene and pushes
        // them to the lighting uniforms, zero-filling unused point light slots
        void UpdateLights(EngineCtx& ctx, Scene& scene, Registry& reg);

        // Creates the shared shader program, uniforms and default textures.
        // Called once from the constructor, after bgfx::init().
        void InitPermanentResources(const fs::path& shaderDir);

        // Allocates a fresh GPU_Resources slot for a newly-loaded scene
        GPU_ResourcesHandle CreateSceneResources();

        // Destroys every mesh/material/texture in a scene's GPU_Resources
        // and frees the slot; called on scene unload. Default textures and
        // the shader program survive this.
        void DestroySceneResources(GPU_ResourcesHandle handle);

        bool IsHandleValid(GPU_ResourcesHandle handle) const;
        GPU_Resources& GetSceneResources(GPU_ResourcesHandle handle);

        bgfx::TextureHandle LoadTexture(GPU_Resources& resources, const fs::path& path);
        static bgfx::TextureHandle CreateSolidTexture(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a);

        MeshHandle UploadMesh(GPU_Resources& resources, const MeshDesc& data);

        // Binds a material's textures/uniforms for the next bgfx::submit()
        void BindMaterial(const GPU_Resources& resources, MaterialHandle handle) const;

        std::optional<GLTF_LoadResult> LoadGLTF(const std::vector<fs::path>& files);

        fs::path m_DataPath;

        Window& m_Window;

        // Scene-scoped GPU resources, one slot per currently-loaded scene;
        // indexed/validated via GPU_ResourcesHandle (index + generation)
        std::vector<GPU_Resources> m_SceneResources;
        std::vector<std::uint32_t> m_Generations;
        std::vector<std::uint32_t> m_FreeSlots;

        bgfx::ProgramHandle m_PBRProgram { bgfx::kInvalidHandle };

        bgfx::UniformHandle u_baseColorFactor { bgfx::kInvalidHandle };
        bgfx::UniformHandle u_metallicRoughnessFactor { bgfx::kInvalidHandle };
        bgfx::UniformHandle u_emissiveFactor { bgfx::kInvalidHandle };
        bgfx::UniformHandle s_albedo { bgfx::kInvalidHandle };
        bgfx::UniformHandle s_normal { bgfx::kInvalidHandle };
        bgfx::UniformHandle s_metallicRoughness { bgfx::kInvalidHandle };
        bgfx::UniformHandle s_emissive { bgfx::kInvalidHandle };

        // Fallback textures bound whenever a Material doesn't provide its own
        bgfx::TextureHandle m_DefaultAlbedo { bgfx::kInvalidHandle };       // flat white
        bgfx::TextureHandle m_DefaultNormal { bgfx::kInvalidHandle };       // flat tangent-space normal (0.5, 0.5, 1.0)
        bgfx::TextureHandle m_DefaultMetallicRoughness { bgfx::kInvalidHandle };
        bgfx::TextureHandle m_DefaultEmissive { bgfx::kInvalidHandle };    // flat black

        bgfx::UniformHandle u_camPos { bgfx::kInvalidHandle };
        bgfx::UniformHandle u_ambientColor { bgfx::kInvalidHandle };
        bgfx::UniformHandle u_dirLightDir { bgfx::kInvalidHandle };
        bgfx::UniformHandle u_dirLightColor { bgfx::kInvalidHandle };
        bgfx::UniformHandle u_pointLightPos { bgfx::kInvalidHandle };   // vec4 array: xyz = world pos, w = range
        bgfx::UniformHandle u_pointLightColor { bgfx::kInvalidHandle }; // vec4 array: rgb = color * intensity
    };
}

#endif // !RENDERER_H
