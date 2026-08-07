#ifndef INPUT_H
#define INPUT_H

#include <glm/glm.hpp>

#include <array>
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



    // --------------------------
    // --- Engine-native keys ---
    // --------------------------

    // Everything below is a self-contained, backend-agnostic vocabulary for
    // specifying keys/buttons/axes when creating bindings - no SDL types or
    // headers are ever exposed through this file. input_manager.cpp maps
    // these to SDL identifiers internally.

    enum class Key : uint16_t
    {
        Unknown = 0,

        A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

        Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,

        F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

        Space, Enter, Escape, Tab, Backspace, Delete, Insert,

        Up, Down, Left, Right, Home, End, PageUp, PageDown,

        LeftShift, RightShift, LeftCtrl, RightCtrl, LeftAlt, RightAlt, LeftGui, RightGui,

        Minus, Equals, LeftBracket, RightBracket, Backslash, Semicolon, Apostrophe,
        Comma, Period, Slash, Grave,

        KP0, KP1, KP2, KP3, KP4, KP5, KP6, KP7, KP8, KP9,
        KPDivide, KPMultiply, KPMinus, KPPlus, KPEnter, KPPeriod,

        CapsLock,

        Count
    };

    enum class MouseButton : uint8_t
    {
        Left, Right, Middle, X1, X2,

        Count
    };

    enum class GamepadButton : uint8_t
    {
        South, East, West, North,
        Back, Guide, Start,
        LeftStick, RightStick,
        LeftShoulder, RightShoulder,
        DPadUp, DPadDown, DPadLeft, DPadRight,

        Count
    };

    enum class GamepadAxis : uint8_t
    {
        LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger,

        Count
    };



    // ----------------------------
    // --- Input state snapshot ---
    // ----------------------------

    // Snapshot of the state of all connected input devices, rebuilt once per
    // frame by InputManager. Bindings only ever see this type - never SDL.
    struct InputCtx
    {
        static constexpr size_t KEY_COUNT { static_cast<size_t>(Key::Count) };
        static constexpr size_t MOUSE_BUTTON_COUNT { static_cast<size_t>(MouseButton::Count) };
        static constexpr size_t GAMEPAD_BUTTON_COUNT { static_cast<size_t>(GamepadButton::Count) };
        static constexpr size_t GAMEPAD_AXIS_COUNT { static_cast<size_t>(GamepadAxis::Count) };

        // Keyboard
        std::array<bool, KEY_COUNT> keysDown { };
        std::array<bool, KEY_COUNT> keysPressed { };  // down this frame, up last frame
        std::array<bool, KEY_COUNT> keysReleased { }; // up this frame, down last frame

        // Mouse
        glm::vec2 mousePos { 0.0f };
        glm::vec2 mouseDelta { 0.0f };
        std::array<bool, MOUSE_BUTTON_COUNT> mouseButtonsDown { };
        std::array<bool, MOUSE_BUTTON_COUNT> mouseButtonsPressed { };
        std::array<bool, MOUSE_BUTTON_COUNT> mouseButtonsReleased { };

        // Gamepad (single, primary controller)
        bool gamepadConnected { false };
        std::array<bool, GAMEPAD_BUTTON_COUNT> gamepadButtonsDown { };
        std::array<bool, GAMEPAD_BUTTON_COUNT> gamepadButtonsPressed { };
        std::array<bool, GAMEPAD_BUTTON_COUNT> gamepadButtonsReleased { };
        std::array<float, GAMEPAD_AXIS_COUNT> gamepadAxes { };

        bool IsKeyDown(Key key) const { return keysDown[static_cast<size_t>(key)]; }
        bool IsKeyPressed(Key key) const { return keysPressed[static_cast<size_t>(key)]; }
        bool IsKeyReleased(Key key) const { return keysReleased[static_cast<size_t>(key)]; }

        bool IsMouseButtonDown(MouseButton button) const { return mouseButtonsDown[static_cast<size_t>(button)]; }
        bool IsMouseButtonPressed(MouseButton button) const { return mouseButtonsPressed[static_cast<size_t>(button)]; }
        bool IsMouseButtonReleased(MouseButton button) const { return mouseButtonsReleased[static_cast<size_t>(button)]; }

        bool IsGamepadButtonDown(GamepadButton button) const { return gamepadButtonsDown[static_cast<size_t>(button)]; }
        bool IsGamepadButtonPressed(GamepadButton button) const { return gamepadButtonsPressed[static_cast<size_t>(button)]; }
        bool IsGamepadButtonReleased(GamepadButton button) const { return gamepadButtonsReleased[static_cast<size_t>(button)]; }

        float GetGamepadAxis(GamepadAxis axis) const { return gamepadAxes[static_cast<size_t>(axis)]; }
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

        // Defined in the .cpp: closes the SDL gamepad handle, if one is open
        ~InputManager();

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

        // Read-only access to this frame's input snapshot, for code that
        // wants to query raw state without going through a binding
        const InputCtx& GetCtx() const
        {
            return m_Ctx;
        }

        private:

        // Called once per frame by the Engine
        void Update(float dt)
        {
            // Refresh the snapshot from the backend
            UpdateCtx(dt);

            // Update bindings
            for (auto& slot : m_Slots)
            {
                if (slot)
                {
                    slot->Update(m_Ctx, dt);
                }
            }
        }

        // Defined in the .cpp: polls SDL and refreshes m_Ctx in place, using
        // its previous contents to derive the *Pressed/*Released edges
        void UpdateCtx(float dt);

        template<typename R>
        BoundInput<R>& GetBoundInput(BindingHandle<R> handle) const
        {
            assert(IsHandleValid(handle) && "Stale or invalid BindingHandle used");
            return static_cast<BoundInput<R>&>(*m_Slots[handle.index]);
        }

        EventManager& m_Events;

        InputCtx m_Ctx { };

        // Type-erased SDL_Gamepad*, kept out of this header on purpose - cast
        // back in input_manager.cpp, where SDL is actually included
        void* m_GamepadHandle { nullptr };

        std::vector<std::unique_ptr<BaseBoundInput>> m_Slots;
        std::vector<uint32_t> m_Generations;
        std::vector<uint32_t> m_FreeList;
    };



    // -----------------
    // --- Built-ins ---
    // -----------------

    // --- Concepts ---

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



    // --- Binding types ---

    struct OnKeyUp
    {
        Key key { Key::Unknown };

        bool Evaluate(const InputCtx& ctx) const
        {
            return ctx.IsKeyReleased(key);
        }
    };

    struct OnKeyHeld
    {
        Key key { Key::Unknown };

        bool Evaluate(const InputCtx& ctx) const
        {
            return ctx.IsKeyDown(key);
        }
    };

    struct OnKeyDown
    {
        Key key { Key::Unknown };

        bool Evaluate(const InputCtx& ctx) const
        {
            return ctx.IsKeyPressed(key);
        }
    };

    struct OnMouseButtonUp
    {
        MouseButton btn { MouseButton::Left };

        bool Evaluate(const InputCtx& ctx) const
        {
            return ctx.IsMouseButtonReleased(btn);
        }
    };

    struct OnMouseButtonHeld
    {
        MouseButton btn { MouseButton::Left };

        bool Evaluate(const InputCtx& ctx) const
        {
            return ctx.IsMouseButtonDown(btn);
        }
    };

    struct OnMouseButtonDown
    {
        MouseButton btn { MouseButton::Left };

        bool Evaluate(const InputCtx& ctx) const
        {
            return ctx.IsMouseButtonPressed(btn);
        }
    };

    struct Composite2D
    {
        Key forward { Key::Unknown };
        Key back { Key::Unknown };
        Key left { Key::Unknown };
        Key right { Key::Unknown };

        glm::vec2 Evaluate(const InputCtx& ctx) const
        {
            glm::vec2 value { 0.0f };

            if (ctx.IsKeyDown(forward)) value.y += 1.0f;
            if (ctx.IsKeyDown(back))    value.y -= 1.0f;
            if (ctx.IsKeyDown(right))   value.x += 1.0f;
            if (ctx.IsKeyDown(left))    value.x -= 1.0f;

            return value;
        }
    };

    struct Composite3D
    {
        Key forward { Key::Unknown };
        Key back { Key::Unknown };
        Key left { Key::Unknown };
        Key right { Key::Unknown };
        Key up { Key::Unknown };
        Key down { Key::Unknown };

        glm::vec3 Evaluate(const InputCtx& ctx) const
        {
            glm::vec3 value { 0.0f };

            if (ctx.IsKeyDown(forward)) value.z += 1.0f;
            if (ctx.IsKeyDown(back))    value.z -= 1.0f;
            if (ctx.IsKeyDown(right))   value.x += 1.0f;
            if (ctx.IsKeyDown(left))    value.x -= 1.0f;
            if (ctx.IsKeyDown(up))      value.y += 1.0f;
            if (ctx.IsKeyDown(down))    value.y -= 1.0f;

            return value;
        }
    };

    struct MouseDelta
    {
        glm::vec2 Evaluate(const InputCtx& ctx) const
        {
            return ctx.mouseDelta;
        }
    };



    // --- Post-processors ---

    // Generic radial/magnitude deadzone, works for any R that supports glm::length();
    template<typename R>
    struct Deadzone
    {
        static_assert(HasLengthGLM<R>,
            "Deadzone<R> requires R to support glm::length()");

        using ValueType = R;

        float threshold { 0.0f };

        R Process(const R& value, float /*dt*/) const
        {
            return glm::length(value) < threshold * threshold ? R {} : value;
        }
    };

    // Clamps glm::vec2/vec3 magnitude to a value (1.0f by default)
    template<typename R>
    struct ClampMagnitude
    {
        static_assert(HasLengthGLM<R>,
            "ClampMagnitude<R> requires R to support glm::length()");

        using ValueType = R;

        float minLength { 0.0f };
        float maxLength { 1.0f };

        R Process(const R& value, float) const
        {
            float length = glm::length(value);
            if (length == 0.0f) return R {}; // Avoid division by zero

            float clampedLength = glm::clamp(length, minLength, maxLength);
            return (value / length) * clampedLength;
        }
    };



    // --- Event conditions ---

    // Emits whenever the action value differs from its previous value, gated by minDeltaSeconds
    template<typename R>
    struct EmitOnChangeCondition
    {
        static_assert(EqualityComparableValue<R>,
            "EmitOnChangeCondition<R> requires R to support operator!=");

        using ValueType = R;

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
        using ValueType = R;

        float minDeltaSeconds { 0.0f };

        bool ShouldEmit(const R& /*oldValue*/, const R& /*newValue*/, float dtSinceLastEmit) const
        {
            return dtSinceLastEmit >= minDeltaSeconds;
        }
    };
}

#endif // !INPUT_H