
#pragma once

#include <exception>
#include <string>

#define RUNTIME_ASSERT(condition)                                                       \
    do {                                                                                \
        if (!(condition)) {                                                             \
            throw std::exception("RUNTIME_ASSERT failed: " #condition);                 \
        }                                                                               \
    } while (0)

#ifdef _DEBUG
#define DEBUG_ASSERT(condition)                                                         \
    do {                                                                                \
        if (!(condition)) {                                                             \
            throw std::exception("DEBUG_ASSERT failed: " #condition);                   \
        }                                                                               \
    } while (0)
#else
#define DEBUG_ASSERT(condition) \
    do { \
    } while (0)
#endif

