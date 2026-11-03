#ifndef LOADER_H
#define LOADER_H

#include <glm/gtc/quaternion.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
namespace fs = std::filesystem;

namespace krn
{
    struct Vertex
    {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec4 tangent; // xyz = tangent, w = bitangent sign (+1/-1)
        glm::vec2 uv;
    };

    struct MeshDesc
    {
        static constexpr std::uint32_t NO_MATERIAL { static_cast<std::uint32_t>(-1) };

        std::vector<Vertex> vertices;
        std::vector<std::uint32_t> indices;
        std::vector<size_t> primitives { };
        std::vector<std::uint32_t> materials { };
    };

    struct MaterialDesc
    {
        glm::vec3 albedo { 1.0f, 1.0f, 1.0f };
        float metallic { 0.0f };
        float roughness { 1.0f };
        glm::vec3 emissive { 0.0f, 0.0f, 0.0f };

        // Relative to the data root, or absolute
        std::string albedoTexture { };
        std::string normalTexture { };
        std::string metallicRoughnessTexture { }; // glTF packing: G = roughness, B = metallic
        std::string emissiveTexture { };
    };

    struct LoadResult
    {
        struct File
        {
            std::vector<MeshDesc> meshes { };
            std::vector<MaterialDesc> materials { };

            std::unordered_map<std::string, size_t> nameToMeshId { };
            std::unordered_map<std::string, size_t> nameToMaterialId { };

            struct Node
            {
                static constexpr std::size_t NO_INDEX { static_cast<std::size_t>(-1) };

                std::size_t parentNodeId { NO_INDEX };
                std::vector<size_t> childNodeIds { };

                glm::vec3 position { };
                glm::quat rotation { 1.0f, 0.0f, 0.0f, 0.0f };
                glm::vec3 scale { 1.0f, 1.0f, 1.0f };

                std::size_t meshId { NO_INDEX };
                std::size_t materialId { NO_INDEX };
            };

            std::vector<Node> nodes { };

            std::unordered_map<std::string, size_t> nameToNodeId;

            // Placeholder for a resource that failed to load, keeps file ids in resource list order
            bool loadFailed { false };
        };

        std::vector<File> files;
        std::unordered_map<fs::path, size_t> pathToFileId;
    };

    // Loads GPU resources (meshes, materials, textures etc.)
    class Loader
    {
        public:

        virtual ~Loader() = default;

        // Loads a file and appends exactly one File to a LoadResult
        // returns false and appends nothing if loading failed
        virtual bool LoadFile(LoadResult& outResult, const fs::path& file) = 0;
    };
}

#endif