/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * For the Win32 code, and only there: windows.h with all of its API, the
 * shell's and Winsock's too, and converting to and from the UTF-16 that its W functions
 * take. Those headers are included here, after the #undefs below, because
 * clang-format would sort them before this header.
 *
 * The build defines NOGDI and NOUSER, so that the windows.h that curl.h
 * and GL/gl.h include leaves out the names that clash with raylib's
 * (Rectangle, CloseWindow, DrawText, ...). Code that includes this header
 * gets them back, and so can't include raylib.h too.
 */

#pragma once

#ifndef _WIN32
#error "Win32.h is for Windows only"
#endif

#undef NOGDI
#undef NOUSER

#include <string>
#include <string_view>

// In this order: Winsock 2 before windows.h, which would bring the old
// winsock.h, and windows.h before the shell's headers, which need it.
// clang-format off
#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
// clang-format on

#include <Error.h>

namespace ST {

std::wstring to_wide(std::string_view utf8);
std::string  from_wide(std::wstring_view wide);

// The Win32 error `code`, with Windows's description of it.
LibCError win32_error(DWORD code = GetLastError());

}
