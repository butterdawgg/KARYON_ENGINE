#include "physics.h"

#include <box3d/box3d.h>

namespace krn
{
    Physics::Physics(SceneManager& scenes)
    {
        b3WorldDef worldDef = b3DefaultWorldDef();

        scenes.OnPreprocess.Connect<&Physics::OnLoadCallback>(*this);
    }

    void Physics::OnLoadCallback(const EngineCtx& ctx, Scene& scene, Registry& reg)
    {
        // Process already loaded Rigidbodies
        reg.View<Rigidbody>().ForEach([this] (Entity e, Rigidbody& rb) ->void
            {
                OnRigidbodyConstructed(e, rb);
            });

        // Subscribe to construction/destruction signals
        reg.OnConstruct<Rigidbody>().Connect<&Physics::OnRigidbodyConstructed>(*this);
        reg.OnDestroy<Rigidbody>().Connect<&Physics::OnRigidbodyDestroyed>(*this);
    }

    void Physics::OnRigidbodyConstructed(Entity e, Rigidbody& rb)
    {
        
    }

    void Physics::OnRigidbodyDestroyed(Entity e, Rigidbody & rb)
    {

    }

    void Physics::Update(float dt)
    {

    }
}