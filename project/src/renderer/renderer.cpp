#include "renderer.h"
#include "registry.h"
#include "engine_ctx.h"
#include "scene_manager.h"

#include <algorithm>
#include <fstream>

#include <glm/gtc/type_ptr.hpp>

#include <bx/allocator.h>
#include <bx/error.h>
#include <bx/math.h>
#include <bimg/decode.h>

#include <stdexcept>

#include <ranges>

#include "utils.h"



namespace krn
{
    // -----------------------
    // --- Private helpers ---
    // -----------------------

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
        m_Window(window),
        m_Scenes(scenes)
    {
        InitBGFX();

        InitPermanentResources(dataPath / "shaders" / "bin");

        scenes.OnResourceLoad.Connect<&Renderer::OnResourceLoad>(*this);
        scenes.OnUnload.Connect<&Renderer::OnUnload>(*this);
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

        bgfx::destroy(u_shadowMapMtx);
        bgfx::destroy(u_lightOrigin);

        for (std::size_t i = 0; i < NUM_CASCADES; ++i)
        {
            bgfx::destroy(s_shadowMap[i]);
            bgfx::destroy(m_ShadowMap.framebuffers[i]);
        }

        for (std::size_t i = 0; i < MAX_POINT_LIGHTS; ++i)
        {
            bgfx::destroy(s_pointShadowMap[i]);

            for (std::size_t face = 0; face < CUBE_FACE_COUNT; ++face)
                bgfx::destroy(m_PointShadow.faceFramebuffers[i][face]);

            bgfx::destroy(m_PointShadow.colorCube[i]);
            bgfx::destroy(m_PointShadow.depthCube[i]);
        }

        bgfx::destroy(m_ShadowCasterProgram);
        bgfx::destroy(m_PointShadowCasterProgram);

        // The current scene outlives the Renderer, so release its GPU resources while bgfx is still up
        if (Scene* scene { m_Scenes.GetCurrentScene() })
            DestroySceneResources(scene->GetReg());

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
        // Ensure the main view is submitted even on frames where nothing else touches it
        bgfx::touch(MAIN_VIEW_ID);

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

        bx::mtxProj(proj, camera.FOV, aspect, camera.nearPlane, camera.farPlane,
            bgfx::getCaps()->homogeneousDepth);

        const glm::vec4 camPos { eye, 0.0f };
        const glm::vec4 ambient { 0.03f, 0.03f, 0.03f, 0.0f };

        glm::vec4 dirDir { };
        glm::vec4 dirColor { };
        glm::vec4 pointPos[MAX_POINT_LIGHTS] { };
        glm::vec4 pointColor[MAX_POINT_LIGHTS] { };
        std::size_t activePointLightCount { 0 };

        GatherLights(ctx, scene, reg, dirDir, dirColor, pointPos, pointColor, activePointLightCount);

        constexpr std::uint64_t SHADOW_CASTER_STATE { BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
            | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS };

        // --- Directional light: cascaded shadow map ---
        float camInvView[16];
        bx::mtxInverse(camInvView, view);

        ComputeCascadeData(m_ShadowMap, camInvView, camera.FOV, aspect,
            camera.nearPlane, camera.farPlane, glm::vec3(dirDir));

        for (std::size_t c = 0; c < NUM_CASCADES; ++c)
        {
            const bgfx::ViewId vid { static_cast<bgfx::ViewId>(c) };

            // Clear colour to PackFloatToRgba(1.0) ("farthest") so any texel
            // no caster touched reads as unoccluded rather than as garbage
            bgfx::setViewClear(vid, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0xffffffff, 1.0f, 0);
            bgfx::setViewFrameBuffer(vid, m_ShadowMap.framebuffers[c]);
            bgfx::setViewRect(vid, 0, 0, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
            bgfx::setViewTransform(vid, m_ShadowMap.lightView, m_ShadowMap.lightProj[c]);

            DrawPrimitives(ctx, reg, [&] (uint32_t)
                {
                    bgfx::setState(SHADOW_CASTER_STATE);
                    bgfx::submit(vid, m_ShadowCasterProgram);
                });
        }

        // --- Point lights: one omnidirectional cube shadow map each ---
        for (std::size_t li = 0; li < activePointLightCount; ++li)
        {
            const glm::vec4 origin { pointPos[li] }; // xyz = world pos, w = range

            for (std::uint8_t face = 0; face < CUBE_FACE_COUNT; ++face)
            {
                const bgfx::ViewId vid { static_cast<bgfx::ViewId>(
                    POINT_SHADOW_VIEW_BASE + static_cast<bgfx::ViewId>(li) * CUBE_FACE_COUNT + face) };

                float pointView[16];
                float pointProj[16];
                ComputePointLightViewProj(face, glm::vec3(origin), origin.w, pointView, pointProj);

                bgfx::setViewClear(vid, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0xffffffff, 1.0f, 0);
                bgfx::setViewFrameBuffer(vid, m_PointShadow.faceFramebuffers[li][face]);
                bgfx::setViewRect(vid, 0, 0, POINT_SHADOW_MAP_SIZE, POINT_SHADOW_MAP_SIZE);
                bgfx::setViewTransform(vid, pointView, pointProj);

                DrawPrimitives(ctx, reg, [&] (uint32_t)
                    {
                        bgfx::setUniform(u_lightOrigin, &origin);
                        bgfx::setState(SHADOW_CASTER_STATE);
                        bgfx::submit(vid, m_PointShadowCasterProgram);
                    });
            }
        }

        // --- Main color pass ---

        bgfx::setViewTransform(MAIN_VIEW_ID, view, proj);
        bgfx::setViewRect(MAIN_VIEW_ID, 0, 0, static_cast<uint16_t>(size.width), static_cast<uint16_t>(size.height));

        constexpr std::uint64_t STATE { BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
            | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA };

        DrawPrimitives(ctx, reg, [&] (uint32_t matId)
            {
                BindGlobalUniforms(camPos, ambient, dirDir, dirColor, pointPos, pointColor);
                BindMaterial(matId);
                BindShadowUniforms();

                bgfx::setState(STATE);

                bgfx::submit(MAIN_VIEW_ID, m_PBRProgram);
            });

        bgfx::frame();
    }

    void Renderer::DrawPrimitives(EngineCtx& ctx, Registry& reg,
        const std::function<void(uint32_t materialIndex)>& onPrimitive)
    {
        auto models = reg.View<Model>();
        const auto& entities = models.Entities();

        std::size_t i = 0;

        for (auto& model : models)
        {
            const Entity entity { entities[i++] };

            if (!bgfx::isValid(model.vbh) || model.forceRenderingOff)
                continue;

            ctx.transforms.Ensure(reg, entity);

            const glm::mat4 worldMat { reg.Get<Transform>(entity).GetWorldMatrix() };

            for (const Primitive& primitive : model.primitives)
            {
                bgfx::setTransform(glm::value_ptr(worldMat));

                bgfx::setVertexBuffer(0, model.vbh);
                bgfx::setIndexBuffer(model.ibh, primitive.indexStart, primitive.indexCount);

                onPrimitive(primitive.materialId);
            }
        }
    }

    MaterialGPU& Renderer::GetMaterial(uint32_t id)
    {
        check(id < m_Materials.size(), "Tried to access invalid material by id");

        return m_Materials[id];
    }



    // ---------------------------
    // --- Resource management ---
    // ---------------------------

    void Renderer::OnResourceLoad(const EngineCtx& ctx, Scene& scene, Registry& reg, LoadResult& result)
    {
        GLTF_UploadCache cache { };
        cache.fileMeshes.resize(result.files.size());
        cache.fileMaterialIds.resize(result.files.size());

        for (std::size_t f = 0; f < result.files.size(); ++f)
        {
            cache.fileMeshes[f].resize(result.files[f].meshes.size());
            cache.fileMaterialIds[f].resize(result.files[f].materials.size());
        }

        PreprocessModels(result, reg, ctx, cache);
    }

    void Renderer::OnUnload(const EngineCtx& ctx, Scene& scene, Registry& reg)
    {
        DestroySceneResources(reg);
    }

    void Renderer::InitBGFX()
    {
        // Init bgfx
        bgfx::PlatformData platformData { };
        platformData.nwh = m_Window.GetNativeHandle();
        platformData.ndt = m_Window.GetNativeDisplay();

        const WindowSize size { m_Window.GetSizeInPixels() };

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

        bgfx::setViewClear(MAIN_VIEW_ID, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030ff, 1.0f, 0);
        bgfx::setViewRect(MAIN_VIEW_ID, 0, 0, static_cast<uint16_t>(size.width), static_cast<uint16_t>(size.height));
    }

    void Renderer::InitPermanentResources(const fs::path& shaderDir)
    {
        const bgfx::ShaderHandle vsh { LoadShader(shaderDir, "vs_pbr") };
        const bgfx::ShaderHandle fsh { LoadShader(shaderDir, "fs_pbr") };

        m_PBRProgram = bgfx::createProgram(vsh, fsh, true);

        const bgfx::ShaderHandle shadowVsh { LoadShader(shaderDir, "vs_shadow_caster") };
        const bgfx::ShaderHandle shadowFsh { LoadShader(shaderDir, "fs_shadow_caster") };

        m_ShadowCasterProgram = bgfx::createProgram(shadowVsh, shadowFsh, true);

        const bgfx::ShaderHandle pointShadowVsh { LoadShader(shaderDir, "vs_shadow_caster_point") };
        const bgfx::ShaderHandle pointShadowFsh { LoadShader(shaderDir, "fs_shadow_caster_point") };

        m_PointShadowCasterProgram = bgfx::createProgram(pointShadowVsh, pointShadowFsh, true);

        // --- Directional light cascaded shadow map resources ---

        u_shadowMapMtx = bgfx::createUniform("u_shadowMapMtx", bgfx::UniformType::Mat4, NUM_CASCADES);

        for (std::size_t i = 0; i < NUM_CASCADES; ++i)
        {
            s_shadowMap[i] = bgfx::createUniform(("s_shadowMap" + std::to_string(i)).c_str(),
                bgfx::UniformType::Sampler);

            bgfx::TextureHandle attachments[2] =
            {
                bgfx::createTexture2D(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, false, 1,
                    bgfx::TextureFormat::BGRA8, BGFX_TEXTURE_RT),
                bgfx::createTexture2D(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, false, 1,
                    bgfx::TextureFormat::D32F, BGFX_TEXTURE_RT),
            };

            m_ShadowMap.framebuffers[i] = bgfx::createFrameBuffer(2, attachments, true);
        }

        // --- Point light cube shadow map resources ---

        u_lightOrigin = bgfx::createUniform("u_lightOrigin", bgfx::UniformType::Vec4);

        for (std::size_t i = 0; i < MAX_POINT_LIGHTS; ++i)
        {
            s_pointShadowMap[i] = bgfx::createUniform(("s_pointShadowMap" + std::to_string(i)).c_str(),
                bgfx::UniformType::Sampler);

            m_PointShadow.colorCube[i] = bgfx::createTextureCube(POINT_SHADOW_MAP_SIZE, false, 1,
                bgfx::TextureFormat::BGRA8, BGFX_TEXTURE_RT);
            m_PointShadow.depthCube[i] = bgfx::createTextureCube(POINT_SHADOW_MAP_SIZE, false, 1,
                bgfx::TextureFormat::D32F, BGFX_TEXTURE_RT);

            for (std::size_t face = 0; face < CUBE_FACE_COUNT; ++face)
            {
                bgfx::Attachment attachments[2];
                attachments[0].init(m_PointShadow.colorCube[i], bgfx::Access::Write,
                    static_cast<uint16_t>(face), 1, 0, BGFX_RESOLVE_NONE);
                attachments[1].init(m_PointShadow.depthCube[i], bgfx::Access::Write,
                    static_cast<uint16_t>(face), 1, 0, BGFX_RESOLVE_NONE);

                m_PointShadow.faceFramebuffers[i][face] = bgfx::createFrameBuffer(2, attachments, false);
            }
        }

        u_baseColorFactor = bgfx::createUniform("u_baseColorFactor", bgfx::UniformType::Vec4);
        u_metallicRoughnessFactor = bgfx::createUniform("u_metallicRoughnessFactor", bgfx::UniformType::Vec4);
        u_emissiveFactor = bgfx::createUniform("u_emissiveFactor", bgfx::UniformType::Vec4);

        s_albedo = bgfx::createUniform("s_albedo", bgfx::UniformType::Sampler);
        s_normal = bgfx::createUniform("s_normal", bgfx::UniformType::Sampler);
        s_metallicRoughness = bgfx::createUniform("s_metallicRoughness", bgfx::UniformType::Sampler);
        s_emissive = bgfx::createUniform("s_emissive", bgfx::UniformType::Sampler);

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

    void Renderer::DestroySceneResources(Registry& reg)
    {
        // Models created from the same gltf mesh/material share handles,
        // so gather the unique ones first and free each exactly once
        std::vector<uint16_t> vertexBuffers;
        std::vector<uint16_t> indexBuffers;
        std::vector<uint32_t> materialIds;

        for (const Model& model : reg.View<Model>())
        {
            if (bgfx::isValid(model.vbh))
                vertexBuffers.push_back(model.vbh.idx);

            if (bgfx::isValid(model.ibh))
                indexBuffers.push_back(model.ibh.idx);

            for (const Primitive& primitive : model.primitives)
                materialIds.push_back(primitive.materialId);
        }

        auto sortUnique = [] (auto& values)
            {
                std::ranges::sort(values);
                values.erase(std::unique(values.begin(), values.end()), values.end());
            };

        sortUnique(vertexBuffers);
        sortUnique(indexBuffers);
        sortUnique(materialIds);

        for (uint16_t idx : vertexBuffers)
            bgfx::destroy(bgfx::VertexBufferHandle { idx });

        for (uint16_t idx : indexBuffers)
            bgfx::destroy(bgfx::IndexBufferHandle { idx });

        for (uint32_t id : materialIds)
            FreeMaterial(id);

        reg.Clear<Model>();
    }

    MeshBuffers Renderer::UploadMesh(const MeshDesc& data)
    {
        static const bgfx::VertexLayout LAYOUT { MakeVertexLayout() };

        // Every primitive got skipped while loading - nothing to draw
        if (data.indices.empty())
            return MeshBuffers { };

        const bgfx::Memory* vertexMem { bgfx::copy(data.vertices.data(),
            static_cast<uint32_t>(data.vertices.size() * sizeof(Vertex))) };

        const bgfx::Memory* indexMem { bgfx::copy(data.indices.data(),
            static_cast<uint32_t>(data.indices.size() * sizeof(uint32_t))) };

        MeshBuffers buffers;
        buffers.vbh = bgfx::createVertexBuffer(vertexMem, LAYOUT);
        buffers.ibh = bgfx::createIndexBuffer(indexMem, BGFX_BUFFER_INDEX32);

        return buffers;
    }

    uint32_t Renderer::UploadMaterial(const MaterialDesc& desc)
    {
        // Note: operator/ yields the right-hand side as is when it is absolute, which
        // is how loaders hand over texture paths (see MaterialDesc)
        MaterialGPU material;
        material.albedo = glm::vec4(desc.albedo, 1.0f);
        material.metallicRoughness = glm::vec4(desc.metallic, desc.roughness, 0.0f, 0.0f);
        material.emissive = glm::vec4(desc.emissive, 0.0f);

        material.albedoTex = desc.albedoTexture.empty()
            ? m_DefaultAlbedo : LoadTexture(m_DataPath / desc.albedoTexture);

        material.normalTex = desc.normalTexture.empty()
            ? m_DefaultNormal : LoadTexture(m_DataPath / desc.normalTexture);

        material.metallicRoughnessTex = desc.metallicRoughnessTexture.empty()
            ? m_DefaultMetallicRoughness : LoadTexture(m_DataPath / desc.metallicRoughnessTexture);

        material.emissiveTex = desc.emissiveTexture.empty()
            ? m_DefaultEmissive : LoadTexture(m_DataPath / desc.emissiveTexture);

        if (!m_FreeMaterialIds.empty())
        {
            const uint32_t materialIndex { m_FreeMaterialIds.back() };
            m_FreeMaterialIds.pop_back();

            m_Materials[materialIndex] = material;

            return materialIndex;
        }

        const uint32_t materialIndex { static_cast<uint32_t>(m_Materials.size()) };
        m_Materials.push_back(material);

        return materialIndex;
    }

    void Renderer::FreeMaterial(uint32_t id)
    {
        check(id < m_Materials.size(), "Tried to free invalid material by id");

        MaterialGPU& material { m_Materials[id] };

        ReleaseTexture(material.albedoTex);
        ReleaseTexture(material.normalTex);
        ReleaseTexture(material.metallicRoughnessTex);
        ReleaseTexture(material.emissiveTex);

        material = MaterialGPU { };

        m_FreeMaterialIds.push_back(id);
    }



    // ---------------
    // --- Helpers ---
    // ---------------

    void Renderer::GatherLights(EngineCtx& ctx, Scene& scene, Registry& reg,
        glm::vec4& dirDir, glm::vec4& dirColor,
        glm::vec4 (&pointPos)[MAX_POINT_LIGHTS], glm::vec4 (&pointColor)[MAX_POINT_LIGHTS],
        std::size_t& activePointLightCount) const
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
        activePointLightCount = count;

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

    void Renderer::BindShadowUniforms() const
    {
        bgfx::setUniform(u_shadowMapMtx, m_ShadowMap.shadowMapMtx[0], static_cast<std::uint16_t>(NUM_CASCADES));

        for (std::size_t i = 0; i < NUM_CASCADES; ++i)
            bgfx::setTexture(static_cast<uint8_t>(4 + i), s_shadowMap[i], bgfx::getTexture(m_ShadowMap.framebuffers[i]));

        for (std::size_t i = 0; i < MAX_POINT_LIGHTS; ++i)
            bgfx::setTexture(static_cast<uint8_t>(4 + NUM_CASCADES + i), s_pointShadowMap[i], m_PointShadow.colorCube[i]);
    }

    bgfx::TextureHandle Renderer::LoadTexture(const fs::path& path)
    {
        const std::string key { path.string() };

        auto it = m_TextureCache.find(key);

        if (it != m_TextureCache.end())
        {
            ++m_TextureRefs[it->second.idx].count;

            return it->second;
        }

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

        m_TextureCache[key] = handle;

        if (handle.idx >= m_TextureRefs.size())
            m_TextureRefs.resize(handle.idx + 1);

        m_TextureRefs[handle.idx] = TextureRef { key, 1 };

        return handle;
    }

    void Renderer::ReleaseTexture(bgfx::TextureHandle handle)
    {
        // Default/fallback textures never come out of the cache, so they have no refs
        if (handle.idx >= m_TextureRefs.size() || m_TextureRefs[handle.idx].count == 0)
            return;

        TextureRef& ref { m_TextureRefs[handle.idx] };

        if (--ref.count > 0)
            return;

        m_TextureCache.erase(ref.path);
        bgfx::destroy(handle);

        ref = TextureRef { };
    }

    void Renderer::BindMaterial(uint32_t materialIndex) const
    {
        const MaterialGPU& material { m_Materials[materialIndex] };

        bgfx::setUniform(u_baseColorFactor, &material.albedo);
        bgfx::setUniform(u_metallicRoughnessFactor, &material.metallicRoughness);
        bgfx::setUniform(u_emissiveFactor, &material.emissive);

        bgfx::setTexture(0, s_albedo, material.albedoTex);
        bgfx::setTexture(1, s_normal, material.normalTex);
        bgfx::setTexture(2, s_metallicRoughness, material.metallicRoughnessTex);
        bgfx::setTexture(3, s_emissive, material.emissiveTex);
    }



    // -----------------------
    // --- GLTF instancing ---
    // -----------------------

    void Renderer::PreprocessModels(LoadResult& result, Registry& reg, const EngineCtx& ctx, GLTF_UploadCache& cache)
    {
        using GLTF_File = LoadResult::File;
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

                if (file.loadFailed)
                {
                    logError("Invalid gltf_file_id attribute in entity metadata: "
                        "file with id '{}' failed to load", metaNode.loadedFileId);

                    return;
                }

                const std::size_t fileId { metaNode.loadedFileId };

                if (metaNode.name.empty())
                {
                    // Load entire file: every root node (one with no parent)
                    // becomes a child of this entity
                    for (size_t i = 0; i < file.nodes.size(); i++)
                    {
                        GLTF_Node& node { file.nodes[i] };

                        if (node.parentNodeId != GLTF_Node::NO_INDEX) // skip non-root nodes
                            continue;

                        LoadGLTFNode(ctx, entity, metaNode.excludeChildNodes, reg, file, fileId, node,
                            static_cast<uint32_t>(i), false, metaNode.loadChildren, cache);
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

                    LoadGLTFNode(ctx, entity, metaNode.excludeChildNodes, reg, file, fileId, node,
                        static_cast<uint32_t>(nodeId), true, metaNode.loadChildren, cache);
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

                // Empty = keep the mesh's own gltf materials
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

                    overrideIds.push_back(GetGLTFMaterial(cache, file, fileId,
                        static_cast<uint32_t>(matIt->second)));
                }
                else
                {
                    overrideIds.reserve(modelDesc.materialDesc.size());

                    for (const MaterialDesc& matDesc : modelDesc.materialDesc)
                        overrideIds.push_back(UploadMaterial(matDesc));
                }

                Model& model { reg.Has<Model>(entity) ? reg.Get<Model>(entity) : reg.Add<Model>(entity) };

                SetModelMesh(model, cache, file, fileId, meshId, overrideIds);
            });
    }

    void Renderer::LoadGLTFNode(const EngineCtx& ctx, Entity entity, const std::vector<std::string>& excludedNodes,
        Registry& reg, LoadResult::File& file, std::size_t fileId, LoadResult::File::Node& node,
        uint32_t nodeId, bool hasEntity, bool loadChildren, GLTF_UploadCache& cache)
    {
        using GLTF_File = LoadResult::File;
        using GLTF_Node = GLTF_File::Node;

        // Skip children explicitly excluded via the parent's gltf_exclude_child_nodes attribute
        if (!hasEntity)
        {
            for (const std::string& excluded : excludedNodes)
            {
                auto it = file.nameToNodeId.find(excluded);

                if (it != file.nameToNodeId.end() && it->second == nodeId)
                    return;
            }
        }

        Entity e { hasEntity ? entity : reg.Create() };

        Transform& tr { reg.Has<Transform>(e) ? reg.Get<Transform>(e) : reg.Add<Transform>(e) };
        tr.SetLocalPosition(node.position);
        tr.SetLocalRotation(node.rotation);
        tr.SetLocalScale(node.scale);

        if (!hasEntity)
            ctx.transforms.SetParent(reg, e, entity, false);

        if (node.meshId != GLTF_Node::NO_INDEX)
        {
            Model& model { reg.Has<Model>(e) ? reg.Get<Model>(e) : reg.Add<Model>(e) };
            SetModelMesh(model, cache, file, fileId, node.meshId);
        }

        // Optionally load children nodes
        if (loadChildren)
        {
            // Exclusions only apply to the direct children of the entity that declared them
            const std::vector<std::string> noExclusions { };

            for (auto& childNodeId : node.childNodeIds)
            {
                auto& childNode { file.nodes[childNodeId] };

                LoadGLTFNode(ctx, e, hasEntity ? excludedNodes : noExclusions, reg, file, fileId, childNode,
                    static_cast<uint32_t>(childNodeId), false, true, cache);
            }
        }
    }

    void Renderer::SetModelMesh(Model& model, GLTF_UploadCache& cache, const LoadResult::File& file,
        std::size_t fileId, std::size_t meshId, const std::vector<uint32_t>& materialOverrides)
    {
        const MeshDesc& desc { file.meshes[meshId] };

        std::optional<MeshBuffers>& buffers { cache.fileMeshes[fileId][meshId] };

        if (!buffers.has_value())
            buffers = UploadMesh(desc);

        model.vbh = buffers->vbh;
        model.ibh = buffers->ibh;
        model.primitives.clear();
        model.primitives.reserve(desc.primitives.size());

        uint32_t start { 0 };

        for (std::size_t i = 0; i < desc.primitives.size(); ++i)
        {
            const uint32_t count { static_cast<uint32_t>(desc.primitives[i]) };

            const uint32_t materialId { materialOverrides.empty()
                ? GetGLTFMaterial(cache, file, fileId, desc.materials[i])
                : materialOverrides[std::min(i, materialOverrides.size() - 1)] };

            model.primitives.push_back(Primitive { start, count, materialId });

            start += count;
        }
    }

    uint32_t Renderer::GetGLTFMaterial(GLTF_UploadCache& cache, const LoadResult::File& file,
        std::size_t fileId, uint32_t materialId)
    {
        if (materialId == MeshDesc::NO_MATERIAL)
        {
            if (!cache.defaultMaterialId.has_value())
                cache.defaultMaterialId = UploadMaterial(MaterialDesc { });

            return *cache.defaultMaterialId;
        }

        std::optional<uint32_t>& cached { cache.fileMaterialIds[fileId][materialId] };

        if (!cached.has_value())
            cached = UploadMaterial(file.materials[materialId]);

        return *cached;
    }
}