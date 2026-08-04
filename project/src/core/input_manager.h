#ifndef INPUT_H
#define INPUT_H

#include <glm/glm.hpp>

#include <cstdint>
#include <limits>
#include <concepts>

#include <cassert>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>
#include <string>
#include <functional>

#include "event_manager.h"

namespace krn
{
    class Engine;

    // ---------------
    // --- Helpers ---
    // ---------------

    // Handle for an input binding, that evaluates to type R
    template<typename R>
    struct BindingHandle
    {
        static constexpr uint32_t INVALID_INDEX = std::numeric_limits<uint32_t>::max();

        uint32_t index { INVALID_INDEX };
        uint32_t generation { 0 };

        constexpr bool IsValid() const
        {
            return index != INVALID_INDEX;
        }

        friend constexpr bool operator==(const BindingHandle&, const BindingHandle&) = default;
    };

    // Snapshot of the state of all connected input devices
    struct InputCtx
    {
        bool keysDown[4] { false, false, false, false }; // dummy code for now
    };



    // ----------------
    // --- Concepts ---
    // ----------------

    // A binding type B must expose Evaluate(const InputCtx&) -> R for the specific R in Bind<R>()
    template<typename B, typename R>
    concept BindingTypeFor =
        requires(B b, const InputCtx& ctx)
    {
        { b.Evaluate(ctx) } -> std::same_as<R>;
    };

    // A postprocessor must expose Process(const ValueType&, float) -> ValueType
    template<typename P>
    concept PostprocessorType =
        requires(P p, const typename P::ValueType & v, float dt)
    {
        { p.Process(v, dt) } -> std::same_as<typename P::ValueType>;
    };

    // An event condition must expose ShouldEmit(const ValueType&, const ValueType&, float) -> bool
    template<typename C>
    concept EventConditionType =
        requires(C c, const typename C::ValueType & a, const typename C::ValueType & b, float dt)
    {
        { c.ShouldEmit(a, b, dt) } -> std::same_as<bool>;
    };

    // An event payload must expose SetActionValue(const R&) -> void, where R is
    // the value type of the binding it's attached to
    template<typename E, typename R>
    concept InputEvent =
        requires(E e, const R & v)
    {
        { e.SetActionValue(v) } -> std::same_as<void>;
    };



    // ---------------------
    // --- Input binding ---
    // ---------------------

    // Type-erased base for a single binding
    class BaseBoundInput
    {
        public:

        virtual ~BaseBoundInput() = default;

        // Evaluates the binding, runs it through the postprocessor chain, then
        // checks all registered event rules against the old/new value
        virtual void Update(const InputCtx& ctx, float dt) = 0;

        std::string_view GetDebugName() const
        {
            return m_DebugName;
        }

        protected:

        explicit BaseBoundInput(std::string_view debugName)
            : m_DebugName(debugName)
        { }

        private:

        std::string m_DebugName;
    };

    // Owns the full pipeline for one binding whose evaluated value is R:
    //   - the type-erased Evaluate call
    //   - an ordered chain of postprocessors (R -> R)
    //   - a set of event rules, each pairing a condition (R,R,dt -> bool) with
    //     an event payload prototype that receives the action value and is pushed to
    //     the EventManager when its condition fires
    template<typename R>
    class BoundInput final : public BaseBoundInput
    {
        public:

        template<typename BindingT>
            requires BindingTypeFor<BindingT, R>
        BoundInput(BindingT binding, std::string_view debugName) : BaseBoundInput(debugName),
            m_Evaluate([b = std::move(binding)] (const InputCtx& ctx) mutable { return b.Evaluate(ctx); })
        { }

        template<typename P>
            requires PostprocessorType<P> &&
                std::same_as<typename P::ValueType, R>
        void AddPostprocessor(P processor)
        {
            m_Postprocessors.push_back(std::make_unique<PostprocessorSlot<P>>(std::move(processor)));
        }

        template<typename E, typename Cond>
            requires InputEvent<E, R> &&
                EventConditionType<Cond> &&
                std::same_as<typename Cond::ValueType, R>
        void AddEvent(E eventPrototype, Cond condition, EventManager& events)
        {
            m_EventRules.push_back(std::make_unique<EventRule<E, Cond>>(
                std::move(eventPrototype), std::move(condition), events));
        }

        void Update(const InputCtx& ctx, float dt) override
        {
            R newValue { m_Evaluate(ctx) };

            for (auto& postprocessor : m_Postprocessors)
            {
                newValue = postprocessor->Process(newValue, dt);
            }

            for (auto& rule : m_EventRules)
            {
                rule->Tick(newValue, dt);
            }

            m_LastValue = std::move(newValue);
        }

        const R& Get() const
        {
            return m_LastValue;
        }

        private:

        // --- postprocessor erasure -------------------------------------------------

        struct BasePostprocessorSlot
        {
            virtual ~BasePostprocessorSlot() = default;
            virtual R Process(const R& value, float dt) = 0;
        };

        template<typename P>
        struct PostprocessorSlot final : BasePostprocessorSlot
        {
            P instance;

            explicit PostprocessorSlot(P p) : instance(std::move(p)) { }

            R Process(const R& value, float dt) override
            {
                return instance.Process(value, dt);
            }
        };

        // --- event rule erasure ------------------------------------------------

        struct BaseEventRule
        {
            virtual ~BaseEventRule() = default;
            virtual void Tick(const R& newValue, float dt) = 0;
        };

        template<typename E, typename Cond>
        struct EventRule final : BaseEventRule
        {
            E prototype;
            Cond condition;
            EventManager& events;
            R lastEmittedValue {};
            float timeSinceLastEmit { std::numeric_limits<float>::max() };

            EventRule(E e, Cond c, EventManager& ev)
                : prototype(std::move(e))
                , condition(std::move(c))
                , events(ev)
            { }

            void Tick(const R& newValue, float dt) override
            {
                timeSinceLastEmit += dt;

                if (condition.ShouldEmit(lastEmittedValue, newValue, timeSinceLastEmit))
                {
                    prototype.SetActionValue(newValue);
                    events.Push(E { prototype }); // explicit copy
                    lastEmittedValue = newValue;
                    timeSinceLastEmit = 0.0f;
                }
            }
        };

        std::function<R(const InputCtx&)> m_Evaluate;
        std::vector<std::unique_ptr<BasePostprocessorSlot>> m_Postprocessors;
        std::vector<std::unique_ptr<BaseEventRule>> m_EventRules;
        R m_LastValue {};
    };



    // --------------------
    // --- Input system ---
    // --------------------

    // Owns all bound input actions in the engine
    class InputManager final
    {
        friend class Engine;

        public:

        explicit InputManager(EventManager& events) :
            m_Events(events)
        { }

        ~InputManager() = default;

        InputManager(const InputManager&) = delete;
        InputManager& operator=(const InputManager&) = delete;
        InputManager(InputManager&&) = delete;
        InputManager& operator=(InputManager&&) = delete;

        // Creates a new binding from an InputBindingType instance whose
        // Evaluate(const InputCtx&) returns R, then returns a handle
        template<typename R, typename BindingT>
            requires BindingTypeFor<BindingT, R>
        BindingHandle<R> Bind(BindingT binding, std::string_view debugName = "")
        {
            auto slot { std::make_unique<BoundInput<R>>(std::move(binding), debugName) };

            uint32_t index;

            if (!m_FreeList.empty())
            {
                index = m_FreeList.back();
                m_FreeList.pop_back();
                m_Slots[index] = std::move(slot);
            }
            else
            {
                index = static_cast<uint32_t>(m_Slots.size());
                m_Slots.push_back(std::move(slot));
                m_Generations.push_back(0);
            }

            return BindingHandle<R> { index, m_Generations[index] };
        }

        // Releases the slot referenced by the handle so it can be reused later
        template<typename R>
        void Unbind(BindingHandle<R> handle)
        {
            if (!IsHandleValid(handle))
            {
                assert(false && "InputManager::Unbind called with an invalid or stale handle");
                return;
            }

            m_Slots[handle.index].reset();
            ++m_Generations[handle.index];
            m_FreeList.push_back(handle.index);
        }


        // Adds a postprocessor to a binding via a handle
        template<typename R, typename P>
            requires PostprocessorType<P> &&
                std::same_as<typename P::ValueType, R>
        void AddPostprocessor(BindingHandle<R> handle, P processor)
        {
            GetBoundInput(handle).AddPostprocessor(std::move(processor));
        }

        // Adds an event emitter to a binding via a handle
        template<typename R, typename E, typename Cond>
            requires InputEvent<E, R> &&
                EventConditionType<Cond> &&
                std::same_as<typename Cond::ValueType, R>
        void AddEvent(BindingHandle<R> handle, E eventPrototype, Cond condition)
        {
            GetBoundInput(handle).AddEvent(std::move(eventPrototype), std::move(condition), m_Events);
        }

        // Returns the current action value of a binding
        template<typename R>
        const R& GetAction(BindingHandle<R> handle) const
        {
            return GetBoundInput(handle).Get();
        }

        // Checks if a handle references a valid binding
        template<typename R>
        bool IsHandleValid(BindingHandle<R> handle) const
        {
            return handle.IsValid()
                && handle.index < m_Slots.size()
                && m_Slots[handle.index] != nullptr
                && m_Generations[handle.index] == handle.generation;
        }

        private:

        // Called once per frame by the Engine
        void Update(float dt)
        {
            // Build new context
            InputCtx ctx { };

            // Update bindings
            for (auto& slot : m_Slots)
            {
                if (slot)
                {
                    slot->Update(ctx, dt);
                }
            }
        }

        template<typename R>
        BoundInput<R>& GetBoundInput(BindingHandle<R> handle) const
        {
            assert(IsHandleValid(handle) && "Stale or invalid BindingHandle used");
            return static_cast<BoundInput<R>&>(*m_Slots[handle.index]);
        }

        EventManager& m_Events;

        std::vector<std::unique_ptr<BaseBoundInput>> m_Slots;
        std::vector<uint32_t> m_Generations;
        std::vector<uint32_t> m_FreeList;
    };



    // -----------------
    // --- Built-ins ---
    // -----------------

    template<typename R>
    concept HasLengthGLM =
        requires(const R& value)
    {
        { glm::length(value) } -> std::convertible_to<float>;
    };

    template<typename R>
    concept EqualityComparableValue =
        requires(const R& a, const R& b)
    {
        { a != b } -> std::convertible_to<bool>;
    };



    // Generic radial/magnitude deadzone, works for any R that supports glm::length();
    template<typename R>
    struct Deadzone
    {
        static_assert(HasLengthGLM<R>,
            "Deadzone<R> requires R to support glm::length()");

        float threshold { 0.0f };

        R Process(const R& value, float /*dt*/) const
        {
            return glm::length(value) < threshold * threshold ? R {} : value;
        }
    };

    // Emits whenever the action value differs from its previous value, gated by minDeltaSeconds
    template<typename R>
    struct EmitOnChangeCondition
    {
        static_assert(EqualityComparableValue<R>,
            "EmitOnChangeCondition<R> requires R to support operator!=");

        float minDeltaSeconds { 0.0f };

        bool ShouldEmit(const R& oldValue, const R& newValue, float dtSinceLastEmit) const
            requires EqualityComparableValue<R>
        {
            return newValue != oldValue && dtSinceLastEmit >= minDeltaSeconds;
        }
    };

    // Emits every frame, gated by minDeltaSeconds
    template<typename R>
    struct AlwaysEmitCondition
    {
        float minDeltaSeconds { 0.0f };

        bool ShouldEmit(const R& /*oldValue*/, const R& /*newValue*/, float dtSinceLastEmit) const
        {
            return dtSinceLastEmit >= minDeltaSeconds;
        }
    };
}

#endif // !INPUT_H