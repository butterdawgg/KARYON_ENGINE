#ifndef REGISTRY_H
#define REGISTRY_H

#include <memory>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "entity.h"
#include "storage.h"
#include "signal.h"

namespace krn
{
    template <typename First, typename... Rest>
    class MultiComponentView;

    template <typename Component>
    class ComponentView;



    // --------------------
    // --- ECS Registry ---
    // --------------------

    class Registry final
    {
        public:

        Registry() = default;
        ~Registry() = default;

        Registry(const Registry&) = delete;
        Registry& operator=(const Registry&) = delete;
        Registry(Registry&&) noexcept = default;
        Registry& operator=(Registry&&) noexcept = default;

        Entity Create()
        {
            return m_NextEntity++;
        }

        void Destroy(Entity e)
        {
            for (auto& [id, pool] : m_Pools)
            {
                pool->Remove(e);
            }
        }

        template <typename Component>
        Component& Add(Entity e)
            requires std::is_default_constructible_v<Component>
        {
            return Assure<Component>().Add(e);
        }

        template <typename Component>
        std::decay_t<Component>& Push(Entity e, Component&& comp)
        {
            return Assure<std::decay_t<Component>>().Push(e, std::forward<Component>(comp));
        }

        template <typename Component, typename... Args>
        Component& Emplace(Entity e, Args &&...args)
        {
            return Assure<Component>().Emplace(e, std::forward<Args>(args)...);
        }

        template <typename Component>
        Component& Get(Entity e)
        {
            return Assure<Component>().Get(e);
        }

        template <typename Component>
        bool Has(Entity e)
        {
            return Assure<Component>().Contains(e);
        }

        template <typename Component>
        void Remove(Entity e)
        {
            Assure<Component>().Remove(e);
        }

        template <typename Component>
        void Clear()
        {
            Assure<Component>().Clear();
        }

        // Iterable view over every Component in the registry, e.g.:
        //   for (auto& mr : reg.View<Model>()) { Draw(mr); }
        template <typename First, typename... Rest>
        auto View()
        {
            if constexpr (sizeof...(Rest) == 0)
            {
                return ComponentView<First>(Assure<First>());
            }
            else
            {
                return MultiComponentView<First, Rest...>(*this, Assure<First>().Entities());
            }
        }

        // Fires after a Component is constructed on an entity, same as Storage<Component>::OnConstruct,
        // but with the owning Registry passed along so subscribers don't need to cache one themselves.
        template<typename T>
        Signal<Registry&, Entity, T&>& OnConstruct()
        {
            return AssureRelay<T>(m_ConstructSignals, &Storage<T>::OnConstruct);
        }

        template<typename T>
        Signal<Registry&, Entity, T&>& OnDestroy()
        {
            return AssureRelay<T>(m_DestroySignals, &Storage<T>::OnDestroy);
        }

        template <typename Component>
        Storage<Component>& Assure()
        {
            TypeId id = getTypeId<Component>();

            auto it = m_Pools.find(id);

            if (it == m_Pools.end())
            {
                it = m_Pools.emplace(id, std::make_unique<Storage<Component>>()).first;
            }

            return static_cast<Storage<Component>&>(*it->second);
        }

        private:

        // Lazily builds the Registry-level relay signal for a component type, wiring it to
        // relay off the pool's own Storage<Component> signal (picked via sourceAccessor) the
        // first time it's requested. Storage itself never learns Registry exists; it just gets
        // one more subscriber like any other.
        template <typename Component>
        Signal<Registry&, Entity, Component&>& AssureRelay(
            std::unordered_map<TypeId, std::unique_ptr<BaseSignal>>& relays,
            Signal<Entity, Component&>& (Storage<Component>::*sourceAccessor)())
        {
            TypeId id = getTypeId<Component>();

            auto it = relays.find(id);

            if (it == relays.end())
            {
                auto relay = std::make_unique<Signal<Registry&, Entity, Component&>>();
                Signal<Registry&, Entity, Component&>* relayPtr = relay.get();

                (Assure<Component>().*sourceAccessor)().Connect(
                    [this, relayPtr] (Entity e, Component& comp)
                    {
                        relayPtr->Emit(*this, e, comp);
                    });

                it = relays.emplace(id, std::move(relay)).first;
            }

            return static_cast<Signal<Registry&, Entity, Component&>&>(*it->second);
        }

        Entity m_NextEntity { 0 };
        std::unordered_map<TypeId, std::unique_ptr<BaseStorage>> m_Pools { };
        std::unordered_map<TypeId, std::unique_ptr<BaseSignal>> m_ConstructSignals { };
        std::unordered_map<TypeId, std::unique_ptr<BaseSignal>> m_DestroySignals { };
    };



    // -----------------------
    // --- Component views ---
    // -----------------------

    // Constrains a View::ForEach callback to one invocable with
    // (Entity, Components&...), matching what the view will actually pass it.
    template <typename Func, typename... Components>
    concept ViewCallback = std::is_invocable_v<Func, Entity, Components&...>;

    // Thin, iterable handle onto a single component pool's dense array.
    // Returned by Registry::View - see registry.h.
    template <typename Component>
    class ComponentView
    {
        public:

        explicit ComponentView(Storage<Component>& storage) : m_Storage(storage) { }

        auto begin() { return m_Storage.Data().begin(); }
        auto end() { return m_Storage.Data().end(); }

        std::size_t Size() const { return m_Storage.Size(); }

        // Entities()[i] owns the component at Data()[i] / the i-th element of this view
        const std::vector<Entity>& Entities() const { return m_Storage.Entities(); }

        // Visits every (Entity, Component&) pair in the view - use this over
        // begin()/end() when the callback needs the owning Entity as well
        // as the component itself.
        template <typename Func>
            requires ViewCallback<Func, Component>
        void ForEach(Func func)
        {
            const std::vector<Entity>& entities = m_Storage.Entities();
            std::vector<Component>& data = m_Storage.Data();

            for (std::size_t i = 0; i < data.size(); ++i)
            {
                func(entities[i], data[i]);
            }
        }

        private:

        Storage<Component>& m_Storage;
    };

    // Iterates entities that have every one of First, Rest... , dereferencing
    // to std::tuple<First&, Rest&...>. Built by Registry::View when given more
    // than one component type; `driver` is First's dense entity list, so every
    // candidate already has First and only Rest... needs to be checked.
    template <typename First, typename... Rest>
    class MultiComponentView
    {
        public:

        MultiComponentView(Registry& reg, const std::vector<Entity>& driver) :
            m_Registry(reg), m_Driver(driver)
        {
        }

        class Iterator
        {
            public:

            Iterator(Registry& reg, const std::vector<Entity>& driver, std::size_t index) :
                m_Registry(reg), m_Driver(driver), m_Index(index)
            {
                SkipMissing();
            }

            std::tuple<First&, Rest&...> operator*() const
            {
                Entity e = m_Driver[m_Index];

                return std::tuple<First&, Rest&...>(m_Registry.Get<First>(e), m_Registry.Get<Rest>(e)...);
            }

            Iterator& operator++()
            {
                ++m_Index;

                SkipMissing();

                return *this;
            }

            bool operator!=(const Iterator& other) const { return m_Index != other.m_Index; }

            private:

            // Advances past any candidate missing one of Rest... (it is
            // already guaranteed to have First, being in m_Driver at all)
            void SkipMissing()
            {
                while (m_Index < m_Driver.size() && !(m_Registry.Has<Rest>(m_Driver[m_Index]) && ...))
                {
                    ++m_Index;
                }
            }

            Registry& m_Registry;
            const std::vector<Entity>& m_Driver;
            std::size_t m_Index;
        };

        Iterator begin() { return Iterator(m_Registry, m_Driver, 0); }
        Iterator end() { return Iterator(m_Registry, m_Driver, m_Driver.size()); }

        // Visits every (Entity, First&, Rest&...) tuple in the view - use
        // this over begin()/end() when the callback needs the owning Entity
        // as well as the components themselves.
        template <typename Func>
            requires ViewCallback<Func, First, Rest...>
        void ForEach(Func func)
        {
            for (Entity e : m_Driver)
            {
                if ((m_Registry.Has<Rest>(e) && ...))
                {
                    func(e, m_Registry.Get<First>(e), m_Registry.Get<Rest>(e)...);
                }
            }
        }

        private:

        Registry& m_Registry;
        const std::vector<Entity>& m_Driver;
    };
}

#endif // !REGISTRY_H