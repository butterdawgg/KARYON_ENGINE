#ifndef RENDERER_H
#define RENDERER_H

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include <bgfx/bgfx.h>

#include "window.h"
#include "transform.h"
#include "rendering_datatypes.h"
#include "camera.h"
#include "model.h"
#include "lights.h"
#include "shadow_map.h"
#include "parser.h"

namespace fs = std::filesystem;

namespace krn
{
    class Registry;
    class Scene;
    class SceneManager;
    struct EngineCtx;

    // GPU-side data
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

    // GPU buffers of one gltf mesh
    struct MeshBuffers
    {
        bgfx::VertexBufferHandle vbh { bgfx::kInvalidHandle };
        bgfx::IndexBufferHandle ibh { bgfx::kInvalidHandle };
    };

    // What one scene load already uploaded, indexed [fileId][meshId/materialId].
    // Gltf meshes/materials are uploaded on first use by a Model, so nothing
    // ends up on the GPU without a Model referencing it (and freeing it on unload).
    struct GLTF_UploadCache
    {
        std::vector<std::vector<std::optional<MeshBuffers>>> fileMeshes { };
        std::vector<std::vector<std::optional<uint32_t>>> fileMaterialIds { };
        std::optional<uint32_t> defaultMaterialId { };
    };

    // Forward PBR renderer: one directional light with cascaded shadow maps,
    // up to MAX_POINT_LIGHTS point lights each with an omnidirectional cube
    // shadow map
    class Renderer final
    {
        public:

        // Bgfx executes views in ascending id order, so every shadow-caster
        // view needs a lower id than the main color pass it feeds:
        // 0..NUM_CASCADES-1 = directional cascade casters,
        // NUM_CASCADES..NUM_CASCADES+MAX_POINT_LIGHTS*6-1 = point light cube
        // face casters (POINT_SHADOW_VIEW_BASE + lightIndex*6 + face),
        // MAIN_VIEW_ID = the main color pass.
        static constexpr bgfx::ViewId POINT_SHADOW_VIEW_BASE { static_cast<bgfx::ViewId>(NUM_CASCADES) };
        static constexpr bgfx::ViewId MAIN_VIEW_ID {
            static_cast<bgfx::ViewId>(NUM_CASCADES + MAX_POINT_LIGHTS * CUBE_FACE_COUNT) };

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

        MaterialGPU& GetMaterial(uint32_t id);



        private:

        // ----------------------------
        // --- Resource managemenet ---
        // ----------------------------

        // Called by the SceneManager on scene load
        void OnResourceLoad(const EngineCtx& ctx, Scene& scene, Registry& reg, std::vector<fs::path>& resToLoad);

        // Called by the SceneManager on scene unload
        void OnUnload(const EngineCtx& ctx, Scene& scene, Registry& reg);

        void InitBGFX();

        // Creates the shared shader program, uniforms and default textures
        void InitPermanentResources(const fs::path& shaderDir);

        // Frees the buffers/materials of every Model in reg and removes the
        // Models; reg is one scene's registry, so only that scene's resources go
        void DestroySceneResources(Registry& reg);

        // Creates a mesh's vertex/index buffers
        MeshBuffers UploadMesh(const MeshDesc& data);

        // Builds one MaterialGPU in the material pool and returns its id
        uint32_t UploadMaterial(const MaterialDesc& desc);

        // Returns a material's pool slot and releases its textures
        void FreeMaterial(uint32_t id);



        // ---------------
        // --- Helpers ---
        // ---------------

        // Gathers every DirectionalLight/PointLight in the scene into the output params
        void GatherLights(EngineCtx& ctx, Scene& scene, Registry& reg,
            glm::vec4& dirDir, glm::vec4& dirColor,
            glm::vec4(&pointPos)[MAX_POINT_LIGHTS], glm::vec4(&pointColor)[MAX_POINT_LIGHTS],
            std::size_t& activePointLightCount) const;

        // Loads one texture from a file (or the cache) and takes a reference to it
        bgfx::TextureHandle LoadTexture(const fs::path& path);

        // Drops a reference taken by LoadTexture, destroying the texture on the last one
        void ReleaseTexture(bgfx::TextureHandle handle);

        // Binds a material's textures/uniforms for the next bgfx::submit()
        void BindMaterial(uint32_t materialIndex) const;

        // Binds the uniforms every draw call this frame needs
        void BindGlobalUniforms(const glm::vec4& camPos, const glm::vec4& ambient,
            const glm::vec4& dirDir, const glm::vec4& dirColor,
            const glm::vec4 (&pointPos)[MAX_POINT_LIGHTS], const glm::vec4 (&pointColor)[MAX_POINT_LIGHTS]) const;

        // Binds the cascade shadow map matrix array + all directional/point shadow samplers
        void BindShadowUniforms() const;

        // Walks every visible Model's every primitive
        void DrawPrimitives(EngineCtx& ctx, Registry& reg,
            const std::function<void(uint32_t materialIndex)>& onPrimitive);



        // --------------------
        // --- GLTF loading ---
        // --------------------

        // Loads scene data from a .gltf/.glb file
        std::optional<GLTF_LoadResult> LoadGLTF(const std::vector<fs::path>& files);

        // Processes a GLTF_LoadResult according to LoadMetadata and ModelDesc
        void PreprocessModels(GLTF_LoadResult& result, Registry& reg, const EngineCtx& ctx, GLTF_UploadCache& cache);

        // Recursive helper function to load a GLTF node
        void LoadGLTFNode(const EngineCtx& ctx, Entity entity, LoadMetadata& metadata,
            Registry& reg, GLTF_LoadResult::File& file, std::size_t fileId, GLTF_LoadResult::File::Node& node,
            uint32_t nodeId, bool hasEntity, bool loadChildren, GLTF_UploadCache& cache);

        // Points model at a gltf mesh's buffers (uploaded on first use). Primitive i
        // uses materialOverrides[min(i, size - 1)] if any are given, else its own gltf material
        void SetModelMesh(Model& model, GLTF_UploadCache& cache, const GLTF_LoadResult::File& file,
            std::size_t fileId, std::size_t meshId, const std::vector<uint32_t>& materialOverrides = { });

        // Returns the pool id of a gltf material (NO_GLTF_MATERIAL = default material), uploading it on first use
        uint32_t GetGLTFMaterial(GLTF_UploadCache& cache, const GLTF_LoadResult::File& file,
            std::size_t fileId, uint32_t materialId);


        // ---------------
        // --- Members ---
        // ---------------

        fs::path m_DataPath;

        Window& m_Window;
        SceneManager& m_Scenes;

        // Material pool shared by all scenes, Primitives refer to it by id
        std::vector<MaterialGPU> m_Materials;
        std::vector<uint32_t> m_FreeMaterialIds;

        // Loaded textures, deduplicated by resolved path and refcounted per
        // referencing material. m_TextureRefs is indexed by TextureHandle::idx
        struct TextureRef
        {
            std::string path { };
            uint32_t count { 0 };
        };

        std::unordered_map<std::string, bgfx::TextureHandle> m_TextureCache;
        std::vector<TextureRef> m_TextureRefs;

        bgfx::ProgramHandle m_PBRProgram { bgfx::kInvalidHandle };
        bgfx::ProgramHandle m_ShadowCasterProgram { bgfx::kInvalidHandle };
        bgfx::ProgramHandle m_PointShadowCasterProgram { bgfx::kInvalidHandle };

        ShadowMapResources m_ShadowMap { };
        PointShadowMapResources m_PointShadow { };

        bgfx::UniformHandle u_shadowMapMtx { bgfx::kInvalidHandle };
        bgfx::UniformHandle s_shadowMap[NUM_CASCADES] { };
        bgfx::UniformHandle s_pointShadowMap[MAX_POINT_LIGHTS] { };
        bgfx::UniformHandle u_lightOrigin { bgfx::kInvalidHandle }; // point shadow caster only: xyz = light pos, w = range

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
