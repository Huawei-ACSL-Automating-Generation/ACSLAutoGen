#ifndef __ACSLG_SRC_MACROS_H__
#define __ACSLG_SRC_MACROS_H__


#include <sstream>
#include <iostream>
#include <cstdlib>
#include <atomic>

namespace acslg {

    namespace logging {
        enum class Level : int {
            Off  = -1,
            Error = 0,
            Warn  = 1,
            Info  = 2,
            Debug = 3,
        };

        inline std::atomic<int> g_level{static_cast<int>(Level::Info)};

        inline void setLevel(Level level) { g_level.store(static_cast<int>(level)); }
        inline Level getLevel() { return static_cast<Level>(g_level.load()); }
        inline bool enabled(Level level) {
            return static_cast<int>(level) <= g_level.load(std::memory_order_relaxed);
        }

        inline Level parseLevel(std::string_view s) {
            auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
            std::string tmp;
            tmp.reserve(s.size());
            for (auto ch : s)
                tmp.push_back(lower(static_cast<unsigned char>(ch)));

            if (tmp == "off" || tmp == "none" || tmp == "silent")
                return Level::Off;
            if (tmp == "error" || tmp == "err")
                return Level::Error;
            if (tmp == "warn" || tmp == "warning")
                return Level::Warn;
            if (tmp == "info")
                return Level::Info;
            if (tmp == "debug")
                return Level::Debug;
            return Level::Info;
        }
    } // namespace logging

#define ANSI_RESET "\033[0m"
#define ANSI_CUSTOM_BLUE "\033[38;2;120;220;232m"
#define ANSI_BRIGHT_YELLOW "\033[0;33m"
#define ANSI_BRIGHT_GREEN "\033[1;32m"
#define ANSI_BRIGHT_RED "\033[1;31m"

#define TODO()                                                                                     \
    do {                                                                                           \
        /* Get relative file path starting with "src/" */                                          \
        std::string file = __FILE__;                                                               \
        size_t pos       = file.rfind("src/");                                                     \
        if (pos != std::string::npos) {                                                            \
            file = file.substr(pos);                                                               \
        }                                                                                          \
                                                                                                   \
        std::cerr << ANSI_CUSTOM_BLUE << "[TODO " << ANSI_BRIGHT_YELLOW << file << ":" << __LINE__ \
                  << ANSI_CUSTOM_BLUE << "]" << ANSI_RESET << " "                                  \
                  << "Not Implemented Yet!" << ANSI_RESET << std::endl;                            \
        std::abort();                                                                              \
    } while (0)

#define UNIMPLEMENT(info)                                                                          \
    do {                                                                                           \
        std::string _file = __FILE__;                                                              \
        size_t pos        = _file.rfind("src/");                                                   \
        if (pos != std::string::npos) {                                                            \
            _file = _file.substr(pos);                                                             \
        }                                                                                          \
                                                                                                   \
        std::ostringstream oss;                                                                    \
        oss << info;                                                                               \
        std::string _msg = oss.str();                                                              \
                                                                                                   \
        std::cerr << ANSI_BRIGHT_RED << "[UNIMPLEMENT " << ANSI_BRIGHT_YELLOW << _file << ":"      \
                  << __LINE__ << ANSI_BRIGHT_RED << "]" << ANSI_RESET << " " << _msg << ANSI_RESET \
                  << std::endl;                                                                    \
        std::abort();                                                                              \
    } while (0)

#define UNREACHABLE()                                                                              \
    do {                                                                                           \
        std::string file = __FILE__;                                                               \
        size_t pos       = file.rfind("src/");                                                     \
        if (pos != std::string::npos) {                                                            \
            file = file.substr(pos);                                                               \
        }                                                                                          \
                                                                                                   \
        std::cerr << ANSI_BRIGHT_RED << "[UNREACHABLE " << ANSI_BRIGHT_YELLOW << file << ":"       \
                  << __LINE__ << ANSI_BRIGHT_RED << "]" << ANSI_RESET << std::endl;                \
        std::abort();                                                                              \
    } while (0)

#define PROCESS(info)                                                                              \
    do {                                                                                           \
        if (!::acslg::logging::enabled(::acslg::logging::Level::Info))                              \
            break;                                                                                 \
        std::ostringstream oss;                                                                    \
        oss << info;                                                                               \
        std::string s              = oss.str();                                                    \
        const int SEPARATOR_LENGTH = 80;                                                           \
        int pad = s.size() < SEPARATOR_LENGTH ? (SEPARATOR_LENGTH - s.size()) / 2 : 0;             \
        std::string stars(SEPARATOR_LENGTH, '=');                                                  \
        std::string padding(pad, ' ');                                                             \
        std::cout << "\n" << ANSI_BRIGHT_GREEN << stars << ANSI_RESET << std::endl;                \
        std::cout << ANSI_BRIGHT_GREEN << padding << "\033[1m" << s << ANSI_RESET << std::endl;    \
        std::cout << ANSI_BRIGHT_GREEN << stars << ANSI_RESET << std::endl;                        \
    } while (0)

#define INFO(info)                                                                                 \
    do {                                                                                           \
        if (!::acslg::logging::enabled(::acslg::logging::Level::Info))                              \
            break;                                                                                 \
        std::string file = __FILE__;                                                               \
        size_t pos       = file.rfind("src/");                                                     \
        if (pos != std::string::npos) {                                                            \
            file = file.substr(pos);                                                               \
        }                                                                                          \
        std::cout << ANSI_BRIGHT_GREEN << "[INFO " << ANSI_BRIGHT_YELLOW << file << ":"            \
                  << __LINE__ << ANSI_BRIGHT_GREEN << "]" << ANSI_RESET << " " << info             \
                  << std::endl;                                                                    \
    } while (0)

#define WARN(info)                                                                                 \
    do {                                                                                           \
        if (!::acslg::logging::enabled(::acslg::logging::Level::Warn))                              \
            break;                                                                                 \
        std::string file = __FILE__;                                                               \
        size_t pos       = file.rfind("src/");                                                     \
        if (pos != std::string::npos) {                                                            \
            file = file.substr(pos);                                                               \
        }                                                                                          \
        std::cout << ANSI_BRIGHT_YELLOW << "[WARN " << file << ":" << __LINE__ << "]"              \
                  << ANSI_RESET << " " << info << std::endl;                                       \
    } while (0)

#define ERROR(info)                                                                                \
    do {                                                                                           \
        std::string file = __FILE__;                                                               \
        size_t pos       = file.rfind("src/");                                                     \
        if (pos != std::string::npos) {                                                            \
            file = file.substr(pos);                                                               \
        }                                                                                          \
        std::cout << ANSI_BRIGHT_RED << "[ERROR " << ANSI_BRIGHT_YELLOW << file << ":" << __LINE__ \
                  << ANSI_BRIGHT_RED << "]" << ANSI_RESET << " " << info << std::endl;             \
        std::abort();                                                                              \
    } while (0)

#ifdef DEBUG_MODE
#define DEBUG(info)                                                                                \
    do {                                                                                           \
        if (!::acslg::logging::enabled(::acslg::logging::Level::Debug))                             \
            break;                                                                                 \
        std::string file = __FILE__;                                                               \
        size_t pos       = file.rfind("src/");                                                     \
        if (pos != std::string::npos) {                                                            \
            file = file.substr(pos);                                                               \
        }                                                                                          \
        std::cout << ANSI_BRIGHT_GREEN << "[DEBUG " << ANSI_BRIGHT_YELLOW << file << ":"           \
                  << __LINE__ << ANSI_BRIGHT_GREEN << "]" << ANSI_RESET << " " << info             \
                  << std::endl;                                                                    \
    } while (0)
#else
#define DEBUG(...)
#endif

    inline void _noWarn() { PROCESS("unreachable"); }
} // namespace acslg

#endif
