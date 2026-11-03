#ifndef UTILS_H
#define UTILS_H

#include <utility>
#include <iostream>
#include <format>
#include <source_location>

#include <filesystem>
namespace fs = std::filesystem;

namespace krn
{
    // -------------------
    // --- Debug utils ---
    // -------------------

    namespace ANSI_Colors
    {
        constexpr const char* YELLOW = "\033[33m";
        constexpr const char* RED = "\033[31m";
        constexpr const char* RESET = "\033[0m";
    }

    // Format string with a source location
    // Safe to be implicitly constructed from a string
    template<typename... Args>
    struct FormatWithLocation
    {
        std::format_string<Args...> fmt;
        std::source_location loc;

        template<typename T>
            requires std::convertible_to<const T&, std::string_view>
        consteval FormatWithLocation(const T& str,
            std::source_location loc = std::source_location::current())
            : fmt(str), loc(loc)
        { }
    };

    // Example: logMsg("Hello {}", world)
    template<typename... Args>
    inline void logMsg([[maybe_unused]] FormatWithLocation<std::type_identity_t<Args>...> fmt,
        [[maybe_unused]] Args&&... args)
    {
        #ifdef _DEBUG

        std::cerr
            << "\nMESSAGE: "
            << std::format(fmt.fmt, std::forward<Args>(args)...)
            << "\n\n";

        #endif // _DEBUG
    }

    template<typename... Args>
    inline void logWarning([[maybe_unused]] FormatWithLocation<std::type_identity_t<Args>...> fmt,
        [[maybe_unused]] Args&&... args)
    {
        #ifdef _DEBUG

        std::cerr
            << ANSI_Colors::YELLOW
            << "--------------\n"
            << "WARNING:\n\n"
            << fmt.loc.file_name() << " (" << fmt.loc.line() << ");\n"
            << fmt.loc.function_name() << ":\n\n"
            << std::format(fmt.fmt, std::forward<Args>(args)...) << "\n"
            << "--------------\n\n"
            << ANSI_Colors::RESET;

        #endif // _DEBUG
    }

    template<typename... Args>
    inline void logError([[maybe_unused]] FormatWithLocation<std::type_identity_t<Args>...> fmt,
        [[maybe_unused]] Args&&... args)
    {
        #ifdef _DEBUG

        std::cerr
            << ANSI_Colors::RED
            << "--------------\n"
            << "ERROR:\n\n"
            << fmt.loc.file_name() << " (" << fmt.loc.line() << ");\n"
            << fmt.loc.function_name() << ":\n\n"
            << std::format(fmt.fmt, std::forward<Args>(args)...) << "\n"
            << "--------------\n\n"
            << ANSI_Colors::RESET;

        #endif // _DEBUG
    }
}

#endif // !UTILS_H