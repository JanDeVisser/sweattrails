/*
 * Copyright (c) 2024, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <functional>

#include <Error.h>
#include <Logging.h>

namespace ST {

template<typename ResultType, typename ErrorType = LibCError>
class [[nodiscard]] Result {
public:
    Result(ResultType const &return_value)
        : m_value(return_value)
    {
    }

    //    Result(ResultType &&return_value)
    //        : m_value(std::move(return_value))
    //    {
    //    }

    template<typename U>
    Result(U &&value)
        requires(!std::is_same_v<std::remove_cvref_t<U>, ResultType> && !std::is_same_v<std::remove_cvref_t<U>, Result<ResultType, ErrorType>> && !std::is_same_v<std::remove_cvref_t<U>, ErrorType>)
        : m_value(std::forward<U>(value))
    {
    }

    Result(ErrorType const &error)
        : m_error(error)
    {
    }

    Result(ErrorType &&error)
        : m_error(std::move(error))
    {
    }

    Result(Result &&) noexcept = default;
    Result(Result const &) = default;
    ~Result() = default;

    Result &operator=(Result &&) noexcept = default;
    Result &operator=(Result const &) = default;

    [[nodiscard]] bool has_value() const { return m_value.has_value(); }
    ResultType const  &value() const { return m_value.value(); }
    [[nodiscard]] bool is_error() const { return m_error.has_value(); }
    ErrorType const   &error() const { return m_error.value(); }

    ResultType const           &operator*() const noexcept { return value(); }
    ResultType                 &operator*() noexcept { return value(); }
    constexpr ResultType const *operator->() const noexcept { return &m_value.value(); }
    constexpr ResultType       *operator->() noexcept { return &m_value.value(); }

    template<typename NewError>
    Result<ResultType, NewError> adapt(std::function<NewError(ErrorType const &)> const &adapter)
    {
        if (m_error.has_value()) {
            return Result(std::move(adapter(m_error.value())));
        }
        return Result(std::move(m_value.value()));
    }

    template<typename Catch>
    ResultType &&must(Catch const &catch_)
    {
        if (m_error.has_value()) {
            catch_(m_error.value());
            abort();
        }
        return std::move(m_value.value());
    }

    ResultType &&must()
    {
        if (m_error.has_value()) {
            abort();
        }
        return std::move(m_value.value());
    }

private:
    std::optional<ResultType> m_value { };
    std::optional<ErrorType>  m_error { };
};

#define TRY_EVAL(...)                 \
    ({                                \
        auto _result = (__VA_ARGS__); \
        if (!_result.has_value()) {   \
            return _result.error();   \
        }                             \
        _result.value();              \
    })

#define MUST_EVAL(...)                                                              \
    ({                                                                              \
        auto _result = (__VA_ARGS__);                                               \
        if (!_result.has_value()) {                                                 \
            fatal("MUST_EVAL {:}: {:}", #__VA_ARGS__, _result.error().to_string()); \
        }                                                                           \
        _result.value();                                                            \
    })

template<typename ErrorType = LibCError>
class [[nodiscard]] Error {
public:
    Error(ErrorType const &error)
        : m_error(error)
    {
    }

    Error(ErrorType &&error)
        : m_error(std::move(error))
    {
    }

    Error() = default;
    Error(Error &&other) noexcept = default;
    Error(Error const &other) = default;
    ~Error() = default;

    Error &operator=(Error &&other) noexcept = default;
    Error &operator=(Error const &other) = default;
           operator bool() const { return is_error(); }

    ErrorType         &error() { return m_error.value(); }
    [[nodiscard]] bool is_error() const { return m_error.has_value(); }

    template<typename Catch>
    void must(Catch const &catch_)
    {
        if (m_error.has_value()) {
            catch_(m_error.value());
            abort();
        }
    }

    void must()
    {
        if (m_error.has_value()) {
            abort();
        }
    }

protected:
    std::optional<ErrorType> m_error { };
};

using CError = Error<LibCError>;

#define TRY(...)                      \
    do {                              \
        auto _result = (__VA_ARGS__); \
        if (_result.is_error()) {     \
            return _result.error();   \
        }                             \
    } while (0)

#define MUST(...)                                                              \
    do {                                                                       \
        auto _result = (__VA_ARGS__);                                          \
        if (_result.is_error()) {                                              \
            fatal("MUST {:}: {:}", #__VA_ARGS__, _result.error().to_string()); \
        }                                                                      \
    } while (0)

#define IGNORE(...)                   \
    do {                              \
        auto _result = (__VA_ARGS__); \
    } while (0)
}
