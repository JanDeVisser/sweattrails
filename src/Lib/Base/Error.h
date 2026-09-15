/*
 * Copyright (c) 2021, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cerrno>
#include <format>
#include <string>

#include <StringUtil.h>

namespace ST {

struct LibCError {
#ifdef IS_APPLE
    static constexpr int ECUSTOM = EQFULL + 1;
#endif
#ifdef IS_LINUX
    static constexpr int ECUSTOM = EHWPOISON + 1;
#endif

    int              err_no = { 0 };
    std::string_view code = { "Unknown" };
    std::string      description = { "Unknown error" };

    explicit LibCError(int err) noexcept;
    LibCError() noexcept
        : LibCError(errno)
    {
    }

    explicit LibCError(std::string const &description) noexcept
        : LibCError(ECUSTOM)
    {
        this->description = description;
    }

    template<typename... Args>
    explicit LibCError(std::string const &fmt, Args const &...args) noexcept
        : LibCError(ECUSTOM)
    {
        description = std::vformat(fmt, std::make_format_args(args...));
    }

    [[nodiscard]] std::string to_string() const
    {
        return std::format("{} ({}): {}", code, err_no, description);
    }
};

}
