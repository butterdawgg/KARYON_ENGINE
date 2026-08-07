#include "input_manager.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gamepad.h>

#include <algorithm>

namespace krn
{
    // ------------------------------------------------------------------
    // --- krn::Key/MouseButton/GamepadButton/GamepadAxis <-> SDL maps ---
    // ------------------------------------------------------------------
    //
    // This is the only place in the engine that knows SDL identifiers exist -
    // everything above InputManager deals exclusively in the krn:: enums.

    namespace
    {
        SDL_Scancode ToSDLScancode(Key key)
        {
            switch (key)
            {
                case Key::A: return SDL_SCANCODE_A;
                case Key::B: return SDL_SCANCODE_B;
                case Key::C: return SDL_SCANCODE_C;
                case Key::D: return SDL_SCANCODE_D;
                case Key::E: return SDL_SCANCODE_E;
                case Key::F: return SDL_SCANCODE_F;
                case Key::G: return SDL_SCANCODE_G;
                case Key::H: return SDL_SCANCODE_H;
                case Key::I: return SDL_SCANCODE_I;
                case Key::J: return SDL_SCANCODE_J;
                case Key::K: return SDL_SCANCODE_K;
                case Key::L: return SDL_SCANCODE_L;
                case Key::M: return SDL_SCANCODE_M;
                case Key::N: return SDL_SCANCODE_N;
                case Key::O: return SDL_SCANCODE_O;
                case Key::P: return SDL_SCANCODE_P;
                case Key::Q: return SDL_SCANCODE_Q;
                case Key::R: return SDL_SCANCODE_R;
                case Key::S: return SDL_SCANCODE_S;
                case Key::T: return SDL_SCANCODE_T;
                case Key::U: return SDL_SCANCODE_U;
                case Key::V: return SDL_SCANCODE_V;
                case Key::W: return SDL_SCANCODE_W;
                case Key::X: return SDL_SCANCODE_X;
                case Key::Y: return SDL_SCANCODE_Y;
                case Key::Z: return SDL_SCANCODE_Z;

                case Key::Num0: return SDL_SCANCODE_0;
                case Key::Num1: return SDL_SCANCODE_1;
                case Key::Num2: return SDL_SCANCODE_2;
                case Key::Num3: return SDL_SCANCODE_3;
                case Key::Num4: return SDL_SCANCODE_4;
                case Key::Num5: return SDL_SCANCODE_5;
                case Key::Num6: return SDL_SCANCODE_6;
                case Key::Num7: return SDL_SCANCODE_7;
                case Key::Num8: return SDL_SCANCODE_8;
                case Key::Num9: return SDL_SCANCODE_9;

                case Key::F1: return SDL_SCANCODE_F1;
                case Key::F2: return SDL_SCANCODE_F2;
                case Key::F3: return SDL_SCANCODE_F3;
                case Key::F4: return SDL_SCANCODE_F4;
                case Key::F5: return SDL_SCANCODE_F5;
                case Key::F6: return SDL_SCANCODE_F6;
                case Key::F7: return SDL_SCANCODE_F7;
                case Key::F8: return SDL_SCANCODE_F8;
                case Key::F9: return SDL_SCANCODE_F9;
                case Key::F10: return SDL_SCANCODE_F10;
                case Key::F11: return SDL_SCANCODE_F11;
                case Key::F12: return SDL_SCANCODE_F12;

                case Key::Space: return SDL_SCANCODE_SPACE;
                case Key::Enter: return SDL_SCANCODE_RETURN;
                case Key::Escape: return SDL_SCANCODE_ESCAPE;
                case Key::Tab: return SDL_SCANCODE_TAB;
                case Key::Backspace: return SDL_SCANCODE_BACKSPACE;
                case Key::Delete: return SDL_SCANCODE_DELETE;
                case Key::Insert: return SDL_SCANCODE_INSERT;

                case Key::Up: return SDL_SCANCODE_UP;
                case Key::Down: return SDL_SCANCODE_DOWN;
                case Key::Left: return SDL_SCANCODE_LEFT;
                case Key::Right: return SDL_SCANCODE_RIGHT;
                case Key::Home: return SDL_SCANCODE_HOME;
                case Key::End: return SDL_SCANCODE_END;
                case Key::PageUp: return SDL_SCANCODE_PAGEUP;
                case Key::PageDown: return SDL_SCANCODE_PAGEDOWN;

                case Key::LeftShift: return SDL_SCANCODE_LSHIFT;
                case Key::RightShift: return SDL_SCANCODE_RSHIFT;
                case Key::LeftCtrl: return SDL_SCANCODE_LCTRL;
                case Key::RightCtrl: return SDL_SCANCODE_RCTRL;
                case Key::LeftAlt: return SDL_SCANCODE_LALT;
                case Key::RightAlt: return SDL_SCANCODE_RALT;
                case Key::LeftGui: return SDL_SCANCODE_LGUI;
                case Key::RightGui: return SDL_SCANCODE_RGUI;

                case Key::Minus: return SDL_SCANCODE_MINUS;
                case Key::Equals: return SDL_SCANCODE_EQUALS;
                case Key::LeftBracket: return SDL_SCANCODE_LEFTBRACKET;
                case Key::RightBracket: return SDL_SCANCODE_RIGHTBRACKET;
                case Key::Backslash: return SDL_SCANCODE_BACKSLASH;
                case Key::Semicolon: return SDL_SCANCODE_SEMICOLON;
                case Key::Apostrophe: return SDL_SCANCODE_APOSTROPHE;
                case Key::Comma: return SDL_SCANCODE_COMMA;
                case Key::Period: return SDL_SCANCODE_PERIOD;
                case Key::Slash: return SDL_SCANCODE_SLASH;
                case Key::Grave: return SDL_SCANCODE_GRAVE;

                case Key::KP0: return SDL_SCANCODE_KP_0;
                case Key::KP1: return SDL_SCANCODE_KP_1;
                case Key::KP2: return SDL_SCANCODE_KP_2;
                case Key::KP3: return SDL_SCANCODE_KP_3;
                case Key::KP4: return SDL_SCANCODE_KP_4;
                case Key::KP5: return SDL_SCANCODE_KP_5;
                case Key::KP6: return SDL_SCANCODE_KP_6;
                case Key::KP7: return SDL_SCANCODE_KP_7;
                case Key::KP8: return SDL_SCANCODE_KP_8;
                case Key::KP9: return SDL_SCANCODE_KP_9;
                case Key::KPDivide: return SDL_SCANCODE_KP_DIVIDE;
                case Key::KPMultiply: return SDL_SCANCODE_KP_MULTIPLY;
                case Key::KPMinus: return SDL_SCANCODE_KP_MINUS;
                case Key::KPPlus: return SDL_SCANCODE_KP_PLUS;
                case Key::KPEnter: return SDL_SCANCODE_KP_ENTER;
                case Key::KPPeriod: return SDL_SCANCODE_KP_PERIOD;

                case Key::CapsLock: return SDL_SCANCODE_CAPSLOCK;

                default: return SDL_SCANCODE_UNKNOWN;
            }
        }

        Uint32 ToSDLMouseButtonMask(MouseButton button)
        {
            switch (button)
            {
                case MouseButton::Left: return SDL_BUTTON_LMASK;
                case MouseButton::Right: return SDL_BUTTON_RMASK;
                case MouseButton::Middle: return SDL_BUTTON_MMASK;
                case MouseButton::X1: return SDL_BUTTON_X1MASK;
                case MouseButton::X2: return SDL_BUTTON_X2MASK;
                default: return 0;
            }
        }

        SDL_GamepadButton ToSDLGamepadButton(GamepadButton button)
        {
            switch (button)
            {
                case GamepadButton::South: return SDL_GAMEPAD_BUTTON_SOUTH;
                case GamepadButton::East: return SDL_GAMEPAD_BUTTON_EAST;
                case GamepadButton::West: return SDL_GAMEPAD_BUTTON_WEST;
                case GamepadButton::North: return SDL_GAMEPAD_BUTTON_NORTH;
                case GamepadButton::Back: return SDL_GAMEPAD_BUTTON_BACK;
                case GamepadButton::Guide: return SDL_GAMEPAD_BUTTON_GUIDE;
                case GamepadButton::Start: return SDL_GAMEPAD_BUTTON_START;
                case GamepadButton::LeftStick: return SDL_GAMEPAD_BUTTON_LEFT_STICK;
                case GamepadButton::RightStick: return SDL_GAMEPAD_BUTTON_RIGHT_STICK;
                case GamepadButton::LeftShoulder: return SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
                case GamepadButton::RightShoulder: return SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
                case GamepadButton::DPadUp: return SDL_GAMEPAD_BUTTON_DPAD_UP;
                case GamepadButton::DPadDown: return SDL_GAMEPAD_BUTTON_DPAD_DOWN;
                case GamepadButton::DPadLeft: return SDL_GAMEPAD_BUTTON_DPAD_LEFT;
                case GamepadButton::DPadRight: return SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
                default: return SDL_GAMEPAD_BUTTON_INVALID;
            }
        }

        SDL_GamepadAxis ToSDLGamepadAxis(GamepadAxis axis)
        {
            switch (axis)
            {
                case GamepadAxis::LeftX: return SDL_GAMEPAD_AXIS_LEFTX;
                case GamepadAxis::LeftY: return SDL_GAMEPAD_AXIS_LEFTY;
                case GamepadAxis::RightX: return SDL_GAMEPAD_AXIS_RIGHTX;
                case GamepadAxis::RightY: return SDL_GAMEPAD_AXIS_RIGHTY;
                case GamepadAxis::LeftTrigger: return SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
                case GamepadAxis::RightTrigger: return SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
                default: return SDL_GAMEPAD_AXIS_INVALID;
            }
        }

        // Sticks report [-32768, 32767], triggers report [0, 32767] - both
        // normalize cleanly against the positive extent
        float NormalizeGamepadAxis(Sint16 raw)
        {
            constexpr float POSITIVE_EXTENT { 32767.0f };
            return std::max(raw / POSITIVE_EXTENT, -1.0f);
        }
    }

    InputManager::~InputManager()
    {
        if (m_GamepadHandle)
        {
            SDL_CloseGamepad(static_cast<SDL_Gamepad*>(m_GamepadHandle));
        }
    }

    void InputManager::UpdateCtx(float /*dt*/)
    {
        // --- Keyboard ---

        const bool* keyState { SDL_GetKeyboardState(nullptr) };

        for (size_t i { 0 }; i < InputCtx::KEY_COUNT; ++i)
        {
            const SDL_Scancode scancode { ToSDLScancode(static_cast<Key>(i)) };

            const bool wasDown { m_Ctx.keysDown[i] };
            const bool isDown { scancode != SDL_SCANCODE_UNKNOWN && keyState[scancode] };

            m_Ctx.keysPressed[i] = isDown && !wasDown;
            m_Ctx.keysReleased[i] = !isDown && wasDown;
            m_Ctx.keysDown[i] = isDown;
        }

        // --- Mouse ---

        float mouseX, mouseY;
        const Uint32 mouseButtons { SDL_GetMouseState(&mouseX, &mouseY) };

        float deltaX, deltaY;
        SDL_GetRelativeMouseState(&deltaX, &deltaY);

        m_Ctx.mousePos = { mouseX, mouseY };
        m_Ctx.mouseDelta = { deltaX, deltaY };

        for (size_t i { 0 }; i < InputCtx::MOUSE_BUTTON_COUNT; ++i)
        {
            const Uint32 mask { ToSDLMouseButtonMask(static_cast<MouseButton>(i)) };

            const bool wasDown { m_Ctx.mouseButtonsDown[i] };
            const bool isDown { (mouseButtons & mask) != 0 };

            m_Ctx.mouseButtonsPressed[i] = isDown && !wasDown;
            m_Ctx.mouseButtonsReleased[i] = !isDown && wasDown;
            m_Ctx.mouseButtonsDown[i] = isDown;
        }

        // --- Gamepad (single, primary controller) ---

        auto* gamepad { static_cast<SDL_Gamepad*>(m_GamepadHandle) };

        if (gamepad && !SDL_GamepadConnected(gamepad))
        {
            SDL_CloseGamepad(gamepad);
            gamepad = nullptr;
            m_GamepadHandle = nullptr;
        }

        if (!gamepad)
        {
            int count { 0 };
            SDL_JoystickID* ids { SDL_GetGamepads(&count) };

            if (ids && count > 0)
            {
                gamepad = SDL_OpenGamepad(ids[0]);
                m_GamepadHandle = gamepad;
            }

            if (ids)
            {
                SDL_free(ids);
            }
        }

        m_Ctx.gamepadConnected = gamepad != nullptr;

        for (size_t i { 0 }; i < InputCtx::GAMEPAD_BUTTON_COUNT; ++i)
        {
            const bool wasDown { m_Ctx.gamepadButtonsDown[i] };
            const bool isDown { gamepad &&
                SDL_GetGamepadButton(gamepad, ToSDLGamepadButton(static_cast<GamepadButton>(i))) };

            m_Ctx.gamepadButtonsPressed[i] = isDown && !wasDown;
            m_Ctx.gamepadButtonsReleased[i] = !isDown && wasDown;
            m_Ctx.gamepadButtonsDown[i] = isDown;
        }

        for (size_t i { 0 }; i < InputCtx::GAMEPAD_AXIS_COUNT; ++i)
        {
            m_Ctx.gamepadAxes[i] = gamepad
                ? NormalizeGamepadAxis(SDL_GetGamepadAxis(gamepad, ToSDLGamepadAxis(static_cast<GamepadAxis>(i))))
                : 0.0f;
        }
    }
}