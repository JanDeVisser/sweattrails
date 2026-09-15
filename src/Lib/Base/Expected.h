
#include <expected>

#include <Error.h>

#pragma once

#define TRY_EVAL(...)                                \
    ({                                               \
        auto _result = (__VA_ARGS__);                \
        if (!_result.has_value()) {                  \
            return std::unexpected(_result.error()); \
        }                                            \
        _result.value();                             \
    })

#define MUST_EVAL(...)                                                              \
    ({                                                                              \
        auto _result = (__VA_ARGS__);                                               \
        if (!_result.has_value()) {                                                 \
            fatal("MUST_EVAL {:}: {:}", #__VA_ARGS__, _result.error().to_string()); \
        }                                                                           \
        _result.value();                                                            \
    })

#define TRY(...)                                     \
    do {                                             \
        auto _result = (__VA_ARGS__);                \
        if (!_result) {                              \
            return std::unexpected(_result.error()); \
        }                                            \
    } while (0)

#define MUST(...)                                                              \
    do {                                                                       \
        auto _result = (__VA_ARGS__);                                          \
        if (!_result) {                                                        \
            fatal("MUST {:}: {:}", #__VA_ARGS__, _result.error().to_string()); \
        }                                                                      \
    } while (0)

#define IGNORE(...)                   \
    do {                              \
        auto _result = (__VA_ARGS__); \
    } while (0)

namespace ST {

using CError = std::expected<void, LibCError>;

template<typename E = LibCError>
using Error = std::expected<void, E>;

}
