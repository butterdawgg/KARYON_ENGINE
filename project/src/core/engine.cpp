#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
    #include <mmsystem.h>
#endif

#include <SDL3/SDL.h>

#include <thread>
#include <iostream>

#include "engine.h"
#include "event_manager.h"
#include "input_manager.h"
#include "xml_parser.h"
#include "renderer.h"
#include "scene_manager.h"
#include "event_manager.h"
#include "input_manager.h"
#include "system.h"
#include "window.h"
#include "transform.h"



namespace krn
{
    Engine::Engine(const EngineSettings& settings, const WindowSettings& windowSettings) :
        m_Settings(settings)
    {
        #ifdef _WIN32

        timeBeginPeriod(1);

        #endif

        // Init SDL
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
        {
            throw std::runtime_error(std::string("SDL_Init Error: ") + SDL_GetError());
        }

        // Init systems
        m_Window = std::make_unique<Window>(windowSettings);

        m_Events = std::make_unique<EventManager>();

        m_Input = std::make_unique<InputManager>(*m_Events);

        m_Scenes = std::make_unique<SceneManager>(
            std::make_unique<XML_Parser>(), m_Settings.dataPath / "scenes");

        // Constructed before Renderer so SystemManager's scene-load callback
        // (which runs user System::OnLoad, where gameplay code populates the
        // scene) registers - and therefore runs - before Renderer's own
        // scene-load callback, which resolves Model material handles
        // for whatever entities exist by that point.
        m_Systems = std::make_unique<SystemManager>(*m_Scenes);

        m_Renderer = std::make_unique<Renderer>(*m_Window, *m_Scenes, m_Settings.dataPath);

        m_Transforms = std::make_unique<TransformManager>(*m_Scenes);

        // Call user-specified load function after initialization is complete
        if (m_Settings.loadCallback)
        {
            m_Settings.loadCallback(
                EngineCtx {
                    .window = *m_Window,
                    .events = *m_Events,
                    .input = *m_Input,
                    .scenes = *m_Scenes,
                    .renderer = *m_Renderer,
                    .systems = *m_Systems,
                    .transforms = *m_Transforms
                });
        }
    }

    Engine::~Engine()
    {
        #ifdef _WIN32

        timeEndPeriod(1);

        #endif

        // Quit SDL
        SDL_Quit();
    }

    void Engine::Run()
    {
        while (!m_ShouldExit)
        {
            PollEvents();

            const auto frameStartTime { clock::now() };

            const float deltaTime { seconds(frameStartTime - m_LastUpdateTime).count() };

            m_LastUpdateTime = frameStartTime;

            m_FixedUpdateLag += std::min(deltaTime, m_Settings.maxFrameTime);

            // Fixed update loop
            while (m_FixedUpdateLag >= m_Settings.fixedTimestep)
            {
                FixedUpdate(m_Settings.fixedTimestep);

                m_FixedUpdateLag -= m_Settings.fixedTimestep;
            }

            // Update once
            Update(deltaTime);

            // Wait until the end of capped frame time
            const auto frameTime {
                std::chrono::duration_cast<nanoseconds>(seconds(m_Settings.frameTime)) };

            const auto targetTime { frameStartTime + frameTime };

            WaitUntil(targetTime);
        }
    }

    void Engine::Update(float deltaTime)
    {
        // FPS counter
        static float accTime { 0.0f };
        static int numFrames { 1 };

        const float printInterval { 1.0f };

        accTime += deltaTime;
        numFrames++;

        if (accTime >= printInterval)
        {
            const float averageFPS { numFrames == 0 ? 0.0f : 1.0f / (accTime / numFrames) };

            std::cout << std::format("FPS: {:.2f}", averageFPS);

            accTime = 0.0f;
            numFrames = 0;
        }

        // Build ctx
        EngineCtx ctx {
            .window = *m_Window,
            .events = *m_Events,
            .input = *m_Input,
            .scenes = *m_Scenes,
            .renderer = *m_Renderer,
            .systems = *m_Systems,
            .transforms = *m_Transforms
        };

        // Update built-in systems (may load/unload the current scene)
        m_Scenes->Update(ctx);
        m_Input->Update(deltaTime);

        // Get current scene - only valid to check after m_Scenes->Update()
        // above has had a chance to process a scheduled load
        auto* scene { m_Scenes->GetCurrentScene() };

        if (scene == nullptr)
        {
            m_Renderer->DrawEmptyFrame();

            return;
        }

        // Update user systems
        m_Systems->Update(ctx, scene->GetReg(), deltaTime);

        // Render frame
        m_Renderer->DrawFrame(ctx, *scene, scene->GetReg());
    }

    void Engine::FixedUpdate(float deltaTime)
    {
        // Build ctx
        EngineCtx ctx {
            .window = *m_Window,
            .events = *m_Events,
            .input = *m_Input,
            .scenes = *m_Scenes,
            .renderer = *m_Renderer,
            .systems = *m_Systems,
            .transforms = *m_Transforms
        };

        // Get current scene
        auto* scene { m_Scenes->GetCurrentScene() };

        if (scene == nullptr)
            return;

        // Update user systems
        m_Systems->Update(ctx, scene->GetReg(), deltaTime);
    }

    void Engine::PollEvents()
    {
        SDL_Event event;

        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
            {
                m_ShouldExit = true;
            }
        }
    }

    void Engine::WaitUntil(clock::time_point targetTime)
    {
        while (true)
        {
            const auto now = clock::now();
            const double secondsLeft = seconds(targetTime - now).count();

            if (secondsLeft <= 0.0)
                break;

            if (secondsLeft > m_EstimatedSleepError)
            {
                const auto sleepStart = clock::now();
                const auto sleepTime = seconds(secondsLeft - m_EstimatedSleepError);

                std::this_thread::sleep_for(sleepTime);

                const double actualSlept = seconds(clock::now() - sleepStart).count();
                const double requestedSlept = sleepTime.count();
                const double error = actualSlept - requestedSlept;

                // Exponential moving average of how much we overshoot sleep by
                m_EstimatedSleepError += K_ERR_SMOOTHING * (error - m_EstimatedSleepError);
                m_EstimatedSleepError = std::max(m_EstimatedSleepError, 0.0);
            }
            else
            {
                // final stretch: spin
                while (clock::now() < targetTime) { }

                break;
            }
        }
    }
}