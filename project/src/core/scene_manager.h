#ifndef SCENE_MANAGER_H
#define SCENE_MANAGER_H

#include <functional>
#include <queue>
#include <memory>

#include "parser.h"
#include "registry.h"
#include "engine_ctx.h"
#include "gpu_resources_handle.h"



namespace krn
{
    class Engine;

    class Scene final
    {
        friend class SceneManager;
        friend class Renderer;

        public:

        ~Scene() = default;

        Scene(const Scene&) = delete;
        Scene& operator=(const Scene&) = delete;
        Scene(Scene&&) noexcept = default;
        Scene& operator=(Scene&&) noexcept = default;

        Registry& GetReg() { return m_Registry; }

        GPU_ResourcesHandle GetResourcesHandle() const { return m_ResourcesHandle; }

        private:

        Scene() = default;

        void SetResourcesHandle(GPU_ResourcesHandle handle) { m_ResourcesHandle = handle; }

        Registry m_Registry { };
        bool m_ScheduledToUnload { false };
        GPU_ResourcesHandle m_ResourcesHandle { };
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

        void AddInternalLoadCallback(const InternalLoadCallback& func);
        void AddInternalUnloadCallback(const InternalUnloadCallback& func);
        void AddSystemsLoadCallback(const SystemsLoadCallback& func);
        void AddSystemsUnloadCallback(const SystemsUnloadCallback& func);

        std::unique_ptr<Parser> m_Parser;
        fs::path m_ScenesPath;

        std::unique_ptr<Scene> m_CurrentScene;

        std::queue<fs::path> m_ScheduledToLoad;

        // Callbacks for internal systems (Renderer, TransformManager etc.)
        std::vector<InternalLoadCallback> m_InternalLoadCallbacks;
        std::vector<InternalUnloadCallback> m_InternalUnloadCallbacks;

        // Callbacks for user-defined systems (i.e. SystemManager)
        std::vector<SystemsLoadCallback> m_SystemsLoadCallbacks;
        std::vector<SystemsUnloadCallback> m_SystemsUnloadCallbacks;
    };
}

#endif // !SCENE_MANAGER_H