#ifndef SIGNAL_H
#define SIGNAL_H

#include <type_traits>

#include "entity.h"

namespace krn
{
    // Type-erased handle onto a Signal<Args...> of unknown Args - lets code that
    // doesn't know a signal's argument list still own/store it (e.g. Registry
    // keying signals by component TypeId, the same way it keys Storage<T> pools).
    class BaseSignal
    {
        public:

        virtual ~BaseSignal() = default;
    };

    template<typename... Args>
    class Signal : public BaseSignal
    {
        public:

        template<auto Function, typename T>
        void Connect(T& instance)
        {
            m_Callbacks.emplace_back(
                [&instance] (Args... args)
                {
                    (instance.*Function)(std::forward<Args>(args)...);
                });
        }

        template<typename Func>
            requires std::is_invocable_v<Func, Args...>
        void Connect(Func func)
        {
            m_Callbacks.emplace_back(func);
        }

        void Emit(Args... args)
        {
            for (auto& callback : m_Callbacks)
                callback(std::forward<Args>(args)...);
        }



        private:

        std::vector<std::function<void(Args...)>> m_Callbacks;
    };
}

#endif