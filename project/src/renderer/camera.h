#ifndef CAMERA_H
#define CAMERA_H

#include "serialization.h"

namespace krn
{
    // Perspective camera. The entity with `primary == true` (first one found,
    // if several) is what the Renderer looks through each frame; an entity
    // needs both a Camera and a Transform to be usable.
    struct Camera : public Registrar<Camera>
    {
        static void Register()
        {
            SerializeManager::RegisterComponent<Camera>("camera")
                .Parameter("fov", &Camera::FOV)
                .Parameter("nearPlane", &Camera::nearPlane)
                .Parameter("farPlane", &Camera::farPlane)
                .Parameter("primary", &Camera::primary);
        }

        float FOV { 60.0f }; // vertical, degrees
        float nearPlane { 0.1f };
        float farPlane { 1000.0f };
        bool primary { true };
    };

    // Utility function to find camera with the highest priority (used by Renderer)
    inline Entity FindPrimaryCamera(Registry& reg)
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
}

#endif // !CAMERA_H