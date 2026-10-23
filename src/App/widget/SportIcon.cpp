/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <widget/SportIcon.h>

namespace ST {

using namespace ST::FIT;

std::string_view sport_icon(sport the_sport, std::optional<sub_sport> the_sub_sport)
{
    switch (the_sport) {
    case sport::Cycling:
    case sport::E_biking:
        if (the_sub_sport == sub_sport::mountain || the_sub_sport == sub_sport::e_bike_mountain) {
            return "icons8-cycling-mountain-bike-skin-type-2-96.png";
        }
        return "icons8-cycling-skin-type-2-96.png";
    case sport::Running:
        if (the_sub_sport == sub_sport::treadmill) {
            return "icons8-treadmill-skin-type-2-96.png";
        }
        return "icons8-running-skin-type-2-96.png";
    case sport::Walking:
        return "icons8-walking-skin-type-2-96.png";
    case sport::Hiking:
        return "icons8-trekking-skin-type-2-96.png";
    case sport::Swimming:
        return "icons8-swimming-skin-type-2-96.png";
    case sport::Multisport:
        // FIT has no triathlon sport: it records one as multisport.
        return "icons8-triathlon-skin-type-2-96.png";
    case sport::Windsurfing:
        return "icons8-windsurfing-skin-type-2-96.png";
    default:
        return "icons8-hyperactive-skin-type-3-96.png";
    }
}

}
