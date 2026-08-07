#ifndef TRANSFORM_H
#define TRANSFORM_H

#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "registry.h"
#include "engine_ctx.h"
#include "serialization.h"

namespace krn
{
    class Engine;
    class TransformManager;
    class Scene;

    // Local TRS data for a node in the scene graph, plus a cache of its
    // resolved world-space equivalents.
    //
    // Transform intentionally has no knowledge of the Registry or its own
    // Entity/Hierarchy, so it cannot resolve its own world-space data (that
    // requires walking up parents, which live in other components). Instead:
    //  - Local data is authoritative and always valid. It can only be mutated
    //    through the Set* methods below, which update it immediately and flag
    //    the cached world data as stale.
    //  - World data is a read-only cache, kept current by TransformManager
    //    (the only class that is allowed to see both a Transform and the
    //    Registry it lives in). Call TransformManager::Ensure(reg, e) before
    //    reading it if you're not certain it has already been refreshed this
    //    frame (e.g. anything read after the engine's per-frame transform
    //    update, such as rendering, is already current and needs no Ensure call).
    struct Transform : public Registrar<Transform>
    {
        friend class TransformManager;

        public:

        static void Register()
        {
            SerializeManager::RegisterComponent<Transform>("transform")
                .Parameter("position", &Transform::m_LocalPosition)
                .Parameter("rotation", &Transform::m_LocalEulerAngles)
                .Parameter("scale", &Transform::m_LocalScale);
        }

        Transform() = default;

        Transform(const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale) :
            m_LocalPosition(position), m_LocalRotation(rotation), m_LocalScale(scale) { }

        // --- Local space (authoritative) ---

        void SetLocalPosition(const glm::vec3& position)
        {
            m_LocalPosition = position;

            MarkDirty();
        }

        void SetLocalRotation(const glm::quat& rotation)
        {
            m_LocalRotation = rotation;
            m_LocalEulerAngles = glm::eulerAngles(rotation);

            MarkDirty();
        }

        void SetLocalEulerAngles(const glm::vec3& eulerAngles)
        {
            m_LocalEulerAngles = eulerAngles;
            m_LocalRotation = glm::quat(eulerAngles);

            MarkDirty();
        }

        void SetLocalScale(const glm::vec3& scale)
        {
            m_LocalScale = scale;

            MarkDirty();
        }

        const glm::vec3& GetLocalPosition() const { return m_LocalPosition; }
        const glm::quat& GetLocalRotation() const { return m_LocalRotation; }
        const glm::vec3& GetLocalEulerAngles() const { return m_LocalEulerAngles; }
        const glm::vec3& GetLocalScale() const { return m_LocalScale; }

        // --- World space (derived cache, see class comment above) ---

        const glm::vec3& GetWorldPosition() const { return m_WorldPosition; }
        const glm::quat& GetWorldRotation() const { return m_WorldRotation; }
        const glm::vec3& GetWorldEulerAngles() const { return m_WorldEulerAngles; }
        const glm::vec3& GetWorldScale() const { return m_WorldScale; }
        const glm::mat4& GetWorldMatrix() const { return m_WorldMat; }

        private:

        void MarkDirty() { m_Dirty = true; }

        // Driver data
        glm::vec3 m_LocalPosition { 0.0f, 0.0f, 0.0f };
        glm::quat m_LocalRotation { 1.0f, 0.0f, 0.0f, 0.0f };
        glm::vec3 m_LocalScale { 1.0f, 1.0f, 1.0f };
        glm::vec3 m_LocalEulerAngles { 0.0f, 0.0f, 0.0f };

        // Query data, owned and refreshed by TransformManager
        glm::vec3 m_WorldPosition { 0.0f, 0.0f, 0.0f };
        glm::quat m_WorldRotation { 1.0f, 0.0f, 0.0f, 0.0f };
        glm::vec3 m_WorldEulerAngles { 0.0f, 0.0f, 0.0f };
        glm::vec3 m_WorldScale { 1.0f, 1.0f, 1.0f };
        glm::mat4 m_WorldMat { 1.0f };

        bool m_Dirty { true };

        // Bumped every time world data is (re)computed; lets a child detect
        // that its parent moved without needing its own dirty flag set, and
        // without needing to eagerly mark an entire subtree dirty on write.
        std::uint32_t m_Version { 0 };

        // This transform's cached view of its parent's m_Version as of its
        // own last recompute; a mismatch means the parent has since changed.
        std::uint32_t m_ParentVersion { 0 };
    };

    // Intrusive, doubly-linked sibling list. Purely structural data with
    // nothing derived to cache, so unlike Transform it stays a plain,
    // fully public, data-oriented struct. Treat it as owned by
    // TransformManager: mutate it through SetParent/ClearParent rather than
    // by hand, or the sibling list will desync from `parent`.
    struct Hierarchy
    {
        Entity parent { NULL_ENTITY };
        Entity firstChild { NULL_ENTITY };
        Entity prevSibling { NULL_ENTITY };
        Entity nextSibling { NULL_ENTITY };
    };

    class TransformManager final
    {
        friend class Engine;

        public:

        explicit TransformManager(SceneManager& scenes);
        ~TransformManager() = default;

        TransformManager(const TransformManager&) = delete;
        TransformManager& operator=(const TransformManager&) = delete;
        TransformManager(TransformManager&&) noexcept = delete;
        TransformManager& operator=(TransformManager&&) noexcept = delete;

        // Attaches 'child' to 'parent' (NULL_ENTITY makes it a root)
        void SetParent(Registry& reg, Entity child, Entity parent, bool keepWorldTransform = true);

        // Equivalent to SetParent(reg, child, NULL_ENTITY, keepWorldTransform)
        void ClearParent(Registry& reg, Entity child, bool keepWorldTransform = true);

        // Guarantees e's world-space data reflects its current local data and
        // that of all its ancestors
        void Ensure(Registry& reg, Entity e);

        private:

        // Called by the SceneManager on scene load
        void OnLoadCallback(const EngineCtx& ctx, Scene& scene, Registry& reg);

        // Called by the Engine once per frame to refresh every transform's world-space data
        void UpdateAll(Registry& reg);

        // Recomputes e's world data if needed, assuming its ancestors are
        // already resolved. Returns true if a recompute happened, i.e. e's
        // world data changed as a result of this call.
        bool EnsureOne(Registry& reg, Entity e, Transform& t);

        void RecomputeRoot(Transform& t) const;
        void RecomputeChild(Transform& t, const Transform& parent) const;

        // Splices e out of its current parent's child list, leaving e a root.
        // e's own children are untouched and move with it as a subtree.
        void Unlink(Registry& reg, Entity e) const;

        // Splices e into parent's child list and sets e's parent link.
        // Assumes both e and parent already have a Hierarchy component.
        void Link(Registry& reg, Entity e, Entity parent) const;
    };
}

#endif // !TRANSFORM_H
