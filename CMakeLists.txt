cmake_minimum_required(VERSION 3.27)
project(sweattrails CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

message("System: ${CMAKE_SYSTEM_NAME}. Processor: ${CMAKE_SYSTEM_PROCESSOR}. Version: ${CMAKE_SYSTEM_VERSION}. Compiler: ${CMAKE_CXX_COMPILER_ID}.")

# if (NOT "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
# message(FATAL_ERROR "Aragorn requires Clang because it used C++ language extensions not supported in GCC")
# endif()
set(CMAKE_C_STANDARD 26)
set(CMAKE_CXX_STANDARD 26)

if (APPLE)
  set(IS_APPLE TRUE)
  set(OS macos)
  set(ICONV -liconv)
elseif (WIN32)
  set(IS_WINDOWS TRUE)
  set(OS windows)
else ()
  set(IS_LINUX TRUE)
  set(OS linux)
  set(ICONV)
endif ()

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++ -lc++abi")
set(CMAKE_INSTALL_PREFIX ${PROJECT_BINARY_DIR} CACHE PATH "..." FORCE)

set(sweattrails_NAME SweatTrails)
set(sweattrails_VERSION_MAJOR 1)
set(sweattrails_VERSION_MINOR 0)
set(sweattrails_DIR ${CMAKE_INSTALL_PREFIX})
set(sweattrails_DATADIR ${CMAKE_INSTALL_PREFIX}/share)

find_package(Freetype)
if (DEFINED RAYLIB)
  set(raylib_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/${RAYLIB}")

  # FIXME abstract out Apple bits
  set(raylib_LIBRARIES "-framework IOKit -framework Cocoa -framework OpenGL -L${PROJECT_SOURCE_DIR}/${RAYLIB} -lraylib -lSDL2 -lglfw3")
else ()
  set(raylib_VERBOSE ON)
  include(cmake/raylib-config.cmake)
endif ()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Debug" FORCE)
endif()

include(CheckCSourceCompiles)
include(CheckCXXSourceCompiles)

check_cxx_source_compiles("
   [[noreturn]] void testFunc()
   {}

   int main(void)
   {
     return 0;
   }
" HAVE_C23_ATTRIBUTE_NORETURN
)

check_cxx_source_compiles("
   __attribute__((noreturn)) void testFunc()
   {}

   int main(void)
   {
     return 0;
   }
" HAVE_LEGACY_ATTRIBUTE_NORETURN
)

check_cxx_source_compiles("
   __attribute__((format(printf,1,2))) void testFunc(char *fmt, ...)
   {
   }

   int main(void)
   {
     return 0;
   }
" HAVE_ATTRIBUTE_FORMAT
)

include(CheckStructHasMember)
check_struct_has_member("struct dirent" d_namlen dirent.h HAVE_DIRENT_D_NAMLEN LANGUAGE CXX)

configure_file(
        "config.h.in"
        "config.h"
)

include_directories("${PROJECT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}")

include_directories(. src)

add_executable(
        sweattrails
        MACOSX_BUNDLE
        src/SweatTrails.cpp
        src/App.cpp
        src/Error.cpp
        src/IO.cpp
        src/JSON.cpp
        src/Layout.cpp
        src/Logging.cpp
        src/Options.cpp
        src/StringUtil.cpp
        src/Token.cpp
        src/Utf8.cpp
        src/Widget.cpp
)

target_include_directories(sweattrails PRIVATE ${raylib_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS})

target_link_libraries(
        sweattrails
        PRIVATE
        ${raylib_LIBRARIES}
        ${FREETYPE_LIBRARIES}
        ${ICONV}
        m
)

install(TARGETS sweattrails
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        BUNDLE DESTINATION bundle)

install(DIRECTORY share DESTINATION .)
