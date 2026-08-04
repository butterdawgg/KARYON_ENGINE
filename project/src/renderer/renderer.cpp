#include "renderer.h"
#include "registry.h"
#include "engine_ctx.h"
#include "scene_manager.h"


#include <algorithm>
#include <cassert>
#include <fstream>

#include <glm/gtc/type_ptr.hpp>

#include <bx/allocator.h>
#include <bx/error.h>
#include <bx/math.h>
#include <bimg/decode.h>

#include <fastgltf/core.hpp>

#include <stdexcept>

#include "utils.h"

namespace krn
{
    // Private helpers
    namespace
    {
        std::vector<std::uint8_t> ReadFile(const fs::path& path)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);

            if (!file.is_open())
                return { };

            const std::streamsize size { file.tellg() };
            file.seekg(0, std::ios::beg);

            std::vector<std::uint8_t> data(static_cast<std::size_t>(size));

            if (!file.read(reinterpret_cast<char*>(data.data()), size))
                return { };

            return data;
        }

        bgfx::VertexLayout MakeVertexLayout()
        {
            bgfx::VertexLayout layout;

            layout.begin()
                .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
                .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
                .add(bgfx::Attrib::Tangent, 4, bgfx::AttribType::Float)
                .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
                .end();

            return layout;
        }

        // Directory name bgfx_compile_shaders() places each profile's output
        // under - must stay in sync with the runtime-type switch below
        const char* ShaderProfileDir()
        {
            switch (bgfx::getRendererType())
            {
                case bgfx::RendererType::Direct3D11: return "dxbc";
                case bgfx::RendererType::Direct3D12: return "dxil";
                case bgfx::RendererType::Metal:      return "metal";
                case bgfx::RendererType::OpenGL:     return "glsl";
                case bgfx::RendererType::OpenGLES:   return "essl";
                case bgfx::RendererType::Vulkan:     return "spirv";
                default:                             return "dxbc";
            }
        }

        bgfx::ShaderHandle LoadShader(const fs::path& dir, const char* name)
        {
            const fs::path path { dir / ShaderProfileDir() / (std::string(name) + ".sc.bin") };

            std::vector<std::uint8_t> data { ReadFile(path) };

            if (data.empty())
            {
                logError("Failed to read shader: {}", path.string());

                return BGFX_INVALID_HANDLE;
            }

            bgfx::ShaderHandle handle { bgfx::createShader(bgfx::copy(data.data(),
                static_cast<uint32_t>(data.size()))) };

            bgfx::setName(handle, name);

            return handle;
        }

        // Creates a single color 1x1 texture
        bgfx::TextureHandle CreateSolidTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
        {
            const std::uint8_t pixel[4] { r, g, b, a };

            return bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                BGFX_SAMPLER_NONE, bgfx::copy(pixel, sizeof(pixel)));
        }
    }



    // ----------------------
    // --- Initialization ---
    // ----------------------

    Renderer::Renderer(Window& window, SceneManager& scenes, const fs::path& dataPath) :
        m_DataPath(dataPath),
        m_Window(window)
    {
        // Init bgfx
        bgfx::PlatformData platformData { };
        platformData.nwh = window.GetNativeHandle();
        platformData.ndt = window.GetNativeDisplay();

        const WindowSize size { window.GetSizeInPixels() };

        bgfx::Init init { };
        init.type = bgfx::RendererType::Count; // let bgfx pick the best backend for this platform
        init.resolution.width = static_cast<uint32_t>(size.width);
        init.resolution.height = static_cast<uint32_t>(size.height);
        init.resolution.reset = BGFX_RESET_VSYNC;
        init.platformData = platformData;

        if (!bgfx::init(init))
        {
            throw std::runtime_error("bgfx::init failed");
        }

        bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030ff, 1.0f, 0);
        bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(size.width), static_cast<uint16_t>(size.height));

        // Everything here is a bgfx:: call, so it can only run after
        // bgfx::init() above - hence a separate method instead of doing this
        // in the member initializer list (members would construct before
        // bgfx::init() gets a chance to run).
        InitPermanentResources(dataPath / "shaders" / "bin");

        u_camPos = bgfx::createUniform("u_camPos", bgfx::UniformType::Vec4);
        u_ambientColor = bgfx::createUniform("u_ambientColor", bgfx::UniformType::Vec4);
        u_dirLightDir = bgfx::createUniform("u_dirLightDir", bgfx::UniformType::Vec4);
        u_dirLightColor = bgfx::createUniform("u_dirLightColor", bgfx::UniformType::Vec4);
        u_pointLightPos = bgfx::createUniform("u_pointLightPos", bgfx::UniformType::Vec4, MAX_POINT_LIGHTS);
        u_pointLightColor = bgfx::createUniform("u_pointLightColor", bgfx::UniformType::Vec4, MAX_POINT_LIGHTS);

        // Add OnLoad/OnUnload callbacks
        scenes.AddInternalLoadCallback(
            [this] (const EngineCtx& ctx, Scene& scene, Registry& reg, std::vector<fs::path> resToLoad)
            {
                OnLoadCallback(ctx, scene, reg, resToLoad);
            });

        scenes.AddInternalUnloadCallback(
            [this] (const EngineCtx& ctx, Scene& scene, Registry& reg)
            {
                OnUnloadCallback(ctx, scene, reg);
            });
    }



    // ---------------
    // --- Cleanup ---
    // ---------------

    Renderer::~Renderer()
    {
        bgfx::destroy(u_camPos);
        bgfx::destroy(u_ambientColor);
        bgfx::destroy(u_dirLightDir);
        bgfx::destroy(u_dirLightColor);
        bgfx::destroy(u_pointLightPos);
        bgfx::destroy(u_pointLightColor);

        // Release every scene's GPU resources while bgfx is still up
        for (uint32_t i = 0; i < m_SceneResources.size(); ++i)
        {
            const bool isFree { std::find(m_FreeSlots.begin(), m_FreeSlots.end(), i) != m_FreeSlots.end() };

            if (isFree)
                continue;

            DestroySceneResources(GPU_ResourcesHandle { i, m_Generations[i] });
        }

        bgfx::destroy(m_DefaultAlbedo);
        bgfx::destroy(m_DefaultNormal);
        bgfx::destroy(m_DefaultMetallicRoughness);
        bgfx::destroy(m_DefaultEmissive);

        bgfx::destroy(s_albedo);
        bgfx::destroy(s_normal);
        bgfx::destroy(s_metallicRoughness);
        bgfx::destroy(s_emissive);

        bgfx::destroy(u_baseColorFactor);
        bgfx::destroy(u_metallicRoughnessFactor);
        bgfx::destroy(u_emissiveFactor);

        bgfx::destroy(m_PBRProgram);

        bgfx::shutdown();
    }



    // ------------------------
    // --- Public interface ---
    // ------------------------

    void Renderer::DrawEmptyFrame()
    {
        // Ensure view 0 is submitted even on frames where nothing else touches it
        bgfx::touch(0);

        bgfx::frame();
    }

    void Renderer::DrawFrame(EngineCtx& ctx, Scene& scene, Registry& reg)
    {
        const Entity cameraEntity { FindPrimaryCamera(reg) };

        if (cameraEntity == NULL_ENTITY)
        {
            DrawEmptyFrame();

            return;
        }

        ctx.transforms.Ensure(reg, cameraEntity);

        const Camera& camera { reg.Get<Camera>(cameraEntity) };
        const Transform& camTransform { reg.Get<Transform>(cameraEntity) };

        const glm::vec3 eye { camTransform.GetWorldPosition() };
        const glm::vec3 forward { camTransform.GetWorldRotation() * glm::vec3(0.0f, 0.0f, 1.0f) };
        const glm::vec3 up { camTransform.GetWorldRotation() * glm::vec3(0.0f, 1.0f, 0.0f) };
        const glm::vec3 at { eye + forward };

        float view[16];
        float proj[16];

        bx::mtxLookAt(view, bx::Vec3(eye.x, eye.y, eye.z), bx::Vec3(at.x, at.y, at.z), bx::Vec3(up.x, up.y, up.z));

        const WindowSize size { m_Window.GetSizeInPixels() };
        const float aspect { size.height != 0
            ? static_cast<float>(size.width) / static_cast<float>(size.height) : 1.0f };

        // bx::mtxProj takes fovy in degrees despite its header comment saying
        // radians (confirmed empirically: passing radians(60) here produced a
        // ~0.5 degree effective FOV - everything off-screen)
        bx::mtxProj(proj, camera.FOV, aspect, camera.nearPlane, camera.farPlane,
            bgfx::getCaps()->homogeneousDepth);

        bgfx::setViewTransform(0, view, proj);
        bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(size.width), static_cast<uint16_t>(size.height));

        const glm::vec4 camPos { eye, 0.0f };
        bgfx::setUniform(u_camPos, &camPos);

        const glm::vec4 ambient { 0.03f, 0.03f, 0.03f, 0.0f };
        bgfx::setUniform(u_ambientColor, &ambient);

        UpdateLights(ctx, scene, reg);

        // Depth test + write, colour + alpha write, MSAA - deliberately no
        // BGFX_STATE_CULL_* bit: this is a "simple" renderer, and getting
        // triangle winding wrong on any mesh would silently cull the wrong
        // side. Double-sided drawing costs some fill-rate but is always
        // correct; culling can be turned on later per-mesh.
        constexpr std::uint64_t STATE { BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
            | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA };

        GPU_Resources& resources { GetSceneResources(scene.GetResourcesHandle()) };

        auto models = reg.View<Model>();
        const auto& entities = models.Entities();

        std::size_t i = 0;

        for (auto& model : models)
        {
            const Entity entity { entities[i++] };

            if (model.meshHandle == INVALID_MESH)
                continue;

            const MeshGPU& mesh { resources.meshes[model.meshHandle] };

            ctx.transforms.Ensure(reg, entity);

            const glm::mat4 worldMat { reg.Get<Transform>(entity).GetWorldMatrix() };

            for (size_t start = 0, primitiveId = 0; primitiveId < mesh.primitives.size(); ++primitiveId)
            {
                if (primitiveId >= mesh.materials.size())
                {
                    logWarning("Model on entity {} is missing a material for primitive {}, skipping",
                        entity, primitiveId);

                    continue;
                }

                const size_t count { mesh.primitives[primitiveId] };
                const uint32_t matId { mesh.materials[primitiveId] };

                // Transform/buffers/state only apply to the next submit() -
                // bgfx resets them after each one, so they're re-set per
                // primitive rather than once per model.
                bgfx::setTransform(glm::value_ptr(worldMat));

                bgfx::setVertexBuffer(0, mesh.vbh);
                bgfx::setIndexBuffer(mesh.ibh, start, count);

                BindMaterial(resources, matId);

                bgfx::setState(STATE);

                bgfx::submit(0, m_PBRProgram);

                start += count;
            }
        }

        bgfx::frame();
    }

    MeshGPU& Renderer::GetMesh(Scene& scene, MeshHandle handle)
    {
        auto& res { GetSceneResources(scene.GetResourcesHandle()) };

        assert(handle >= res.meshes.size() && "tried to access invalid mesh handle");

        return res.meshes[handle];
    }

    MaterialGPU& Renderer::GetMaterial(Scene& scene, uint32_t id)
    {
        auto& res { GetSceneResources(scene.GetResourcesHandle()) };

        assert(id >= res.materials.size() && "tried to access invalid material by id");

        return res.materials[id];
    }



    // ---------------------------
    // --- Resource management ---
    // ---------------------------

    void Renderer::OnLoadCallback(const EngineCtx& ctx, Scene& scene, Registry& reg, std::vector<fs::path> resToLoad)
    {
        // TODO: load gltf files, create entities and give them each a Model, Transform,
        // LoadMetadata (with correct parent id that TransformManager will process later)
        // Processs ModelDesc components afterwards

        // Create handle and give it to the scene
        const GPU_ResourcesHandle handle { CreateSceneResources() };
        scene.SetResourcesHandle(handle);

        GPU_Resources& res { GetSceneResources(handle) };

        // TODO: for each ModelDesc, resolve gltfFileName/gtlfMeshName into a
        // MeshDesc (via LoadGLTF), UploadMaterial() each entry in
        // materialDesc into res, fill the resulting MeshDesc::materials with
        // those indices, then CreateMesh(handle, meshDesc) and store the
        // returned MeshHandle on that entity's Model.
    }

    void Renderer::OnUnloadCallback(const EngineCtx& ctx, Scene& scene, Registry& reg)
    {
        DestroySceneResources(scene.GetResourcesHandle());

        scene.SetResourcesHandle(GPU_ResourcesHandle { });
    }

    void Renderer::InitPermanentResources(const fs::path& shaderDir)
    {
        const bgfx::ShaderHandle vsh { LoadShader(shaderDir, "vs_pbr") };
        const bgfx::ShaderHandle fsh { LoadShader(shaderDir, "fs_pbr") };

        m_PBRProgram = bgfx::createProgram(vsh, fsh, true);

        u_baseColorFactor = bgfx::createUniform("u_baseColorFactor", bgfx::UniformType::Vec4);
        u_metallicRoughnessFactor = bgfx::createUniform("u_metallicRoughnessFactor", bgfx::UniformType::Vec4);
        u_emissiveFactor = bgfx::createUniform("u_emissiveFactor", bgfx::UniformType::Vec4);

        s_albedo = bgfx::createUniform("s_albedo", bgfx::UniformType::Sampler);
        s_normal = bgfx::createUniform("s_normal", bgfx::UniformType::Sampler);
        s_metallicRoughness = bgfx::createUniform("s_metallicRoughness", bgfx::UniformType::Sampler);
        s_emissive = bgfx::createUniform("s_emissive", bgfx::UniformType::Sampler);

        // Multiplicative identity for anything combined with a *Factor uniform
        // in the shader (albedo/emissive: rgb * texture; metallic/roughness:
        // factor * texture channel), so an unset texture leaves the factor
        // value unchanged rather than zeroing it out.
        m_DefaultAlbedo = CreateSolidTexture(255, 255, 255, 255);
        m_DefaultNormal = CreateSolidTexture(128, 128, 255, 255); // flat tangent-space normal (0, 0, 1)
        m_DefaultMetallicRoughness = CreateSolidTexture(255, 255, 255, 255); // G = roughness, B = metallic
        m_DefaultEmissive = CreateSolidTexture(255, 255, 255, 255);
    }

    GPU_ResourcesHandle Renderer::CreateSceneResources()
    {
        uint32_t index;

        if (!m_FreeSlots.empty())
        {
            index = m_FreeSlots.back();
            m_FreeSlots.pop_back();
        }
        else
        {
            index = static_cast<uint32_t>(m_SceneResources.size());
            m_SceneResources.emplace_back();
            m_Generations.push_back(0);
        }

        return GPU_ResourcesHandle { index, m_Generations[index] };
    }

    void Renderer::DestroySceneResources(GPU_ResourcesHandle handle)
    {
        if (!IsHandleValid(handle))
        {
            assert(false && "Renderer::DestroySceneResources called with an invalid or stale handle");

            return;
        }

        GPU_Resources& resources { m_SceneResources[handle.index] };

        for (MeshGPU& mesh : resources.meshes)
        {
            bgfx::destroy(mesh.vbh);
            bgfx::destroy(mesh.ibh);
        }

        for (auto& [path, texHandle] : resources.textureCache)
        {
            bgfx::destroy(texHandle);
        }

        resources = GPU_Resources { };

        ++m_Generations[handle.index];
        m_FreeSlots.push_back(handle.index);
    }

    bool Renderer::IsHandleValid(GPU_ResourcesHandle handle) const
    {
        return handle.IsValid()
            && handle.index < m_SceneResources.size()
            && m_Generations[handle.index] == handle.generation;
    }

    GPU_Resources& Renderer::GetSceneResources(GPU_ResourcesHandle handle)
    {
        assert(IsHandleValid(handle) && "invalid or stale GPU_ResourcesHandle");

        return m_SceneResources[handle.index];
    }

    MeshHandle Renderer::UploadMesh(GPU_Resources& resources, const MeshDesc& data)
    {
        static const bgfx::VertexLayout LAYOUT { MakeVertexLayout() };

        const bgfx::Memory* vertexMem { bgfx::copy(data.vertices.data(),
            static_cast<uint32_t>(data.vertices.size() * sizeof(Vertex))) };

        const bgfx::Memory* indexMem { bgfx::copy(data.indices.data(),
            static_cast<uint32_t>(data.indices.size() * sizeof(uint32_t))) };

        MeshGPU mesh;
        mesh.vbh = bgfx::createVertexBuffer(vertexMem, LAYOUT);
        mesh.ibh = bgfx::createIndexBuffer(indexMem, BGFX_BUFFER_INDEX32);

        if (data.primitives.empty())
        {
            // No submesh info given - treat the whole index buffer as one
            // primitive; caller must set data.materials[0] for it to draw
            mesh.primitives.push_back(data.indices.size());
        }
        else
        {
            mesh.primitives = data.primitives;
        }

        mesh.materials = data.materials;

        const MeshHandle meshHandle { static_cast<MeshHandle>(resources.meshes.size()) };
        resources.meshes.push_back(std::move(mesh));

        return meshHandle;
    }

    uint32_t Renderer::UploadMaterial(GPU_Resources& resources, const MaterialDesc& desc)
    {
        MaterialGPU material;
        material.albedo = glm::vec4(desc.albedo, 1.0f);
        material.metallicRoughness = glm::vec4(desc.metallic, desc.roughness, 0.0f, 0.0f);
        material.emissive = glm::vec4(desc.emissive, 0.0f);

        material.albedoTex = desc.albedoTexture.empty()
            ? m_DefaultAlbedo : LoadTexture(resources, m_DataPath / desc.albedoTexture);

        material.normalTex = desc.normalTexture.empty()
            ? m_DefaultNormal : LoadTexture(resources, m_DataPath / desc.normalTexture);

        material.metallicRoughnessTex = desc.metallicRoughnessTexture.empty()
            ? m_DefaultMetallicRoughness : LoadTexture(resources, m_DataPath / desc.metallicRoughnessTexture);

        material.emissiveTex = desc.emissiveTexture.empty()
            ? m_DefaultEmissive : LoadTexture(resources, m_DataPath / desc.emissiveTexture);

        const uint32_t materialIndex { static_cast<uint32_t>(resources.materials.size()) };
        resources.materials.push_back(material);

        return materialIndex;
    }



    // ---------------
    // --- Helpers ---
    // ---------------

    Entity Renderer::FindPrimaryCamera(Registry& reg)
    {
        auto cameras = reg.View<Camera>();
        const auto& entities = cameras.Entities();

        Entity fallback { NULL_ENTITY };
        std::size_t i { 0 };

        for (auto& cam : cameras)
        {
            if (fallback == NULL_ENTITY)
                fallback = entities[i];

            if (cam.primary)
                return entities[i];

            ++i;
        }

        return fallback;
    }

    void Renderer::UpdateLights(EngineCtx& ctx, Scene& scene, Registry& reg)
    {
        glm::vec4 dirDir { 0.0f, -1.0f, 0.0f, 0.0f };
        glm::vec4 dirColor { 0.0f, 0.0f, 0.0f, 0.0f };

        auto dirLights = reg.View<DirectionalLight>();
        const auto& dirEntities = dirLights.Entities();

        if (!dirEntities.empty())
        {
            const Entity e { dirEntities[0] };
            const DirectionalLight& light { reg.Get<DirectionalLight>(e) };

            ctx.transforms.Ensure(reg, e);

            const glm::vec3 forward { reg.Get<Transform>(e).GetWorldRotation() * glm::vec3(0.0f, 0.0f, 1.0f) };

            dirDir = glm::vec4(forward, 0.0f);
            dirColor = glm::vec4(light.color * light.intensity, 0.0f);
        }

        bgfx::setUniform(u_dirLightDir, &dirDir);
        bgfx::setUniform(u_dirLightColor, &dirColor);

        glm::vec4 pointPos[MAX_POINT_LIGHTS] { };
        glm::vec4 pointColor[MAX_POINT_LIGHTS] { };

        auto pointLights = reg.View<PointLight>();
        const auto& pointEntities = pointLights.Entities();

        const std::size_t count { std::min(pointEntities.size(), MAX_POINT_LIGHTS) };

        for (std::size_t i = 0; i < count; ++i)
        {
            const Entity e { pointEntities[i] };
            const PointLight& light { reg.Get<PointLight>(e) };

            ctx.transforms.Ensure(reg, e);

            const glm::vec3 pos { reg.Get<Transform>(e).GetWorldPosition() };

            pointPos[i] = glm::vec4(pos, light.range);
            pointColor[i] = glm::vec4(light.color * light.intensity, 0.0f);
        }

        bgfx::setUniform(u_pointLightPos, pointPos, static_cast<std::uint16_t>(MAX_POINT_LIGHTS));
        bgfx::setUniform(u_pointLightColor, pointColor, static_cast<std::uint16_t>(MAX_POINT_LIGHTS));
    }

    bgfx::TextureHandle Renderer::LoadTexture(GPU_Resources& resources, const fs::path& path) const
    {
        const std::string key { path.string() };

        auto it = resources.textureCache.find(key);

        if (it != resources.textureCache.end())
            return it->second;

        const std::vector<std::uint8_t> fileData { ReadFile(path) };

        if (fileData.empty())
        {
            logError("Failed to read texture: {}", key);

            return m_DefaultAlbedo;
        }

        bx::DefaultAllocator allocator;
        bx::Error err;

        bimg::ImageContainer* image { bimg::imageParse(&allocator, fileData.data(),
            static_cast<uint32_t>(fileData.size()), bimg::TextureFormat::RGBA8, &err) };

        if (image == nullptr)
        {
            logError("Failed to decode texture: {}", key);

            return m_DefaultAlbedo;
        }

        const bgfx::Memory* mem { bgfx::copy(image->m_data, image->m_size) };

        const bgfx::TextureHandle handle { bgfx::createTexture2D(
            static_cast<std::uint16_t>(image->m_width),
            static_cast<std::uint16_t>(image->m_height),
            false, 1, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_NONE, mem) };

        bimg::imageFree(image);

        resources.textureCache[key] = handle;

        return handle;
    }

    void Renderer::BindMaterial(const GPU_Resources& resources, uint32_t materialIndex) const
    {
        const MaterialGPU& material { resources.materials[materialIndex] };

        bgfx::setUniform(u_baseColorFactor, &material.albedo);
        bgfx::setUniform(u_metallicRoughnessFactor, &material.metallicRoughness);
        bgfx::setUniform(u_emissiveFactor, &material.emissive);

        bgfx::setTexture(0, s_albedo, material.albedoTex);
        bgfx::setTexture(1, s_normal, material.normalTex);
        bgfx::setTexture(2, s_metallicRoughness, material.metallicRoughnessTex);
        bgfx::setTexture(3, s_emissive, material.emissiveTex);
    }



    // --------------------
    // --- GLTF loading ---
    // --------------------

    std::optional<GLTF_LoadResult> Renderer::LoadGLTF(const std::vector<fs::path>& files)
    {
        for (auto& path : files)
        {
            if (!std::filesystem::exists(path))
            {
                logError("Resource load failed: can't find '{}' .gltf/.glb file", path.string());

                return std::nullopt;
            }

            static constexpr auto supportedExtensions =
                fastgltf::Extensions::KHR_mesh_quantization |
                fastgltf::Extensions::KHR_texture_transform |
                fastgltf::Extensions::KHR_materials_variants;

            fastgltf::Parser parser(supportedExtensions);

            constexpr auto gltfOptions =
                fastgltf::Options::DontRequireValidAssetMember |
                fastgltf::Options::AllowDouble |
                fastgltf::Options::LoadExternalBuffers |
                fastgltf::Options::LoadExternalImages |
                fastgltf::Options::GenerateMeshIndices;

            auto gltfFile = fastgltf::MappedGltfFile::FromPath(path);
            if (!bool(gltfFile))
            {
                logError("Resource load failed: can't opent gltf file: {}",
                    fastgltf::getErrorMessage(gltfFile.error()));

                return std::nullopt;
            }

            auto asset = parser.loadGltf(gltfFile.get(), path.parent_path(), gltfOptions);
            if (asset.error() != fastgltf::Error::None)
            {
                logError("Resource load failed: can't load gltf file: {}",
                    fastgltf::getErrorMessage(asset.error()));

                return std::nullopt;
            }
        }
    }
}