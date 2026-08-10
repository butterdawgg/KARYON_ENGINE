#ifndef GPU_RESOURCES_HANDLE_H
#define GPU_RESOURCES_HANDLE_H

#include <cstdint>
#include <limits>

namespace krn
{
    // Opaque handle to one loaded scene's GPU_Resources slot inside the Renderer (see renderer.h)
    struct GPU_ResourcesHandle
    {
        static constexpr std::uint32_t INVALID_INDEX = std::numeric_limits<std::uint32_t>::max();

        std::uint32_t index { INVALID_INDEX };
        std::uint32_t generation { 0 };

        constexpr bool IsValid() const { return index != INVALID_INDEX; }

        friend constexpr bool operator==(const GPU_ResourcesHandle&, const GPU_ResourcesHandle&) = default;
    };
}

#endif // !GPU_RESOURCES_HANDLE_H