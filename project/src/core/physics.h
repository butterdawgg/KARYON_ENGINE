#ifndef PHYSICS_H
#define PHYSICS_H

#include "entity.h"
#include "registry.h"
#include "engine_ctx.h"
#include "scene_manager.h"

namespace krn
{
    struct Rigidbody
    {

    };

    struct Collider
    {

    };

    class Physics final
    {
        friend class Engine;

        public:

        Physics(SceneManager& scenes);
        ~Physics() = default;

        Physics(const Physics&) = delete;
        Physics& operator=(const Physics&) = delete;
        Physics(Physics&&) noexcept = delete;
        Physics& operator=(Physics&&) noexcept = delete;

        private:

        void OnLoadCallback(const EngineCtx& ctx, Scene& scene, Registry& reg);

        void OnRigidbodyConstructed(Entity e, Rigidbody& rb);
        void OnRigidbodyDestroyed(Entity e, Rigidbody& rb);

        // Called each fixed update by the Engine
        void Update(float dt);
    };
}

#endif