#ifndef ENTITY_H
#define ENTITY_H

#include <cstdint>
#include <cstddef>

namespace krn
{
    using Entity = std::uint32_t;
    using TypeId = std::size_t;

    // Sentinel meaning "no entity" (e.g. a root's parent, an absent sibling link)
    inline constexpr Entity NULL_ENTITY = static_cast<Entity>(-1);

    // Returns typeid hash code of the component type
    template <typename Component>
    inline constexpr TypeId getTypeId()
    {
        return typeid(Component).hash_code();
    }

    // Helper struct for a type-checked void*
    struct ErasedPtr
    {
        void* ptr;
        TypeId type;
    };
}

#endif // !ENTITY_H