#ifndef LIGHTS_H
#define LIGHTS_H

#include "serialization.h"

namespace krn
{
    // Sun-style light with no position, only a direction
    struct DirectionalLight : public Registrar<DirectionalLight>
    {
        static void Register()
        {
            SerializeManager::RegisterComponent<DirectionalLight>("directional_light")
                .Parameter("color", &DirectionalLight::color)
                .Parameter("intensity", &DirectionalLight::intensity);
        }

        glm::vec3 color { 1.0f, 1.0f, 1.0f };
        float intensity { 1.0f };
    };

    // Omnidirectional light at the entity's Transform world position
    struct PointLight : public Registrar<PointLight>
    {
        static void Register()
        {
            SerializeManager::RegisterComponent<PointLight>("point_light")
                .Parameter("color", &PointLight::color)
                .Parameter("intensity", &PointLight::intensity)
                .Parameter("range", &PointLight::range);
        }

        glm::vec3 color { 1.0f, 1.0f, 1.0f };
        float intensity { 1.0f };
        float range { 10.0f };
    };
}

#endif // !LIGHTS_H