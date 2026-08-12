#ifndef PHYSICS_H
#define PHYSICS_H

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "entity.h"
#include "registry.h"
#include "engine_ctx.h"
#include "scene_manager.h"

namespace krn
{
    class TransformManager;

    enum class BodyType : std::uint8_t
    {
        Static,
        Kinematic,
        Dynamic
    };

    enum class ColliderShape : std::uint8_t
    {
        Box,
        Sphere,
        Capsule
    };

    // Drives a physics body. Add a Collider alongside it to give the body
    // something to collide with - a Rigidbody with no Collider simulates but
    // never touches anything. Use BodyType::Static for immovable geometry.
    struct Rigidbody
    {
        friend class Physics;

        BodyType type { BodyType::Dynamic };

        glm::vec3 linearVelocity { 0.0f, 0.0f, 0.0f };
        glm::vec3 angularVelocity { 0.0f, 0.0f, 0.0f };

        float linearDamping { 0.0f };
        float angularDamping { 0.0f };
        float gravityScale { 1.0f };

        bool enableSleep { true };
        bool isBullet { false };

        private:

        // Packed body id, owned and interpreted only by Physics. Zero means
        // "no simulation body created yet".
        std::uint64_t m_BodyHandle { 0 };
    };

    // A collision shape. Requires a sibling Rigidbody to attach to (use
    // BodyType::Static for immovable geometry) - a Collider with no
    // Rigidbody on the same entity never enters the simulation.
    struct Collider
    {
        friend class Physics;

        ColliderShape shape { ColliderShape::Box };

        glm::vec3 boxHalfExtents { 0.5f, 0.5f, 0.5f }; // Box only
        float radius { 0.5f };                          // Sphere & Capsule
        float capsuleHalfHeight { 0.5f };                // Capsule only: half-length of the cylindrical segment, excluding the end caps

        glm::vec3 center { 0.0f, 0.0f, 0.0f }; // local offset from the body origin

        float density { 1.0f };
        float friction { 0.6f };
        float restitution { 0.0f };
        bool isSensor { false };

        // Collision filtering: a query only reports a shape whose categoryBits
        // intersect the query's mask, and shapes only collide with each other
        // when each one's categoryBits intersect the other's maskBits.
        std::uint64_t categoryBits { ~0ull };
        std::uint64_t maskBits { ~0ull };

        private:

        // Packed shape id, owned and interpreted only by Physics. Zero means
        // "no simulation shape created yet".
        std::uint64_t m_ShapeHandle { 0 };
    };

    class Physics final
    {
        friend class Engine;

        public:

        struct CastHit
        {
            Entity entity { NULL_ENTITY };
            glm::vec3 point { 0.0f, 0.0f, 0.0f };
            glm::vec3 normal { 0.0f, 0.0f, 0.0f };
            float fraction { 0.0f };
        };

        static constexpr std::uint64_t K_ALL_CATEGORIES { ~0ull };

        Physics(SceneManager& scenes);
        ~Physics();

        Physics(const Physics&) = delete;
        Physics& operator=(const Physics&) = delete;
        Physics(Physics&&) noexcept = delete;
        Physics& operator=(Physics&&) noexcept = delete;

        // --- World ---

        void SetGravity(const glm::vec3& gravity);
        glm::vec3 GetGravity() const;

        // --- Body control ---

        void SetLinearVelocity(Rigidbody& rb, const glm::vec3& velocity);
        glm::vec3 GetLinearVelocity(const Rigidbody& rb) const;

        void SetAngularVelocity(Rigidbody& rb, const glm::vec3& velocity);
        glm::vec3 GetAngularVelocity(const Rigidbody& rb) const;

        void ApplyForce(Rigidbody& rb, const glm::vec3& force, const glm::vec3& worldPoint, bool wake = true);
        void ApplyForceToCenter(Rigidbody& rb, const glm::vec3& force, bool wake = true);
        void ApplyTorque(Rigidbody& rb, const glm::vec3& torque, bool wake = true);

        void ApplyLinearImpulse(Rigidbody& rb, const glm::vec3& impulse, const glm::vec3& worldPoint, bool wake = true);
        void ApplyLinearImpulseToCenter(Rigidbody& rb, const glm::vec3& impulse, bool wake = true);
        void ApplyAngularImpulse(Rigidbody& rb, const glm::vec3& impulse, bool wake = true);

        bool IsAwake(const Rigidbody& rb) const;
        void SetAwake(Rigidbody& rb, bool awake);

        // --- Queries ---

        // Closest hit along the ray, up to maxDistance. Returns false if nothing was hit.
        bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
            CastHit& outHit, std::uint64_t categoryMask = K_ALL_CATEGORIES) const;

        // Every shape along the ray, in no particular order. Returns the hit count.
        std::size_t RaycastAll(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
            std::vector<CastHit>& outHits, std::uint64_t categoryMask = K_ALL_CATEGORIES) const;

        // Closest hit of a shape swept from origin along direction, up to maxDistance.
        bool SphereCast(const glm::vec3& origin, float radius, const glm::vec3& direction, float maxDistance,
            CastHit& outHit, std::uint64_t categoryMask = K_ALL_CATEGORIES) const;

        bool CapsuleCast(const glm::vec3& origin, float radius, float halfHeight, const glm::quat& rotation,
            const glm::vec3& direction, float maxDistance, CastHit& outHit,
            std::uint64_t categoryMask = K_ALL_CATEGORIES) const;

        bool BoxCast(const glm::vec3& origin, const glm::vec3& halfExtents, const glm::quat& rotation,
            const glm::vec3& direction, float maxDistance, CastHit& outHit,
            std::uint64_t categoryMask = K_ALL_CATEGORIES) const;

        // Overlap queries: entities whose collider currently overlaps the given volume. Returns whether anything was found.
        bool OverlapSphere(const glm::vec3& center, float radius, std::vector<Entity>& outEntities,
            std::uint64_t categoryMask = K_ALL_CATEGORIES) const;

        bool OverlapBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation,
            std::vector<Entity>& outEntities, std::uint64_t categoryMask = K_ALL_CATEGORIES) const;

        // Penetration between two entities' colliders, read from the current simulation state
        // (i.e. as of the last fixed update). Returns false if they aren't touching.
        // outNormal points from a's collider toward b's; outDepth is the positive overlap distance.
        bool ComputePenetration(Registry& reg, Entity a, Entity b, glm::vec3& outNormal, float& outDepth) const;

        private:

        void OnLoadCallback(const EngineCtx& ctx, Scene& scene, Registry& reg);
        void OnUnloadCallback(const EngineCtx& ctx, Scene& scene, Registry& reg);

        void OnRigidbodyConstructed(Registry& reg, Entity e, Rigidbody& rb);
        void OnRigidbodyDestroyed(Registry& reg, Entity e, Rigidbody& rb);

        void OnColliderConstructed(Registry& reg, Entity e, Collider& col);
        void OnColliderDestroyed(Registry& reg, Entity e, Collider& col);

        void CreateShapeForCollider(Entity e, Rigidbody& rb, Collider& col);

        // Called each fixed update by the Engine
        void Update(const EngineCtx& ctx, Registry& reg, float dt);

        std::uint32_t m_WorldHandle { 0 }; // packed world id
        int m_SubStepCount { 4 };

        // The transforms of the currently loaded scene, valid between
        // OnLoadCallback and OnUnloadCallback
        TransformManager* m_ActiveTransforms { nullptr };
    };
}

#endif
