#ifndef CAMERA_MOVEMENT_H
#define CAMERA_MOVEMENT_H

#include "karyon.h"

class CameraMovement final : public krn::System
{
    public:

    CameraMovement(krn::BindingHandle<glm::vec3> moveHandle, krn::BindingHandle<glm::vec2> lookHandle,
        krn::BindingHandle<bool> activateLookHandle) :
        m_MoveHandle(moveHandle), m_LookHandle(lookHandle),
        m_ActivateLookHandle(activateLookHandle)
    { };

    ~CameraMovement() override = default;

    void Update(const krn::EngineCtx& ctx, krn::Registry& reg, float dt) override;

    private:

    krn::BindingHandle<glm::vec3> m_MoveHandle;
    krn::BindingHandle<glm::vec2> m_LookHandle;
    krn::BindingHandle<bool> m_ActivateLookHandle;
};

#endif // !CAMERA_MOVEMENT_H