/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cerrno>
#include <unistd.h>

#include <Process.h>

extern "C" {

void sigchld(int)
{
}
}
