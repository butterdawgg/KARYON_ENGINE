#ifndef EVENTS_H
#define EVENTS_H

#include <vector>
#include <unordered_map>
#include <memory>

#include "entity.h"

namespace krn
{
    class Engine;

    class BaseEventStorage
    {
        public:

        virtual ~BaseEventStorage() = default;

        virtual void Clear() = 0;
    };

    template<typename T>
    class EventStorage final : public BaseEventStorage
    {
        public:

        void Clear() override
        {
            m_Data.clear();
        }

        const std::vector<T>& Get() const
        {
            return m_Data;
        }

        std::vector<T>& Get()
        {
            return m_Data;
        }

        private:

        std::vector<T> m_Data;
    };

    class EventManager final
    {
        friend class Engine;

        public:

        EventManager() = default;
        ~EventManager() = default;

        EventManager(const EventManager&) = delete;
        EventManager& operator=(const EventManager&) = delete;
        EventManager(EventManager&&) noexcept = delete;
        EventManager& operator=(EventManager&&) noexcept = delete;

        template<typename T>
        void Push(T&& event)
        {
            Assure<T>().Get().push_back(std::forward<T>(event));
        }

        template<typename T, typename... Args>
        void Emplace(Args&&... args)
        {
            Assure<T>().Get().emplace_back(std::forward<Args>(args)...);
        }

        template<typename T>
        const std::vector<T>& View()
        {
            return Assure<T>().Get();
        }

        template<typename T>
        void Clear()
        {
            Assure<T>().Clear();
        }

        private:

        template<typename T>
        EventStorage<T>& Assure()
        {
            TypeId id = getTypeId<T>();

            auto it { m_EventStorages.find(id) };

            if (it == m_EventStorages.end())
            {
                it = m_EventStorages.emplace(id, std::make_unique<EventStorage<T>>()).first;
            }

            return static_cast<EventStorage<T>&>(*(it->second));
        }

        /* Called by Engine at the end of frame*/
        void FlushEvents()
        {
            for (auto& [key, storage] : m_EventStorages)
            {
                storage->Clear();
            }
        }

        std::unordered_map<TypeId, std::unique_ptr<BaseEventStorage>> m_EventStorages {};
    };
}

#endif // !EVENTS_H