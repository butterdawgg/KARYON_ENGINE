#include "transform.h"

#include "scene_manager.h"

#include <cassert>

#include <glm/gtc/matrix_transform.hpp>

namespace krn
{
    TransformManager::TransformManager(SceneManager& scenes)
    {
        // Add OnLoad callback
        scenes.AddInternalLoadCallback(
            [this] (const EngineCtx& ctx, Scene& scene, Registry& reg, std::vector<fs::path>)
            {
                OnLoadCallback(ctx, scene, reg);
            });
    }

    void TransformManager::SetParent(Registry& reg, Entity child, Entity parent, bool keepWorldTransform)
    {
        assert(child != NULL_ENTITY && "cannot parent a null childEntity");
        assert(child != parent && "an childEntity cannot be its own parent");

        #ifndef NDEBUG

        for (Entity a = parent; a != NULL_ENTITY;
            a = reg.Has<Hierarchy>(a) ? reg.Get<Hierarchy>(a).parent : NULL_ENTITY)
        {
            assert(a != child && "Hierarchy cycle detected");
        }

        #endif // !NDEBUG

        // Ensure both entities have a Transform component
        if (!reg.Has<Transform>(child))
            reg.Add<Transform>(child);

        if (parent != NULL_ENTITY && !reg.Has<Transform>(parent))
            reg.Add<Transform>(parent);

        // Ensure both entities have a Hierarchy component
        if (!reg.Has<Hierarchy>(child))
            reg.Add<Hierarchy>(child);

        if (parent != NULL_ENTITY && !reg.Has<Hierarchy>(parent))
            reg.Add<Hierarchy>(parent);

        // Ensure child isn't already parented to the provided entity
        if (reg.Get<Hierarchy>(child).parent == parent)
            return;

        Transform& t = reg.Get<Transform>(child);

        glm::vec3 worldPosition { };
        glm::quat worldRotation { };
        glm::vec3 worldScale { };

        if (keepWorldTransform)
        {
            Ensure(reg, child);

            worldPosition = t.m_WorldPosition;
            worldRotation = t.m_WorldRotation;
            worldScale = t.m_WorldScale;
        }

        Unlink(reg, child);
        Link(reg, child, parent);

        if (keepWorldTransform)
        {
            if (parent == NULL_ENTITY)
            {
                t.SetLocalPosition(worldPosition);
                t.SetLocalRotation(worldRotation);
                t.SetLocalScale(worldScale);
            }
            else
            {
                Ensure(reg, parent);

                const Transform& pt = reg.Get<Transform>(parent);
                const glm::quat invParentRotation = glm::conjugate(pt.m_WorldRotation);

                t.SetLocalPosition(invParentRotation * (worldPosition - pt.m_WorldPosition) / pt.m_WorldScale);
                t.SetLocalRotation(invParentRotation * worldRotation);
                t.SetLocalScale(worldScale / pt.m_WorldScale);
            }
        }
        else
        {
            // Local data is unchanged, but the effective parent is: force a
            // recompute even though nothing tripped the dirty flag.
            t.MarkDirty();
        }
    }

    void TransformManager::ClearParent(Registry& reg, Entity child, bool keepWorldTransform)
    {
        SetParent(reg, child, NULL_ENTITY, keepWorldTransform);
    }

    void TransformManager::Ensure(Registry& reg, Entity e)
    {
        EnsureOne(reg, e, reg.Get<Transform>(e));
    }

    void TransformManager::OnLoadCallback(const EngineCtx& ctx, Scene& scene, Registry& reg)
    {
        // Get the view of LoadMetadata components
        auto view { reg.View<LoadMetadata>() };

        // Build a map from a entity id to its Entity
        std::unordered_map<uint32_t, Entity> idToEntity;

        view.ForEach([&idToEntity] (Entity entity, LoadMetadata& metadata)
            {
                if (metadata.id != LoadMetadata::INVALID_ID)
                {
                    assert(idToEntity.find(metadata.id) == idToEntity.end()
                        && "entity with this id already exists");

                    idToEntity[metadata.id] = entity;
                }
            });

        // Construct the scene graph
        view.ForEach([&idToEntity, this, &reg] (Entity childEntity, LoadMetadata& metadata)
            {
                if (metadata.parent == LoadMetadata::INVALID_ID)
                    return;

                if (auto it = idToEntity.find(metadata.parent); it != idToEntity.end())
                {
                    SetParent(reg, childEntity, it->second, false);
                }
            });
    }

    void TransformManager::UpdateAll(Registry& reg)
    {
        Storage<Transform>& transforms = reg.Assure<Transform>();

        for (Entity e : transforms.Entities())
        {
            EnsureOne(reg, e, reg.Get<Transform>(e));
        }
    }

    bool TransformManager::EnsureOne(Registry& reg, Entity e, Transform& t)
    {
        Entity parent = NULL_ENTITY;
        std::uint32_t parentVersion = 0;

        if (reg.Has<Hierarchy>(e))
        {
            parent = reg.Get<Hierarchy>(e).parent;

            if (parent != NULL_ENTITY)
            {
                Transform& pt = reg.Get<Transform>(parent);

                EnsureOne(reg, parent, pt);

                parentVersion = pt.m_Version;
            }
        }

        const bool parentChanged = parent != NULL_ENTITY && parentVersion != t.m_ParentVersion;

        if (!t.m_Dirty && !parentChanged)
            return false;

        if (parent == NULL_ENTITY)
        {
            RecomputeRoot(t);
        }
        else
        {
            RecomputeChild(t, reg.Get<Transform>(parent));
        }

        t.m_ParentVersion = parentVersion;
        t.m_Dirty = false;
        t.m_Version++;

        return true;
    }

    void TransformManager::RecomputeRoot(Transform& t) const
    {
        t.m_WorldPosition = t.m_LocalPosition;
        t.m_WorldRotation = t.m_LocalRotation;
        t.m_WorldScale = t.m_LocalScale;
        t.m_WorldEulerAngles = t.m_LocalEulerAngles;

        t.m_WorldMat =
            glm::translate(glm::mat4(1.0f), t.m_LocalPosition) *
            glm::mat4_cast(t.m_LocalRotation) *
            glm::scale(glm::mat4(1.0f), t.m_LocalScale);
    }

    void TransformManager::RecomputeChild(Transform& t, const Transform& parent) const
    {
        t.m_WorldRotation = parent.m_WorldRotation * t.m_LocalRotation;
        t.m_WorldScale = parent.m_WorldScale * t.m_LocalScale;
        t.m_WorldPosition = parent.m_WorldPosition +
            parent.m_WorldRotation * (parent.m_WorldScale * t.m_LocalPosition);
        t.m_WorldEulerAngles = glm::eulerAngles(t.m_WorldRotation);

        t.m_WorldMat = parent.m_WorldMat *
            glm::translate(glm::mat4(1.0f), t.m_LocalPosition) *
            glm::mat4_cast(t.m_LocalRotation) *
            glm::scale(glm::mat4(1.0f), t.m_LocalScale);
    }

    void TransformManager::Unlink(Registry& reg, Entity e) const
    {
        Hierarchy& h = reg.Get<Hierarchy>(e);

        if (h.prevSibling != NULL_ENTITY)
        {
            reg.Get<Hierarchy>(h.prevSibling).nextSibling = h.nextSibling;
        }
        else if (h.parent != NULL_ENTITY)
        {
            reg.Get<Hierarchy>(h.parent).firstChild = h.nextSibling;
        }

        if (h.nextSibling != NULL_ENTITY)
        {
            reg.Get<Hierarchy>(h.nextSibling).prevSibling = h.prevSibling;
        }

        h.parent = NULL_ENTITY;
        h.prevSibling = NULL_ENTITY;
        h.nextSibling = NULL_ENTITY;
    }

    void TransformManager::Link(Registry& reg, Entity e, Entity parent) const
    {
        Hierarchy& h = reg.Get<Hierarchy>(e);

        h.parent = parent;

        if (parent == NULL_ENTITY)
            return;

        Hierarchy& ph = reg.Get<Hierarchy>(parent);

        h.nextSibling = ph.firstChild;

        if (ph.firstChild != NULL_ENTITY)
        {
            reg.Get<Hierarchy>(ph.firstChild).prevSibling = e;
        }

        ph.firstChild = e;
    }
}
