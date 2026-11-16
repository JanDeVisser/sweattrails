/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * For the Win32 code, and only there: windows.h with all of its API, and
 * converting to and from the UTF-16 that its W functions take.
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

#include <windows.h>

#include <Error.h>

namespace ST {

std::wstring to_wide(std::string_view utf8);
std::string  from_wide(std::wstring_view wide);

// The Win32 error `code`, with Windows's description of it.
LibCError win32_error(DWORD code = GetLastError());

}
