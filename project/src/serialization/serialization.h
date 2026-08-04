#ifndef SERIALIZATION_H
#define SERIALIZATION_H

#include <cassert>
#include <charconv>
#include <concepts>
#include <memory>
#include <string>
#include <unordered_map>

#include <glm/glm.hpp>

#include "registry.h"

namespace krn
{
    // ------------------------------
    // --- Component registration ---
    // ------------------------------

    template<typename T>
    concept Registrable =
        std::is_default_constructible_v<T> &&
        requires { { T::Register() } -> std::same_as<void>; };

    template<typename T>
    class Registrar
    {
        public:

        // Forces the compiler to actually instantiate/keep `reg` around instead of
        // discarding it as an unused static — call this once from somewhere that's
        // guaranteed to be linked in (e.g. from Component's own constructor, or
        // from an explicit registration pass). See note below.
        //static void Touch() { (void)reg; }

        private:

        static inline const struct Reg
        {
            Reg() { T::Register(); }
        } reg { };
    };

    class BaseComponentFactory
    {
        public:

        virtual ~BaseComponentFactory() = default;

        virtual ErasedPtr operator()(Registry& reg, Entity e) = 0;
    };

    template<typename T>
    class ComponentFactory final : public BaseComponentFactory
    {
        public:

        ErasedPtr operator()(Registry& reg, Entity e) override
        {
            T& component = reg.Add<T>(e);
            return ErasedPtr { &component, getTypeId<T>() };
        }
    };



    // -------------------------
    // --- Parameter parsing ---
    // -------------------------

    template<typename T>
    struct ParseValue;

    template<>
    struct ParseValue<int>
    {
        static int Parse(const std::string& value)
        {
            int result {};
            std::from_chars(value.data(), value.data() + value.size(), result);
            return result;
        }
    };

    template<>
    struct ParseValue<float>
    {
        static float Parse(const std::string& value) { return std::stof(value); }
    };

    template<>
    struct ParseValue<double>
    {
        static double Parse(const std::string& value) { return std::stod(value); }
    };

    template<>
    struct ParseValue<bool>
    {
        static bool Parse(const std::string& value) { return value == "true" || value == "1"; }
    };

    template<>
    struct ParseValue<std::string>
    {
        static std::string Parse(const std::string& value) { return value; }
    };

    // Parses "x,y,z" (e.g. a colour or position triple in scene markup)
    template<>
    struct ParseValue<glm::vec3>
    {
        static glm::vec3 Parse(const std::string& value)
        {
            glm::vec3 result { 0.0f, 0.0f, 0.0f };
            std::size_t start = 0;

            for (int i = 0; i < 3; ++i)
            {
                std::size_t comma = value.find(',', start);
                result[i] = std::stof(value.substr(start, comma - start));

                if (comma == std::string::npos)
                    break;

                start = comma + 1;
            }

            return result;
        }
    };



    // ------------------------------
    // --- Parameter registration ---
    // ------------------------------

    class BaseParameter
    {
        public:

        virtual ~BaseParameter() = default;

        virtual void Set(ErasedPtr component, const std::string& value) const = 0;
    };

    template<typename Component, typename FieldType>
    class Parameter final : public BaseParameter
    {
        public:

        using MemberPtr = FieldType Component::*;

        explicit Parameter(MemberPtr member) : m_Member { member } { }

        void Set(ErasedPtr component, const std::string& value) const override
        {
            assert(component.type == getTypeId<Component>() &&
                "ErasedPtr type mismatch: SerializeManager handed the wrong component to this Parameter");

            Component* typed = static_cast<Component*>(component.ptr);

            typed->*m_Member = ParseValue<FieldType>::Parse(value);
        }

        private:

        MemberPtr m_Member;
    };



    // -----------------------------------
    // --- Registration builder helper ---
    // -----------------------------------

    template<typename Component>
    class ComponentRegistration
    {
        public:

        ComponentRegistration(std::unordered_map<std::string,
            std::unique_ptr<BaseParameter>>& fields) : m_Fields { fields }
        { }

        template<typename FieldType>
        ComponentRegistration& Parameter(const std::string& name, FieldType Component::* member)
        {
            assert(m_Fields.find(name) == m_Fields.end() &&
                "parameter with this name is already registered for this component");

            m_Fields[name] = std::make_unique<krn::Parameter<Component, FieldType>>(member);

            return *this;
        }

        private:

        std::unordered_map<std::string, std::unique_ptr<BaseParameter>>& m_Fields;
    };



    // ------------------------------
    // --- Serialization registry ---
    // ------------------------------

    class SerializeManager
    {
        public:

        template<Registrable Component>
        static ComponentRegistration<Component> RegisterComponent(const std::string& name)
        {
            assert(m_ComponentTypes.find(name) == m_ComponentTypes.end() &&
                "component with this name is already registered");

            const TypeId typeId { getTypeId<Component>() };

            m_ComponentTypes[name] = typeId;
            m_ComponentFactories[typeId] = std::make_unique<ComponentFactory<Component>>();

            return ComponentRegistration<Component>{ m_ComponentFields[name] };
        }

        static void ApplyBlock(
            Registry& reg,
            Entity e,
            const std::string& componentName,
            const std::vector<std::pair<std::string, std::string>>& rawFields)
        {
            auto typeIt = m_ComponentTypes.find(componentName);
            assert(typeIt != m_ComponentTypes.end() && "unknown component name in markup");

            BaseComponentFactory& factory = *m_ComponentFactories.at(typeIt->second);
            ErasedPtr component = factory(reg, e);
            assert(component.ptr != nullptr);

            auto fieldsIt = m_ComponentFields.find(componentName);
            if (fieldsIt == m_ComponentFields.end())
                return; // component has no registered parameters, allowed

            for (const auto& [fieldName, rawValue] : rawFields)
            {
                auto paramIt = fieldsIt->second.find(fieldName);
                assert(paramIt != fieldsIt->second.end() &&
                    "unknown parameter name in markup for this component");

                paramIt->second->Set(component, rawValue);
            }
        }

        private:

        static inline std::unordered_map<std::string, TypeId> m_ComponentTypes;
        static inline std::unordered_map<TypeId, std::unique_ptr<BaseComponentFactory>> m_ComponentFactories;

        static inline std::unordered_map<std::string, std::unordered_map<std::string, std::unique_ptr<BaseParameter>>>
            m_ComponentFields;
    };
}

#endif