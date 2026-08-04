#ifndef RENDERING_DATATYPES_H
#define RENDERING_DATATYPES_H

namespace krn
{
    using MeshHandle = std::uint32_t;
    inline constexpr MeshHandle INVALID_MESH { static_cast<MeshHandle>(-1) };

    struct Vertex
    {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec4 tangent; // xyz = tangent, w = bitangent sign (+1/-1)
        glm::vec2 uv;
    };

    // CPU-side data
    struct MeshDesc
    {
        std::vector<Vertex> vertices;
        std::vector<std::uint32_t> indices;
        std::vector<size_t> primitives { };
        std::vector<size_t> materials { };
    };

    struct MaterialDesc
    {
        glm::vec3 albedo { 1.0f, 1.0f, 1.0f };
        float metallic { 0.0f };
        float roughness { 1.0f };
        glm::vec3 emissive { 0.0f, 0.0f, 0.0f };

        std::string albedoTexture { };
        std::string normalTexture { };
        std::string metallicRoughnessTexture { }; // glTF packing: G = roughness, B = metallic
        std::string emissiveTexture { };
    };
}

#endif // !RENDERING_DATATYPES_H