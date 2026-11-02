#include "scene_manager.h"

#include "serialization.h"
#include "transform.h"

namespace krn
{
    SceneManager::SceneManager(std::unique_ptr<Parser>&& parser, const fs::path& scenesPath) :
        m_Parser(std::move(parser)),
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

                if (checkScheduledToUnload && !m_CurrentScene->m_ScheduledToUnload)
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

    std::unique_ptr<Scene> SceneManager::LoadScheduledScene(const EngineCtx& ctx, const fs::path& path)
    {
        auto sceneDesc { m_Parser->ParseScene(path) };

        if (!sceneDesc.has_value())
        {
            logError("Can't load scene: parser error!");

            return nullptr;
        }

        std::unique_ptr<Scene> newScene { new Scene() };
        auto& reg { newScene->GetReg() };

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

        // Execute load callbacks
        OnResourceLoad.Emit(ctx, *newScene, reg, sceneDesc->resourcesToLoad);
        OnPreprocess.Emit(ctx, *newScene, reg);

        reg.Clear<LoadMetadata>();

        return newScene;
    }
}