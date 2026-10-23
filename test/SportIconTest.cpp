/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

#include <widget/SportIcon.h>

#include <TestApp.h>

using namespace ST;
using namespace ST::FIT;

namespace {

namespace fs = std::filesystem;

fs::path const Icons = fs::path { SWEATTRAILS_SOURCE_DIR } / "share" / "icons";

void check_icon(sport the_sport, std::optional<sub_sport> the_sub_sport, std::string_view expected)
{
    auto const icon = sport_icon_file(the_sport, the_sub_sport);
    CHECK_EQ(icon, expected);
    // A typo in a name, or a path in front of it, and the icon isn't found.
    CHECK(fs::exists(Icons / icon));
}

void test_rides()
{
    check_icon(sport::Cycling, { }, "icons8-cycling-skin-type-2-96.png");
    check_icon(sport::Cycling, sub_sport::road, "icons8-cycling-skin-type-2-96.png");
    check_icon(sport::Cycling, sub_sport::mountain, "icons8-cycling-mountain-bike-skin-type-2-96.png");
    check_icon(sport::E_biking, { }, "icons8-cycling-skin-type-2-96.png");
    check_icon(sport::E_biking, sub_sport::e_bike_mountain, "icons8-cycling-mountain-bike-skin-type-2-96.png");
    // The table pairs both sports with both mountain sub-sports.
    check_icon(sport::Cycling, sub_sport::e_bike_mountain, "icons8-cycling-mountain-bike-skin-type-2-96.png");
    check_icon(sport::E_biking, sub_sport::mountain, "icons8-cycling-mountain-bike-skin-type-2-96.png");
    check_icon(sport::E_biking, sub_sport::road, "icons8-cycling-skin-type-2-96.png");
}

void test_runs()
{
    check_icon(sport::Running, { }, "icons8-running-skin-type-2-96.png");
    check_icon(sport::Running, sub_sport::treadmill, "icons8-treadmill-skin-type-2-96.png");
}

void test_other_sports()
{
    check_icon(sport::Walking, { }, "icons8-walking-skin-type-2-96.png");
    check_icon(sport::Hiking, { }, "icons8-trekking-skin-type-2-96.png");
    check_icon(sport::Swimming, { }, "icons8-swimming-skin-type-2-96.png");
    check_icon(sport::Multisport, { }, "icons8-triathlon-skin-type-2-96.png");
    check_icon(sport::Windsurfing, { }, "icons8-windsurfing-skin-type-2-96.png");
}

// The sub-sport only matters for the sport it belongs to.
void test_sub_sport_of_another_sport()
{
    check_icon(sport::Running, sub_sport::mountain, "icons8-running-skin-type-2-96.png");
    check_icon(sport::Cycling, sub_sport::treadmill, "icons8-cycling-skin-type-2-96.png");
}

void test_default()
{
    check_icon(sport::Rowing, { }, "icons8-hyperactive-skin-type-3-96.png");
    check_icon(sport::Generic, { }, "icons8-hyperactive-skin-type-3-96.png");
}

// Whatever the sport and sub-sport, the icon is one that's there.
void test_every_sport_has_an_icon()
{
    std::vector<sport> sports { };
#define S(V, N) sports.push_back(sport::V);
    SPORT(S)
#undef S
    std::vector<std::optional<sub_sport>> sub_sports { std::nullopt };
#define S(V, N) sub_sports.emplace_back(sub_sport::V);
    SUB_SPORT(S)
#undef S
    for (auto const the_sport : sports) {
        for (auto const &the_sub_sport : sub_sports) {
            CHECK(fs::exists(Icons / sport_icon_file(the_sport, the_sub_sport)));
        }
    }
}

// make-app-icon.sh makes them from the SVG; the build expects them in
// share/icons.
void test_app_icon_files_exist()
{
    CHECK(fs::exists(Icons / "sweattrails.svg"));
    CHECK(fs::exists(Icons / "sweattrails.png"));
    CHECK(fs::exists(Icons / "sweattrails.icns"));
}

}

int main()
{
    test_rides();
    test_runs();
    test_other_sports();
    test_sub_sport_of_another_sport();
    test_default();
    test_every_sport_has_an_icon();
    test_app_icon_files_exist();
    return ST::Test::test_result();
}
