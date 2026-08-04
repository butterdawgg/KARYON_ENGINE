#include <tinyxml2.h>

#include <unordered_map>

#include "xml_parser.h"
#include "parser.h"



namespace krn
{
    std::optional<SceneDesc> XML_Parser::ParseScene(fs::path path)
    {
        // Open XML document
        tinyxml2::XMLDocument doc;

        if (doc.LoadFile(path.string().c_str()) != tinyxml2::XML_SUCCESS)
        {
            logError(".xml parse error in file '{}': {}", path.string(), doc.ErrorStr());

            return std::nullopt;
        }



        // Get <scene> element
        tinyxml2::XMLElement* sceneElement { doc.FirstChildElement("scene") };

        if (!sceneElement)
        {
            logError("Missing <scene> root element in scene file '{}'", path.string());

            return std::nullopt;
        }



        // Get <resources> element
        std::vector<fs::path> resourcesToLoad { };

        if (auto* resElem { sceneElement->FirstChildElement("resources") }; resElem != nullptr)
        {
            if (resElem->GetText() != nullptr)
            {
                std::istringstream iss { resElem->GetText() };

                while (!iss.eof())
                {
                    std::string value;

                    if (!(iss >> value))
                        break;

                    fs::path resPath { value };

                    if (fs::exists(resPath))
                        resourcesToLoad.push_back(resPath);
                }
            }
        }



        // Component parsing helper lambda
        auto parseComponent =
            [path] (tinyxml2::XMLElement* componentElement) -> std::optional<ComponentDesc>
            {
                ComponentDesc desc { };

                desc.name = componentElement->Name();

                std::unordered_map<std::string, std::string> params;

                for (tinyxml2::XMLElement* parameterElem { componentElement->FirstChildElement() };
                    parameterElem != nullptr; parameterElem = parameterElem->NextSiblingElement())
                {
                    const auto paramName { parameterElem->Name() };
                    const auto paramValue { parameterElem->GetText() };

                    if (paramValue != nullptr && params.find(paramName) == params.end())
                        params[paramName] = paramValue;
                }

                desc.rawFields.reserve(params.size());

                for (auto& [name, value] : params)
                {
                    desc.rawFields.push_back({ name, value });
                }

                return desc;
            };



        // Load metadata parsing helper lambda
        auto parseLoadMetatdata = [path] (tinyxml2::XMLElement* entityElement) -> std::optional<LoadMetadata>
            {
                LoadMetadata metadata { };
                bool has { false };

                if (auto* idAttr { entityElement->FindAttribute("id") })
                {
                    int value { };

                    auto err = idAttr->QueryIntValue(&value);

                    if (err == tinyxml2::XML_SUCCESS)
                    {
                        if (value >= 0)
                        {
                            metadata.id = static_cast<uint32_t>(value);
                        }
                    }

                    has = true;
                }

                if (auto* parentAttr { entityElement->FindAttribute("parent") })
                {
                    int value { };

                    auto err = parentAttr->QueryIntValue(&value);

                    if (err == tinyxml2::XML_SUCCESS)
                    {
                        if (value >= 0)
                        {
                            metadata.parent = static_cast<uint32_t>(value);
                        }
                    }

                    has = true;
                }

                if (auto* gltfNodeNameAttr { entityElement->FindAttribute("gltf_node_name") })
                {
                    metadata.gltfNode.name = gltfNodeNameAttr->Value();

                    has = true;
                }

                if (auto* gltfLoadHierarchyAttr { entityElement->FindAttribute("gltf_load_children") })
                {
                    gltfLoadHierarchyAttr->QueryBoolValue(&metadata.gltfNode.loadChildren);

                    has = true;
                }

                if (auto* gltfExcludeChildNodesAttr {
                    entityElement->FindAttribute("gltf_exclude_child_nodes") })
                {
                    if (gltfExcludeChildNodesAttr->Value() != nullptr)
                    {
                        std::istringstream iss { gltfExcludeChildNodesAttr->Value() };

                        while (!iss.eof())
                        {
                            std::string value { };

                            if (!(iss >> value))
                                break;

                            metadata.gltfNode.excludeChildNodes.push_back(value);
                            has = true;
                        }
                    }
                }

                return has ? std::optional<LoadMetadata>(metadata) : std::nullopt;
            };



        // Entity parsing helper lambda
        auto parseEntity =
            [path, parseLoadMetatdata, parseComponent] (tinyxml2::XMLElement* entityElement) -> std::optional<EntityDesc>
            {
                EntityDesc desc { };

                desc.metadata = parseLoadMetatdata(entityElement);

                if (tinyxml2::XMLElement* componentsElem { entityElement->FirstChildElement("components") };
                    componentsElem != nullptr)
                {
                    for (tinyxml2::XMLElement* componentElem { componentsElem->FirstChildElement() };
                        componentElem != nullptr; componentElem = componentElem->NextSiblingElement())
                    {
                        auto componentDesc { parseComponent(componentElem) };

                        if (componentDesc.has_value())
                            desc.components.push_back(componentDesc.value());
                    }
                }

                return desc;
            };



        // Get <entities> element
        tinyxml2::XMLElement* entitiesElement { sceneElement->FirstChildElement("entities") };

        std::vector<EntityDesc> entities { };

        if (entitiesElement)
        {
            // If <entities> element present, loop through all <entity> elements
            for (tinyxml2::XMLElement* entityElement { entitiesElement->FirstChildElement("entity") };
                entityElement != nullptr; entityElement = entityElement->NextSiblingElement("entity"))
            {
                auto entityDesc { parseEntity(entityElement) };

                if (entityDesc.has_value())
                    entities.push_back(entityDesc.value());
            }
        }
        else
        {
            logWarning("Missing <entities> element inside <scene> element in file '{}'", path.string());
        }

        return SceneDesc { std::move(entities), std::move(resourcesToLoad) };
    }
}