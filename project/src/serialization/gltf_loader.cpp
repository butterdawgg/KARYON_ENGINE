#include "gltf_loader.h"

#include <cmath>
#include <system_error>

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>

#include "utils.h"



namespace krn
{
    // -----------------------
    // --- Private helpers ---
    // -----------------------

    namespace
    {
        // Derives per-vertex tangents (Lengyel's method)
        void ComputeTangents(std::vector<Vertex>& vertices, std::size_t baseVertex, std::size_t vertexCount,
            const std::vector<std::uint32_t>& indices, std::size_t baseIndex, std::size_t indexCount)
        {
            std::vector<glm::vec3> tan1(vertexCount, glm::vec3(0.0f));
            std::vector<glm::vec3> tan2(vertexCount, glm::vec3(0.0f));

            for (std::size_t i = 0; i + 2 < indexCount; i += 3)
            {
                const std::size_t i0 { indices[baseIndex + i]     - baseVertex };
                const std::size_t i1 { indices[baseIndex + i + 1] - baseVertex };
                const std::size_t i2 { indices[baseIndex + i + 2] - baseVertex };

                const Vertex& v0 { vertices[baseVertex + i0] };
                const Vertex& v1 { vertices[baseVertex + i1] };
                const Vertex& v2 { vertices[baseVertex + i2] };

                const glm::vec3 edge1 { v1.position - v0.position };
                const glm::vec3 edge2 { v2.position - v0.position };
                const glm::vec2 deltaUV1 { v1.uv - v0.uv };
                const glm::vec2 deltaUV2 { v2.uv - v0.uv };

                const float denom { deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y };

                if (std::abs(denom) < 1.0e-8f)
                    continue; // degenerate/absent UVs - this triangle contributes nothing, handled by the per-vertex fallback below

                const float r { 1.0f / denom };
                const glm::vec3 sdir { (edge1 * deltaUV2.y - edge2 * deltaUV1.y) * r };
                const glm::vec3 tdir { (edge2 * deltaUV1.x - edge1 * deltaUV2.x) * r };

                tan1[i0] += sdir; tan1[i1] += sdir; tan1[i2] += sdir;
                tan2[i0] += tdir; tan2[i1] += tdir; tan2[i2] += tdir;
            }

            for (std::size_t i = 0; i < vertexCount; ++i)
            {
                Vertex& v { vertices[baseVertex + i] };
                const glm::vec3& n { v.normal };

                glm::vec3 t { tan1[i] };

                if (glm::dot(t, t) < 1.0e-12f)
                {
                    // No UV-derived tangent survived (isolated vertex, fully
                    // degenerate UVs) - fall back to an arbitrary vector
                    // orthogonal to the normal so the TBN basis stays valid,
                    // even though it won't be UV-aligned
                    const glm::vec3 arbitrary { std::abs(n.z) < 0.999f
                        ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f) };

                    t = glm::normalize(glm::cross(n, arbitrary));
                }
                else
                {
                    // Gram-Schmidt orthogonalize against the normal
                    t = glm::normalize(t - n * glm::dot(n, t));
                }

                const float w { glm::dot(glm::cross(n, t), tan2[i]) < 0.0f ? -1.0f : 1.0f };

                v.tangent = glm::vec4(t, w);
            }
        }
    }



    // --------------------
    // --- GLTF loading ---
    // --------------------

    bool GLTF_Loader::LoadFile(LoadResult& outResult, const fs::path& file)
    {
        using GLTF_File = LoadResult::File;
        using GLTF_Node = GLTF_File::Node;

        static constexpr auto supportedExtensions =
            fastgltf::Extensions::KHR_mesh_quantization |
            fastgltf::Extensions::KHR_texture_transform |
            fastgltf::Extensions::KHR_materials_variants;

        constexpr auto gltfOptions =
            fastgltf::Options::DontRequireValidAssetMember |
            fastgltf::Options::AllowDouble |
            fastgltf::Options::LoadExternalBuffers |
            fastgltf::Options::GenerateMeshIndices |
            fastgltf::Options::DecomposeNodeMatrices;

        if (!fs::exists(file))
        {
            logError("Resource load failed: can't find '{}' .gltf/.glb file", file.string());

            return false;
        }

        fastgltf::Parser parser(supportedExtensions);

        auto gltfFile = fastgltf::MappedGltfFile::FromPath(file);
        if (!bool(gltfFile))
        {
            logError("Resource load failed: can't open gltf file: {}",
                fastgltf::getErrorMessage(gltfFile.error()));

            return false;
        }

        auto assetResult = parser.loadGltf(gltfFile.get(), file.parent_path(), gltfOptions);
        if (assetResult.error() != fastgltf::Error::None)
        {
            logError("Resource load failed: can't load gltf file: {}",
                fastgltf::getErrorMessage(assetResult.error()));

            return false;
        }

        fastgltf::Asset& asset { assetResult.get() };

        GLTF_File data { };

        auto resolveTexturePath = [&] (const auto& infoOpt) -> std::string
            {
                if (!infoOpt.has_value() || infoOpt->textureIndex >= asset.textures.size())
                    return { };

                const fastgltf::Texture& texture { asset.textures[infoOpt->textureIndex] };

                fastgltf::Optional<std::size_t> imageIndex { texture.imageIndex };
                if (!imageIndex.has_value()) imageIndex = texture.basisuImageIndex;
                if (!imageIndex.has_value()) imageIndex = texture.ddsImageIndex;
                if (!imageIndex.has_value()) imageIndex = texture.webpImageIndex;

                if (!imageIndex.has_value() || *imageIndex >= asset.images.size())
                    return { };

                const fastgltf::Image& image { asset.images[*imageIndex] };
                const auto* uriSource { std::get_if<fastgltf::sources::URI>(&image.data) };

                if (uriSource == nullptr || !uriSource->uri.isLocalPath())
                {
                    logWarning("glTF file '{}' references an embedded or non-local image - "
                        "only file-based textures are supported, skipping", file.string());

                    return { };
                }

                std::error_code ec;

                const fs::path absolute { fs::weakly_canonical(
                    file.parent_path() / uriSource->uri.fspath(), ec) };

                if (ec)
                    return { };

                return absolute.generic_string();
            };

        // --- Materials ---

        data.materials.reserve(asset.materials.size());

        for (std::size_t i = 0; i < asset.materials.size(); ++i)
        {
            const fastgltf::Material& mat { asset.materials[i] };

            MaterialDesc desc { };

            desc.albedo = glm::vec3(static_cast<float>(mat.pbrData.baseColorFactor[0]),
                static_cast<float>(mat.pbrData.baseColorFactor[1]),
                static_cast<float>(mat.pbrData.baseColorFactor[2]));
            desc.metallic = static_cast<float>(mat.pbrData.metallicFactor);
            desc.roughness = static_cast<float>(mat.pbrData.roughnessFactor);
            desc.emissive = glm::vec3(static_cast<float>(mat.emissiveFactor[0]),
                static_cast<float>(mat.emissiveFactor[1]),
                static_cast<float>(mat.emissiveFactor[2]));

            desc.albedoTexture = resolveTexturePath(mat.pbrData.baseColorTexture);
            desc.metallicRoughnessTexture = resolveTexturePath(mat.pbrData.metallicRoughnessTexture);
            desc.normalTexture = resolveTexturePath(mat.normalTexture);
            desc.emissiveTexture = resolveTexturePath(mat.emissiveTexture);

            if (!mat.name.empty())
                data.nameToMaterialId[std::string(mat.name.data(), mat.name.size())] = i;

            data.materials.push_back(std::move(desc));
        }

        // --- Meshes ---

        data.meshes.reserve(asset.meshes.size());

        for (std::size_t i = 0; i < asset.meshes.size(); ++i)
        {
            const fastgltf::Mesh& srcMesh { asset.meshes[i] };

            MeshDesc meshDesc { };

            for (const fastgltf::Primitive& primitive : srcMesh.primitives)
            {
                if (primitive.type != fastgltf::PrimitiveType::Triangles)
                {
                    logWarning("Mesh '{}' in '{}' has a non-triangle primitive, skipping it",
                        srcMesh.name, file.string());

                    continue;
                }

                const auto posIt { primitive.findAttribute("POSITION") };
                if (posIt == primitive.attributes.end())
                {
                    logWarning("Mesh '{}' in '{}' has a primitive with no POSITION attribute, skipping it",
                        srcMesh.name, file.string());

                    continue;
                }

                if (!primitive.indicesAccessor.has_value())
                {
                    logWarning("Mesh '{}' in '{}' has a primitive with no indices, skipping it",
                        srcMesh.name, file.string());

                    continue;
                }

                const fastgltf::Accessor& posAccessor { asset.accessors[posIt->accessorIndex] };

                const std::size_t baseVertex { meshDesc.vertices.size() };
                meshDesc.vertices.resize(baseVertex + posAccessor.count);

                fastgltf::iterateAccessorWithIndex<glm::vec3>(asset, posAccessor,
                    [&] (glm::vec3 v, std::size_t idx) { meshDesc.vertices[baseVertex + idx].position = v; });

                if (const auto normIt { primitive.findAttribute("NORMAL") };
                    normIt != primitive.attributes.end())
                {
                    fastgltf::iterateAccessorWithIndex<glm::vec3>(asset, asset.accessors[normIt->accessorIndex],
                        [&] (glm::vec3 v, std::size_t idx) { meshDesc.vertices[baseVertex + idx].normal = v; });
                }

                const auto tanIt { primitive.findAttribute("TANGENT") };
                const bool hasTangents { tanIt != primitive.attributes.end() };

                if (hasTangents)
                {
                    fastgltf::iterateAccessorWithIndex<glm::vec4>(asset, asset.accessors[tanIt->accessorIndex],
                        [&] (glm::vec4 v, std::size_t idx) { meshDesc.vertices[baseVertex + idx].tangent = v; });
                }

                if (const auto uvIt { primitive.findAttribute("TEXCOORD_0") };
                    uvIt != primitive.attributes.end())
                {
                    fastgltf::iterateAccessorWithIndex<glm::vec2>(asset, asset.accessors[uvIt->accessorIndex],
                        [&] (glm::vec2 v, std::size_t idx) { meshDesc.vertices[baseVertex + idx].uv = v; });
                }

                const fastgltf::Accessor& idxAccessor { asset.accessors[*primitive.indicesAccessor] };

                const std::size_t baseIndex { meshDesc.indices.size() };
                meshDesc.indices.resize(baseIndex + idxAccessor.count);

                fastgltf::iterateAccessorWithIndex<std::uint32_t>(asset, idxAccessor,
                    [&] (std::uint32_t vertIdx, std::size_t idx)
                    {
                        meshDesc.indices[baseIndex + idx] = static_cast<std::uint32_t>(baseVertex) + vertIdx;
                    });

                if (!hasTangents)
                {
                    ComputeTangents(meshDesc.vertices, baseVertex, posAccessor.count,
                        meshDesc.indices, baseIndex, idxAccessor.count);
                }

                meshDesc.primitives.push_back(idxAccessor.count);
                meshDesc.materials.push_back(primitive.materialIndex
                    ? static_cast<std::uint32_t>(*primitive.materialIndex) : MeshDesc::NO_MATERIAL);
            }

            if (!srcMesh.name.empty())
                data.nameToMeshId[std::string(srcMesh.name.data(), srcMesh.name.size())] = i;

            data.meshes.push_back(std::move(meshDesc));
        }

        // --- Nodes ---

        data.nodes.resize(asset.nodes.size());

        for (std::size_t i = 0; i < asset.nodes.size(); ++i)
        {
            const fastgltf::Node& srcNode { asset.nodes[i] };
            GLTF_Node& node { data.nodes[i] };

            node.childNodeIds.assign(srcNode.children.begin(), srcNode.children.end());

            if (const auto* trs { std::get_if<fastgltf::TRS>(&srcNode.transform) })
            {
                node.position = glm::vec3(static_cast<float>(trs->translation[0]),
                    static_cast<float>(trs->translation[1]), static_cast<float>(trs->translation[2]));
                node.rotation = glm::quat(static_cast<float>(trs->rotation[3]),
                    static_cast<float>(trs->rotation[0]), static_cast<float>(trs->rotation[1]),
                    static_cast<float>(trs->rotation[2]));
                node.scale = glm::vec3(static_cast<float>(trs->scale[0]),
                    static_cast<float>(trs->scale[1]), static_cast<float>(trs->scale[2]));
            }

            node.meshId = srcNode.meshIndex.has_value() ? *srcNode.meshIndex : GLTF_Node::NO_INDEX;

            if (!srcNode.name.empty())
                data.nameToNodeId[std::string(srcNode.name.data(), srcNode.name.size())] = i;
        }

        for (std::size_t i = 0; i < data.nodes.size(); ++i)
        {
            for (std::size_t child : data.nodes[i].childNodeIds)
            {
                if (child < data.nodes.size())
                    data.nodes[child].parentNodeId = i;
            }
        }

        outResult.pathToFileId.try_emplace(file, outResult.files.size());
        outResult.files.push_back(std::move(data));

        return true;
    }
}
