#ifndef STORAGE_H
#define STORAGE_H

#include <cassert>
#include <vector>
#include <type_traits>

#include "entity.h"
#include "signal.h"

namespace krn
{
    class BaseStorage
    {
        public:

        virtual ~BaseStorage() = default;
        virtual bool Contains(Entity e) const = 0;
        virtual void Remove(Entity e) = 0;
        virtual std::size_t Size() const = 0;
    };

    template <typename Component>
    class Storage : public BaseStorage
    {
        public:

        Component& Add(Entity e)
            requires std::is_default_constructible_v<Component>
        {
            assert(!Contains(e) && "entity already has this component");

            if (e >= m_Sparse.size())
            {
                m_Sparse.resize(e + 1, TOMBSTONE);
            }

            m_Sparse[e] = m_Dense.size();
            m_Dense.push_back(e);
            m_Data.push_back(Component { });

            Component& comp = m_Data.back();
            m_OnConstruct.Emit(e, comp);

            return comp;
        }

        Component& Push(Entity e, const Component& comp)
        {
            assert(!Contains(e) && "entity already has this component");

            if (e >= m_Sparse.size())
            {
                m_Sparse.resize(e + 1, TOMBSTONE);
            }

            m_Sparse[e] = m_Dense.size();
            m_Dense.push_back(e);
            m_Data.push_back(comp);

            Component& stored = m_Data.back();
            m_OnConstruct.Emit(e, stored);

            return stored;
        }

        Component& Push(Entity e, Component&& comp)
        {
            assert(!Contains(e) && "entity already has this component");

            if (e >= m_Sparse.size())
            {
                m_Sparse.resize(e + 1, TOMBSTONE);
            }

            m_Sparse[e] = m_Dense.size();
            m_Dense.push_back(e);
            m_Data.push_back(std::move(comp));

            Component& stored = m_Data.back();
            m_OnConstruct.Emit(e, stored);

            return stored;
        }

        template <typename... Args>
        Component& Emplace(Entity e, Args &&...args)
        {
            assert(!Contains(e) && "entity already has this component");

            if (e >= m_Sparse.size())
            {
                m_Sparse.resize(e + 1, TOMBSTONE);
            }

            m_Sparse[e] = m_Dense.size();
            m_Dense.push_back(e);
            m_Data.push_back(Component { std::forward<Args>(args)... });

            Component& comp = m_Data.back();
            m_OnConstruct.Emit(e, comp);

            return comp;
        }

        Component& Get(Entity e)
        {
            assert(Contains(e) && "entity has no such component");
            return m_Data[m_Sparse[e]];
        }

        bool Contains(Entity e) const override
        {
            return e < m_Sparse.size() && m_Sparse[e] != TOMBSTONE;
        }

        void Remove(Entity e) override
        {
            if (!Contains(e)) return;

            std::size_t idx = m_Sparse[e];

            m_OnDestroy.Emit(e, m_Data[idx]);

            std::size_t last_idx = m_Dense.size() - 1;

            if (idx != last_idx)
            {
                // swap-and-pop: move the last element into the removed slot
                Entity last_entity = m_Dense[last_idx];
                m_Dense[idx] = last_entity;
                m_Data[idx] = std::move(m_Data[last_idx]);
                m_Sparse[last_entity] = idx;
            }

            m_Dense.pop_back();
            m_Data.pop_back();
            m_Sparse[e] = TOMBSTONE;
        }

        void Clear()
        {
            m_Sparse.clear();
            m_Dense.clear();
            m_Data.clear();
        }

        std::size_t Size() const override { return m_Dense.size(); }

        const std::vector<Entity>& Entities() const { return m_Dense; }
        std::vector<Component>& Data() { return m_Data; }

        Signal<Entity, Component&>& OnConstruct()
        {
            return m_OnConstruct;
        }

        Signal<Entity, Component&>& OnDestroy()
        {
            return m_OnDestroy;
        }

        private:

        static constexpr std::size_t TOMBSTONE = static_cast<std::size_t>(-1);

        std::vector<std::size_t> m_Sparse;    // entity -> index in packed/m_Data
        std::vector<Entity> m_Dense;         // dense list of entities with this component
        std::vector<Component> m_Data;  // dense data, parallel to packed

        Signal<Entity, Component&> m_OnConstruct;
        Signal<Entity, Component&> m_OnDestroy;
    };
}

#endif