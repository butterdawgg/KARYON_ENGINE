#include "karyon.h"
#include "temp/camera_movement.h"

static void load(const krn::EngineCtx& ctx)
{
    auto moveHandle = ctx.input.Bind<glm::vec3>(krn::Composite3D {
        krn::Key::W, krn::Key::S, krn::Key::A, krn::Key::D,
        krn::Key::Space, krn::Key::LeftCtrl }, "move");

    auto lookHandle = ctx.input.Bind<glm::vec2>(krn::MouseDelta { }, "look");

    auto activateLookHandle = ctx.input.Bind<bool>(
        krn::OnMouseButtonHeld { krn::MouseButton::Right }, "activateLook");

    std::unique_ptr<CameraMovement> cameraMovement =
        std::make_unique<CameraMovement>(moveHandle, lookHandle, activateLookHandle);

    ctx.systems.AddSystem(std::move(cameraMovement), 10);

    ctx.scenes.LoadScene("scene0.xml");
}

int main()
{
    krn::EngineSettings engineSettings { .loadCallback = load };
    krn::WindowSettings windowSettings { };
    krn::Engine karyon { engineSettings, windowSettings };

    karyon.Run();
}