#include "renderer.h"
#include "registry.h"
#include "engine_ctx.h"
#include "scene_manager.h"


#include <algorithm>
#include <cassert>
#include <cmath>
#include <fstream>

#include <glm/gtc/type_ptr.hpp>

#include <bx/allocator.h>
#include <bx/error.h>
#include <bx/math.h>
#include <bimg/decode.h>

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>

#include <stdexcept>

#include <ranges>
#include <system_error>

#include "utils.h"

namespace krn
{
    // -----------------------
    // --- Private helpers ---
    // -----------------------

    namespace
    {
        // No material index for a glTF primitive - 0 is a valid material
        // index, so it can't double as the empty value. Shared between
        // LoadGLTF (which produces it) and OnLoadCallback (which has to
        // resolve it to some actual uploaded material)
        constexpr std::uint32_t NO_GLTF_MATERIAL { static_cast<std::uint32_t>(-1) };

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

        // Derives per-vertex tangents (Lengyel's method) for a primitive whose
        // glTF data had no TANGENT attribute. Left at their default-constructed
        // (0,0,0,0), fs_pbr.sc's normalize(v_tangent.xyz - ...) divides by a
        // zero-length vector and taints the whole TBN basis - and therefore the
        // lit color - with NaN. [baseVertex, baseVertex + vertexCount) and
        // [baseIndex, baseIndex + indexCount) address this one primitive's
        // slice of the mesh's (already-global-indexed) vertex/index arrays.
        void ComputeTangents(std::vector<Vertex>& vertices, std::size_t baseVertex, std::size_t vertexCount,
            const std::vector<std::uint32_t>& indices, std::size_t baseIndex, std::size_t indexCount)
        {
            std::vector<glm::vec3> tan1(vertexCount, glm::vec3(0.0f));
            std::vector<glm::vec3> tan2(vertexCount, glm::vec3(0.0f));

            for (std::size_t i = 0; i + 2 < indexCount; i += 3)
            {
                const std::size_t i0 { indices[baseIndex + i]     - baseVertex };
                const std::size_t i1 { indices[baseIndex + i + 1] - baseVertex };
                const std::size_t i2 { indices[baseIndex + i + 2] - baseVertex };

                const Vertex& v0 { vertices[baseVertex + i0] };
                const Vertex& v1 { vertices[baseVertex + i1] };
                const Vertex& v2 { vertices[baseVertex + i2] };

                const glm::vec3 edge1 { v1.position - v0.position };
                const glm::vec3 edge2 { v2.position - v0.position };
                const glm::vec2 deltaUV1 { v1.uv - v0.uv };
                const glm::vec2 deltaUV2 { v2.uv - v0.uv };

                const float denom { deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y };

                if (std::abs(denom) < 1.0e-8f)
                    continue; // degenerate/absent UVs - this triangle contributes nothing, handled by the per-vertex fallback below

                const float r { 1.0f / denom };
                const glm::vec3 sdir { (edge1 * deltaUV2.y - edge2 * deltaUV1.y) * r };
                const glm::vec3 tdir { (edge2 * deltaUV1.x - edge1 * deltaUV2.x) * r };

                tan1[i0] += sdir; tan1[i1] += sdir; tan1[i2] += sdir;
                tan2[i0] += tdir; tan2[i1] += tdir; tan2[i2] += tdir;
            }

            for (std::size_t i = 0; i < vertexCount; ++i)
            {
                Vertex& v { vertices[baseVertex + i] };
                const glm::vec3& n { v.normal };

                glm::vec3 t { tan1[i] };

                if (glm::dot(t, t) < 1.0e-12f)
                {
                    // No UV-derived tangent survived (isolated vertex, fully
                    // degenerate UVs) - fall back to an arbitrary vector
                    // orthogonal to the normal so the TBN basis stays valid,
                    // even though it won't be UV-aligned
                    const glm::vec3 arbitrary { std::abs(n.z) < 0.999f
                        ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f) };

                    t = glm::normalize(glm::cross(n, arbitrary));
                }
                else
                {
                    // Gram-Schmidt orthogonalize against the normal
                    t = glm::normalize(t - n * glm::dot(n, t));
                }

                const float w { glm::dot(glm::cross(n, t), tan2[i]) < 0.0f ? -1.0f : 1.0f };

                v.tangent = glm::vec4(t, w);
            }
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

        // Create uniforms

        // Add OnLoad/OnUnload callbacks
        scenes.AddInternalLoadCallback(
            [this] (const EngineCtx& ctx, Scene& scene, Registry& reg, std::vector<fs::path>& resToLoad)
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

        if (cameraEntity == NULL_ENTITY || !reg.Has<Transform>(cameraEntity))
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
        const glm::vec4 ambient { 0.03f, 0.03f, 0.03f, 0.0f };

        glm::vec4 dirDir { };
        glm::vec4 dirColor { };
        glm::vec4 pointPos[MAX_POINT_LIGHTS] { };
        glm::vec4 pointColor[MAX_POINT_LIGHTS] { };

        GatherLights(ctx, scene, reg, dirDir, dirColor, pointPos, pointColor);

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

            if (model.meshHandle == INVALID_MESH || model.forceRenderingOff)
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

                // Transform/buffers/state/uniforms only apply to the next
                // submit() - bgfx discards them all after each one (see
                // BindGlobalUniforms), so everything is re-set per primitive
                // rather than once per model/frame.
                bgfx::setTransform(glm::value_ptr(worldMat));

                bgfx::setVertexBuffer(0, mesh.vbh);
                bgfx::setIndexBuffer(mesh.ibh, start, count);

                BindGlobalUniforms(camPos, ambient, dirDir, dirColor, pointPos, pointColor);
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

        assert(handle < res.meshes.size() && "tried to access invalid mesh handle");

        return res.meshes[handle];
    }

    MaterialGPU& Renderer::GetMaterial(Scene& scene, uint32_t id)
    {
        auto& res { GetSceneResources(scene.GetResourcesHandle()) };

        assert(id < res.materials.size() && "tried to access invalid material by id");

        return res.materials[id];
    }



    // ---------------------------
    // --- Resource management ---
    // ---------------------------

    void Renderer::OnLoadCallback(const EngineCtx& ctx, Scene& scene, Registry& reg, std::vector<fs::path>& resToLoad)
    {
        auto gltfData { LoadGLTF(resToLoad) };

        // Create resources handle and give it to the scene
        const GPU_ResourcesHandle handle { CreateSceneResources() };
        scene.SetResourcesHandle(handle);

        GPU_Resources& res { GetSceneResources(handle) };

        GLTF_LoadResult result { gltfData.has_value() ? std::move(*gltfData) : GLTF_LoadResult { } };

        // Upload every material and mesh from every loaded file into this
        // scene's single shared GPU_Resources buffer up front. A mesh's/
        // material's index in GLTF_LoadResult::File is only meaningful
        // within its own file, so fileMaterialIds/fileMeshHandles record
        // where each one actually landed, letting node/ModelDesc references
        // (which only know the local, per-file index) resolve to the
        // correct shared entry afterwards.
        std::optional<uint32_t> defaultMaterialId { };

        std::vector<std::vector<uint32_t>> fileMaterialIds(result.files.size());
        std::vector<std::vector<MeshHandle>> fileMeshHandles(result.files.size());

        for (std::size_t f = 0; f < result.files.size(); ++f)
        {
            GLTF_LoadResult::File& file { result.files[f] };

            std::vector<uint32_t>& materialIds { fileMaterialIds[f] };
            materialIds.reserve(file.materials.size());

            for (const MaterialDesc& matDesc : file.materials)
                materialIds.push_back(UploadMaterial(res, matDesc));

            std::vector<MeshHandle>& meshHandles { fileMeshHandles[f] };
            meshHandles.reserve(file.meshes.size());

            for (const MeshDesc& meshDesc : file.meshes)
            {
                // Copy rather than move: file.meshes has to stay intact for
                // ModelDesc entries that want to build their own, separately
                // uploaded mesh out of it (see PreprocessModels)
                MeshDesc resolved { meshDesc };

                for (uint32_t& matIdx : resolved.materials)
                {
                    if (matIdx == NO_GLTF_MATERIAL)
                    {
                        if (!defaultMaterialId.has_value())
                            defaultMaterialId = UploadMaterial(res, MaterialDesc { });

                        matIdx = *defaultMaterialId;
                    }
                    else
                    {
                        matIdx = materialIds[matIdx];
                    }
                }

                meshHandles.push_back(UploadMesh(res, resolved));
            }
        }

        PreprocessModels(result, reg, ctx, res, fileMaterialIds, fileMeshHandles);
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

        u_camPos = bgfx::createUniform("u_camPos", bgfx::UniformType::Vec4);
        u_ambientColor = bgfx::createUniform("u_ambientColor", bgfx::UniformType::Vec4);
        u_dirLightDir = bgfx::createUniform("u_dirLightDir", bgfx::UniformType::Vec4);
        u_dirLightColor = bgfx::createUniform("u_dirLightColor", bgfx::UniformType::Vec4);
        u_pointLightPos = bgfx::createUniform("u_pointLightPos", bgfx::UniformType::Vec4, MAX_POINT_LIGHTS);
        u_pointLightColor = bgfx::createUniform("u_pointLightColor", bgfx::UniformType::Vec4, MAX_POINT_LIGHTS);
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

    void Renderer::GatherLights(EngineCtx& ctx, Scene& scene, Registry& reg,
        glm::vec4& dirDir, glm::vec4& dirColor,
        glm::vec4 (&pointPos)[MAX_POINT_LIGHTS], glm::vec4 (&pointColor)[MAX_POINT_LIGHTS]) const
    {
        dirDir = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);
        dirColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

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

        std::fill(std::begin(pointPos), std::end(pointPos), glm::vec4 { });
        std::fill(std::begin(pointColor), std::end(pointColor), glm::vec4 { });

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
    }

    void Renderer::BindGlobalUniforms(const glm::vec4& camPos, const glm::vec4& ambient,
        const glm::vec4& dirDir, const glm::vec4& dirColor,
        const glm::vec4 (&pointPos)[MAX_POINT_LIGHTS], const glm::vec4 (&pointColor)[MAX_POINT_LIGHTS]) const
    {
        bgfx::setUniform(u_camPos, &camPos);
        bgfx::setUniform(u_ambientColor, &ambient);
        bgfx::setUniform(u_dirLightDir, &dirDir);
        bgfx::setUniform(u_dirLightColor, &dirColor);
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
        using GLTF_File = GLTF_LoadResult::File;
        using GLTF_Node = GLTF_File::Node;

        GLTF_LoadResult result { };

        static constexpr auto supportedExtensions =
            fastgltf::Extensions::KHR_mesh_quantization |
            fastgltf::Extensions::KHR_texture_transform |
            fastgltf::Extensions::KHR_materials_variants;

        // LoadExternalImages is deliberately left out: MaterialDesc only
        // ever stores texture *paths* (LoadTexture reads + decodes them
        // itself later, on demand), so image bytes should stay on disk
        // here rather than get pre-loaded into memory. DecomposeNodeMatrices
        // guarantees Node::transform always holds the TRS alternative,
        // matching GLTF_LoadResult::Data::Node's position/rotation/scale.
        constexpr auto gltfOptions =
            fastgltf::Options::DontRequireValidAssetMember |
            fastgltf::Options::AllowDouble |
            fastgltf::Options::LoadExternalBuffers |
            fastgltf::Options::GenerateMeshIndices |
            fastgltf::Options::DecomposeNodeMatrices;

        for (const auto& path : files)
        {
            if (path.extension() != ".gltf" && path.extension() != ".glb")
                continue;

            if (result.pathToFileId.contains(path))
                continue;

            if (!std::filesystem::exists(path))
            {
                logError("Resource load failed: can't find '{}' .gltf/.glb file", path.string());

                continue;
            }

            fastgltf::Parser parser(supportedExtensions);

            auto gltfFile = fastgltf::MappedGltfFile::FromPath(path);
            if (!bool(gltfFile))
            {
                logError("Resource load failed: can't open gltf file: {}",
                    fastgltf::getErrorMessage(gltfFile.error()));

                continue;
            }

            auto assetResult = parser.loadGltf(gltfFile.get(), path.parent_path(), gltfOptions);
            if (assetResult.error() != fastgltf::Error::None)
            {
                logError("Resource load failed: can't load gltf file: {}",
                    fastgltf::getErrorMessage(assetResult.error()));

                continue;
            }

            fastgltf::Asset& asset { assetResult.get() };

            GLTF_File data { };

            // Resolves a glTF TextureInfo (base color/normal/metallic-roughness/
            // emissive) down to a path relative to m_DataPath, the way
            // MaterialDesc/UploadMaterial expect it. Returns "" (meaning
            // "use the default texture") for unset, embedded or otherwise
            // non-file-based images, since this renderer only knows how to
            // load textures off disk.
            auto resolveTexturePath = [&] (const auto& infoOpt) -> std::string
                {
                    if (!infoOpt.has_value() || infoOpt->textureIndex >= asset.textures.size())
                        return { };

                    const fastgltf::Texture& texture { asset.textures[infoOpt->textureIndex] };

                    fastgltf::Optional<std::size_t> imageIndex { texture.imageIndex };
                    if (!imageIndex.has_value()) imageIndex = texture.basisuImageIndex;
                    if (!imageIndex.has_value()) imageIndex = texture.ddsImageIndex;
                    if (!imageIndex.has_value()) imageIndex = texture.webpImageIndex;

                    if (!imageIndex.has_value() || *imageIndex >= asset.images.size())
                        return { };

                    const fastgltf::Image& image { asset.images[*imageIndex] };
                    const auto* uriSource { std::get_if<fastgltf::sources::URI>(&image.data) };

                    if (uriSource == nullptr || !uriSource->uri.isLocalPath())
                    {
                        logWarning("glTF file '{}' references an embedded or non-local image - "
                            "only file-based textures are supported, skipping", path.string());

                        return { };
                    }

                    std::error_code ec;

                    const fs::path absolute { fs::weakly_canonical(
                        path.parent_path() / uriSource->uri.fspath(), ec) };

                    if (ec)
                        return { };

                    const fs::path relative { fs::relative(absolute, m_DataPath, ec) };

                    if (ec)
                    {
                        logWarning("Texture '{}' referenced by '{}' isn't reachable from the data root '{}'",
                            absolute.string(), path.string(), m_DataPath.string());

                        return { };
                    }

                    return relative.generic_string();
                };

                // --- Materials ---

            data.materials.reserve(asset.materials.size());

            for (std::size_t i = 0; i < asset.materials.size(); ++i)
            {
                const fastgltf::Material& mat { asset.materials[i] };

                MaterialDesc desc { };

                desc.albedo = glm::vec3(static_cast<float>(mat.pbrData.baseColorFactor[0]),
                    static_cast<float>(mat.pbrData.baseColorFactor[1]),
                    static_cast<float>(mat.pbrData.baseColorFactor[2]));
                desc.metallic = static_cast<float>(mat.pbrData.metallicFactor);
                desc.roughness = static_cast<float>(mat.pbrData.roughnessFactor);
                desc.emissive = glm::vec3(static_cast<float>(mat.emissiveFactor[0]),
                    static_cast<float>(mat.emissiveFactor[1]),
                    static_cast<float>(mat.emissiveFactor[2]));

                desc.albedoTexture = resolveTexturePath(mat.pbrData.baseColorTexture);
                desc.metallicRoughnessTexture = resolveTexturePath(mat.pbrData.metallicRoughnessTexture);
                desc.normalTexture = resolveTexturePath(mat.normalTexture);
                desc.emissiveTexture = resolveTexturePath(mat.emissiveTexture);

                if (!mat.name.empty())
                    data.nameToMaterialId[std::string(mat.name.data(), mat.name.size())] = i;

                data.materials.push_back(std::move(desc));
            }

            // --- Meshes ---

            data.meshes.reserve(asset.meshes.size());

            for (std::size_t i = 0; i < asset.meshes.size(); ++i)
            {
                const fastgltf::Mesh& srcMesh { asset.meshes[i] };

                MeshDesc meshDesc { };

                for (const fastgltf::Primitive& primitive : srcMesh.primitives)
                {
                    if (primitive.type != fastgltf::PrimitiveType::Triangles)
                    {
                        logWarning("Mesh '{}' in '{}' has a non-triangle primitive, skipping it",
                            srcMesh.name, path.string());

                        continue;
                    }

                    const auto posIt { primitive.findAttribute("POSITION") };
                    if (posIt == primitive.attributes.end())
                    {
                        logWarning("Mesh '{}' in '{}' has a primitive with no POSITION attribute, skipping it",
                            srcMesh.name, path.string());

                        continue;
                    }

                    if (!primitive.indicesAccessor.has_value())
                    {
                        logWarning("Mesh '{}' in '{}' has a primitive with no indices, skipping it",
                            srcMesh.name, path.string());

                        continue;
                    }

                    const fastgltf::Accessor& posAccessor { asset.accessors[posIt->accessorIndex] };

                    const std::size_t baseVertex { meshDesc.vertices.size() };
                    meshDesc.vertices.resize(baseVertex + posAccessor.count);

                    fastgltf::iterateAccessorWithIndex<glm::vec3>(asset, posAccessor,
                        [&] (glm::vec3 v, std::size_t idx) { meshDesc.vertices[baseVertex + idx].position = v; });

                    if (const auto normIt { primitive.findAttribute("NORMAL") };
                        normIt != primitive.attributes.end())
                    {
                        fastgltf::iterateAccessorWithIndex<glm::vec3>(asset, asset.accessors[normIt->accessorIndex],
                            [&] (glm::vec3 v, std::size_t idx) { meshDesc.vertices[baseVertex + idx].normal = v; });
                    }

                    const auto tanIt { primitive.findAttribute("TANGENT") };
                    const bool hasTangents { tanIt != primitive.attributes.end() };

                    if (hasTangents)
                    {
                        fastgltf::iterateAccessorWithIndex<glm::vec4>(asset, asset.accessors[tanIt->accessorIndex],
                            [&] (glm::vec4 v, std::size_t idx) { meshDesc.vertices[baseVertex + idx].tangent = v; });
                    }

                    if (const auto uvIt { primitive.findAttribute("TEXCOORD_0") };
                        uvIt != primitive.attributes.end())
                    {
                        fastgltf::iterateAccessorWithIndex<glm::vec2>(asset, asset.accessors[uvIt->accessorIndex],
                            [&] (glm::vec2 v, std::size_t idx) { meshDesc.vertices[baseVertex + idx].uv = v; });
                    }

                    const fastgltf::Accessor& idxAccessor { asset.accessors[*primitive.indicesAccessor] };

                    const std::size_t baseIndex { meshDesc.indices.size() };
                    meshDesc.indices.resize(baseIndex + idxAccessor.count);

                    fastgltf::iterateAccessorWithIndex<std::uint32_t>(asset, idxAccessor,
                        [&] (std::uint32_t vertIdx, std::size_t idx)
                        {
                            meshDesc.indices[baseIndex + idx] = static_cast<std::uint32_t>(baseVertex) + vertIdx;
                        });

                    // glTF makes TANGENT optional, but Vertex::tangent otherwise stays
                    // zero-initialized, which is a zero-length vector fs_pbr.sc can't
                    // normalize (see ComputeTangents) - derive one whenever it's missing
                    if (!hasTangents)
                    {
                        ComputeTangents(meshDesc.vertices, baseVertex, posAccessor.count,
                            meshDesc.indices, baseIndex, idxAccessor.count);
                    }

                    meshDesc.primitives.push_back(idxAccessor.count);
                    meshDesc.materials.push_back(primitive.materialIndex
                        ? static_cast<std::uint32_t>(*primitive.materialIndex) : NO_GLTF_MATERIAL);
                }

                if (!srcMesh.name.empty())
                    data.nameToMeshId[std::string(srcMesh.name.data(), srcMesh.name.size())] = i;

                data.meshes.push_back(std::move(meshDesc));
            }

            // --- Nodes ---
            // GLTF_LoadResult::Data::Node mirrors asset.nodes index-for-index,
            // so names/meshes resolved above can address nodes directly by
            // their glTF index without a separate remapping table.

            data.nodes.resize(asset.nodes.size());

            for (std::size_t i = 0; i < asset.nodes.size(); ++i)
            {
                const fastgltf::Node& srcNode { asset.nodes[i] };
                GLTF_Node& node { data.nodes[i] };

                node.childNodeIds.assign(srcNode.children.begin(), srcNode.children.end());

                if (const auto* trs { std::get_if<fastgltf::TRS>(&srcNode.transform) })
                {
                    node.position = glm::vec3(static_cast<float>(trs->translation[0]),
                        static_cast<float>(trs->translation[1]), static_cast<float>(trs->translation[2]));
                    node.rotation = glm::quat(static_cast<float>(trs->rotation[3]),
                        static_cast<float>(trs->rotation[0]), static_cast<float>(trs->rotation[1]),
                        static_cast<float>(trs->rotation[2]));
                    node.scale = glm::vec3(static_cast<float>(trs->scale[0]),
                        static_cast<float>(trs->scale[1]), static_cast<float>(trs->scale[2]));
                }

                node.meshId = srcNode.meshIndex.has_value() ? *srcNode.meshIndex : GLTF_Node::NO_INDEX;

                // glTF has no notion of a per-node material (materials are
                // per-primitive) - materialId is left at NO_INDEX until
                // there's an actual override mechanism to populate it from.

                if (!srcNode.name.empty())
                    data.nameToNodeId[std::string(srcNode.name.data(), srcNode.name.size())] = i;
            }

            for (std::size_t i = 0; i < data.nodes.size(); ++i)
            {
                for (std::size_t child : data.nodes[i].childNodeIds)
                {
                    if (child < data.nodes.size())
                        data.nodes[child].parentNodeId = i;
                }
            }

            result.pathToFileId[path] = result.files.size();
            result.files.push_back(std::move(data));
        }

        return result.files.empty() ? std::nullopt : std::optional<GLTF_LoadResult>(std::move(result));
    }

    void Renderer::PreprocessModels(GLTF_LoadResult& result, Registry& reg, const EngineCtx& ctx, GPU_Resources& res,
        const std::vector<std::vector<uint32_t>>& fileMaterialIds,
        const std::vector<std::vector<MeshHandle>>& fileMeshHandles)
    {
        using GLTF_File = GLTF_LoadResult::File;
        using GLTF_Node = GLTF_File::Node;

        auto view { reg.View<LoadMetadata>() };

        view.ForEach([&] (Entity entity, LoadMetadata& metadata) -> void
            {
                auto& metaNode { metadata.gltfNode };

                if (metaNode.loadedFileId == LoadMetadata::INVALID_ID)
                    return;

                if (metaNode.loadedFileId >= result.files.size())
                {
                    logError("Invalid gltf_file_id attribute in entity metadata: "
                        "file with id '{}' isn't loaded in this scene", metaNode.loadedFileId);

                    return;
                }

                GLTF_File& file { result.files[metaNode.loadedFileId] };
                const std::vector<MeshHandle>& meshHandles { fileMeshHandles[metaNode.loadedFileId] };

                if (metaNode.name.empty())
                {
                    // Load entire file: every root node (one with no parent)
                    // becomes a child of this entity
                    for (size_t i = 0; i < file.nodes.size(); i++)
                    {
                        GLTF_Node& node { file.nodes[i] };

                        if (node.parentNodeId != GLTF_Node::NO_INDEX) // skip non-root nodes
                            continue;

                        LoadGLTFNode(ctx, entity, metadata, reg, file, node,
                            static_cast<uint32_t>(i), true, metaNode.loadChildren, meshHandles);
                    }
                }
                else
                {
                    auto it = file.nameToNodeId.find(metaNode.name);

                    if (it == file.nameToNodeId.end())
                    {
                        logError("Invalid gltf_node_name attribute in entity metadata: "
                            "node with name '{}' in file with id {} does not exist",
                            metaNode.name, metaNode.loadedFileId);

                        return;
                    }

                    size_t nodeId { it->second };
                    auto& node { file.nodes[nodeId] };

                    LoadGLTFNode(ctx, entity, metadata, reg, file, node,
                        static_cast<uint32_t>(nodeId), true, metaNode.loadChildren, meshHandles);
                }
            });

        auto modelDescs { reg.View<ModelDesc>() };
        modelDescs.ForEach([&] (Entity entity, ModelDesc& modelDesc) -> void
            {
                if (modelDesc.gltfFileName.empty() || modelDesc.gtlfMeshName.empty())
                    return;

                auto fileIt = result.pathToFileId.find(fs::path(modelDesc.gltfFileName));

                if (fileIt == result.pathToFileId.end())
                {
                    logError("ModelDesc on entity {} references gltf file '{}', which isn't loaded in this scene",
                        entity, modelDesc.gltfFileName);

                    return;
                }

                const std::size_t fileId { fileIt->second };
                GLTF_File& file { result.files[fileId] };

                auto meshIt = file.nameToMeshId.find(modelDesc.gtlfMeshName);

                if (meshIt == file.nameToMeshId.end())
                {
                    logError("ModelDesc on entity {} references mesh '{}', which doesn't exist in gltf file '{}'",
                        entity, modelDesc.gtlfMeshName, modelDesc.gltfFileName);

                    return;
                }

                const std::size_t meshId { meshIt->second };

                Model& model { reg.Has<Model>(entity) ? reg.Get<Model>(entity) : reg.Add<Model>(entity) };

                if (modelDesc.gtlfMaterialName.empty() && modelDesc.materialDesc.empty())
                {
                    // No material override - share the file's own, already-uploaded mesh
                    model.meshHandle = fileMeshHandles[fileId][meshId];

                    return;
                }

                // Material override requested: this entity needs its own,
                // separately-uploaded mesh rather than the shared one, since
                // every other reference to this mesh still wants the file's
                // original materials
                std::vector<uint32_t> overrideIds;

                if (!modelDesc.gtlfMaterialName.empty())
                {
                    auto matIt = file.nameToMaterialId.find(modelDesc.gtlfMaterialName);

                    if (matIt == file.nameToMaterialId.end())
                    {
                        logError("ModelDesc on entity {} references material '{}', "
                            "which doesn't exist in gltf file '{}'",
                            entity, modelDesc.gtlfMaterialName, modelDesc.gltfFileName);

                        return;
                    }

                    overrideIds.push_back(fileMaterialIds[fileId][matIt->second]);
                }
                else
                {
                    overrideIds.reserve(modelDesc.materialDesc.size());

                    for (const MaterialDesc& matDesc : modelDesc.materialDesc)
                        overrideIds.push_back(UploadMaterial(res, matDesc));
                }

                MeshDesc resolved { file.meshes[meshId] };

                for (std::size_t i = 0; i < resolved.materials.size(); ++i)
                {
                    // Fewer overrides than primitives: the last override
                    // applies to every remaining primitive
                    resolved.materials[i] = overrideIds[std::min(i, overrideIds.size() - 1)];
                }

                model.meshHandle = UploadMesh(res, resolved);
            });
    }

    void Renderer::LoadGLTFNode(const EngineCtx& ctx, Entity entity, LoadMetadata& metadata,
        Registry& reg, GLTF_LoadResult::File& file, GLTF_LoadResult::File::Node& node,
        uint32_t nodeId, bool hasEntity, bool loadChildren, const std::vector<MeshHandle>& meshHandles)
    {
        using GLTF_File = GLTF_LoadResult::File;
        using GLTF_Node = GLTF_File::Node;

        // Skip children explicitly excluded via the parent's
        // gltf_exclude_child_nodes attribute - metadata here is still the
        // originally-targeted entity's LoadMetadata (it only gets swapped
        // for a fresh one below, for this and deeper recursive calls), so
        // this only ever filters that entity's direct children
        if (!hasEntity)
        {
            for (const std::string& excluded : metadata.gltfNode.excludeChildNodes)
            {
                auto it = file.nameToNodeId.find(excluded);

                if (it != file.nameToNodeId.end() && it->second == nodeId)
                    return;
            }
        }

        Entity e { hasEntity ? entity : reg.Create() };
        LoadMetadata& md { hasEntity ? metadata : reg.Add<LoadMetadata>(e) };

        Transform& tr { reg.Has<Transform>(e) ? reg.Get<Transform>(e) : reg.Add<Transform>(e) };
        tr.SetLocalPosition(node.position);
        tr.SetLocalRotation(node.rotation);
        tr.SetLocalScale(node.scale);

        // Newly-created child entities have no LoadMetadata::id for
        // TransformManager's own load callback to link up later (and don't
        // need one - nothing else needs to reference them by id), so link
        // them into the hierarchy directly
        if (!hasEntity)
            ctx.transforms.SetParent(reg, e, entity, false);

        if (node.meshId != GLTF_Node::NO_INDEX)
        {
            Model& model { reg.Has<Model>(e) ? reg.Get<Model>(e) : reg.Add<Model>(e) };
            model.meshHandle = meshHandles[node.meshId];
        }

        // Optionally load children nodes
        if (loadChildren)
        {
            for (auto& childNodeId : node.childNodeIds)
            {
                auto& childNode { file.nodes[childNodeId] };

                LoadGLTFNode(ctx, e, md, reg, file, childNode,
                    static_cast<uint32_t>(childNodeId), false, true, meshHandles);
            }
        }
    }
}