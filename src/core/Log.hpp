#pragma once

#include <cstdio>

#define RAIDEN_LOG(...)                                 \
    do {                                                \
        std::fprintf(stdout, "[raiden] " __VA_ARGS__);  \
        std::fprintf(stdout, "\n");                     \
        std::fflush(stdout);                            \
    } while (0)

#define RAIDEN_ERR(...)                                 \
    do {                                                \
        std::fprintf(stderr, "[raiden] " __VA_ARGS__);  \
        std::fprintf(stderr, "\n");                     \
        std::fflush(stderr);                            \
    } while (0)
