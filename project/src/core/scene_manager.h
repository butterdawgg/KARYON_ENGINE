#ifndef SCENE_MANAGER_H
#define SCENE_MANAGER_H

#include <functional>
#include <queue>
#include <memory>

#include "parser.h"
#include "loader.h"
#include "registry.h"
#include "engine_ctx.h"



namespace krn
{
    template <typename T>
    concept ParserType = std::is_base_of_v<Parser, T> &&
        requires {{ std::make_unique<T>() } -> std::same_as<std::unique_ptr<T>>; };

    template <typename T>
    concept LoaderType = std::is_base_of_v<Loader, T>&&
        requires {{ std::make_unique<T>() } -> std::same_as<std::unique_ptr<T>>; };

    class Scene final
    {
        public:

        Scene() = default;
        ~Scene() = default;

        Scene(const Scene&) = delete;
        Scene& operator=(const Scene&) = delete;
        Scene(Scene&&) noexcept = default;
        Scene& operator=(Scene&&) noexcept = default;

        Registry& GetReg() { return m_Registry; }

        void Unload() { m_Unloaded = true; }
        bool IsUnloaded() const { return m_Unloaded; }

        private:

        Registry m_Registry { };
        bool m_Unloaded { false };
    };

    class SceneManager final
    {
        friend class Engine;

        public:

        SceneManager(const fs::path& scenesPath);

        template <ParserType T>
        void AddParser(fs::path fileExtension);

        template <LoaderType T>
        void AddLoader(fs::path fileExtension);

        void LoadScene(const std::string& filename);

        Scene* GetCurrentScene() { return m_CurrentScene.get(); }

        Signal<const EngineCtx&, Scene&, Registry&, LoadResult&> OnResourceLoad;
        Signal<const EngineCtx&, Scene&, Registry&> OnPreprocess;
        Signal<const EngineCtx&, Scene&, Registry&> OnLoad;
        Signal<const EngineCtx&, Scene&, Registry&> OnUnload;

        ~SceneManager() = default;
        SceneManager(const SceneManager&) = delete;
        SceneManager& operator=(const SceneManager&) = delete;
        SceneManager(SceneManager&&) noexcept = delete;
        SceneManager& operator=(SceneManager&&) noexcept = delete;

        private:

        void Update(const EngineCtx& ctx);

        std::optional<SceneDesc> ParseScene(const fs::path& path) const;
        bool LoadFile(LoadResult& outResult, const fs::path& path) const;

        std::unique_ptr<Scene> LoadScheduledScene(const EngineCtx& ctx, const fs::path& path);

        fs::path m_ScenesPath;

        std::unique_ptr<Scene> m_CurrentScene;

        std::queue<fs::path> m_ScheduledToLoad;

        std::unordered_map<fs::path, std::unique_ptr<Parser>> m_Parsers;
        std::unordered_map<fs::path, std::unique_ptr<Loader>> m_Loaders;
    };



    template <ParserType T>
    void SceneManager::AddParser(fs::path fileExtension)
    {
        if (fileExtension.empty())
        {
            logError("SceneManager: cannot add a parser for an empty extension!");

            return;
        }

        if (m_Parsers.contains(fileExtension))
        {
            logError("SceneManager: failed to add parser for extension '{}', it already has a parser!",
                fileExtension.string());

            return;
        }

        m_Parsers[fileExtension] = std::make_unique<T>();
    }

    template <LoaderType T>
    void SceneManager::AddLoader(fs::path fileExtension)
    {
        if (fileExtension.empty())
        {
            logError("SceneManager: cannot add a loader for an empty extension!");

            return;
        }

        if (m_Loaders.contains(fileExtension))
        {
            logError("SceneManager: failed to add loader for extension '{}', it already has a loader!",
                fileExtension.string());

            return;
        }

        m_Loaders[fileExtension] = std::make_unique<T>();
    }
}

#endif // !SCENE_MANAGER_H