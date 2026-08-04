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
        std::vector<size_t> primitives { };
        std::vector<uint32_t> materials { };
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

    // Every GPU resource one loaded scene's meshes/materials own
    struct GPU_Resources
    {
        std::vector<MeshGPU> meshes;
        std::vector<MaterialGPU> materials;

        // Deduplicated by resolved path
        std::unordered_map<std::string, bgfx::TextureHandle> textureCache;
    };

    // Output of loading a gltf scene file
    struct GLTF_LoadResult
    {
        std::vector<MeshDesc> meshes { };
        std::vector<MaterialDesc> materials { };

        std::unordered_map<std::string, size_t> nameToMeshId { };
        std::unordered_map<std::string, size_t> nameToMaterialId { };

        struct Node
        {
            size_t parentNodeId;
            std::vector<size_t> childNodeIds;

            glm::vec3 position { };
            glm::quat rotation { };
            glm::vec3 scale { 1.0f, 1.0f, 1.0f };

            size_t meshName { };
            size_t materialName { };
        };

        std::vector<Node> nodes { };

        std::unordered_map<std::string, size_t> nameToNodeId;
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

        MeshGPU& GetMesh(Scene& scene, MeshHandle handle);
        MaterialGPU& GetMaterial(Scene& scene, uint32_t id);



        private:

        // ----------------------------
        // --- Resource managemenet ---
        // ----------------------------

        // Called by the SceneManager on scene load
        void OnLoadCallback(const EngineCtx& ctx, Scene& scene, Registry& reg, std::vector<fs::path> resToLoad);

        // Called by the SceneManager on scene unload
        void OnUnloadCallback(const EngineCtx& ctx, Scene& scene, Registry& reg);

        // Creates the shared shader program, uniforms and default textures
        void InitPermanentResources(const fs::path& shaderDir);

        // Allocates a fresh GPU_Resources slot for a newly-loaded scene
        GPU_ResourcesHandle CreateSceneResources();

        // Destroys every mesh/material/texture in a scene's GPU_Resources
        // and frees the slot; called on scene unload
        void DestroySceneResources(GPU_ResourcesHandle handle);

        // Checks if handle is valid and not stale
        bool IsHandleValid(GPU_ResourcesHandle handle) const;

        // Access GPU_Resources by handle
        GPU_Resources& GetSceneResources(GPU_ResourcesHandle handle);

        // Builds a MeshGPU and adds it to resources
        MeshHandle UploadMesh(GPU_Resources& resources, const MeshDesc& data);

        // Builds one MaterialGPU and adds it to resources
        uint32_t UploadMaterial(GPU_Resources& resources, const MaterialDesc& desc);



        // ---------------
        // --- Helpers ---
        // ---------------

        // First entity with a Camera, preferring one with primary == true
        Entity FindPrimaryCamera(Registry& reg);

        // Gathers every DirectionalLight/PointLight in the scene and pushes
        // them to the lighting uniforms, zero-filling unused point light slots
        void UpdateLights(EngineCtx& ctx, Scene& scene, Registry& reg);

        // Loads one texture from a file
        bgfx::TextureHandle LoadTexture(GPU_Resources& resources, const fs::path& path) const;

        // Binds a material's textures/uniforms for the next bgfx::submit()
        void BindMaterial(const GPU_Resources& resources, uint32_t materialIndex) const;



        // --------------------
        // --- GLTF loading ---
        // --------------------

        std::optional<GLTF_LoadResult> LoadGLTF(const std::vector<fs::path>& files);



        // ---------------
        // --- Members ---
        // ---------------

        fs::path m_DataPath;

        Window& m_Window;

        std::vector<GPU_Resources> m_SceneResources;
        std::vector<uint32_t> m_Generations;
        std::vector<uint32_t> m_FreeSlots;

        bgfx::ProgramHandle m_PBRProgram { bgfx::kInvalidHandle };

        bgfx::UniformHandle u_baseColorFactor { bgfx::kInvalidHandle };
        bgfx::UniformHandle u_metallicRoughnessFactor { bgfx::kInvalidHandle };
        bgfx::UniformHandle u_emissiveFactor { bgfx::kInvalidHandle };
        bgfx::UniformHandle s_albedo { bgfx::kInvalidHandle };
        bgfx::UniformHandle s_normal { bgfx::kInvalidHandle };
        bgfx::UniformHandle s_metallicRoughness { bgfx::kInvalidHandle };
        bgfx::UniformHandle s_emissive { bgfx::kInvalidHandle };

        bgfx::TextureHandle m_DefaultAlbedo { bgfx::kInvalidHandle }; // flat white
        bgfx::TextureHandle m_DefaultNormal { bgfx::kInvalidHandle }; // flat tangent-space normal (0.5, 0.5, 1.0)
        bgfx::TextureHandle m_DefaultMetallicRoughness { bgfx::kInvalidHandle };
        bgfx::TextureHandle m_DefaultEmissive { bgfx::kInvalidHandle }; // flat black

        bgfx::UniformHandle u_camPos { bgfx::kInvalidHandle };
        bgfx::UniformHandle u_ambientColor { bgfx::kInvalidHandle };
        bgfx::UniformHandle u_dirLightDir { bgfx::kInvalidHandle };
        bgfx::UniformHandle u_dirLightColor { bgfx::kInvalidHandle };
        bgfx::UniformHandle u_pointLightPos { bgfx::kInvalidHandle }; // vec4 array: xyz = world pos, w = range
        bgfx::UniformHandle u_pointLightColor { bgfx::kInvalidHandle }; // vec4 array: rgb = color * intensity
    };
}

#endif // !RENDERER_H
