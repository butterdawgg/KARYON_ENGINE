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
#include "parser.h" // for LoadMetadata, used by LoadGLTFNode below

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
        struct File
        {
            std::vector<MeshDesc> meshes { };
            std::vector<MaterialDesc> materials { };

            std::unordered_map<std::string, size_t> nameToMeshId { };
            std::unordered_map<std::string, size_t> nameToMaterialId { };

            struct Node
            {
                // Sentinel for "no parent" / "no mesh" / "no material
                // override" - 0 is a valid index, so it can't double as
                // the empty value
                static constexpr std::size_t NO_INDEX { static_cast<std::size_t>(-1) };

                std::size_t parentNodeId { NO_INDEX };
                std::vector<size_t> childNodeIds { };

                glm::vec3 position { };
                glm::quat rotation { 1.0f, 0.0f, 0.0f, 0.0f };
                glm::vec3 scale { 1.0f, 1.0f, 1.0f };

                std::size_t meshId { NO_INDEX };
                std::size_t materialId { NO_INDEX };
            };

            std::vector<Node> nodes { };

            std::unordered_map<std::string, size_t> nameToNodeId;
        };

        std::vector<File> files;
        std::unordered_map<fs::path, size_t> pathToFileId;
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
        void OnLoadCallback(const EngineCtx& ctx, Scene& scene, Registry& reg, std::vector<fs::path>& resToLoad);

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

        // Gathers every DirectionalLight/PointLight in the scene into the
        // output params, zero-filling unused point light slots. Doesn't touch
        // bgfx uniforms directly - see BindGlobalUniforms for why.
        void GatherLights(EngineCtx& ctx, Scene& scene, Registry& reg,
            glm::vec4& dirDir, glm::vec4& dirColor,
            glm::vec4 (&pointPos)[MAX_POINT_LIGHTS], glm::vec4 (&pointColor)[MAX_POINT_LIGHTS]) const;

        // Loads one texture from a file
        bgfx::TextureHandle LoadTexture(GPU_Resources& resources, const fs::path& path) const;

        // Binds a material's textures/uniforms for the next bgfx::submit()
        void BindMaterial(const GPU_Resources& resources, uint32_t materialIndex) const;

        // Binds the uniforms every draw call this frame needs (camera
        // position, ambient, lights). bgfx::submit()'s default flags
        // (BGFX_DISCARD_ALL) discard uniform bindings after every single
        // call - BGFX_DISCARD_STATE, included in that mask, is documented as
        // "discard state AND UNIFORM BINDINGS" - so unlike setViewTransform
        // (persistent for the whole view), these have to be re-bound before
        // every bgfx::submit(), not just once per frame. Skipping that left
        // every primitive after the first frame's one bgfx::submit() reading
        // stale/zeroed light+ambient uniforms - i.e. rendering fully unlit.
        void BindGlobalUniforms(const glm::vec4& camPos, const glm::vec4& ambient,
            const glm::vec4& dirDir, const glm::vec4& dirColor,
            const glm::vec4 (&pointPos)[MAX_POINT_LIGHTS], const glm::vec4 (&pointColor)[MAX_POINT_LIGHTS]) const;



        // --------------------
        // --- GLTF loading ---
        // --------------------

        // Loads scene data from a .gltf/.glb file, returns { filename, loaded data }
        std::optional<GLTF_LoadResult> LoadGLTF(const std::vector<fs::path>& files);

        // Processes a GLTF_LoadResult according to LoadMetadata and ModelDesc.
        // fileMaterialIds/fileMeshHandles map each file's local material/mesh
        // index to its already-uploaded slot in res (see OnLoadCallback)
        void PreprocessModels(GLTF_LoadResult& result, Registry& reg, const EngineCtx& ctx, GPU_Resources& res,
            const std::vector<std::vector<uint32_t>>& fileMaterialIds,
            const std::vector<std::vector<MeshHandle>>& fileMeshHandles);

        // Recursive helper function to load a GLTF node. meshHandles is the
        // owning file's slice of fileMeshHandles, already resolved to global
        // MeshHandles
        void LoadGLTFNode(const EngineCtx& ctx, Entity entity, LoadMetadata& metadata,
            Registry& reg, GLTF_LoadResult::File& file, GLTF_LoadResult::File::Node& node,
            uint32_t nodeId, bool hasEntity, bool loadChildren, const std::vector<MeshHandle>& meshHandles);


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
