#include "scene_manager.h"

#include "serialization.h"
#include "transform.h"

namespace krn
{
    SceneManager::SceneManager(const fs::path& scenesPath) :
        m_ScenesPath(scenesPath)
    { }

    void SceneManager::LoadScene(const std::string& filename)
    {
        m_ScheduledToLoad.push(m_ScenesPath / filename);
    }

    void SceneManager::Update(const EngineCtx& ctx)
    {
        // Helper lambdas
        auto unloadCurrentScene =
            [&] (bool checkScheduledToUnload) -> void
            {
                if (m_CurrentScene == nullptr)
                    return;

                if (checkScheduledToUnload && !m_CurrentScene->IsUnloaded())
                    return;

                OnUnload.Emit(ctx, *m_CurrentScene, m_CurrentScene->GetReg());

                m_CurrentScene.reset();
            };

        auto popAllScheduled =
            [&] () -> void
            {
                while (!m_ScheduledToLoad.empty())
                {
                    m_ScheduledToLoad.pop();
                }
            };



        // Check if current scene is scheduled to unload, if true -> unload
        unloadCurrentScene(true);

        // Check if exactly any scenes is sheduled to load
        if (!m_ScheduledToLoad.empty())
        {
            // Unload old scene
            unloadCurrentScene(false);

            // Load new scene (also executes internal load callbacks)
            m_CurrentScene = LoadScheduledScene(ctx, m_ScheduledToLoad.front());

            popAllScheduled(); // multiple scenes aren't supported yet

            if (m_CurrentScene != nullptr)
                OnLoad.Emit(ctx, *m_CurrentScene, m_CurrentScene->GetReg());
        }
    }

    std::optional<SceneDesc> SceneManager::ParseScene(const fs::path& file) const
    {
        if (!file.has_extension())
        {
            logError("SceneManager: failed to load file '{}', it has no extension!", file.string());

            return std::nullopt;
        }

        auto it = m_Parsers.find(file.extension());

        if (it == m_Parsers.end())
        {
            logError("SceneManager: failed to load file '{}', parser for this extension not found! "
                "Did you forget to add it to SceneManager via AddParser(...)?", file.string());

            return std::nullopt;
        }

        auto& parser = it->second;

        return parser->ParseScene(file);
    }

    bool SceneManager::LoadFile(LoadResult& outResult, const fs::path& file) const
    {
        if (!file.has_extension())
        {
            logError("SceneManager: failed to load resource file '{}', it has no extension!", file.string());

            return false;
        }

        auto it = m_Loaders.find(file.extension());

        if (it == m_Loaders.end())
        {
            logError("SceneManager: failed to load resource file '{}', loader for this extension not found! "
                "Did you forget to add it to SceneManager via AddLoader(...)?", file.string());

            return false;
        }

        auto& loader = it->second;

        return loader->LoadFile(outResult, file);
    }

    std::unique_ptr<Scene> SceneManager::LoadScheduledScene(const EngineCtx& ctx, const fs::path& path)
    {
        auto sceneDesc { ParseScene(path) };

        if (!sceneDesc.has_value())
        {
            logError("SceneManager: failed to load scene file '{}': parser error!", path.string());

            return nullptr;
        }

        // Make new scene
        auto newScene = std::make_unique<Scene>();
        auto& reg { newScene->GetReg() };

        // Make all entities and components
        for (auto& entityDesc : sceneDesc->entities)
        {
            Entity newEntity { reg.Create() };

            // Add metadata
            if (entityDesc.metadata.has_value())
                reg.Push<LoadMetadata>(newEntity, std::move(entityDesc.metadata.value()));

            // Add components from raw values
            for (auto& componentDesc : entityDesc.components)
            {
                SerializeManager::ApplyBlock(reg, newEntity,
                    componentDesc.name, componentDesc.rawFields);
            }
        }

        // Load resources: every entry adds exactly one file, so gltf_file_id always matches the list order
        LoadResult loadResult;

        for (auto& resPath : sceneDesc->resourcesToLoad)
        {
            if (!LoadFile(loadResult, resPath))
            {
                logError("SceneManager: failed to load resource file '{}' for scene file '{}'",
                    resPath.string(), path.string());

                loadResult.files.emplace_back().loadFailed = true;
            }
        }

        // Execute load callbacks
        OnResourceLoad.Emit(ctx, *newScene, reg, loadResult);
        OnPreprocess.Emit(ctx, *newScene, reg);

        // Remove metadata
        reg.Clear<LoadMetadata>();

        return newScene;
    }
}