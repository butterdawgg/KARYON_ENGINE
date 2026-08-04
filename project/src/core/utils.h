#ifndef UTILS_H
#define UTILS_H

#include <utility>
#include <iostream>
#include <format>

#include <filesystem>
namespace fs = std::filesystem;

namespace krn
{
    // -------------------
    // --- Debug utils ---
    // -------------------

    namespace ANSI_Colors
    {
        constexpr const char* MAYBE_RED = "\033[33m";
        constexpr const char* JUST_RED = "\033[31m";
        constexpr const char* VERY_RED = "\033[30;101m";
        constexpr const char* RESET = "\033[0m";
    }

    template<typename... Args>
    inline void logMsg([[maybe_unused]] std::format_string<Args...> fmt,
        [[maybe_unused]] Args&&... args)
    {
        #ifdef _DEBUG

        std::cerr << "LOG: "
            << std::format(fmt, std::forward<Args>(args)...)
            << '\n';

        #endif // _DEBUG
    }

    template<typename... Args>
    inline void logWarning([[maybe_unused]] std::format_string<Args...> fmt,
        [[maybe_unused]] Args&&... args)
    {
        #ifdef _DEBUG

        std::cerr << ANSI_Colors::MAYBE_RED
            << "WARNING: "
            << std::format(fmt, std::forward<Args>(args)...)
            << ANSI_Colors::RESET
            << '\n';

        #endif // _DEBUG
    }

    template<typename... Args>
    inline void logError([[maybe_unused]] std::format_string<Args...> fmt,
        [[maybe_unused]] Args&&... args)
    {
        #ifdef _DEBUG

        std::cerr << ANSI_Colors::JUST_RED
            << "ERROR: "
            << std::format(fmt, std::forward<Args>(args)...)
            << ANSI_Colors::RESET
            << '\n';

        #endif // _DEBUG
    }

    template<typename... Args>
    inline void logCaughtException([[maybe_unused]] std::format_string<Args...> fmt,
        [[maybe_unused]] Args&&... args)
    {
        #ifdef _DEBUG

        std::cerr << ANSI_Colors::VERY_RED
            << "EXCEPTION CAUGHT: "
            << std::format(fmt, std::forward<Args>(args)...)
            << ANSI_Colors::RESET
            << '\n';

        #endif // _DEBUG
    }
}

#endif // !UTILS_H