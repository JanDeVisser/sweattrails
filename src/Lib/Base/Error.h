/*
 * Copyright (c) 2021, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cerrno>
#include <format>
#include <string>
#include <system_error>

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

    // std::filesystem reports errno values in the generic or system category.
    // Errors from other categories become ECUSTOM with their message, and so
    // do errno values LibCError doesn't know.
    explicit LibCError(std::error_code const &ec) noexcept
        : LibCError((ec.category() == std::generic_category() || ec.category() == std::system_category()) ? ec.value() : ECUSTOM)
    {
        if (code == "UNKNOWN") {
            err_no = ECUSTOM;
            code = "ECUSTOM";
        }
        if (err_no == ECUSTOM) {
            description = ec.message();
        }
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
