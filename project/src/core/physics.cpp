#include "physics.h"

#include <array>
#include <limits>

#include <box3d/box3d.h>

#include "transform.h"
#include "utils.h"

namespace krn
{
    namespace
    {
        // --- glm <-> box3d conversions (kept local to this file so no box3d type ever appears in physics.h) ---

        b3Vec3 ToB3(const glm::vec3& v) { return b3Vec3 { v.x, v.y, v.z }; }
        glm::vec3 ToGlm(const b3Vec3& v) { return glm::vec3(v.x, v.y, v.z); }

        b3Quat ToB3(const glm::quat& q) { return b3Quat { b3Vec3 { q.x, q.y, q.z }, q.w }; }
        glm::quat ToGlm(const b3Quat& q) { return glm::quat(q.s, q.v.x, q.v.y, q.v.z); }

        b3BodyType ToB3(BodyType type)
        {
            switch (type)
            {
                case BodyType::Static: return b3_staticBody;
                case BodyType::Kinematic: return b3_kinematicBody;
                case BodyType::Dynamic: default: return b3_dynamicBody;
            }
        }

        // Bodies/shapes stash the owning Entity as userData so query callbacks and body-move
        // events can resolve back to an Entity without any side table.
        void* EntityToUserData(Entity e)
        {
            return reinterpret_cast<void*>(static_cast<std::uintptr_t>(e));
        }

        Entity UserDataToEntity(void* userData)
        {
            return static_cast<Entity>(reinterpret_cast<std::uintptr_t>(userData));
        }

        // The 8 corners of an oriented box, as offsets from its center - used to build a
        // b3ShapeProxy for box casts/overlaps (box3d proxies are point clouds relative to
        // a query origin, so orientation has to be baked into the points themselves).
        std::array<b3Vec3, 8> BoxProxyPoints(const glm::vec3& halfExtents, const glm::quat& rotation)
        {
            return
            {
                ToB3(rotation * glm::vec3( halfExtents.x,  halfExtents.y,  halfExtents.z)),
                ToB3(rotation * glm::vec3(-halfExtents.x,  halfExtents.y,  halfExtents.z)),
                ToB3(rotation * glm::vec3( halfExtents.x, -halfExtents.y,  halfExtents.z)),
                ToB3(rotation * glm::vec3(-halfExtents.x, -halfExtents.y,  halfExtents.z)),
                ToB3(rotation * glm::vec3( halfExtents.x,  halfExtents.y, -halfExtents.z)),
                ToB3(rotation * glm::vec3(-halfExtents.x,  halfExtents.y, -halfExtents.z)),
                ToB3(rotation * glm::vec3( halfExtents.x, -halfExtents.y, -halfExtents.z)),
                ToB3(rotation * glm::vec3(-halfExtents.x, -halfExtents.y, -halfExtents.z)),
            };
        }

        std::array<b3Vec3, 2> CapsuleProxyPoints(float halfHeight, const glm::quat& rotation)
        {
            return
            {
                ToB3(rotation * glm::vec3(0.0f, halfHeight, 0.0f)),
                ToB3(rotation * glm::vec3(0.0f, -halfHeight, 0.0f)),
            };
        }

        // --- Query callback trampolines ---

        struct RaycastAllContext
        {
            std::vector<Physics::CastHit>* hits;
        };

        float RaycastAllCallback(b3ShapeId shapeId, b3Pos point, b3Vec3 normal, float fraction,
            std::uint64_t, int, int, void* context)
        {
            auto* ctx = static_cast<RaycastAllContext*>(context);

            ctx->hits->push_back(Physics::CastHit {
                UserDataToEntity(b3Shape_GetUserData(shapeId)),
                ToGlm(point),
                ToGlm(normal),
                fraction
            });

            return 1.0f; // keep the ray unclipped and continue
        }

        struct ClosestCastContext
        {
            bool hit { false };
            b3ShapeId shapeId { };
            b3Pos point { };
            b3Vec3 normal { };
            float fraction { 1.0f };
        };

        float ClosestCastCallback(b3ShapeId shapeId, b3Pos point, b3Vec3 normal, float fraction,
            std::uint64_t, int, int, void* context)
        {
            auto* ctx = static_cast<ClosestCastContext*>(context);

            ctx->hit = true;
            ctx->shapeId = shapeId;
            ctx->point = point;
            ctx->normal = normal;
            ctx->fraction = fraction;

            return fraction; // clip subsequent candidates to this fraction -> closest hit wins
        }

        bool CastShapeClosest(b3WorldId world, const b3ShapeProxy& proxy, const glm::vec3& origin,
            const glm::vec3& translation, std::uint64_t categoryMask, Physics::CastHit& outHit)
        {
            b3QueryFilter filter = b3DefaultQueryFilter();
            filter.maskBits = categoryMask;

            ClosestCastContext ctx { };

            b3World_CastShape(world, ToB3(origin), &proxy, ToB3(translation), filter, &ClosestCastCallback, &ctx);

            if (!ctx.hit)
                return false;

            outHit.entity = UserDataToEntity(b3Shape_GetUserData(ctx.shapeId));
            outHit.point = ToGlm(ctx.point);
            outHit.normal = ToGlm(ctx.normal);
            outHit.fraction = ctx.fraction;

            return true;
        }

        struct OverlapContext
        {
            std::vector<Entity>* entities;
        };

        bool OverlapCallback(b3ShapeId shapeId, void* context)
        {
            auto* ctx = static_cast<OverlapContext*>(context);

            ctx->entities->push_back(UserDataToEntity(b3Shape_GetUserData(shapeId)));

            return true; // keep collecting
        }
    }

    Physics::Physics(SceneManager& scenes)
    {
        b3WorldDef worldDef = b3DefaultWorldDef();
        worldDef.gravity = ToB3(glm::vec3(0.0f, -9.81f, 0.0f));

        m_WorldHandle = b3StoreWorldId(b3CreateWorld(&worldDef));

        scenes.OnPreprocess.Connect<&Physics::OnLoadCallback>(*this);
        scenes.OnUnload.Connect<&Physics::OnUnloadCallback>(*this);
    }

    Physics::~Physics()
    {
        b3DestroyWorld(b3LoadWorldId(m_WorldHandle));
    }

    void Physics::SetGravity(const glm::vec3& gravity)
    {
        b3World_SetGravity(b3LoadWorldId(m_WorldHandle), ToB3(gravity));
    }

    glm::vec3 Physics::GetGravity() const
    {
        return ToGlm(b3World_GetGravity(b3LoadWorldId(m_WorldHandle)));
    }

    void Physics::SetLinearVelocity(Rigidbody& rb, const glm::vec3& velocity)
    {
        if (rb.m_BodyHandle == 0) return;

        b3Body_SetLinearVelocity(b3LoadBodyId(rb.m_BodyHandle), ToB3(velocity));
    }

    glm::vec3 Physics::GetLinearVelocity(const Rigidbody& rb) const
    {
        if (rb.m_BodyHandle == 0) return glm::vec3(0.0f);

        return ToGlm(b3Body_GetLinearVelocity(b3LoadBodyId(rb.m_BodyHandle)));
    }

    void Physics::SetAngularVelocity(Rigidbody& rb, const glm::vec3& velocity)
    {
        if (rb.m_BodyHandle == 0) return;

        b3Body_SetAngularVelocity(b3LoadBodyId(rb.m_BodyHandle), ToB3(velocity));
    }

    glm::vec3 Physics::GetAngularVelocity(const Rigidbody& rb) const
    {
        if (rb.m_BodyHandle == 0) return glm::vec3(0.0f);

        return ToGlm(b3Body_GetAngularVelocity(b3LoadBodyId(rb.m_BodyHandle)));
    }

    void Physics::ApplyForce(Rigidbody& rb, const glm::vec3& force, const glm::vec3& worldPoint, bool wake)
    {
        if (rb.m_BodyHandle == 0) return;

        b3Body_ApplyForce(b3LoadBodyId(rb.m_BodyHandle), ToB3(force), ToB3(worldPoint), wake);
    }

    void Physics::ApplyForceToCenter(Rigidbody& rb, const glm::vec3& force, bool wake)
    {
        if (rb.m_BodyHandle == 0) return;

        b3Body_ApplyForceToCenter(b3LoadBodyId(rb.m_BodyHandle), ToB3(force), wake);
    }

    void Physics::ApplyTorque(Rigidbody& rb, const glm::vec3& torque, bool wake)
    {
        if (rb.m_BodyHandle == 0) return;

        b3Body_ApplyTorque(b3LoadBodyId(rb.m_BodyHandle), ToB3(torque), wake);
    }

    void Physics::ApplyLinearImpulse(Rigidbody& rb, const glm::vec3& impulse, const glm::vec3& worldPoint, bool wake)
    {
        if (rb.m_BodyHandle == 0) return;

        b3Body_ApplyLinearImpulse(b3LoadBodyId(rb.m_BodyHandle), ToB3(impulse), ToB3(worldPoint), wake);
    }

    void Physics::ApplyLinearImpulseToCenter(Rigidbody& rb, const glm::vec3& impulse, bool wake)
    {
        if (rb.m_BodyHandle == 0) return;

        b3Body_ApplyLinearImpulseToCenter(b3LoadBodyId(rb.m_BodyHandle), ToB3(impulse), wake);
    }

    void Physics::ApplyAngularImpulse(Rigidbody& rb, const glm::vec3& impulse, bool wake)
    {
        if (rb.m_BodyHandle == 0) return;

        b3Body_ApplyAngularImpulse(b3LoadBodyId(rb.m_BodyHandle), ToB3(impulse), wake);
    }

    bool Physics::IsAwake(const Rigidbody& rb) const
    {
        if (rb.m_BodyHandle == 0) return false;

        return b3Body_IsAwake(b3LoadBodyId(rb.m_BodyHandle));
    }

    void Physics::SetAwake(Rigidbody& rb, bool awake)
    {
        if (rb.m_BodyHandle == 0) return;

        b3Body_SetAwake(b3LoadBodyId(rb.m_BodyHandle), awake);
    }

    bool Physics::Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
        CastHit& outHit, std::uint64_t categoryMask) const
    {
        b3QueryFilter filter = b3DefaultQueryFilter();
        filter.maskBits = categoryMask;

        b3RayResult result = b3World_CastRayClosest(b3LoadWorldId(m_WorldHandle),
            ToB3(origin), ToB3(glm::normalize(direction) * maxDistance), filter);

        if (!result.hit)
            return false;

        outHit.entity = UserDataToEntity(b3Shape_GetUserData(result.shapeId));
        outHit.point = ToGlm(result.point);
        outHit.normal = ToGlm(result.normal);
        outHit.fraction = result.fraction;

        return true;
    }

    std::size_t Physics::RaycastAll(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
        std::vector<CastHit>& outHits, std::uint64_t categoryMask) const
    {
        outHits.clear();

        b3QueryFilter filter = b3DefaultQueryFilter();
        filter.maskBits = categoryMask;

        RaycastAllContext ctx { &outHits };

        b3World_CastRay(b3LoadWorldId(m_WorldHandle), ToB3(origin),
            ToB3(glm::normalize(direction) * maxDistance), filter, &RaycastAllCallback, &ctx);

        return outHits.size();
    }

    bool Physics::SphereCast(const glm::vec3& origin, float radius, const glm::vec3& direction, float maxDistance,
        CastHit& outHit, std::uint64_t categoryMask) const
    {
        const b3Vec3 localOrigin { 0.0f, 0.0f, 0.0f };
        b3ShapeProxy proxy { &localOrigin, 1, radius };

        return CastShapeClosest(b3LoadWorldId(m_WorldHandle), proxy, origin,
            glm::normalize(direction) * maxDistance, categoryMask, outHit);
    }

    bool Physics::CapsuleCast(const glm::vec3& origin, float radius, float halfHeight, const glm::quat& rotation,
        const glm::vec3& direction, float maxDistance, CastHit& outHit, std::uint64_t categoryMask) const
    {
        std::array<b3Vec3, 2> points = CapsuleProxyPoints(halfHeight, rotation);
        b3ShapeProxy proxy { points.data(), static_cast<int>(points.size()), radius };

        return CastShapeClosest(b3LoadWorldId(m_WorldHandle), proxy, origin,
            glm::normalize(direction) * maxDistance, categoryMask, outHit);
    }

    bool Physics::BoxCast(const glm::vec3& origin, const glm::vec3& halfExtents, const glm::quat& rotation,
        const glm::vec3& direction, float maxDistance, CastHit& outHit, std::uint64_t categoryMask) const
    {
        std::array<b3Vec3, 8> points = BoxProxyPoints(halfExtents, rotation);
        b3ShapeProxy proxy { points.data(), static_cast<int>(points.size()), 0.0f };

        return CastShapeClosest(b3LoadWorldId(m_WorldHandle), proxy, origin,
            glm::normalize(direction) * maxDistance, categoryMask, outHit);
    }

    bool Physics::OverlapSphere(const glm::vec3& center, float radius, std::vector<Entity>& outEntities,
        std::uint64_t categoryMask) const
    {
        outEntities.clear();

        const b3Vec3 localOrigin { 0.0f, 0.0f, 0.0f };
        b3ShapeProxy proxy { &localOrigin, 1, radius };

        b3QueryFilter filter = b3DefaultQueryFilter();
        filter.maskBits = categoryMask;

        OverlapContext ctx { &outEntities };

        b3World_OverlapShape(b3LoadWorldId(m_WorldHandle), ToB3(center), &proxy, filter, &OverlapCallback, &ctx);

        return !outEntities.empty();
    }

    bool Physics::OverlapBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation,
        std::vector<Entity>& outEntities, std::uint64_t categoryMask) const
    {
        outEntities.clear();

        std::array<b3Vec3, 8> points = BoxProxyPoints(halfExtents, rotation);
        b3ShapeProxy proxy { points.data(), static_cast<int>(points.size()), 0.0f };

        b3QueryFilter filter = b3DefaultQueryFilter();
        filter.maskBits = categoryMask;

        OverlapContext ctx { &outEntities };

        b3World_OverlapShape(b3LoadWorldId(m_WorldHandle), ToB3(center), &proxy, filter, &OverlapCallback, &ctx);

        return !outEntities.empty();
    }

    bool Physics::ComputePenetration(Entity a, Entity b, glm::vec3& outNormal, float& outDepth) const
    {
        if (m_ActiveRegistry == nullptr || !m_ActiveRegistry->Has<Collider>(a) || !m_ActiveRegistry->Has<Collider>(b))
            return false;

        Collider& colA = m_ActiveRegistry->Get<Collider>(a);
        Collider& colB = m_ActiveRegistry->Get<Collider>(b);

        if (colA.m_ShapeHandle == 0 || colB.m_ShapeHandle == 0)
            return false;

        b3ShapeId shapeA = b3LoadShapeId(colA.m_ShapeHandle);
        b3ShapeId shapeB = b3LoadShapeId(colB.m_ShapeHandle);

        int capacity = b3Shape_GetContactCapacity(shapeA);

        if (capacity <= 0)
            return false;

        std::vector<b3ContactData> contacts(static_cast<std::size_t>(capacity));
        int count = b3Shape_GetContactData(shapeA, contacts.data(), capacity);

        for (int i = 0; i < count; ++i)
        {
            const b3ContactData& contact = contacts[i];

            bool matchesAB = B3_ID_EQUALS(contact.shapeIdB, shapeB);
            bool matchesBA = !matchesAB && B3_ID_EQUALS(contact.shapeIdA, shapeB);

            if (!matchesAB && !matchesBA)
                continue;

            float minSeparation = std::numeric_limits<float>::max();
            glm::vec3 normal { 0.0f, 0.0f, 0.0f };

            for (int m = 0; m < contact.manifoldCount; ++m)
            {
                const b3Manifold& manifold = contact.manifolds[m];

                for (int p = 0; p < manifold.pointCount; ++p)
                {
                    if (manifold.points[p].separation < minSeparation)
                    {
                        minSeparation = manifold.points[p].separation;
                        normal = ToGlm(manifold.normal);
                    }
                }
            }

            if (minSeparation >= 0.0f)
                return false;

            // The manifold normal points from shapeIdA to shapeIdB; flip it to always point from a to b
            outNormal = matchesAB ? normal : -normal;
            outDepth = -minSeparation;

            return true;
        }

        return false;
    }

    void Physics::OnLoadCallback(const EngineCtx& ctx, Scene&, Registry& reg)
    {
        m_ActiveRegistry = &reg;
        m_ActiveTransforms = &ctx.transforms;

        // Process already loaded Rigidbodies before Colliders, so a Collider deserialized
        // alongside a Rigidbody always finds its body already created
        reg.View<Rigidbody>().ForEach([this] (Entity e, Rigidbody& rb) -> void
            {
                OnRigidbodyConstructed(e, rb);
            });

        reg.View<Collider>().ForEach([this] (Entity e, Collider& col) -> void
            {
                OnColliderConstructed(e, col);
            });

        // Subscribe to construction/destruction signals
        reg.OnConstruct<Rigidbody>().Connect<&Physics::OnRigidbodyConstructed>(*this);
        reg.OnDestroy<Rigidbody>().Connect<&Physics::OnRigidbodyDestroyed>(*this);

        reg.OnConstruct<Collider>().Connect<&Physics::OnColliderConstructed>(*this);
        reg.OnDestroy<Collider>().Connect<&Physics::OnColliderDestroyed>(*this);
    }

    void Physics::OnUnloadCallback(const EngineCtx&, Scene&, Registry& reg)
    {
        // The scene's Registry is about to be torn down without going through Registry::Destroy
        // per entity, so bodies need to be swept here explicitly or they'd outlive their entities
        reg.View<Rigidbody>().ForEach([this] (Entity e, Rigidbody& rb) -> void
            {
                OnRigidbodyDestroyed(e, rb);
            });

        m_ActiveRegistry = nullptr;
        m_ActiveTransforms = nullptr;
    }

    void Physics::OnRigidbodyConstructed(Entity e, Rigidbody& rb)
    {
        glm::vec3 worldPos { 0.0f, 0.0f, 0.0f };
        glm::quat worldRot { 1.0f, 0.0f, 0.0f, 0.0f };

        if (m_ActiveRegistry != nullptr && m_ActiveRegistry->Has<Transform>(e))
        {
            if (m_ActiveTransforms != nullptr)
                m_ActiveTransforms->Ensure(*m_ActiveRegistry, e);

            const Transform& t = m_ActiveRegistry->Get<Transform>(e);

            worldPos = t.GetWorldPosition();
            worldRot = t.GetWorldRotation();
        }

        b3BodyDef bodyDef = b3DefaultBodyDef();

        bodyDef.type = ToB3(rb.type);
        bodyDef.position = ToB3(worldPos);
        bodyDef.rotation = ToB3(worldRot);
        bodyDef.linearVelocity = ToB3(rb.linearVelocity);
        bodyDef.angularVelocity = ToB3(rb.angularVelocity);
        bodyDef.linearDamping = rb.linearDamping;
        bodyDef.angularDamping = rb.angularDamping;
        bodyDef.gravityScale = rb.gravityScale;
        bodyDef.enableSleep = rb.enableSleep;
        bodyDef.isBullet = rb.isBullet;
        bodyDef.userData = EntityToUserData(e);

        b3BodyId body = b3CreateBody(b3LoadWorldId(m_WorldHandle), &bodyDef);

        rb.m_BodyHandle = b3StoreBodyId(body);

        if (m_ActiveRegistry != nullptr && m_ActiveRegistry->Has<Collider>(e))
            CreateShapeForCollider(e, rb, m_ActiveRegistry->Get<Collider>(e));
    }

    void Physics::OnRigidbodyDestroyed(Entity e, Rigidbody& rb)
    {
        if (rb.m_BodyHandle == 0)
            return;

        b3BodyId body = b3LoadBodyId(rb.m_BodyHandle);

        // b3DestroyBody also destroys every shape still attached to it
        if (b3Body_IsValid(body))
            b3DestroyBody(body);

        rb.m_BodyHandle = 0;

        if (m_ActiveRegistry != nullptr && m_ActiveRegistry->Has<Collider>(e))
            m_ActiveRegistry->Get<Collider>(e).m_ShapeHandle = 0;
    }

    void Physics::OnColliderConstructed(Entity e, Collider& col)
    {
        if (m_ActiveRegistry == nullptr || !m_ActiveRegistry->Has<Rigidbody>(e))
        {
            logWarning("Collider on entity {} has no sibling Rigidbody and will not collide with anything; "
                "add a Rigidbody (BodyType::Static for immovable geometry) for it to take effect", e);

            return;
        }

        Rigidbody& rb = m_ActiveRegistry->Get<Rigidbody>(e);

        if (rb.m_BodyHandle != 0)
            CreateShapeForCollider(e, rb, col);
    }

    void Physics::OnColliderDestroyed(Entity, Collider& col)
    {
        if (col.m_ShapeHandle == 0)
            return;

        b3ShapeId shape = b3LoadShapeId(col.m_ShapeHandle);

        if (b3Shape_IsValid(shape))
            b3DestroyShape(shape, true);

        col.m_ShapeHandle = 0;
    }

    void Physics::CreateShapeForCollider(Entity e, Rigidbody& rb, Collider& col)
    {
        b3BodyId body = b3LoadBodyId(rb.m_BodyHandle);

        b3ShapeDef shapeDef = b3DefaultShapeDef();

        shapeDef.density = col.density;
        shapeDef.baseMaterial.friction = col.friction;
        shapeDef.baseMaterial.restitution = col.restitution;
        shapeDef.isSensor = col.isSensor;
        shapeDef.filter.categoryBits = col.categoryBits;
        shapeDef.filter.maskBits = col.maskBits;
        shapeDef.userData = EntityToUserData(e);

        b3ShapeId shape { };

        switch (col.shape)
        {
            case ColliderShape::Sphere:
            {
                b3Sphere sphere { ToB3(col.center), col.radius };
                shape = b3CreateSphereShape(body, &shapeDef, &sphere);

                break;
            }

            case ColliderShape::Capsule:
            {
                const glm::vec3 c1 = col.center + glm::vec3(0.0f, col.capsuleHalfHeight, 0.0f);
                const glm::vec3 c2 = col.center - glm::vec3(0.0f, col.capsuleHalfHeight, 0.0f);

                b3Capsule capsule { ToB3(c1), ToB3(c2), col.radius };
                shape = b3CreateCapsuleShape(body, &shapeDef, &capsule);

                break;
            }

            case ColliderShape::Box:
            default:
            {
                b3BoxHull hull = b3MakeOffsetBoxHull(
                    col.boxHalfExtents.x, col.boxHalfExtents.y, col.boxHalfExtents.z, ToB3(col.center));

                shape = b3CreateHullShape(body, &shapeDef, &hull.base);

                break;
            }
        }

        col.m_ShapeHandle = b3StoreShapeId(shape);
    }

    void Physics::Update(const EngineCtx& ctx, Registry& reg, float dt)
    {
        b3WorldId world = b3LoadWorldId(m_WorldHandle);

        b3World_Step(world, dt, m_SubStepCount);

        b3BodyEvents events = b3World_GetBodyEvents(world);

        for (int i = 0; i < events.moveCount; ++i)
        {
            const b3BodyMoveEvent& ev = events.moveEvents[i];

            Entity e = UserDataToEntity(ev.userData);

            if (e == NULL_ENTITY || !reg.Has<Transform>(e))
                continue;

            ctx.transforms.SetWorldTransform(reg, e, ToGlm(ev.transform.p), ToGlm(ev.transform.q));
        }
    }
}
