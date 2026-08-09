#ifndef PHYSICS_H
#define PHYSICS_H

namespace krn
{
    class Physics final
    {
        public:

        Physics() = default;
        ~Physics() = default;

        Physics(const Physics&) = delete;
        Physics& operator=(const Physics&) = delete;
        Physics(Physics&&) noexcept = delete;
        Physics& operator=(Physics&&) noexcept = delete;

        void Update(float dt);


    };
}

#endif