#ifndef RIGIDBODY_H
#define RIGIDBODY_H

namespace krn
{
    class Physics;

    enum class BodyType : std::uint8_t
    {
        Static,
        Kinematic,
        Dynamic
    };

    // Drives a physics body. Add a Collider alongside it to give the body
    // something to collide with - a Rigidbody with no Collider simulates but
    // never touches anything. Use BodyType::Static for immovable geometry.
    struct Rigidbody
    {
        friend class Physics;

        BodyType type { BodyType::Dynamic };

        glm::vec3 linearVelocity { 0.0f, 0.0f, 0.0f };
        glm::vec3 angularVelocity { 0.0f, 0.0f, 0.0f };

        float linearDamping { 0.0f };
        float angularDamping { 0.0f };
        float gravityScale { 1.0f };

        bool enableSleep { true };
        bool isBullet { false };

        private:

        // Packed body id, owned and interpreted only by Physics. Zero means
        // "no simulation body created yet".
        std::uint64_t m_BodyHandle { 0 };
    };
}

#endif // !RIGIDBODY_H