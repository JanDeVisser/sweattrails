/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/storage/activity.zig. See Types.h for the assumptions
 * made about the modules that have not been translated yet.
 */

#pragma once

#include <optional>

#include <Date.h>
#include <Format.h>
#include <Logging.h>

#include <Coordinates.h>
#include <fit/FIT.h>

namespace ST::FIT {

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

#define SUB_SPORT(S)               \
    S(generic, 0)                  \
    S(treadmill, 1)                \
    S(street, 2)                   \
    S(trail, 3)                    \
    S(track, 4)                    \
    S(spin, 5)                     \
    S(indoor_cycling, 6)           \
    S(road, 7)                     \
    S(mountain, 8)                 \
    S(downhill, 9)                 \
    S(recumbent, 10)               \
    S(cyclocross, 11)              \
    S(hand_cycling, 12)            \
    S(track_cycling, 13)           \
    S(indoor_rowing, 14)           \
    S(elliptical, 15)              \
    S(stair_climbing, 16)          \
    S(lap_swimming, 17)            \
    S(open_water, 18)              \
    S(flexibility_training, 19)    \
    S(strength_training, 20)       \
    S(warm_up, 21)                 \
    S(match, 22)                   \
    S(exercise, 23)                \
    S(challenge, 24)               \
    S(indoor_skiing, 25)           \
    S(cardio_training, 26)         \
    S(indoor_walking, 27)          \
    S(e_bike_fitness, 28)          \
    S(bmx, 29)                     \
    S(casual_walking, 30)          \
    S(speed_walking, 31)           \
    S(bike_to_run_transition, 32)  \
    S(run_to_bike_transition, 33)  \
    S(swim_to_bike_transition, 34) \
    S(atv, 35)                     \
    S(motocross, 36)               \
    S(backcountry, 37)             \
    S(resort, 38)                  \
    S(rc_drone, 39)                \
    S(wingsuit, 40)                \
    S(whitewater, 41)              \
    S(skate_skiing, 42)            \
    S(yoga, 43)                    \
    S(pilates, 44)                 \
    S(indoor_running, 45)          \
    S(gravel_cycling, 46)          \
    S(e_bike_mountain, 47)         \
    S(commuting, 48)               \
    S(mixed_surface, 49)           \
    S(navigate, 50)                \
    S(track_me, 51)                \
    S(map, 52)                     \
    S(single_gas_diving, 53)       \
    S(multi_gas_diving, 54)        \
    S(gauge_diving, 55)            \
    S(apnea_diving, 56)            \
    S(apnea_hunting, 57)           \
    S(virtual_activity, 58)        \
    S(obstacle, 59)                \
    S(breathing, 62)               \
    S(sail_race, 65)               \
    S(ultra, 67)                   \
    S(indoor_climbing, 68)         \
    S(bouldering, 69)              \
    S(hiit, 70)                    \
    S(amrap, 73)                   \
    S(emom, 74)                    \
    S(tabata, 75)                  \
    S(pickleball, 84)              \
    S(padel, 85)                   \
    S(indoor_wheelchair_walk, 86)  \
    S(indoor_wheelchair_run, 87)   \
    S(indoor_hand_cycling, 88)     \
    S(squash, 94)                  \
    S(badminton, 95)               \
    S(racquetball, 96)             \
    S(table_tennis, 97)            \
    S(fly_canopy, 110)             \
    S(fly_paraglide, 111)          \
    S(fly_paramotor, 112)          \
    S(fly_pressurized, 113)        \
    S(fly_navigate, 114)           \
    S(fly_timer, 115)              \
    S(fly_altimeter, 116)          \
    S(fly_wx, 117)                 \
    S(fly_vfr, 118)                \
    S(fly_ifr, 119)                \
    S(all, 254)

enum class sub_sport : u8 {
    SUB_SPORT(ENUMVALUE_INT)
};

// How a device in a device_info message is connected to the one that
// recorded the file: local is one of its own parts, such as its GPS.
#define SOURCE_TYPE(S)         \
    S(ant, 0)                  \
    S(antplus, 1)              \
    S(bluetooth, 2)            \
    S(bluetooth_low_energy, 3) \
    S(wifi, 4)                 \
    S(local, 5)

enum class source_type : u8 {
    SOURCE_TYPE(ENUMVALUE_INT)
};

#define BATTERY_STATUS(S) \
    S(new_, 1)            \
    S(good, 2)            \
    S(ok, 3)              \
    S(low, 4)             \
    S(critical, 5)        \
    S(charging, 6)        \
    S(unknown, 7)

enum class battery_status : u8 {
    BATTERY_STATUS(ENUMVALUE_INT)
};

// A device_info's device_type, for an ANT+ device.
#define ANTPLUS_DEVICE_TYPE(S)        \
    S(antfs, 1)                       \
    S(bike_power, 11)                 \
    S(environment_sensor_legacy, 12)  \
    S(multi_sport_speed_distance, 15) \
    S(control, 16)                    \
    S(fitness_equipment, 17)          \
    S(blood_pressure, 18)             \
    S(geocache_node, 19)              \
    S(light_electric_vehicle, 20)     \
    S(env_sensor, 25)                 \
    S(racquet, 26)                    \
    S(control_hub, 27)                \
    S(muscle_oxygen, 31)              \
    S(shifting, 34)                   \
    S(bike_light_main, 35)            \
    S(bike_light_shared, 36)          \
    S(exd, 38)                        \
    S(bike_radar, 40)                 \
    S(bike_aero, 46)                  \
    S(weight_scale, 119)              \
    S(heart_rate, 120)                \
    S(bike_speed_cadence, 121)        \
    S(bike_cadence, 122)              \
    S(bike_speed, 123)                \
    S(stride_speed_distance, 124)

enum class antplus_device_type : u8 {
    ANTPLUS_DEVICE_TYPE(ENUMVALUE_INT)
};

// A device_info's device_type, for a Bluetooth LE device.
#define BLE_DEVICE_TYPE(S)   \
    S(connected_gps, 0)      \
    S(heart_rate, 1)         \
    S(bike_power, 2)         \
    S(bike_speed_cadence, 3) \
    S(bike_speed, 4)         \
    S(bike_cadence, 5)       \
    S(footpod, 6)            \
    S(bike_trainer, 7)

enum class ble_device_type : u8 {
    BLE_DEVICE_TYPE(ENUMVALUE_INT)
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
    std::optional<f32>           total_timer_time = { }; // Older files only have the elapsed time
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
    std::optional<f32>       total_timer_time = { }; // Older files only have the elapsed time
    std::optional<f32>       total_distance = { };
    std::optional<u16>       total_calories = { };
    std::optional<u16>       first_lap_index = { };
    std::optional<u16>       num_laps = { };
    std::optional<f32>       total_moving_time = { };
    std::optional<u16>       total_ascent = { };
    std::vector<record>      records = { };
    std::vector<lap>         laps = { };
};

struct activity {
    DateTime                timestamp;
    std::optional<f32>      total_timer_time = { };
    u16                     num_sessions;
    std::optional<DateTime> local_timestamp;
    std::vector<session>    sessions;
};

struct workout {
    std::optional<message_index> message_index = { };
    std::optional<sport>         sport = { };
    std::optional<sub_sport>     sub_sport = { };
    std::optional<std::string>   wkt_name = { };
};

// The device that recorded the file, one of its parts, or a sensor
// connected to it, by its index: 0 is the recording device. A file lists
// them at the start, and again during the activity and at the end.
struct device_info {
    device_index                  device_index { 0 };
    std::optional<DateTime>       timestamp = { };
    std::optional<u8>             device_type = { }; // Its meaning goes with source_type
    std::optional<manufacturer>   manufacturer = { };
    std::optional<u32>            serial_number = { };
    std::optional<u16>            product = { };
    std::optional<f32>            software_version = { };
    std::optional<battery_status> battery_status = { };
    std::optional<source_type>    source_type = { };
    std::optional<std::string>    product_name = { };
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
template<>
std::expected<workout, FITError> make_from_rec(FITDataRecord const &rec);
template<>
std::expected<device_info, FITError> make_from_rec(FITDataRecord const &rec);

template<>
std::ostream &format_record<mesg_num::activity>(std::ostream &out, FITDataRecord const &rec);
template<>
std::ostream &format_record<mesg_num::session>(std::ostream &out, FITDataRecord const &rec);
template<>
std::ostream &format_record<mesg_num::lap>(std::ostream &out, FITDataRecord const &rec);
template<>
std::ostream &format_record<mesg_num::record>(std::ostream &out, FITDataRecord const &rec);
template<>
std::ostream &format_record<mesg_num::workout>(std::ostream &out, FITDataRecord const &rec);
template<>
std::ostream &format_record<mesg_num::device_info>(std::ostream &out, FITDataRecord const &rec);

char const *tag(source_type t);
char const *tag(battery_status s);
// Nothing for a type FIT's profile doesn't have.
std::optional<antplus_device_type> antplus_device_type_of(u8 type);
std::optional<ble_device_type>     ble_device_type_of(u8 type);

inline char const *tag(FIT::sport s)
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

inline char const *tag(FIT::sub_sport s)
{
    switch (s) {
#undef S
#define S(V, I)             \
    case FIT::sub_sport::V: \
        return #V;
        SUB_SPORT(S)
#undef S
    default:
        UNREACHABLE();
    }
}

}

template<>
struct std::formatter<ST::FIT::sport> : std::formatter<std::string> {
    template<class FmtContext>
    FmtContext::iterator format(ST::FIT::sport const &val, FmtContext &ctx) const
    {
        std::ostringstream out;
        out << ST::FIT::tag(val);
        return std::ranges::copy(std::move(out).str(), ctx.out()).out;
    }
};

template<>
struct std::formatter<ST::FIT::sub_sport> : std::formatter<std::string> {
    template<class FmtContext>
    FmtContext::iterator format(ST::FIT::sub_sport const &val, FmtContext &ctx) const
    {
        std::ostringstream out;
        out << ST::FIT::tag(val);
        return std::ranges::copy(std::move(out).str(), ctx.out()).out;
    }
};
