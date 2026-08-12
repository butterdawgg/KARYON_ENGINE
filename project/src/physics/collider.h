#ifndef COLLIDER_H
#define COLLIDER_H

namespace krn
{
    class Physics;

    enum class ColliderShape : std::uint8_t
    {
        Box,
        Sphere,
        Capsule
    };

    // A collision shape. Requires a sibling Rigidbody to attach to (use
    // BodyType::Static for immovable geometry) - a Collider with no
    // Rigidbody on the same entity never enters the simulation.
    struct Collider
    {
        friend class Physics;

        ColliderShape shape { ColliderShape::Box };

        glm::vec3 boxHalfExtents { 0.5f, 0.5f, 0.5f }; // Box only
        float radius { 0.5f };                         // Sphere & Capsule
        float capsuleHalfHeight { 0.5f };              // Capsule only: half-length of the cylindrical segment, excluding the end caps

        glm::vec3 center { 0.0f, 0.0f, 0.0f }; // local offset from the body origin

        float density { 1.0f };
        float friction { 0.6f };
        float restitution { 0.0f };
        bool isSensor { false };

        // Collision filtering: a query only reports a shape whose categoryBits
        // intersect the query's mask, and shapes only collide with each other
        // when each one's categoryBits intersect the other's maskBits.
        std::uint64_t categoryBits { ~0ull };
        std::uint64_t maskBits { ~0ull };

        private:

        // Packed shape id, owned and interpreted only by Physics. Zero means
        // "no simulation shape created yet".
        std::uint64_t m_ShapeHandle { 0 };
    };
}

#endif // !COLLIDER_H