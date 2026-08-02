
#pragma once

#include <exception>
#include <stdexcept>
#include <string>

#define RUNTIME_ASSERT(condition)                                                       \
    do {                                                                                \
        if (!(condition)) {                                                             \
            throw std::runtime_error("RUNTIME_ASSERT failed: " #condition);                 \
        }                                                                               \
    } while (0)

#ifdef _DEBUG
#define DEBUG_ASSERT(condition)                                                         \
    do {                                                                                \
        if (!(condition)) {                                                             \
            throw std::runtime_error("DEBUG_ASSERT failed: " #condition);                   \
        }                                                                               \
    } while (0)
#else
#define DEBUG_ASSERT(condition) \
    do { \
    } while (0)
#endif

