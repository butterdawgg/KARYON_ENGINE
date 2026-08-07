#include "camera_movement.h"
#include "camera.h"

void CameraMovement::Update(const krn::EngineCtx& ctx, krn::Registry& reg, float dt)
{
    krn::Entity cameraEntity { krn::FindPrimaryCamera(reg) };

    if (cameraEntity == krn::NULL_ENTITY)
        return;

    if (!reg.Has<krn::Transform>(cameraEntity))
        return;

    auto& transform { reg.Get<krn::Transform>(cameraEntity) };

    const auto moveInput { ctx.input.GetAction(m_MoveHandle) };
    const auto lookInput { ctx.input.GetAction(m_LookHandle) };
    const auto canLook { ctx.input.GetAction(m_ActivateLookHandle) };

    // Rotation
    if (canLook)
    {
        const float radiansDelta { 0.005f };

        static float totalPitch { };
        static float totalYaw { };

        totalPitch += radiansDelta * lookInput.y;
        totalYaw += radiansDelta * lookInput.x;

        totalPitch = std::clamp(totalPitch, -glm::half_pi<float>(), glm::half_pi<float>());;

        transform.SetLocalEulerAngles(glm::vec3(totalPitch, totalYaw, 0.0f));
    }

    // Translation
    const float moveSpeed { 5.0f };

    const glm::vec3 position { transform.GetLocalPosition() };
    const glm::vec3 direction { (transform.GetLocalRotation() * glm::vec3(moveInput.x, 0.0f, moveInput.z)) +
        glm::vec3(0.0f, moveInput.y, 0.0f) };
    const glm::vec3 translation { direction * moveSpeed * dt };

    transform.SetLocalPosition(position + translation);
}