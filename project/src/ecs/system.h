#ifndef SYSTEM_H
#define SYSTEM_H

#include <algorithm>

#include "registry.h"
#include "engine_ctx.h"

#include "scene_manager.h"

namespace krn
{
    class Engine;

    // Abstract system interface
    class System
    {
        public:

        virtual ~System() = default;

        // Called after scene load
        virtual void OnLoad(const EngineCtx&, Registry&) { };

        // Called each frame
        virtual void Update(const EngineCtx&, Registry&, float) { };

        // Called each frame at fixed dt
        virtual void FixedUpdate(const EngineCtx&, Registry&, float) { };

        // Called after scene unload
        virtual void OnUnload(const EngineCtx&, Registry&) { };
    };

    // Owns, schedules and execute user-defined systems that operate on registries
    class SystemManager final
    {
        friend class Engine;

        public:

        SystemManager(SceneManager& scenes)
        {
            scenes.OnLoad.Connect<&SystemManager::OnLoad>(*this);
            scenes.OnUnload.Connect<&SystemManager::OnUnload>(*this);
        };

        ~SystemManager() = default;

        SystemManager(const SystemManager&) = delete;
        SystemManager& operator=(const SystemManager&) = delete;
        SystemManager(SystemManager&&) noexcept = delete;
        SystemManager& operator=(SystemManager&&) noexcept = delete;

        // Registers a system to receive engine notifications (like Update, FixedUpdate etc.)
        // executionOrder is the system priority (lower -> executed first)
        void AddSystem(std::unique_ptr<System> system, int executionOrder)
        {
            m_Systems.emplace_back(std::move(system), executionOrder);

            SortSystems(m_Systems);
        }

        template<typename T>
        T* GetSystem()
        {
            for (auto& [system, _] : m_Systems)
            {
                T* sys { dynamic_cast<T*>(system.get()) };

                if (sys != nullptr)
                    return sys;
            }

            return nullptr;
        }

        // Removes a registered system
        void RemoveSystem(System* system)
        {
            auto it = std::find_if(m_Systems.begin(), m_Systems.end(),
                [system] (const SystemEntry& entry) -> bool
                {
                    return entry.first.get() == system;
                });

            if (it == m_Systems.end())
            {
                logError("Attempted to remove a system that is not registered!");

                return;
            }

            m_Systems.erase(it);

            SortSystems(m_Systems);
        }

        private:

        using SystemEntry = std::pair<std::unique_ptr<System>, int>;

        // Sorts the systems by execution order
        void SortSystems(std::vector<SystemEntry>& systems) const
        {
            std::stable_sort(systems.begin(), systems.end(),
                [] (const SystemEntry& lhs, const SystemEntry& rhs) -> bool
                {
                    return lhs.second < rhs.second;
                });
        }

        // Called by the SceneManager on scene load
        void OnLoad(const EngineCtx& ctx, Scene&, Registry& reg)
        {
            for (auto& [system, _] : m_Systems)
            {
                system->OnLoad(ctx, reg);
            }
        }

        // Called by the SceneManager on scene unload
        void OnUnload(const EngineCtx& ctx, Scene&, Registry& reg)
        {
            for (auto& [system, _] : m_Systems)
            {
                system->OnUnload(ctx, reg);
            }
        }

        // Called by the Engine every frame
        void Update(const EngineCtx& ctx, Registry& reg, float dt)
        {
            for (auto& [system, _] : m_Systems)
            {
                system->Update(ctx, reg, dt);
            }
        }

        // Called by the Engine every frame at a fixed dt
        void FixedUpdate(const EngineCtx& ctx, Registry& reg, float dt)
        {
            for (auto& [system, _] : m_Systems)
            {
                system->FixedUpdate(ctx, reg, dt);
            }
        }

        // Stored as { system ptr, execution order }
        std::vector<SystemEntry> m_Systems { };
    };
}

#endif // !SYSTEM_H