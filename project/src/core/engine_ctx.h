#ifndef ENGINE_CTX_H
#define ENGINE_CTX_H

namespace krn
{
    class Window;
    class EventManager;
    class InputManager;
    class SceneManager;
    class Renderer;
    class SystemManager;
    class TransformManager;

    struct EngineCtx
    {
        Window& window;
        EventManager& events;
        InputManager& input;
        SceneManager& scenes;
        Renderer& renderer;
        SystemManager& systems;
        TransformManager& transforms;
    };
}

#endif // !ENGINE_CTX_H