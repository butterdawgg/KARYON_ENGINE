#ifndef SHADOW_MAP_H
#define SHADOW_MAP_H

#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>

#include <bgfx/bgfx.h>



namespace krn
{
    inline constexpr std::size_t MAX_POINT_LIGHTS { 4 };



    // ---------------------------------
    // --- Directional light cascades ---
    // ---------------------------------

    inline constexpr std::size_t NUM_CASCADES { 4 };
    inline constexpr std::uint16_t SHADOW_MAP_SIZE { 2048 };

    // Clamps how far cascades extend along the camera's view direction
    inline constexpr float SHADOW_MAX_DISTANCE { 100.0f };

    // GPU Gems 3 log/uniform split blend factor - 0 = fully uniform splits, 1 = fully logarithmic
    inline constexpr float SHADOW_SPLIT_LAMBDA { 0.6f };

    // Every GPU/CPU resource the directional light's cascaded shadow map needs
    struct ShadowMapResources
    {
        bgfx::FrameBufferHandle framebuffers[NUM_CASCADES] { };

        float lightView[16] { }; // shared by every cascade - the light has no position, only a direction
        float lightProj[NUM_CASCADES][16] { }; // per-cascade bounding-sphere-fitted ortho projection

        // shadowMapMtx[i] = lightView * lightProj[i] * biasMtx
        float shadowMapMtx[NUM_CASCADES][16] { };
    };

    // Recomputes every cascade's split distances, light view/projection and
    // shadowMapMtx for the current frame's camera + light direction
    void ComputeCascadeData(ShadowMapResources& shadows, const float* camInvView,
        float camFovYDegrees, float camAspect, float camNear, float camFar,
        const glm::vec3& lightDirection);



    // ------------------------------------
    // --- Point light cube shadow maps ---
    // ------------------------------------

    inline constexpr std::uint16_t POINT_SHADOW_MAP_SIZE { 512 };
    inline constexpr float POINT_SHADOW_NEAR { 0.05f };

    inline constexpr std::uint8_t CUBE_FACE_COUNT { 6 };

    // One depth cubemap pair (BGRA8 packed linear distance + D32F hardware
    // depth-test-only) and 6 per-face framebuffers per point light slot
    struct PointShadowMapResources
    {
        bgfx::TextureHandle colorCube[MAX_POINT_LIGHTS] { };
        bgfx::TextureHandle depthCube[MAX_POINT_LIGHTS] { };
        bgfx::FrameBufferHandle faceFramebuffers[MAX_POINT_LIGHTS][CUBE_FACE_COUNT] { };
    };

    // Builds the view/projection matrices for one face of a point light's
    // shadow cube (90 degree FOV, looking down that face's axis-aligned
    // direction). face must be in [0, CUBE_FACE_COUNT) and follows bgfx's
    // cubemap layer order: +X,-X,+Y,-Y,+Z,-Z
    void ComputePointLightViewProj(std::uint8_t face, const glm::vec3& lightPos, float range,
        float* outView, float* outProj);
}

#endif // !SHADOW_MAP_H