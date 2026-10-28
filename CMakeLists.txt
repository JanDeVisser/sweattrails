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
set(sweattrails_ID sweattrails)
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

find_package(CURL)
find_package(ZLIB REQUIRED)
# The overlay's render reads its frames back with glReadPixels.
set(OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL REQUIRED)

# Homebrew's OpenSSL isn't linked into /opt/homebrew, so CMake doesn't find
# it by itself.
if (APPLE AND NOT OPENSSL_ROOT_DIR)
    foreach (prefix /opt/homebrew/opt/openssl /usr/local/opt/openssl)
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
        src/Lib/Base/Zip.cpp
)

target_include_directories(
        Base
        PRIVATE
        src/Lib/Base
)
# Only the library: on macOS, ZLIB::ZLIB adds the SDK's usr/include in front
# of libc++'s headers, which breaks them. The compiler finds zlib.h anyway.
target_link_libraries(Base PUBLIC ${ZLIB_LIBRARIES})

add_library(
        Widget
        STATIC
        src/Lib/Widget/App.h
        src/Lib/Widget/App.cpp
        src/Lib/Widget/ClipShader.h
        src/Lib/Widget/ClipShader.cpp
        src/Lib/Widget/Colors.h
        src/Lib/Widget/Colors.cpp
        src/Lib/Widget/FontCache.h
        src/Lib/Widget/FontCache.cpp
        src/Lib/Widget/Layout.cpp
        src/Lib/Widget/Modal.h
        src/Lib/Widget/Scroll.h
        src/Lib/Widget/Scroll.cpp
        src/Lib/Widget/Table.h
        src/Lib/Widget/Table.cpp
        src/Lib/Widget/TextLine.h
        src/Lib/Widget/TextLine.cpp
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
        src/App/Http.cpp
        src/App/OAuth.cpp
        src/App/Map.h
        src/App/Map.cpp
        src/App/Range.h
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/overlay/Encoder.cpp
        src/App/overlay/Fields.cpp
        src/App/overlay/Layout.cpp
        src/App/overlay/LayoutFile.cpp
        src/App/overlay/OverlayRender.cpp
        src/App/overlay/OverlayTiles.cpp
        src/App/overlay/Render.cpp
        src/App/overlay/ElevationProfile.cpp
        src/App/overlay/Renderer.cpp
        src/App/overlay/Shapes.cpp
        src/App/overlay/Template.cpp
        src/App/overlay/TrackMap.cpp
        src/App/widget/Activity.cpp
        src/App/widget/ActivityMap.cpp
        src/App/widget/ActivitySelector.cpp
        src/App/widget/DataFields.cpp
        src/App/widget/DisplayedActivity.cpp
        src/App/widget/OverlayPreview.cpp
        src/App/widget/SportIcon.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Download.cpp
        src/App/storage/Garmin.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Types.cpp
        src/App/storage/Wahoo.cpp
        share/icons/sweattrails.icns
)

# The bundle's icon, which the Dock and Finder show.
set_source_files_properties(share/icons/sweattrails.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
set_target_properties(sweattrails PROPERTIES MACOSX_BUNDLE_ICON_FILE sweattrails.icns)

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
        ${ICONV}
        m
)

add_executable(
        fitexplore
        src/App/fit/Explore.cpp
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
        ${ICONV}
        m
)

enable_testing()

# A test program in test/, run by CTest. See test/README.md.
function(sweattrails_test name)
    add_executable(${name} ${ARGN})
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
            ${ICONV}
            m
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

sweattrails_test(listbox_test test/ListBoxTest.cpp)
sweattrails_test(scroll_test test/ScrollTest.cpp)
sweattrails_test(table_test test/TableTest.cpp)
sweattrails_test(text_line_test test/TextLineTest.cpp)
sweattrails_test(colors_test test/ColorsTest.cpp)
sweattrails_test(font_cache_test test/FontCacheTest.cpp)
sweattrails_test(pick_box_test test/PickBoxTest.cpp)
sweattrails_test(input_box_test test/InputBoxTest.cpp)
sweattrails_test(key_binding_test test/KeyBindingTest.cpp)
sweattrails_test(zip_test test/ZipTest.cpp)
sweattrails_test(json_test test/JSONTest.cpp)
sweattrails_test(cursor_test test/CursorTest.cpp)
sweattrails_test(string_util_test test/StringUtilTest.cpp)
sweattrails_test(io_test test/IOTest.cpp)
sweattrails_test(date_test test/DateTest.cpp)

sweattrails_test(http_test test/HttpTest.cpp src/App/Http.cpp)
target_include_directories(http_test PRIVATE src/App)
target_link_libraries(http_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(oauth_test test/OAuthTest.cpp src/App/OAuth.cpp)
target_include_directories(oauth_test PRIVATE src/App)
target_link_libraries(oauth_test PRIVATE OpenSSL::SSL)

# A test of app code also needs that code, and what it uses.
sweattrails_test(
        activity_selector_test
        test/ActivitySelectorTest.cpp
        src/App/widget/ActivitySelector.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(activity_selector_test PRIVATE src/App)
target_link_libraries(activity_selector_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        map_test
        test/MapTest.cpp
        src/App/Map.cpp
        src/App/Coordinates.cpp
)
target_include_directories(map_test PRIVATE src/App)
target_link_libraries(map_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        data_fields_test
        test/DataFieldsTest.cpp
        src/App/widget/DataFields.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(data_fields_test PRIVATE src/App)
target_link_libraries(data_fields_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        overlay_fields_test
        test/OverlayFieldsTest.cpp
        src/App/overlay/Fields.cpp
        src/App/overlay/Template.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Storage.cpp
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
        src/App/storage/Storage.cpp
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
        src/App/storage/Storage.cpp
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
        src/App/storage/Storage.cpp
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
        src/App/storage/Storage.cpp
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
        src/App/storage/Storage.cpp
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
sweattrails_test(overlay_image_test test/OverlayImageTest.cpp)
target_compile_definitions(overlay_image_test PRIVATE SWEATTRAILS_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

sweattrails_test(
        overlay_render_test
        test/OverlayRenderTest.cpp
        src/App/overlay/Fields.cpp
        src/App/overlay/Layout.cpp
        src/App/overlay/Render.cpp
        src/App/overlay/Template.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Storage.cpp
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
        src/App/storage/Storage.cpp
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
        src/App/overlay/Render.cpp
        src/App/overlay/ElevationProfile.cpp
        src/App/overlay/Renderer.cpp
        src/App/overlay/Shapes.cpp
        src/App/overlay/Template.cpp
        src/App/overlay/TrackMap.cpp
        src/App/widget/DisplayedActivity.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Storage.cpp
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
        src/App/overlay/ElevationProfile.cpp
        src/App/overlay/Renderer.cpp
        src/App/overlay/Shapes.cpp
        src/App/overlay/Template.cpp
        src/App/overlay/TrackMap.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(overlay_tiles_needed_test PRIVATE src/App)
target_link_libraries(overlay_tiles_needed_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        displayed_activity_test
        test/DisplayedActivityTest.cpp
        src/App/widget/DisplayedActivity.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(displayed_activity_test PRIVATE src/App)
target_link_libraries(displayed_activity_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        activity_load_test
        test/ActivityLoadTest.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Storage.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(activity_load_test PRIVATE src/App)
target_link_libraries(activity_load_test PRIVATE ${CURL_LIBRARIES})

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
        src/App/storage/Storage.cpp
        src/App/storage/Types.cpp
        src/App/fit/FIT.cpp
        src/App/fit/Profile.cpp
        src/App/Coordinates.cpp
        src/App/Map.cpp
)
target_include_directories(garmin_test PRIVATE src/App)
target_link_libraries(garmin_test PRIVATE ${CURL_LIBRARIES})

sweattrails_test(
        wahoo_test
        test/WahooTest.cpp
        src/App/storage/Wahoo.cpp
        src/App/storage/Activity.cpp
        src/App/storage/Storage.cpp
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

sweattrails_test(download_test test/DownloadTest.cpp src/App/storage/Download.cpp)
target_include_directories(download_test PRIVATE src/App)

install(TARGETS Base Widget sweattrails fitexplore
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        BUNDLE DESTINATION bundle)

install(DIRECTORY share DESTINATION .)
