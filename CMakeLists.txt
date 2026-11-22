cmake_minimum_required(VERSION 3.27)
# The oldest macOS the app runs on: floating-point std::to_chars, which
# std::format uses for doubles, needs 13.3. Only builds with the libraries
# from scripts/macos-deps.sh are made for it: Homebrew's are built for the
# macOS they're on, and the linker would warn about each one, at every link.
set(SWEATTRAILS_MACOS_MINIMUM 13.3)
if (SWEATTRAILS_MACOS_DEPS AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET ${SWEATTRAILS_MACOS_MINIMUM} CACHE STRING "The oldest macOS to run on")
endif ()

# The version: CMakeLists.txt is the only place it is set. See
# specs/63-packaging.md.
project(sweattrails VERSION 1.0.0 LANGUAGES CXX)
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
elseif (WIN32)
  set(IS_WINDOWS TRUE)
  set(OS windows)
else ()
  set(IS_LINUX TRUE)
  set(OS linux)
endif ()

# libc++ for compiling as well as linking: Apple's Clang uses it anyway,
# but Linux's defaults to GCC's libstdc++. llvm-mingw only has libc++.
add_compile_options(-stdlib=libc++)
if (WIN32)
  # Everything linked in, so that the program needs no DLLs next to it.
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
else ()
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++ -lc++abi")
endif ()

if (WIN32)
  enable_language(RC)
  # windows.h, which curl.h and GL/gl.h include, defines names that clash
  # with raylib's: Rectangle, CloseWindow, DrawText. NOGDI and NOUSER leave
  # them out; the Win32 code gets them back with src/Lib/Base/Win32.h.
  # Windows 10 is the oldest Windows the app runs on: the manifest's UTF-8
  # code page needs it.
  add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX NOGDI NOUSER _WIN32_WINNT=0x0A00 CURL_STATICLIB)
  set(PLATFORM_SOURCES
          src/Lib/Base/ChildWin32.cpp
          src/Lib/Base/SocketWin32.cpp
          src/Lib/Base/Win32.h
          src/Lib/Base/Win32.cpp
  )
  set(PLATFORM_LIBRARIES ws2_32 shell32 ole32 bcrypt crypt32 secur32 iphlpapi winmm gdi32)
  # The manifest, with the UTF-8 code page, goes into every program: the
  # tests need UTF-8 paths and arguments as much as the app.
  set(WINDOWS_MANIFEST src/App/windows/sweattrails.rc)
else ()
  set(PLATFORM_SOURCES
          src/Lib/Base/ChildPosix.cpp
          src/Lib/Base/SocketPosix.cpp
  )
  set(PLATFORM_LIBRARIES)
endif ()
set(CMAKE_INSTALL_PREFIX ${PROJECT_BINARY_DIR} CACHE PATH "..." FORCE)

set(sweattrails_NAME SweatTrails)
set(sweattrails_ID sweattrails)
set(sweattrails_DIR ${CMAKE_INSTALL_PREFIX})
set(sweattrails_DATADIR ${CMAKE_INSTALL_PREFIX}/share)

# The packaged app's libraries, from scripts/macos-deps.sh, linked
# statically, so the app doesn't need Homebrew. Without them, the build uses
# Homebrew's.
if (APPLE AND SWEATTRAILS_MACOS_DEPS)
  list(PREPEND CMAKE_PREFIX_PATH ${SWEATTRAILS_MACOS_DEPS})
  set(CMAKE_IGNORE_PREFIX_PATH /opt/homebrew /usr/local)
  set(ENV{PKG_CONFIG_LIBDIR} ${SWEATTRAILS_MACOS_DEPS}/lib/pkgconfig)
  set(OPENSSL_ROOT_DIR ${SWEATTRAILS_MACOS_DEPS})
  set(OPENSSL_USE_STATIC_LIBS TRUE)
  set(raylib_USE_STATIC_LIBS TRUE)
endif ()

find_package(Freetype)
if (DEFINED RAYLIB)
  set(raylib_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/${RAYLIB}")

  # FIXME abstract out Apple bits
  set(raylib_LIBRARIES "-framework IOKit -framework Cocoa -framework OpenGL -L${PROJECT_SOURCE_DIR}/${RAYLIB} -lraylib -lSDL2 -lglfw3")
else ()
  set(raylib_VERBOSE ON)
  include(cmake/raylib-config.cmake)
  if (APPLE AND raylib_USE_STATIC_LIBS)
    # What the dynamic library would have brought along.
    list(APPEND raylib_LIBRARIES "-framework Cocoa" "-framework IOKit" "-framework CoreVideo" "-framework CoreAudio" "-framework QuartzCore")
  endif ()
endif ()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Debug" FORCE)
endif()

find_package(CURL)
find_package(ZLIB REQUIRED)
# The overlay's render reads its frames back with glReadPixels.
set(OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL REQUIRED)

# Homebrew's OpenSSL isn't linked into /opt/homebrew, so CMake doesn't find
# it by itself.
if (APPLE AND NOT OPENSSL_ROOT_DIR)
    foreach (prefix /opt/homebrew/opt/openssl /opt/homebrew/opt/openssl@3
                    /usr/local/opt/openssl /usr/local/opt/openssl@3)
        if (EXISTS ${prefix})
            set(OPENSSL_ROOT_DIR ${prefix})
            break()
        endif ()
    endforeach ()
endif ()
find_package(OpenSSL REQUIRED)

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
        src/Lib/Base/CSV.cpp
        src/Lib/Base/Error.cpp
        src/Lib/Base/Expected.h
        src/Lib/Base/Format.h
        src/Lib/Base/FileIO.cpp
        src/Lib/Base/JSON.h
        src/Lib/Base/JSON.cpp
        src/Lib/Base/Logging.cpp
        src/Lib/Base/Child.h
        src/Lib/Base/Socket.h
        ${PLATFORM_SOURCES}
        src/Lib/Base/Options.cpp
        src/Lib/Base/StringUtil.cpp
        src/Lib/Base/Token.cpp
        src/Lib/Base/Utf8.cpp
        src/Lib/Base/Zip.cpp
)

target_include_directories(
        Base
        PRIVATE
        src/Lib/Base
)
# Only the library: on macOS, ZLIB::ZLIB adds the SDK's usr/include in front
# of libc++'s headers, which breaks them. The compiler finds zlib.h anyway.
target_link_libraries(Base PUBLIC ${ZLIB_LIBRARIES} ${PLATFORM_LIBRARIES})
if (WIN32)
  target_include_directories(Base PRIVATE ${ZLIB_INCLUDE_DIRS})
endif ()

add_library(
        Widget
        STATIC
        src/Lib/Widget/App.h
        src/Lib/Widget/App.cpp
        src/Lib/Widget/BarChart.h
        src/Lib/Widget/BarChart.cpp
        src/Lib/Widget/Chart.h
        src/Lib/Widget/Chart.cpp
        src/Lib/Widget/LineChart.h
        src/Lib/Widget/LineChart.cpp
        src/Lib/Widget/ClipShader.h
        src/Lib/Widget/ClipShader.cpp
        src/Lib/Widget/CommandPalette.h
        src/Lib/Widget/CommandPalette.cpp
        src/Lib/Widget/PopupMenu.h
        src/Lib/Widget/PopupMenu.cpp
        src/Lib/Widget/TopBar.h
        src/Lib/Widget/TopBar.cpp
        src/Lib/Widget/Colors.h
        src/Lib/Widget/Colors.cpp
        src/Lib/Widget/Dashes.h
        src/Lib/Widget/Dashes.cpp
        src/Lib/Widget/Emoji.h
        src/Lib/Widget/Emoji.cpp
        src/Lib/Widget/FontCache.h
        src/Lib/Widget/FontCache.cpp
        src/Lib/Widget/InfoBox.h
        src/Lib/Widget/InfoBox.cpp
        src/Lib/Widget/Layout.cpp
        src/Lib/Widget/Modal.h
        src/Lib/Widget/Scroll.h
        src/Lib/Widget/Scroll.cpp
        src/Lib/Widget/Table.h
        src/Lib/Widget/Table.cpp
        src/Lib/Widget/TabStrip.h
        src/Lib/Widget/TabStrip.cpp
        src/Lib/Widget/TextArea.h
        src/Lib/Widget/TextArea.cpp
        src/Lib/Widget/TextAreaBox.h
        src/Lib/Widget/TextLine.h
        src/Lib/Widget/TextLine.cpp
        src/Lib/Widget/Widget.h
        src/Lib/Widget/Widget.cpp
        src/Lib/Widget/Window.h
        src/Lib/Widget/Window.cpp
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
        src/App/DataDir.h
        src/App/DataDir.cpp
        src/App/Desktop.cpp
        src/App/Http.cpp
        src/App/OAuth.cpp
        src/App/Map.h
        src/App/Map.cpp
        src/App/Range.h
        src/App/Zwift.h
        src/App/Zwift.cpp
        src/App/fit/FIT.cpp
        src/App/fit/FITPatch.cpp
        src/App/fit/Profile.cpp
        src/App/overlay/Encoder.cpp
        src/App/overlay/Fields.cpp
        src/App/overlay/Layout.cpp
        src/App/overlay/LayoutFile.cpp
        src/App/overlay/OverlayRender.cpp
        src/App/overlay/OverlayTiles.cpp
        src/App/overlay/Picture.cpp
        src/App/overlay/Render.cpp
        src/App/overlay/ElevationProfile.cpp
        src/App/overlay/Renderer.cpp
        src/App/overlay/Shapes.cpp
        src/App/overlay/Template.cpp
        src/App/overlay/TrackMap.cpp
        src/App/widget/Activity.cpp
        src/App/widget/ActivityInfo.cpp
        src/App/widget/ActivityInfoBox.cpp
        src/App/widget/ActivityMap.cpp
        src/App/widget/ActivitySelector.cpp
        src/App/widget/DataFields.cpp
        src/App/widget/DisplayedActivity.cpp
        src/App/widget/GraphData.cpp
        src/App/widget/OverlayPreview.cpp
        src/App/widget/SportIcon.cpp
        src/App/widget/Summaries.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Boundaries.cpp
        src/App/storage/Gaps.cpp
        src/App/storage/Paused.cpp
        src/App/storage/Comparison.cpp
        src/App/storage/Recordings.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Summaries.cpp
        src/App/storage/PowerCurve.cpp
        src/App/storage/PaceCurve.cpp
        src/App/storage/Fitness.cpp
        src/App/storage/Thresholds.cpp
        src/App/storage/Volume.cpp
        src/App/storage/Download.cpp
        src/App/storage/Garmin.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/storage/Strava.cpp
        src/App/storage/StravaBacklog.cpp
        src/App/storage/StravaExport.cpp
        src/App/storage/StravaTitles.cpp
        src/App/storage/StravaUpload.cpp
        src/App/storage/Wahoo.cpp
        src/App/storage/Weather.cpp
        src/App/widget/Weather.cpp
        share/icons/sweattrails.icns
        ${WINDOWS_MANIFEST}
)

# A window and no console on Windows.
set_target_properties(sweattrails PROPERTIES WIN32_EXECUTABLE TRUE)

# The bundle's icon, which the Dock and Finder show.
set_source_files_properties(share/icons/sweattrails.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
set_target_properties(
        sweattrails
        PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${PROJECT_SOURCE_DIR}/src/App/macos/Info.plist.in
        MACOSX_BUNDLE_ICON_FILE sweattrails.icns
        MACOSX_BUNDLE_BUNDLE_NAME ${sweattrails_NAME}
        MACOSX_BUNDLE_GUI_IDENTIFIER com.finiandarcy.sweattrails
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_COPYRIGHT "Copyright (c) 2026, Jan de Visser"
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
        OpenGL::GL
        ${FREETYPE_LIBRARIES}
        ${CURL_LIBRARIES}
        OpenSSL::SSL
        m
)

add_executable(
        fitexplore
        src/App/fit/Explore.cpp
        ${WINDOWS_MANIFEST}
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
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
        m
)

enable_testing()

# What tests start in place of ffmpeg and other programs.
add_executable(fake_program test/FakeProgram.cpp ${WINDOWS_MANIFEST})

# A test program in test/, run by CTest. See test/README.md.
function(sweattrails_test name)
    add_executable(${name} ${ARGN} ${WINDOWS_MANIFEST})
    add_dependencies(${name} fake_program)
    target_compile_definitions(${name} PRIVATE FAKE_PROGRAM="$<TARGET_FILE:fake_program>")
    target_include_directories(
            ${name}
            PRIVATE
            test
            src/Lib/Base
            src/Lib/Widget
            ${raylib_INCLUDE_DIRS}
            ${FREETYPE_INCLUDE_DIRS}
    )
    target_link_libraries(
            ${name}
            PRIVATE
            Widget
            Base
            ${raylib_LIBRARIES}
            ${FREETYPE_LIBRARIES}
                m
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

sweattrails_test(listbox_test test/ListBoxTest.cpp)
sweattrails_test(command_palette_test test/CommandPaletteTest.cpp)
sweattrails_test(popup_menu_test test/PopupMenuTest.cpp)
sweattrails_test(scroll_test test/ScrollTest.cpp)
sweattrails_test(table_test test/TableTest.cpp)
sweattrails_test(tab_strip_test test/TabStripTest.cpp)
sweattrails_test(bar_chart_test test/BarChartTest.cpp)
sweattrails_test(line_chart_test test/LineChartTest.cpp)
sweattrails_test(text_line_test test/TextLineTest.cpp)
sweattrails_test(status_bar_test test/StatusBarTest.cpp)
sweattrails_test(top_bar_test test/TopBarTest.cpp)
sweattrails_test(text_area_test test/TextAreaTest.cpp)
sweattrails_test(emoji_test test/EmojiTest.cpp)
sweattrails_test(emoji_pack_test test/EmojiPackTest.cpp)
# The test reads the emoji pack from the source tree.
target_compile_definitions(emoji_pack_test PRIVATE SWEATTRAILS_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
sweattrails_test(text_area_box_test test/TextAreaBoxTest.cpp)
sweattrails_test(colors_test test/ColorsTest.cpp)
sweattrails_test(dashes_test test/DashesTest.cpp)
sweattrails_test(window_test test/WindowTest.cpp)
sweattrails_test(font_cache_test test/FontCacheTest.cpp)
sweattrails_test(pick_box_test test/PickBoxTest.cpp)
sweattrails_test(input_box_test test/InputBoxTest.cpp)
sweattrails_test(query_box_test test/QueryBoxTest.cpp)
sweattrails_test(key_binding_test test/KeyBindingTest.cpp)
sweattrails_test(zip_test test/ZipTest.cpp)
sweattrails_test(csv_test test/CSVTest.cpp)
sweattrails_test(json_test test/JSONTest.cpp)
sweattrails_test(cursor_test test/CursorTest.cpp)
sweattrails_test(string_util_test test/StringUtilTest.cpp)
sweattrails_test(utf8_test test/Utf8Test.cpp)
sweattrails_test(io_test test/IOTest.cpp)
sweattrails_test(child_test test/ChildTest.cpp)
sweattrails_test(date_test test/DateTest.cpp)

sweattrails_test(http_test test/HttpTest.cpp src/App/Http.cpp)
target_include_directories(http_test PRIVATE src/App)
target_link_libraries(http_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(oauth_test test/OAuthTest.cpp src/App/OAuth.cpp)
target_include_directories(oauth_test PRIVATE src/App)
target_link_libraries(oauth_test PRIVATE OpenSSL::SSL)

sweattrails_test(desktop_test test/DesktopTest.cpp src/App/Desktop.cpp)
target_include_directories(desktop_test PRIVATE src/App)

# A test of app code also needs that code, and what it uses.
sweattrails_test(
        activity_selector_test
        test/ActivitySelectorTest.cpp
        src/App/DataDir.cpp
        src/App/storage/Thresholds.cpp
        src/App/storage/Volume.cpp
        src/App/widget/ActivitySelector.cpp
        src/App/storage/Summaries.cpp
        src/App/storage/PowerCurve.cpp
        src/App/storage/PaceCurve.cpp
        src/App/widget/DataFields.cpp
        src/App/storage/Comparison.cpp
        src/App/widget/SportIcon.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(activity_selector_test PRIVATE src/App)
target_link_libraries(activity_selector_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        zwift_test
        test/ZwiftTest.cpp
        src/App/Zwift.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(zwift_test PRIVATE src/App)
target_link_libraries(zwift_test PRIVATE ${CURL_LIBRARIES})
target_compile_definitions(zwift_test PRIVATE SWEATTRAILS_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

sweattrails_test(
        map_test
        test/MapTest.cpp
        src/App/Map.cpp
        src/App/Coordinates.cpp
)
target_include_directories(map_test PRIVATE src/App)
target_link_libraries(map_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        activity_info_test
        test/ActivityInfoTest.cpp
        src/App/widget/ActivityInfo.cpp
        src/App/storage/Thresholds.cpp
        src/App/storage/Volume.cpp
        src/App/widget/DataFields.cpp
        src/App/storage/Comparison.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(activity_info_test PRIVATE src/App)
target_link_libraries(activity_info_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        activity_info_box_test
        test/ActivityInfoBoxTest.cpp
        src/App/widget/ActivityInfoBox.cpp
        src/App/widget/ActivityInfo.cpp
        src/App/storage/Thresholds.cpp
        src/App/storage/Volume.cpp
        src/App/widget/DataFields.cpp
        src/App/storage/Comparison.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(activity_info_box_test PRIVATE src/App)
target_link_libraries(activity_info_box_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        data_fields_test
        test/DataFieldsTest.cpp
        src/App/storage/Thresholds.cpp
        src/App/storage/Volume.cpp
        src/App/widget/DataFields.cpp
        src/App/storage/Comparison.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(data_fields_test PRIVATE src/App)
target_link_libraries(data_fields_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        graph_data_test
        test/GraphDataTest.cpp
        src/App/widget/GraphData.cpp
        src/App/widget/DataFields.cpp
        src/App/storage/Thresholds.cpp
        src/App/storage/Volume.cpp
        src/App/storage/Comparison.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(graph_data_test PRIVATE src/App)
target_link_libraries(graph_data_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        overlay_fields_test
        test/OverlayFieldsTest.cpp
        src/App/overlay/Fields.cpp
        src/App/overlay/Template.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(overlay_fields_test PRIVATE src/App)
target_link_libraries(overlay_fields_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        overlay_layout_test
        test/OverlayLayoutTest.cpp
        src/App/overlay/Fields.cpp
        src/App/overlay/Layout.cpp
        src/App/overlay/Template.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(overlay_layout_test PRIVATE src/App)
target_link_libraries(overlay_layout_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        overlay_shapes_test
        test/OverlayShapesTest.cpp
        src/App/overlay/Fields.cpp
        src/App/overlay/Layout.cpp
        src/App/overlay/Shapes.cpp
        src/App/overlay/Template.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(overlay_shapes_test PRIVATE src/App)
target_link_libraries(overlay_shapes_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        overlay_elevation_profile_test
        test/OverlayElevationProfileTest.cpp
        src/App/overlay/ElevationProfile.cpp
        src/App/overlay/Fields.cpp
        src/App/overlay/Layout.cpp
        src/App/overlay/Shapes.cpp
        src/App/overlay/Template.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(overlay_elevation_profile_test PRIVATE src/App)
target_link_libraries(overlay_elevation_profile_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        overlay_track_map_test
        test/OverlayTrackMapTest.cpp
        src/App/overlay/Fields.cpp
        src/App/overlay/Layout.cpp
        src/App/overlay/Shapes.cpp
        src/App/overlay/Template.cpp
        src/App/overlay/TrackMap.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(overlay_track_map_test PRIVATE src/App)
target_link_libraries(overlay_track_map_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        overlay_tiles_test
        test/OverlayTilesTest.cpp
        src/App/overlay/OverlayTiles.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(overlay_tiles_test PRIVATE src/App)
target_link_libraries(overlay_tiles_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        layout_file_test
        test/LayoutFileTest.cpp
        src/App/overlay/Fields.cpp
        src/App/overlay/Layout.cpp
        src/App/overlay/LayoutFile.cpp
        src/App/overlay/Template.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(layout_file_test PRIVATE src/App)
target_compile_definitions(layout_file_test PRIVATE SWEATTRAILS_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(layout_file_test PRIVATE ${CURL_LIBRARIES})

# Reads the built-in icons, and pictures, with raylib, without a window.
sweattrails_test(
        overlay_image_test
        test/OverlayImageTest.cpp
        src/App/overlay/Fields.cpp
        src/App/overlay/Layout.cpp
        src/App/overlay/LayoutFile.cpp
        src/App/overlay/OverlayTiles.cpp
        src/App/overlay/Picture.cpp
        src/App/overlay/ElevationProfile.cpp
        src/App/overlay/Renderer.cpp
        src/App/overlay/Shapes.cpp
        src/App/overlay/Template.cpp
        src/App/overlay/TrackMap.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(overlay_image_test PRIVATE src/App)
target_compile_definitions(overlay_image_test PRIVATE SWEATTRAILS_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(overlay_image_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        overlay_render_test
        test/OverlayRenderTest.cpp
        src/App/overlay/Fields.cpp
        src/App/overlay/Layout.cpp
        src/App/overlay/Render.cpp
        src/App/overlay/Template.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(overlay_render_test PRIVATE src/App)
target_link_libraries(overlay_render_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        encoder_test
        test/EncoderTest.cpp
        src/App/overlay/Encoder.cpp
        src/App/overlay/Fields.cpp
        src/App/overlay/Render.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(encoder_test PRIVATE src/App)
target_link_libraries(encoder_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        overlay_render_start_test
        test/OverlayRenderStartTest.cpp
        src/App/overlay/Encoder.cpp
        src/App/overlay/Fields.cpp
        src/App/overlay/Layout.cpp
        src/App/overlay/LayoutFile.cpp
        src/App/overlay/OverlayRender.cpp
        src/App/overlay/OverlayTiles.cpp
        src/App/overlay/Picture.cpp
        src/App/overlay/Render.cpp
        src/App/overlay/ElevationProfile.cpp
        src/App/overlay/Renderer.cpp
        src/App/overlay/Shapes.cpp
        src/App/overlay/Template.cpp
        src/App/overlay/TrackMap.cpp
        src/App/widget/DisplayedActivity.cpp
        src/App/storage/Boundaries.cpp
        src/App/storage/Comparison.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(overlay_render_start_test PRIVATE src/App)
target_compile_definitions(overlay_render_start_test PRIVATE SWEATTRAILS_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(overlay_render_start_test PRIVATE ${CURL_LIBRARIES} OpenGL::GL)

sweattrails_test(
        overlay_tiles_needed_test
        test/OverlayTilesNeededTest.cpp
        src/App/overlay/Fields.cpp
        src/App/overlay/Layout.cpp
        src/App/overlay/LayoutFile.cpp
        src/App/overlay/OverlayTiles.cpp
        src/App/overlay/Picture.cpp
        src/App/overlay/ElevationProfile.cpp
        src/App/overlay/Renderer.cpp
        src/App/overlay/Shapes.cpp
        src/App/overlay/Template.cpp
        src/App/overlay/TrackMap.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(overlay_tiles_needed_test PRIVATE src/App)
target_link_libraries(overlay_tiles_needed_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        boundaries_test
        test/BoundariesTest.cpp
        src/App/storage/Boundaries.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(boundaries_test PRIVATE src/App)
target_link_libraries(boundaries_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        displayed_activity_test
        test/DisplayedActivityTest.cpp
        src/App/widget/DisplayedActivity.cpp
        src/App/storage/Boundaries.cpp
        src/App/storage/Comparison.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(displayed_activity_test PRIVATE src/App)
target_link_libraries(displayed_activity_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        laps_test
        test/LapsTest.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(laps_test PRIVATE src/App)
target_link_libraries(laps_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        summaries_test
        test/SummariesTest.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Summaries.cpp
        src/App/storage/PowerCurve.cpp
        src/App/storage/PaceCurve.cpp
        src/App/storage/Volume.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(summaries_test PRIVATE src/App)
target_link_libraries(summaries_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        summaries_screen_test
        test/SummariesScreenTest.cpp
        src/App/widget/Summaries.cpp
        src/App/storage/Volume.cpp
        src/App/storage/Fitness.cpp
        src/App/storage/Thresholds.cpp
        src/App/storage/PowerCurve.cpp
        src/App/storage/PaceCurve.cpp
)
target_include_directories(summaries_screen_test PRIVATE src/App)

sweattrails_test(
        power_curve_test
        test/PowerCurveTest.cpp
        src/App/storage/PowerCurve.cpp
        src/App/storage/Volume.cpp
)
target_include_directories(power_curve_test PRIVATE src/App)

sweattrails_test(
        pace_curve_test
        test/PaceCurveTest.cpp
        src/App/storage/PaceCurve.cpp
        src/App/storage/PowerCurve.cpp
        src/App/storage/Volume.cpp
)
target_include_directories(pace_curve_test PRIVATE src/App)

sweattrails_test(
        fit_patch_test
        test/FITPatchTest.cpp
        src/App/fit/FITPatch.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(fit_patch_test PRIVATE src/App)
target_link_libraries(fit_patch_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        comparison_test
        test/ComparisonTest.cpp
        src/App/storage/Comparison.cpp
)
target_include_directories(comparison_test PRIVATE src/App)

sweattrails_test(
        recordings_test
        test/RecordingsTest.cpp
        src/App/storage/Recordings.cpp
        src/App/storage/Summaries.cpp
        src/App/storage/PowerCurve.cpp
        src/App/storage/PaceCurve.cpp
        src/App/storage/Volume.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(recordings_test PRIVATE src/App)
target_link_libraries(recordings_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        delete_test
        test/DeleteTest.cpp
        src/App/storage/Recordings.cpp
        src/App/storage/Summaries.cpp
        src/App/storage/PowerCurve.cpp
        src/App/storage/PaceCurve.cpp
        src/App/storage/Volume.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(delete_test PRIVATE src/App)
target_link_libraries(delete_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        stubs_test
        test/StubsTest.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(stubs_test PRIVATE src/App)
target_link_libraries(stubs_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        gaps_test
        test/GapsTest.cpp
        src/App/storage/Gaps.cpp
        src/App/fit/FITPatch.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(gaps_test PRIVATE src/App)
target_link_libraries(gaps_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        paused_test
        test/PausedTest.cpp
        src/App/storage/Paused.cpp
        src/App/storage/Gaps.cpp
        src/App/fit/FITPatch.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(paused_test PRIVATE src/App)
target_link_libraries(paused_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        legs_test
        test/LegsTest.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(legs_test PRIVATE src/App)
target_link_libraries(legs_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        fitness_test
        test/FitnessTest.cpp
        src/App/storage/Fitness.cpp
        src/App/storage/Thresholds.cpp
        src/App/storage/Volume.cpp
)
target_include_directories(fitness_test PRIVATE src/App)

sweattrails_test(
        volume_test
        test/VolumeTest.cpp
        src/App/storage/Volume.cpp
)
target_include_directories(volume_test PRIVATE src/App)

sweattrails_test(
        activity_load_test
        test/ActivityLoadTest.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(activity_load_test PRIVATE src/App)
target_link_libraries(activity_load_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        fit_test
        test/FITTest.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
)
target_include_directories(fit_test PRIVATE src/App)

sweattrails_test(
        device_info_test
        test/DeviceInfoTest.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(device_info_test PRIVATE src/App)
target_link_libraries(device_info_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        activity_months_test
        test/ActivityMonthsTest.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(activity_months_test PRIVATE src/App)
target_link_libraries(activity_months_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        sport_icon_test
        test/SportIconTest.cpp
        src/App/widget/SportIcon.cpp
)
target_include_directories(sport_icon_test PRIVATE src/App)
# The test checks that the icons are in the source tree.
target_compile_definitions(sport_icon_test PRIVATE SWEATTRAILS_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

sweattrails_test(
        garmin_test
        test/GarminTest.cpp
        src/App/storage/Garmin.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(garmin_test PRIVATE src/App)
target_link_libraries(garmin_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        strava_test
        test/StravaTest.cpp
        src/App/storage/Strava.cpp
        src/App/Http.cpp
        src/App/OAuth.cpp
)
target_include_directories(strava_test PRIVATE src/App)
target_link_libraries(strava_test PRIVATE ${CURL_LIBRARIES} OpenSSL::SSL)

sweattrails_test(
        wahoo_test
        test/WahooTest.cpp
        src/App/storage/Wahoo.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Http.cpp
        src/App/Map.cpp
        src/App/OAuth.cpp
)
target_include_directories(wahoo_test PRIVATE src/App)
target_link_libraries(wahoo_test PRIVATE ${CURL_LIBRARIES} OpenSSL::SSL)

sweattrails_test(
        weather_test
        test/WeatherTest.cpp
        src/App/storage/Weather.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Http.cpp
        src/App/Map.cpp
)
target_include_directories(weather_test PRIVATE src/App)
target_link_libraries(weather_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        weather_box_test
        test/WeatherBoxTest.cpp
        src/App/widget/Weather.cpp
        src/App/storage/Weather.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Http.cpp
        src/App/Map.cpp
)
target_include_directories(weather_box_test PRIVATE src/App)
target_link_libraries(weather_box_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        strava_titles_test
        test/StravaTitlesTest.cpp
        src/App/storage/Strava.cpp
        src/App/storage/StravaTitles.cpp
        src/App/storage/Summaries.cpp
        src/App/storage/PowerCurve.cpp
        src/App/storage/PaceCurve.cpp
        src/App/storage/Volume.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Http.cpp
        src/App/Map.cpp
        src/App/OAuth.cpp
)
target_include_directories(strava_titles_test PRIVATE src/App)
target_link_libraries(strava_titles_test PRIVATE ${CURL_LIBRARIES} OpenSSL::SSL)

sweattrails_test(
        strava_upload_test
        test/StravaUploadTest.cpp
        src/App/storage/Strava.cpp
        src/App/storage/StravaUpload.cpp
        src/App/storage/Summaries.cpp
        src/App/storage/PowerCurve.cpp
        src/App/storage/PaceCurve.cpp
        src/App/storage/Volume.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Http.cpp
        src/App/Map.cpp
        src/App/OAuth.cpp
)
target_include_directories(strava_upload_test PRIVATE src/App)
target_link_libraries(strava_upload_test PRIVATE ${CURL_LIBRARIES} OpenSSL::SSL)

sweattrails_test(
        strava_backlog_test
        test/StravaBacklogTest.cpp
        src/App/storage/Strava.cpp
        src/App/storage/StravaBacklog.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Http.cpp
        src/App/Map.cpp
        src/App/OAuth.cpp
)
target_include_directories(strava_backlog_test PRIVATE src/App)
target_link_libraries(strava_backlog_test PRIVATE ${CURL_LIBRARIES} OpenSSL::SSL)

sweattrails_test(
        strava_export_test
        test/StravaExportTest.cpp
        src/App/storage/StravaExport.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Legs.cpp
        src/App/storage/Devices.cpp
        src/App/storage/Laps.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Recordings.cpp
        src/App/storage/Volume.cpp
        src/App/storage/Stubs.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Http.cpp
        src/App/Map.cpp
        src/App/OAuth.cpp
)
target_include_directories(strava_export_test PRIVATE src/App)
target_link_libraries(strava_export_test PRIVATE ${CURL_LIBRARIES} OpenSSL::SSL)
# The test reads its fixture from the source tree.
target_compile_definitions(strava_export_test PRIVATE SWEATTRAILS_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

sweattrails_test(download_test test/DownloadTest.cpp src/App/storage/Download.cpp)
sweattrails_test(logging_test test/LoggingTest.cpp)
target_include_directories(download_test PRIVATE src/App)

install(TARGETS Base Widget sweattrails fitexplore
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        BUNDLE DESTINATION bundle)

install(DIRECTORY share DESTINATION .)
