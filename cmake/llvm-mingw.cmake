# Cross-compiling for 64-bit Windows with llvm-mingw (clang and libc++, as
# on macOS), from https://github.com/mstorsjo/llvm-mingw. See
# specs/62-windows-port.md.
#
#   scripts/windows-deps.sh
#   cmake -B build-windows -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake
#   ninja -C build-windows
#   ctest --test-dir build-windows
#
# LLVM_MINGW_ROOT is where llvm-mingw is (default ~/opt/llvm-mingw), and
# SWEATTRAILS_WINDOWS_DEPS where scripts/windows-deps.sh put the libraries
# (default build-windows/deps). Both can be CMake or environment variables.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

if (NOT LLVM_MINGW_ROOT)
    if (DEFINED ENV{LLVM_MINGW_ROOT})
        set(LLVM_MINGW_ROOT $ENV{LLVM_MINGW_ROOT})
    else ()
        set(LLVM_MINGW_ROOT $ENV{HOME}/opt/llvm-mingw)
    endif ()
endif ()
if (NOT SWEATTRAILS_WINDOWS_DEPS)
    if (DEFINED ENV{SWEATTRAILS_WINDOWS_DEPS})
        set(SWEATTRAILS_WINDOWS_DEPS $ENV{SWEATTRAILS_WINDOWS_DEPS})
    else ()
        set(SWEATTRAILS_WINDOWS_DEPS ${CMAKE_CURRENT_LIST_DIR}/../build-windows/deps)
    endif ()
endif ()
# CMake's test compiles read this file again, without the cache.
set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES LLVM_MINGW_ROOT SWEATTRAILS_WINDOWS_DEPS)

set(target x86_64-w64-mingw32)
set(CMAKE_C_COMPILER ${LLVM_MINGW_ROOT}/bin/${target}-clang)
set(CMAKE_CXX_COMPILER ${LLVM_MINGW_ROOT}/bin/${target}-clang++)
set(CMAKE_RC_COMPILER ${LLVM_MINGW_ROOT}/bin/${target}-windres)
set(CMAKE_AR ${LLVM_MINGW_ROOT}/bin/${target}-ar)
set(CMAKE_RANLIB ${LLVM_MINGW_ROOT}/bin/${target}-ranlib)

# Libraries and headers come from llvm-mingw and the dependencies, never
# from macOS. pkg-config would find Homebrew's.
set(CMAKE_FIND_ROOT_PATH ${LLVM_MINGW_ROOT}/${target} ${SWEATTRAILS_WINDOWS_DEPS})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(ENV{PKG_CONFIG_LIBDIR} ${SWEATTRAILS_WINDOWS_DEPS}/lib/pkgconfig)
set(ENV{PKG_CONFIG_SYSROOT_DIR} "")

# Tests and other programs the build runs go through Wine.
set(CMAKE_CROSSCOMPILING_EMULATOR ${CMAKE_CURRENT_LIST_DIR}/../scripts/wine-run.sh)
