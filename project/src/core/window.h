#ifndef WINDOW_H
#define WINDOW_H

#include <string_view>

struct SDL_Window;

namespace krn
{
    struct WindowSettings
    {
        std::string_view name { "KARYON" };
        int width { 1920 };
        int height { 1080 };
        bool resizable { false };
        bool fullscreen { false };
        bool borderlessFullscreen { false };
    };

    struct WindowSize
    {
        int width;
        int height;
    };

    // SDL_Window abstraction
    class Window final
    {
        public:

        explicit Window(const WindowSettings& settings);
        ~Window();

        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;
        Window(Window&&) = delete;
        Window& operator=(Window&&) = delete;

        // Exclusive fullscreen: changes the display's videomode to match the window
        void SetFullscreen(bool fullscreen);

        // Borderless fullscreen: resizes the (undecorated) window to match the desktop, no videomode change
        void SetBorderlessFullscreen(bool borderlessFullscreen);

        void SetResizable(bool resizable);

        bool IsFullscreen() const { return m_Fullscreen; }
        bool IsBorderlessFullscreen() const { return m_BorderlessFullscreen; }

        // Logical window size, in screen coordinates
        WindowSize GetSize() const;

        // Drawable size in pixels; what renderers should size their backbuffer/viewport to
        WindowSize GetSizeInPixels() const;

        // Native window handle (HWND / NSWindow* / wl_surface* / X11 Window)
        void* GetNativeHandle() const;

        // Native display handle, required by bgfx::PlatformData::ndt on X11/Wayland; null elsewhere
        void* GetNativeDisplay() const;

        SDL_Window* GetSDLWindow() const { return m_Window; }

        private:

        SDL_Window* m_Window { nullptr };

        bool m_Fullscreen { false };
        bool m_BorderlessFullscreen { false };
    };
}

#endif // !WINDOW_H
