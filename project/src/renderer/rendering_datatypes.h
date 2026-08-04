#ifndef RENDERING_DATATYPES_H
#define RENDERING_DATATYPES_H

namespace krn
{
    using MeshHandle = std::uint32_t;
    using MaterialHandle = std::uint32_t;

    inline constexpr MeshHandle INVALID_MESH { static_cast<MeshHandle>(-1) };
    inline constexpr MaterialHandle INVALID_MATERIAL { static_cast<MaterialHandle>(-1) };

    // CPU-side data
    struct Vertex
    {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec4 tangent; // xyz = tangent, w = bitangent sign (+1/-1)
        glm::vec2 uv;
    };

    struct MeshDesc
    {
        std::vector<Vertex> vertices;
        std::vector<std::uint32_t> indices;
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