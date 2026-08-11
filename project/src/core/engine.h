#ifndef ENGINE_H
#define ENGINE_H

#include <chrono>
#include <filesystem>
#include <functional>

#include "engine_ctx.h"



namespace fs = std::filesystem;

namespace krn
{
    struct WindowSettings;

    struct EngineSettings
    {
        fs::path dataPath { "data" };
        float fixedTimestep { 0.02f };
        float maxFrameTime { 0.25f };
        float frameTime { 1.0f / 60.0f };
        std::function<void(const EngineCtx&)> loadCallback { };
    };

    class Engine
    {
        public:

        Engine(const EngineSettings& settings, const WindowSettings& windowSettings);
        ~Engine();

        Engine(const Engine&) = delete;
        Engine& operator=(const Engine&) = delete;
        Engine(Engine&&) = delete;
        Engine& operator=(Engine&&) = delete;

        void Run();

        private:

        using clock = std::chrono::steady_clock;
        using seconds = std::chrono::duration<float>;
        using milliseconds = std::chrono::milliseconds;
        using nanoseconds = std::chrono::nanoseconds;

        static constexpr double K_ERR_SMOOTHING { 0.1 };

        // Runs every frame
        void Update(float deltaTime);

        // Runs every fixed timestep
        void FixedUpdate(float deltaTime);

        // Waits smartly on any operating system
        void WaitUntil(clock::time_point targetTime);

        // Pumps the OS event queue; keeps the window responsive and detects close/quit
        void PollEvents();

        EngineSettings m_Settings { };

        bool m_ShouldExit { };

        clock::time_point m_LastUpdateTime { clock::now() };
        float m_FixedUpdateLag { };
        double m_EstimatedSleepError { 1e-4 }; // seconds, initial guess

        std::unique_ptr<Window> m_Window;
        std::unique_ptr<EventManager> m_Events;
        std::unique_ptr<InputManager> m_Input;
        std::unique_ptr<SceneManager> m_Scenes;
        std::unique_ptr<Renderer> m_Renderer;
        std::unique_ptr<SystemManager> m_Systems;
        std::unique_ptr<TransformManager> m_Transforms;
        std::unique_ptr<Physics> m_Physics;
    };
}

#endif // !KARYON_H