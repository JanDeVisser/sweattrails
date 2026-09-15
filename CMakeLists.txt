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

include_directories(.)

add_library(
        Base
        STATIC
        src/Lib/Base/Date.cpp
        src/Lib/Base/Defer.h
        src/Lib/Base/Error.cpp
        src/Lib/Base/Expected.h
        src/Lib/Base/Format.h
        src/Lib/Base/IO.cpp
        src/Lib/Base/JSON.h
        src/Lib/Base/JSON.cpp
        src/Lib/Base/Logging.cpp
        src/Lib/Base/Options.cpp
        src/Lib/Base/Pipe.h
        src/Lib/Base/Process.cpp
        src/Lib/Base/StringUtil.cpp
        src/Lib/Base/Token.cpp
        src/Lib/Base/Utf8.cpp
)

target_include_directories(
        Base
        PRIVATE
        src/Lib/Base
)

add_library(
        Widget
        STATIC
        src/Lib/Widget/App.h
        src/Lib/Widget/App.cpp
        src/Lib/Widget/Layout.cpp
        src/Lib/Widget/Modal.h
        src/Lib/Widget/Widget.h
        src/Lib/Widget/Widget.cpp
)

target_include_directories(
        Widget
        PRIVATE
        src/Lib/Base
        src/Lib/Widget
        ${raylib_INCLUDE_DIRS}
        ${FREETYPE_INCLUDE_DIRS}
)

add_executable(
        sweattrails
        MACOSX_BUNDLE
        src/App/SweatTrails.h
        src/App/SweatTrails.cpp
        src/App/Coordinates.cpp
        src/App/Map.h
        src/App/Map.cpp
        src/App/Range.h
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/widget/Activity.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Types.cpp
)

target_include_directories(
        sweattrails
        PRIVATE
        src/App
        src/Lib/Base
        src/Lib/Widget
        ${raylib_INCLUDE_DIRS}
        ${FREETYPE_INCLUDE_DIRS}
  )

target_link_libraries(
        sweattrails
        PRIVATE
        Base
        Widget
        ${raylib_LIBRARIES}
        ${FREETYPE_LIBRARIES}
        ${ICONV}
        m
)

add_executable(
        fitexplore
        src/App/fit/Explore.cpp
        src/App/fit/FIT.cpp
)

# target_include_directories(fitexplore PRIVATE ${raylib_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS})

target_include_directories(
        fitexplore
        PRIVATE
        src/App
        src/Lib/Base
  )

target_link_libraries(
        fitexplore
        PRIVATE
        Base
        ${ICONV}
        m
)

install(TARGETS Base Widget sweattrails fitexplore
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        BUNDLE DESTINATION bundle)

install(DIRECTORY share DESTINATION .)
