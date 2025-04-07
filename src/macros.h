#include <sstream>
#include <iostream>

#define TODO()                                                                                     \
    do                                                                                             \
    {                                                                                              \
        std::cerr << "TODO: Not implemented yet!" << std::endl;                                    \
        std::abort();                                                                              \
    } while(0)

#define PROCESS(info)                                                                              \
    do                                                                                             \
    {                                                                                              \
        std::ostringstream oss;                                                                    \
        oss << info;                                                                               \
        std::string s = oss.str();                                                                 \
        const int SEPARATOR_LENGTH = 80;                                                           \
        int pad = s.size() < SEPARATOR_LENGTH ? (SEPARATOR_LENGTH - s.size()) / 2 : 0;             \
        std::string stars(SEPARATOR_LENGTH, '=');                                                  \
        std::string padding(pad, ' ');                                                             \
        std::cout << "\n\033[1;32m" << stars << "\033[0m" << std::endl;                            \
        std::cout << "\033[1;32m" << padding << "\033[1m" << s << "\033[0m" << std::endl;          \
        std::cout << "\033[1;32m" << stars << "\033[0m\n\n" << std::endl;                          \
    } while(0)

#define INFO(info)                                                                                 \
    do                                                                                             \
    {                                                                                              \
        std::string file = __FILE__;                                                               \
        size_t pos = file.rfind("src/");                                                           \
        if(pos != std::string::npos)                                                               \
        {                                                                                          \
            file = file.substr(pos);                                                               \
        }                                                                                          \
        std::cout << "\033[1;32m[INFO "                                                            \
                  << "\033[0;33m" << file << ":" << __LINE__ << "\033[1;32m]" << "\033[0m "        \
                  << info << std::endl;                                                            \
    } while(0)

inline void _noWarn() { PROCESS("unreachable"); }