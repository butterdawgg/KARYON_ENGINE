#ifndef GPU_RESOURCES_HANDLE_H
#define GPU_RESOURCES_HANDLE_H

#include <cstdint>
#include <limits>

namespace krn
{
    // Opaque handle to one loaded scene's GPU_Resources slot inside the
    // Renderer (see renderer.h). Generational, same pattern as BindingHandle
    // in input_manager.h: a handle from an already-unloaded scene has a
    // stale generation, so reusing it is caught by IsHandleValid() instead of
    // silently aliasing whatever scene's resources now occupy that slot.
    //
    // Kept in its own header (no bgfx include) so code that only needs to
    // hold/pass the handle - e.g. Scene in scene_manager.h - doesn't have to
    // pull in the renderer's bgfx dependency.
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
