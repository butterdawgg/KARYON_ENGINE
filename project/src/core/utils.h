#ifndef UTILS_H
#define UTILS_H

#include <utility>
#include <iostream>
#include <format>
#include <source_location>
#include <concepts>
#include <functional>
#include <type_traits>

#include <filesystem>
namespace fs = std::filesystem;

namespace krn
{
    // use constepxr instead of macros because I hate them
    constexpr bool IS_DEBUG
    {
        #ifdef NDEBUG
        false
        #else
        true
        #endif // !NDEBUG
    };

    namespace ANSI_Colors
    {
        constexpr const char* BLUE = "\033[94m";
        constexpr const char* YELLOW = "\033[33m";
        constexpr const char* RED = "\033[31m";
        constexpr const char* RESET = "\033[0m";
    }

    // Has to be castable to bool
    template<typename T>
    concept BooleanTestable = requires(T&& value)
    {
        { static_cast<bool>(std::forward<T>(value)) } -> std::same_as<bool>;
    };

    // Either convertible to bool or invokable and returns bool
    template<typename T>
    concept ConditionType =
        BooleanTestable<T> ||
        (std::invocable<T> && BooleanTestable<std::invoke_result_t<T>>);

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

    namespace
    {
        // helper to log a formated message
        template<typename... Args>
        void log(const char* preMsg, const char* ansiColor,
            std::format_string<Args...> fmt, Args&&... args)
        {
            std::cerr
                << (ansiColor ? ansiColor : "")
                << (preMsg ? preMsg : "LOG: ")
                << std::format(fmt, std::forward<Args>(args)...)
                << (ansiColor ? ANSI_Colors::RESET : "")
                << "\n";
        }

        // helper to log source location and a formatted message
        template<typename... Args>
        void logWithLocation(const char* preMsg, const char* ansiColor,
            FormatWithLocation<std::type_identity_t<Args>...> fmt, Args&&... args)
        {
            std::cerr
                << (ansiColor ? ansiColor : "")
                << "--------------\n"
                << (preMsg ? preMsg : "LOG:") << "\n\n"
                << fmt.loc.file_name() << " (" << fmt.loc.line() << ");\n"
                << fmt.loc.function_name() << ":\n\n"
                << std::format(fmt.fmt, std::forward<Args>(args)...) << "\n"
                << "--------------\n\n"
                << (ansiColor ? ANSI_Colors::RESET : "");
        }
    }

    template<typename... Args>
    inline void logMsg([[maybe_unused]] std::format_string<Args...> fmt,
        [[maybe_unused]] Args&&... args)
    {
        if constexpr (IS_DEBUG)
        {
            log("MESSAGE: ", ANSI_Colors::BLUE, fmt, std::forward<Args>(args)...);
        }
    }

    template<typename... Args>
    inline void logWarning([[maybe_unused]] FormatWithLocation<std::type_identity_t<Args>...> fmt,
        [[maybe_unused]] Args&&... args)
    {
        if constexpr (IS_DEBUG)
        {
            logWithLocation("WARNING: ", ANSI_Colors::YELLOW, fmt, std::forward<Args>(args)...);
        }
    }

    template<typename... Args>
    inline void logError([[maybe_unused]] FormatWithLocation<std::type_identity_t<Args>...> fmt,
        [[maybe_unused]] Args&&... args)
    {
        if constexpr (IS_DEBUG)
        {
            logWithLocation("ERROR: ", ANSI_Colors::RED, fmt, std::forward<Args>(args)...);
        }
    }

    // Terminates the program (in debug and release), and prints a message (in debug)
    template<typename... Args>
    [[noreturn]] inline void fail([[maybe_unused]] FormatWithLocation<std::type_identity_t<Args>...> fmt,
        [[maybe_unused]] Args&&... args)
    {
        if constexpr (IS_DEBUG)
        {
            logWithLocation("CHECK FAILED: ", ANSI_Colors::YELLOW, fmt, std::forward<Args>(args)...);
        }

        std::terminate();
    }

    // Checks a condition and fails if condition if false
    template<ConditionType Condition, typename... Args>
    inline void check(
        Condition&& condition,
        [[maybe_unused]] FormatWithLocation<std::type_identity_t<Args>...> fmt,
        [[maybe_unused]] Args&&... args)
    {
        if constexpr (IS_DEBUG)
        {
            bool result;

            if constexpr (std::invocable<Condition>)
                result = std::invoke(std::forward<Condition>(condition));
            else
                result = static_cast<bool>(condition);

            if (!result)
                fail(fmt, std::forward<Args>(args)...);
        }
    }
}

#endif // !UTILS_H