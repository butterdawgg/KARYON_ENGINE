#include "shadow_map.h"

#include <bx/math.h>

namespace krn
{
    namespace
    {
        // GPU Gems 3, ch. 10 "Parallel-Split Shadow Maps" practical split scheme
        void CalculateCascadeSplits(float* cascadeSplits, std::size_t numSplits,
            float nearClip, float farClip, float splitLambda)
        {
            const float clipRange = farClip - nearClip;
            const float minZ = nearClip;
            const float maxZ = nearClip + clipRange;
            const float range = maxZ - minZ;
            const float ratio = maxZ / minZ;
            const float numSlicesF = static_cast<float>(numSplits) * 2.5f;

            for (std::size_t i = 0; i < numSplits; ++i)
            {
                const float si = static_cast<float>(i * 2 + 1) / numSlicesF;
                const float logSplit = minZ * bx::pow(ratio, si);
                const float uniformSplit = minZ + range * si;
                const float d = splitLambda * (logSplit - uniformSplit) + uniformSplit;

                cascadeSplits[i] = (d - nearClip) / clipRange;
            }
        }

        // Reconstructs the 8 world-space corners of the camera's sub-frustum
        // between near/far, from its inverse-view matrix and precomputed
        // tan(fovY/2)-derived proj width/height
        void WorldSpaceFrustumCorners(bx::Vec3 (&outCorners)[8], float near, float far,
            float projWidth, float projHeight, const float* invView)
        {
            const float nw = near * projWidth;
            const float nh = near * projHeight;
            const float fw = far * projWidth;
            const float fh = far * projHeight;

            const bx::Vec3 corners[8] =
            {
                { -nw,  nh, near }, {  nw,  nh, near }, {  nw, -nh, near }, { -nw, -nh, near },
                { -fw,  fh, far  }, {  fw,  fh, far  }, {  fw, -fh, far  }, { -fw, -fh, far  },
            };

            for (std::size_t i = 0; i < 8; ++i)
                outCorners[i] = bx::mul(corners[i], invView);
        }
    }

    void ComputeCascadeData(ShadowMapResources& shadows, const float* camInvView,
        float camFovYDegrees, float camAspect, float camNear, float camFar,
        const glm::vec3& lightDirection)
    {
        const float nearClip = camNear;
        const float farClip = bx::min(camFar, SHADOW_MAX_DISTANCE);
        const float clipRange = farClip - nearClip;

        float cascadeSplits[NUM_CASCADES];
        CalculateCascadeSplits(cascadeSplits, NUM_CASCADES, nearClip, farClip, SHADOW_SPLIT_LAMBDA);

        const float projHeight = bx::tan(bx::toRad(camFovYDegrees) * 0.5f);
        const float projWidth = projHeight * camAspect;

        const float lightLenSq { glm::dot(lightDirection, lightDirection) };
        const glm::vec3 lightDirNorm { lightLenSq > 1.0e-8f
            ? lightDirection * (1.0f / bx::sqrt(lightLenSq)) : glm::vec3(0.0f, -1.0f, 0.0f) };

        const bx::Vec3 lightDir { lightDirNorm.x, lightDirNorm.y, lightDirNorm.z };
        const bx::Vec3 lightEye { 0.0f, 0.0f, 0.0f };
        const bx::Vec3 lightAt { lightDir };

        bx::Vec3 upVec { 0.0f, 1.0f, 0.0f };
        if (bx::abs(bx::dot(lightDir, upVec)) > 0.99f)
            upVec = { 0.0f, 0.0f, 1.0f };

        bx::mtxLookAt(shadows.lightView, lightEye, lightAt, upVec);

        const bgfx::Caps* caps { bgfx::getCaps() };

        const float zScale { caps->homogeneousDepth ? 0.5f : 1.0f };
        const float zOffset { caps->homogeneousDepth ? 0.5f : 0.0f };
        const float yMul { caps->originBottomLeft ? 0.5f : -0.5f };

        const float biasMtx[16] =
        {
            0.5f, 0.0f, 0.0f,    0.0f,
            0.0f, yMul, 0.0f,    0.0f,
            0.0f, 0.0f, zScale,  0.0f,
            0.5f, 0.5f, zOffset, 1.0f,
        };

        float lastSplitDist { 0.0f };

        for (std::size_t i = 0; i < NUM_CASCADES; ++i)
        {
            const float splitDist { cascadeSplits[i] };
            const float cascadeNear { nearClip + lastSplitDist * clipRange };
            const float cascadeFar { nearClip + splitDist * clipRange };

            bx::Vec3 corners[8] =
            {
                { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f },
                { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f },
            };

            WorldSpaceFrustumCorners(corners, cascadeNear, cascadeFar, projWidth, projHeight, camInvView);

            bx::Vec3 center { 0.0f, 0.0f, 0.0f };
            for (const bx::Vec3& c : corners)
                center = bx::add(center, c);
            center = bx::mul(center, 1.0f / 8.0f);

            const bx::Vec3 lightSpaceCenter { bx::mul(center, shadows.lightView) };

            float radius { 0.0f };

            for (const bx::Vec3& c : corners)
            {
                const bx::Vec3 xyz { bx::mul(c, shadows.lightView) };
                const float dist { bx::length(bx::sub(xyz, lightSpaceCenter)) };

                radius = bx::max(radius, dist);
            }

            radius = bx::ceil(radius * 8.0f) / 8.0f;

            const float texelWorldSize { (2.0f * radius) / static_cast<float>(SHADOW_MAP_SIZE) };
            const float centerX { bx::floor(lightSpaceCenter.x / texelWorldSize) * texelWorldSize };
            const float centerY { bx::floor(lightSpaceCenter.y / texelWorldSize) * texelWorldSize };

            const float zMargin { radius + SHADOW_MAX_DISTANCE };

            bx::mtxOrtho(shadows.lightProj[i],
                centerX - radius, centerX + radius,
                centerY - radius, centerY + radius,
                lightSpaceCenter.z - zMargin, lightSpaceCenter.z + zMargin,
                0.0f, caps->homogeneousDepth);

            float viewProjBias[16];
            bx::mtxMul(viewProjBias, shadows.lightProj[i], biasMtx);
            bx::mtxMul(shadows.shadowMapMtx[i], shadows.lightView, viewProjBias);

            lastSplitDist = splitDist;
        }
    }

    void ComputePointLightViewProj(std::uint8_t face, const glm::vec3& lightPos, float range,
        float* outView, float* outProj)
    {
        // bgfx cubemap layer order: +X,-X,+Y,-Y,+Z,-Z
        static const bx::Vec3 DIRS[CUBE_FACE_COUNT] =
        {
            {  1.0f,  0.0f,  0.0f }, { -1.0f,  0.0f,  0.0f },
            {  0.0f,  1.0f,  0.0f }, {  0.0f, -1.0f,  0.0f },
            {  0.0f,  0.0f,  1.0f }, {  0.0f,  0.0f, -1.0f },
        };

        static const bx::Vec3 UPS[CUBE_FACE_COUNT] =
        {
            { 0.0f, -1.0f,  0.0f }, { 0.0f, -1.0f,  0.0f },
            { 0.0f,  0.0f,  1.0f }, { 0.0f,  0.0f, -1.0f },
            { 0.0f, -1.0f,  0.0f }, { 0.0f, -1.0f,  0.0f },
        };

        const bx::Vec3 eye { lightPos.x, lightPos.y, lightPos.z };
        const bx::Vec3 at { bx::add(eye, DIRS[face]) };

        bx::mtxLookAt(outView, eye, at, UPS[face]);

        const float farClip { bx::max(range, POINT_SHADOW_NEAR * 2.0f) };

        bx::mtxProj(outProj, 90.0f, 1.0f, POINT_SHADOW_NEAR, farClip, bgfx::getCaps()->homogeneousDepth);
    }
}