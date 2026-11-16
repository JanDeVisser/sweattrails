/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <format>

#include <Win32.h>

namespace ST {

std::wstring to_wide(std::string_view utf8)
{
    if (utf8.empty()) {
        return { };
    }
    auto const   size = static_cast<int>(utf8.size());
    auto const   length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), size, nullptr, 0);
    std::wstring ret(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), size, ret.data(), length);
    return ret;
}

std::string from_wide(std::wstring_view wide)
{
    if (wide.empty()) {
        return { };
    }
    auto const  size = static_cast<int>(wide.size());
    auto const  length = WideCharToMultiByte(CP_UTF8, 0, wide.data(), size, nullptr, 0, nullptr, nullptr);
    std::string ret(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), size, ret.data(), length, nullptr, nullptr);
    return ret;
}

LibCError win32_error(DWORD code)
{
    wchar_t   *text { nullptr };
    auto const length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t *>(&text), 0, nullptr);
    auto       description = from_wide(std::wstring_view { text, length });
    LocalFree(text);
    while (!description.empty() && (description.back() == '\n' || description.back() == '\r' || description.back() == '.')) {
        description.pop_back();
    }
    if (description.empty()) {
        description = std::format("Windows error {}", code);
    }
    return LibCError { description };
}

}
