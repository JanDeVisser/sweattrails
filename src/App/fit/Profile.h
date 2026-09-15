/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/storage/activity.zig. See Types.h for the assumptions
 * made about the modules that have not been translated yet.
 */

#include <optional>

#include <Coordinates.h>
#include <Date.h>
#include <Format.h>
#include <Logging.h>
#include <fit/FIT.h>

namespace ST {
namespace FIT {

//
// --------------------------------------------------------------------------
// (Enum) Types
// --------------------------------------------------------------------------
//

enum class display_measure : u8 {
    metric = 0,
    statute = 1,
    nautical = 2,
};

enum class intensity : u8 {
    active = 0,
    rest = 1,
    warmup = 2,
    cooldown = 3,
    recovery = 4,
    interval = 5,
    other = 6,
};

#define SPORT(S)                   \
    S(Generic, 0)                  \
    S(Running, 1)                  \
    S(Cycling, 2)                  \
    S(Transition, 3)               \
    S(Fitness_equipment, 4)        \
    S(Swimming, 5)                 \
    S(Basketball, 6)               \
    S(Soccer, 7)                   \
    S(Tennis, 8)                   \
    S(American_football, 9)        \
    S(Training, 10)                \
    S(Walking, 11)                 \
    S(Cross_country_skiing, 12)    \
    S(Alpine_skiing, 13)           \
    S(Snowboarding, 14)            \
    S(Rowing, 15)                  \
    S(Mountaineering, 16)          \
    S(Hiking, 17)                  \
    S(Multisport, 18)              \
    S(Paddling, 19)                \
    S(Flying, 20)                  \
    S(E_biking, 21)                \
    S(Motorcycling, 22)            \
    S(Boating, 23)                 \
    S(Driving, 24)                 \
    S(Golf, 25)                    \
    S(Hang_gliding, 26)            \
    S(Horseback_riding, 27)        \
    S(Hunting, 28)                 \
    S(Fishing, 29)                 \
    S(Inline_skating, 30)          \
    S(Rock_climbing, 31)           \
    S(Sailing, 32)                 \
    S(Ice_skating, 33)             \
    S(Sky_diving, 34)              \
    S(Snowshoeing, 35)             \
    S(Snowmobiling, 36)            \
    S(Stand_up_paddleboarding, 37) \
    S(Surfing, 38)                 \
    S(Wakeboarding, 39)            \
    S(Water_skiing, 40)            \
    S(Kayaking, 41)                \
    S(Rafting, 42)                 \
    S(Windsurfing, 43)             \
    S(Kitesurfing, 44)             \
    S(Tactical, 45)                \
    S(Jumpmaster, 46)              \
    S(Boxing, 47)                  \
    S(Floor_climbing, 48)          \
    S(Baseball, 49)                \
    S(Diving, 53)                  \
    S(Hiit, 62)                    \
    S(Racket, 64)                  \
    S(Wheelchair_push_walk, 65)    \
    S(Wheelchair_push_run, 66)     \
    S(Meditation, 67)              \
    S(Disc_golf, 69)               \
    S(Cricket, 71)                 \
    S(Rugby, 72)                   \
    S(Hockey, 73)                  \
    S(Lacrosse, 74)                \
    S(Volleyball, 75)              \
    S(Water_tubing, 76)            \
    S(Wakesurfing, 77)             \
    S(Mixed_martial_arts, 80)      \
    S(Snorkeling, 82)              \
    S(Dance, 83)                   \
    S(Jump_rope, 84)               \
    S(All, 254)

enum class sport {
    SPORT(ENUMVALUE_INT)
};

enum class sub_sport : u8 {
    generic = 0,
    treadmill = 1,
    street = 2,
    trail = 3,
    track = 4,
    spin = 5,
    indoor_cycling = 6,
    road = 7,
    mountain = 8,
    downhill = 9,
    recumbent = 10,
    cyclocross = 11,
    hand_cycling = 12,
    track_cycling = 13,
    indoor_rowing = 14,
    elliptical = 15,
    stair_climbing = 16,
    lap_swimming = 17,
    open_water = 18,
    flexibility_training = 19,
    strength_training = 20,
    warm_up = 21,
    match = 22,
    exercise = 23,
    challenge = 24,
    indoor_skiing = 25,
    cardio_training = 26,
    indoor_walking = 27,
    e_bike_fitness = 28,
    bmx = 29,
    casual_walking = 30,
    speed_walking = 31,
    bike_to_run_transition = 32,
    run_to_bike_transition = 33,
    swim_to_bike_transition = 34,
    atv = 35,
    motocross = 36,
    backcountry = 37,
    resort = 38,
    rc_drone = 39,
    wingsuit = 40,
    whitewater = 41,
    skate_skiing = 42,
    yoga = 43,
    pilates = 44,
    indoor_running = 45,
    gravel_cycling = 46,
    e_bike_mountain = 47,
    commuting = 48,
    mixed_surface = 49,
    navigate = 50,
    track_me = 51,
    map = 52,
    single_gas_diving = 53,
    multi_gas_diving = 54,
    gauge_diving = 55,
    apnea_diving = 56,
    apnea_hunting = 57,
    virtual_activity = 58,
    obstacle = 59,
    breathing = 62,
    sail_race = 65,
    ultra = 67,
    indoor_climbing = 68,
    bouldering = 69,
    hiit = 70,
    amrap = 73,
    emom = 74,
    tabata = 75,
    pickleball = 84,
    padel = 85,
    indoor_wheelchair_walk = 86,
    indoor_wheelchair_run = 87,
    indoor_hand_cycling = 88,
    squash = 94,
    badminton = 95,
    racquetball = 96,
    table_tennis = 97,
    fly_canopy = 110,
    fly_paraglide = 111,
    fly_paramotor = 112,
    fly_pressurized = 113,
    fly_navigate = 114,
    fly_timer = 115,
    fly_altimeter = 116,
    fly_wx = 117,
    fly_vfr = 118,
    fly_ifr = 119,
    all = 254,
};

// ==========================================================================
// ==========================================================================

struct record {
    DateTime                   timestamp;
    std::optional<Coordinates> position = { };
    std::optional<f32>         altitude = { };
    std::optional<u8>          heart_rate = { };
    std::optional<u8>          cadence = { };
    std::optional<f32>         distance = { };
    std::optional<f32>         speed = { };
    std::optional<u16>         power = { };
    std::optional<f32>         grade = { };
    std::optional<i8>          temperature = { };
    std::optional<u8>          left_right_balance = { };
    std::optional<u16>         calories = { };
};

struct lap {
    std::optional<message_index> message_index = { };
    DateTime                     timestamp;
    DateTime                     start_time;
    f32                          total_elapsed_time;
    f32                          total_timer_time;
    std::optional<f32>           total_distance = { };
    std::optional<u16>           total_calories = { };
    std::optional<sport>         sport = { };
    std::optional<sub_sport>     sub_sport = { };
    std::optional<f32>           total_moving_time = { };
};

struct session {
    DateTime                 timestamp;
    DateTime                 start_time;
    std::optional<sport>     sport = { };
    std::optional<sub_sport> sub_sport = { };
    f32                      total_elapsed_time;
    f32                      total_timer_time;
    std::optional<f32>       total_distance = { };
    std::optional<u16>       total_calories = { };
    std::optional<u16>       first_lap_index = { };
    std::optional<u16>       num_laps = { };
    std::optional<f32>       total_moving_time = { };
    std::vector<record>      records = { };
    std::vector<lap>         laps = { };
};

struct activity {
    DateTime                timestamp;
    f32                     total_timer_time;
    u16                     num_sessions;
    std::optional<DateTime> local_timestamp;
    std::vector<session>    sessions;
};

template<>
std::optional<FITError> on_load(FITDataRecord const &fitrec, record &rec);

template<>
std::expected<activity, FITError> make_from_rec(FITDataRecord const &rec);
template<>
std::expected<session, FITError> make_from_rec(FITDataRecord const &rec);
template<>
std::expected<lap, FITError> make_from_rec(FITDataRecord const &rec);
template<>
std::expected<record, FITError> make_from_rec(FITDataRecord const &rec);
}

template<>
inline char const *value_to_string(FIT::sport s)
{
    switch (s) {
#undef S
#define S(V, I)         \
    case FIT::sport::V: \
        return #V;
        SPORT(S)
#undef S
    default:
        UNREACHABLE();
    }
}

}
