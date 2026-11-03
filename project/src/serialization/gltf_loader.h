#ifndef GLTF_LOADER_H
#define GLTF_LOADER_H

#include "loader.h"

namespace krn
{
    class GLTF_Loader final : public Loader
    {
        public:

        ~GLTF_Loader() override = default;

        bool LoadFile(LoadResult& outResult, const fs::path& file) override;
    };
}

#endif