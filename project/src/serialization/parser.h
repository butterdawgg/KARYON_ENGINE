#ifndef PARSER_H
#define PARSER_H

#include <string>
#include <vector>
#include <optional>

#include "utils.h"

namespace krn
{
    // ----------------------------------
    // --- Helper description structs ---
    // ----------------------------------

    struct ComponentDesc
    {
        std::string name;
        std::vector<std::pair<std::string, std::string>> rawFields;
    };

    struct LoadMetadata
    {
        struct GLTF_Node
        {
            std::string name { };
            bool loadChildren { false };
            std::vector<std::string> excludeChildNodes { };
        };

        static constexpr uint32_t INVALID_ID { static_cast<uint32_t>(-1) };

        uint32_t id { INVALID_ID };
        uint32_t parent { INVALID_ID };

        GLTF_Node gltfNode { };
    };

    struct EntityDesc
    {
        std::vector<ComponentDesc> components { };
        std::optional<LoadMetadata> metadata { };
    };

    struct SceneDesc
    {
        std::vector<EntityDesc> entities { };
        std::vector<fs::path> resourcesToLoad { };
    };



    // ------------------------
    // --- Parser interface ---
    // ------------------------

    class Parser
    {
        public:

        virtual ~Parser() = default;

        virtual std::optional<SceneDesc> ParseScene(fs::path filepath) = 0;
    };
}

#endif // !PARSER_H