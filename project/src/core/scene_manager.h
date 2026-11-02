#ifndef SCENE_MANAGER_H
#define SCENE_MANAGER_H

#include <functional>
#include <queue>
#include <memory>

#include "parser.h"
#include "registry.h"
#include "engine_ctx.h"



namespace krn
{
    class Engine;

    class Scene final
    {
        friend class SceneManager;

        public:

        ~Scene() = default;

        Scene(const Scene&) = delete;
        Scene& operator=(const Scene&) = delete;
        Scene(Scene&&) noexcept = default;
        Scene& operator=(Scene&&) noexcept = default;

        Registry& GetReg() { return m_Registry; }

        private:

        Scene() = default;

        Registry m_Registry { };
        bool m_ScheduledToUnload { false };
    };

    class SceneManager final
    {
        friend class Engine;
        friend class SystemManager;
        friend class Renderer;
        friend class TransformManager;

        public:

        SceneManager(std::unique_ptr<Parser>&& parser, const fs::path& scenesPath);

        ~SceneManager() = default;
        SceneManager(const SceneManager&) = delete;
        SceneManager& operator=(const SceneManager&) = delete;
        SceneManager(SceneManager&&) noexcept = delete;
        SceneManager& operator=(SceneManager&&) noexcept = delete;

        void LoadScene(const std::string& filename);

        Scene* GetCurrentScene() { return m_CurrentScene.get(); }

        private:

        using InternalLoadCallback = std::function<void(const EngineCtx&, Scene&, Registry&, std::vector<fs::path>&)>;
        using InternalUnloadCallback = std::function<void(const EngineCtx&, Scene&, Registry&)>;

        using SystemsLoadCallback = std::function<void(const EngineCtx&, Scene&, Registry&)>;
        using SystemsUnloadCallback = std::function<void(const EngineCtx&, Scene&, Registry&)>;

        void Update(const EngineCtx& ctx);

        std::unique_ptr<Scene> LoadScheduledScene(const EngineCtx& ctx, const fs::path& path);

        std::unique_ptr<Parser> m_Parser;
        fs::path m_ScenesPath;

        std::unique_ptr<Scene> m_CurrentScene;

        std::queue<fs::path> m_ScheduledToLoad;

        // Accessed only by friend classes
        Signal<const EngineCtx&, Scene&, Registry&, std::vector<fs::path>&> OnResourceLoad;
        Signal<const EngineCtx&, Scene&, Registry&> OnPreprocess;
        Signal<const EngineCtx&, Scene&, Registry&> OnLoad;
        Signal<const EngineCtx&, Scene&, Registry&> OnUnload;
    };
}

#endif // !SCENE_MANAGER_H