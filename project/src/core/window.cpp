#include <SDL3/SDL.h>

#include <stdexcept>
#include <string>

#include "window.h"

namespace krn
{
    Window::Window(const WindowSettings& settings)
    {
        Uint32 flags { 0 };

        if (settings.resizable)
            flags |= SDL_WINDOW_RESIZABLE;

        m_Window = SDL_CreateWindow(settings.name.data(), settings.width, settings.height, flags);

        if (m_Window == nullptr)
        {
            SDL_Quit();

            throw std::runtime_error(std::string("SDL_CreateWindow Error: ") + SDL_GetError());
        }

        if (settings.borderlessFullscreen)
            SetBorderlessFullscreen(true);
        else if (settings.fullscreen)
            SetFullscreen(true);
    }

    Window::~Window()
    {
        if (m_Window)
            SDL_DestroyWindow(m_Window);
    }

    void Window::SetFullscreen(bool fullscreen)
    {
        if (fullscreen)
        {
            const SDL_DisplayID display { SDL_GetDisplayForWindow(m_Window) };
            const SDL_DisplayMode* mode { SDL_GetDesktopDisplayMode(display) };

            SDL_SetWindowFullscreenMode(m_Window, mode);
        }

        SDL_SetWindowFullscreen(m_Window, fullscreen);

        m_Fullscreen = fullscreen;
        m_BorderlessFullscreen = false;
    }

    void Window::SetBorderlessFullscreen(bool borderlessFullscreen)
    {
        // A null videomode tells SDL to match the desktop's current mode without changing it
        SDL_SetWindowFullscreenMode(m_Window, nullptr);
        SDL_SetWindowFullscreen(m_Window, borderlessFullscreen);

        m_BorderlessFullscreen = borderlessFullscreen;
        m_Fullscreen = false;
    }

    void Window::SetResizable(bool resizable)
    {
        SDL_SetWindowResizable(m_Window, resizable);
    }

    WindowSize Window::GetSize() const
    {
        int width, height;

        SDL_GetWindowSize(m_Window, &width, &height);

        return { width, height };
    }

    WindowSize Window::GetSizeInPixels() const
    {
        int width, height;

        SDL_GetWindowSizeInPixels(m_Window, &width, &height);

        return { width, height };
    }

    void* Window::GetNativeHandle() const
    {
        SDL_PropertiesID props { SDL_GetWindowProperties(m_Window) };

        #if defined(SDL_PLATFORM_WIN32)

        return SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);

        #elif defined(SDL_PLATFORM_MACOS)

        return SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);

        #elif defined(SDL_PLATFORM_LINUX)

        if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0)
        {
            return SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
        }

        const Sint64 x11Window { SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0) };

        return reinterpret_cast<void*>(static_cast<uintptr_t>(x11Window));

        #else

        #error "Window::GetNativeHandle: unsupported platform"

        #endif
    }

    void* Window::GetNativeDisplay() const
    {
        #if defined(SDL_PLATFORM_WIN32) || defined(SDL_PLATFORM_MACOS)

        return nullptr; // not required by bgfx on this platform

        #elif defined(SDL_PLATFORM_LINUX)

        SDL_PropertiesID props { SDL_GetWindowProperties(m_Window) };

        if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0)
        {
            return SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        }

        return SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);

        #else

        #error "Window::GetNativeDisplay: unsupported platform"

        #endif
    }
}