#ifndef MODEL_H
#define MODEL_H

#include "rendering_datatypes.h"
#include "serialization.h"

namespace krn
{
    struct ModelDesc : public Registrar<ModelDesc>
    {
        static void Register()
        {
            SerializeManager::RegisterComponent<ModelDesc>("model")
                .Parameter("gltf_file_name", &ModelDesc::gltfFileName)
                .Parameter("gltf_mesh_name", &ModelDesc::gtlfMeshName)
                .Parameter("gltf_material_name", &ModelDesc::gtlfMaterialName)
                .Parameter("material_desc", &ModelDesc::materialDesc);
        }

        std::string gltfFileName { };
        std::string gtlfMeshName { };
        std::string gtlfMaterialName { };

        std::vector<MaterialDesc> materialDesc { };
    };

    // Materials are separated by '|', fields within one material by ';'
    // (e.g. "albedo: 1 0 0; roughness: 0.5 | albedo: 0 1 0; metallic: 1")
    template<>
    struct ParseValue<std::vector<MaterialDesc>>
    {
        static std::vector<MaterialDesc> Parse(const std::string& value)
        {
            auto trim = [] (std::string_view str) -> std::string_view
                {
                    constexpr char whitespace[] = " \t\r\n";

                    const auto begin = str.find_first_not_of(whitespace);
                    if (begin == std::string_view::npos)
                        return {};

                    const auto end = str.find_last_not_of(whitespace);
                    return str.substr(begin, end - begin + 1);
                };

            auto parseVec3 = [] (std::string_view str) -> std::optional<glm::vec3>
                {
                    std::stringstream ss { std::string(str) };
                    glm::vec3 v;

                    if (ss >> v.x >> v.y >> v.z)
                        return v;

                    return std::nullopt;
                };

            auto parseFloat = [] (std::string_view str) -> std::optional<float>
                {
                    std::stringstream ss { std::string(str) };
                    float v;

                    if (ss >> v)
                        return v;

                    return std::nullopt;
                };

            auto parseOne = [&] (std::string_view chunk) -> std::optional<MaterialDesc>
                {
                    MaterialDesc desc {};

                    const std::unordered_map<std::string_view, std::function<bool(std::string_view)>> parsers =
                    {
                        { "albedo", [&] (std::string_view s)
                            {
                                auto v = parseVec3(s);
                                if (!v) return false;
                                desc.albedo = *v;
                                return true;
                            }
                        },
                        { "metallic", [&] (std::string_view s)
                            {
                                auto v = parseFloat(s);
                                if (!v) return false;
                                desc.metallic = *v;
                                return true;
                            }
                        },
                        { "roughness", [&] (std::string_view s)
                            {
                                auto v = parseFloat(s);
                                if (!v) return false;
                                desc.roughness = *v;
                                return true;
                            }
                        },
                        { "emissive", [&] (std::string_view s)
                            {
                                auto v = parseVec3(s);
                                if (!v) return false;
                                desc.emissive = *v;
                                return true;
                            }
                        },
                        { "albedoTexture", [&] (std::string_view s)
                            {
                                desc.albedoTexture = trim(s);
                                return true;
                            }
                        },
                        { "normalTexture", [&] (std::string_view s)
                            {
                                desc.normalTexture = trim(s);
                                return true;
                            }
                        },
                        { "metallicRoughnessTexture", [&] (std::string_view s)
                            {
                                desc.metallicRoughnessTexture = trim(s);
                                return true;
                            }
                        },
                        { "emissiveTexture", [&] (std::string_view s)
                            {
                                desc.emissiveTexture = trim(s);
                                return true;
                            }
                        }
                    };

                    std::string_view input = chunk;

                    while (!input.empty())
                    {
                        const auto semicolon = input.find(';');
                        const auto line = trim(input.substr(0, semicolon));

                        if (!line.empty())
                        {
                            const auto colon = line.find(':');
                            if (colon == std::string_view::npos)
                                return std::nullopt;

                            const auto key = trim(line.substr(0, colon));
                            const auto val = trim(line.substr(colon + 1));

                            const auto it = parsers.find(key);
                            if (it == parsers.end() || !it->second(val))
                                return std::nullopt;
                        }

                        if (semicolon == std::string_view::npos)
                            break;

                        input.remove_prefix(semicolon + 1);
                    }

                    return desc;
                };

            std::vector<MaterialDesc> result;
            std::string_view input = value;

            while (!input.empty())
            {
                const auto pipe = input.find('|');
                const auto chunk = trim(input.substr(0, pipe));

                if (!chunk.empty())
                {
                    auto desc = parseOne(chunk);
                    if (!desc)
                        return { };

                    result.push_back(std::move(*desc));
                }

                if (pipe == std::string_view::npos)
                    break;

                input.remove_prefix(pipe + 1);
            }

            return result;
        }
    };

    struct Model
    {
        MeshHandle meshHandle { INVALID_MESH };
        bool forceRenderingOff { false };
    };
}

#endif // !MODEL_H