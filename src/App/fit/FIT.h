#pragma once

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <expected>
#include <format>
#include <optional>
#include <ostream>
#include <print>
#include <sstream>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <Date.h>
#include <Expected.h>
#include <Logging.h>

namespace ST::FIT {

constexpr uint32_t FIT_TIMESTAMP_OFFSET = 631065600;

constexpr uint16_t FIT_MESG_RECORD = 20;
constexpr uint16_t FIT_MESG_FILE_ID = 0;
constexpr uint8_t  FIT_FIELD_TIMESTAMP = 253;
constexpr uint8_t  FIT_FIELD_TIME_CREATED = 4;

using device_index = uint8_t;
using message_index = uint16_t;
using left_right_balance = uint8_t;
using left_right_balance_100 = uint16_t;
using semicircles = float;

#define FIT_MESG_NUM(S)                    \
    S(file_id, 0)                          \
    S(capabilities, 1)                     \
    S(device_settings, 2)                  \
    S(user_profile, 3)                     \
    S(hrm_profile, 4)                      \
    S(sdm_profile, 5)                      \
    S(bike_profile, 6)                     \
    S(zones_target, 7)                     \
    S(hr_zone, 8)                          \
    S(power_zone, 9)                       \
    S(met_zone, 10)                        \
    S(sport, 12)                           \
    S(goal, 15)                            \
    S(session, 18)                         \
    S(lap, 19)                             \
    S(record, 20)                          \
    S(event, 21)                           \
    S(device_info, 23)                     \
    S(workout, 26)                         \
    S(workout_step, 27)                    \
    S(schedule, 28)                        \
    S(weight_scale, 30)                    \
    S(course, 31)                          \
    S(course_point, 32)                    \
    S(totals, 33)                          \
    S(activity, 34)                        \
    S(software, 35)                        \
    S(file_capabilities, 37)               \
    S(mesg_capabilities, 38)               \
    S(field_capabilities, 39)              \
    S(file_creator, 49)                    \
    S(blood_pressure, 51)                  \
    S(speed_zone, 53)                      \
    S(monitoring, 55)                      \
    S(training_file, 72)                   \
    S(hrv, 78)                             \
    S(ant_rx, 80)                          \
    S(ant_tx, 81)                          \
    S(ant_channel_id, 82)                  \
    S(length, 101)                         \
    S(monitoring_info, 103)                \
    S(pad, 105)                            \
    S(slave_device, 106)                   \
    S(connectivity, 127)                   \
    S(weather_conditions, 128)             \
    S(weather_alert, 129)                  \
    S(cadence_zone, 131)                   \
    S(hr, 132)                             \
    S(segment_lap, 142)                    \
    S(memo_glob, 145)                      \
    S(segment_id, 148)                     \
    S(segment_leaderboard_entry, 149)      \
    S(segment_point, 150)                  \
    S(segment_file, 151)                   \
    S(workout_session, 158)                \
    S(watchface_settings, 159)             \
    S(gps_metadata, 160)                   \
    S(camera_event, 161)                   \
    S(timestamp_correlation, 162)          \
    S(gyroscope_data, 164)                 \
    S(accelerometer_data, 165)             \
    S(three_d_sensor_calibration, 167)     \
    S(video_frame, 169)                    \
    S(obdii_data, 174)                     \
    S(nmea_sentence, 177)                  \
    S(aviation_attitude, 178)              \
    S(video, 184)                          \
    S(video_title, 185)                    \
    S(video_description, 186)              \
    S(video_clip, 187)                     \
    S(ohr_settings, 188)                   \
    S(exd_screen_configuration, 200)       \
    S(exd_data_field_configuration, 201)   \
    S(exd_data_concept_configuration, 202) \
    S(field_description, 206)              \
    S(developer_data_id, 207)              \
    S(magnetometer_data, 208)              \
    S(barometer_data, 209)                 \
    S(one_d_sensor_calibration, 210)       \
    S(monitoring_hr_data, 211)             \
    S(time_in_zone, 216)                   \
    S(set, 225)                            \
    S(stress_level, 227)                   \
    S(max_met_data, 229)                   \
    S(dive_settings, 258)                  \
    S(dive_gas, 259)                       \
    S(dive_alarm, 262)                     \
    S(exercise_title, 264)                 \
    S(dive_summary, 268)                   \
    S(spo2_data, 269)                      \
    S(sleep_level, 275)                    \
    S(jump, 285)                           \
    S(aad_accel_features, 289)             \
    S(beat_intervals, 290)                 \
    S(respiration_rate, 297)               \
    S(hsa_accelerometer_data, 302)         \
    S(hsa_step_data, 304)                  \
    S(hsa_spo2_data, 305)                  \
    S(hsa_stress_data, 306)                \
    S(hsa_respiration_data, 307)           \
    S(hsa_heart_rate_data, 308)            \
    S(split, 312)                          \
    S(split_summary, 313)                  \
    S(hsa_body_battery_data, 314)          \
    S(hsa_event, 315)                      \
    S(climb_pro, 317)                      \
    S(tank_update, 319)                    \
    S(tank_summary, 323)                   \
    S(sleep_assessment, 346)               \
    S(hrv_status_summary, 370)             \
    S(hrv_value, 371)                      \
    S(raw_bbi, 372)                        \
    S(device_aux_battery_info, 375)        \
    S(hsa_gyroscope_data, 376)             \
    S(chrono_shot_session, 387)            \
    S(chrono_shot_data, 388)               \
    S(hsa_configuration_data, 389)         \
    S(dive_apnea_alarm, 393)               \
    S(skin_temp_overnight, 398)            \
    S(hsa_wrist_temperature_data, 409)     \
    S(vendor, 0xFF00)

enum class mesg_num : uint16_t {
#undef S
#define S(M, N) M = N,
    FIT_MESG_NUM(S)
#undef S
};

#define FIT_FILE_TYPE(S)     \
    S(device, 1)             \
    S(settings, 2)           \
    S(sport, 3)              \
    S(activity, 4)           \
    S(workout, 5)            \
    S(course, 6)             \
    S(schedules, 7)          \
    S(weight, 9)             \
    S(totals, 10)            \
    S(goals, 11)             \
    S(blood_pressure, 14)    \
    S(monitoring_a, 15)      \
    S(activity_summary, 20)  \
    S(monitoring_daily, 28)  \
    S(monitoring_b, 32)      \
    S(segment, 34)           \
    S(segment_list, 35)      \
    S(exd_configuration, 40) \
    S(mfg_0, 0xF7)           \
    S(mfg_1, 0xF8)           \
    S(mfg_2, 0xF9)           \
    S(mfg_3, 0xFA)           \
    S(mfg_4, 0xFB)           \
    S(mfg_5, 0xFC)           \
    S(mfg_6, 0xFD)           \
    S(mfg_7, 0xFE)

enum class file_type : u8 {
#undef S
#define S(FT, N) FT = N,
    FIT_FILE_TYPE(S)
#undef S
};

#define FIT_MANUFACTURER(S)         \
    S(garmin, 1)                    \
    S(garmin_fr405_antfs, 2)        \
    S(zephyr, 3)                    \
    S(dayton, 4)                    \
    S(idt, 5)                       \
    S(srm, 6)                       \
    S(quarq, 7)                     \
    S(ibike, 8)                     \
    S(saris, 9)                     \
    S(spark_hk, 10)                 \
    S(tanita, 11)                   \
    S(echowell, 12)                 \
    S(dynastream_oem, 13)           \
    S(nautilus, 14)                 \
    S(dynastream, 15)               \
    S(timex, 16)                    \
    S(metrigear, 17)                \
    S(xelic, 18)                    \
    S(beurer, 19)                   \
    S(cardiosport, 20)              \
    S(a_and_d, 21)                  \
    S(hmm, 22)                      \
    S(suunto, 23)                   \
    S(thita_elektronik, 24)         \
    S(gpulse, 25)                   \
    S(clean_mobile, 26)             \
    S(pedal_brain, 27)              \
    S(peaksware, 28)                \
    S(saxonar, 29)                  \
    S(lemond_fitness, 30)           \
    S(dexcom, 31)                   \
    S(wahoo_fitness, 32)            \
    S(octane_fitness, 33)           \
    S(archinoetics, 34)             \
    S(the_hurt_box, 35)             \
    S(citizen_systems, 36)          \
    S(magellan, 37)                 \
    S(osynce, 38)                   \
    S(holux, 39)                    \
    S(concept2, 40)                 \
    S(shimano, 41)                  \
    S(one_giant_leap, 42)           \
    S(ace_sensor, 43)               \
    S(brim_brothers, 44)            \
    S(xplova, 45)                   \
    S(perception_digital, 46)       \
    S(bf1systems, 47)               \
    S(pioneer, 48)                  \
    S(spantec, 49)                  \
    S(metalogics, 50)               \
    S(_4iiiis, 51)                  \
    S(seiko_epson, 52)              \
    S(seiko_epson_oem, 53)          \
    S(ifor_powell, 54)              \
    S(maxwell_guider, 55)           \
    S(star_trac, 56)                \
    S(breakaway, 57)                \
    S(alatech_technology_ltd, 58)   \
    S(mio_technology_europe, 59)    \
    S(rotor, 60)                    \
    S(geonaute, 61)                 \
    S(id_bike, 62)                  \
    S(specialized, 63)              \
    S(wtek, 64)                     \
    S(physical_enterprises, 65)     \
    S(north_pole_engineering, 66)   \
    S(bkool, 67)                    \
    S(cateye, 68)                   \
    S(stages_cycling, 69)           \
    S(sigmasport, 70)               \
    S(tomtom, 71)                   \
    S(peripedal, 72)                \
    S(wattbike, 73)                 \
    S(moxy, 76)                     \
    S(ciclosport, 77)               \
    S(powerbahn, 78)                \
    S(acorn_projects_aps, 79)       \
    S(lifebeam, 80)                 \
    S(bontrager, 81)                \
    S(wellgo, 82)                   \
    S(scosche, 83)                  \
    S(magura, 84)                   \
    S(woodway, 85)                  \
    S(elite, 86)                    \
    S(nielsen_kellerman, 87)        \
    S(dk_city, 88)                  \
    S(tacx, 89)                     \
    S(direction_technology, 90)     \
    S(magtonic, 91)                 \
    S(_1partcarbon, 92)             \
    S(inside_ride_technologies, 93) \
    S(sound_of_motion, 94)          \
    S(stryd, 95)                    \
    S(icg, 96)                      \
    S(MiPulse, 97)                  \
    S(bsx_athletics, 98)            \
    S(look, 99)                     \
    S(campagnolo_srl, 100)          \
    S(body_bike_smart, 101)         \
    S(praxisworks, 102)             \
    S(limits_technology, 103)       \
    S(topaction_technology, 104)    \
    S(cosinuss, 105)                \
    S(fitcare, 106)                 \
    S(magene, 107)                  \
    S(giant_manufacturing_co, 108)  \
    S(tigrasport, 109)              \
    S(salutron, 110)                \
    S(technogym, 111)               \
    S(bryton_sensors, 112)          \
    S(latitude_limited, 113)        \
    S(soaring_technology, 114)      \
    S(igpsport, 115)                \
    S(thinkrider, 116)              \
    S(gopher_sport, 117)            \
    S(waterrower, 118)              \
    S(orangetheory, 119)            \
    S(inpeak, 120)                  \
    S(kinetic, 121)                 \
    S(johnson_health_tech, 122)     \
    S(polar_electro, 123)           \
    S(seesense, 124)                \
    S(nci_technology, 125)          \
    S(iqsquare, 126)                \
    S(leomo, 127)                   \
    S(ifit_com, 128)                \
    S(coros_byte, 129)              \
    S(versa_design, 130)            \
    S(chileaf, 131)                 \
    S(cycplus, 132)                 \
    S(gravaa_byte, 133)             \
    S(sigeyi, 134)                  \
    S(coospo, 135)                  \
    S(geoid, 136)                   \
    S(bosch, 137)                   \
    S(kyto, 138)                    \
    S(kinetic_sports, 139)          \
    S(decathlon_byte, 140)          \
    S(tq_systems, 141)              \
    S(tag_heuer, 142)               \
    S(keiser_fitness, 143)          \
    S(zwift_byte, 144)              \
    S(porsche_ep, 145)              \
    S(blackbird, 146)               \
    S(meilan_byte, 147)             \
    S(ezon, 148)                    \
    S(laisi, 149)                   \
    S(myzone, 150)                  \
    S(development, 255)             \
    S(healthandlife, 257)           \
    S(lezyne, 258)                  \
    S(scribe_labs, 259)             \
    S(zwift, 260)                   \
    S(watteam, 261)                 \
    S(recon, 262)                   \
    S(favero_electronics, 263)      \
    S(dynovelo, 264)                \
    S(strava, 265)                  \
    S(precor, 266)                  \
    S(bryton, 267)                  \
    S(sram, 268)                    \
    S(navman, 269)                  \
    S(cobi, 270)                    \
    S(spivi, 271)                   \
    S(mio_magellan, 272)            \
    S(evesports, 273)               \
    S(sensitivus_gauge, 274)        \
    S(podoon, 275)                  \
    S(life_time_fitness, 276)       \
    S(falco_e_motors, 277)          \
    S(minoura, 278)                 \
    S(cycliq, 279)                  \
    S(luxottica, 280)               \
    S(trainer_road, 281)            \
    S(the_sufferfest, 282)          \
    S(fullspeedahead, 283)          \
    S(virtualtraining, 284)         \
    S(feedbacksports, 285)          \
    S(omata, 286)                   \
    S(vdo, 287)                     \
    S(magneticdays, 288)            \
    S(hammerhead, 289)              \
    S(kinetic_by_kurt, 290)         \
    S(shapelog, 291)                \
    S(dabuziduo, 292)               \
    S(jetblack, 293)                \
    S(coros, 294)                   \
    S(virtugo, 295)                 \
    S(velosense, 296)               \
    S(cycligentinc, 297)            \
    S(trailforks, 298)              \
    S(mahle_ebikemotion, 299)       \
    S(nurvv, 300)                   \
    S(microprogram, 301)            \
    S(zone5cloud, 302)              \
    S(greenteg, 303)                \
    S(yamaha_motors, 304)           \
    S(whoop, 305)                   \
    S(gravaa, 306)                  \
    S(onelap, 307)                  \
    S(monark_exercise, 308)         \
    S(form, 309)                    \
    S(decathlon, 310)               \
    S(syncros, 311)                 \
    S(heatup, 312)                  \
    S(cannondale, 313)              \
    S(true_fitness, 314)            \
    S(RGT_cycling, 315)             \
    S(vasa, 316)                    \
    S(race_republic, 317)           \
    S(fazua, 318)                   \
    S(oreka_training, 319)          \
    S(lsec, 320)                    \
    S(lululemon_studio, 321)        \
    S(shanyue, 322)                 \
    S(spinning_mda, 323)            \
    S(hilldating, 324)              \
    S(aero_sensor, 325)             \
    S(nike, 326)                    \
    S(magicshine, 327)              \
    S(ictrainer, 328)               \
    S(absolute_cycling, 329)        \
    S(actigraphcorp, 5759)

enum class manufacturer : u16 {
#undef S
#define S(V, N) V = N,
    FIT_MANUFACTURER(S)
#undef S
};

#define FITARCHITECTURE(S) \
    S(LittleEndian, 0)     \
    S(BigEndian, 1)

enum class FITArchitecture : uint8_t {
#undef S
#define S(A, N) A = N,
    FITARCHITECTURE(S)
#undef S
};

char const *tag(mesg_num m);
char const *tag(file_type t);
char const *tag(manufacturer m);
char const *tag(FITArchitecture a);

template<typename T>
T fit_read(FITArchitecture, std::string_view)
{
    std::unreachable();
}

template<std::integral T>
T fit_read(FITArchitecture arch, std::string_view data)
{
    uint8_t buf[sizeof(T)];
    memcpy(buf, data.data(), sizeof(T));
    T const value = *(reinterpret_cast<T *>(buf));
    switch (arch) {
    case FITArchitecture::LittleEndian: {
        if constexpr (std::endian::native == std::endian::big)
            return std::byteswap(value);
        return value;
    } break;
    case FITArchitecture::BigEndian: {
        if constexpr (std::endian::native == std::endian::little)
            return std::byteswap(value);
        return value;
    } break;
    }
}

template<>
inline float32 fit_read(FITArchitecture arch, std::string_view data)
{
    uint8_t buf[sizeof(u32)];
    memcpy(buf, data.data(), sizeof(u32));
    u32 bin_value = *(reinterpret_cast<u32 *>(buf));
    switch (arch) {
    case FITArchitecture::LittleEndian: {
        if constexpr (std::endian::native == std::endian::big) {
            u32 swapped = std::byteswap(bin_value);
            return *(reinterpret_cast<float32 *>(&swapped));
        }
        return *(reinterpret_cast<float32 *>(&bin_value));
    } break;
    case FITArchitecture::BigEndian: {
        if constexpr (std::endian::native == std::endian::little) {
            u32 swapped = std::byteswap(bin_value);
            return *(reinterpret_cast<float32 *>(&swapped));
        }
        return *(reinterpret_cast<float32 *>(&bin_value));
    } break;
    }
}

template<>
inline float64 fit_read(FITArchitecture arch, std::string_view data)
{
    uint8_t buf[sizeof(u64)];
    memcpy(buf, data.data(), sizeof(u64));
    u64 bin_value = *(reinterpret_cast<u64 *>(buf));
    switch (arch) {
    case FITArchitecture::LittleEndian: {
        if constexpr (std::endian::native == std::endian::big) {
            u64 swapped = std::byteswap(bin_value);
            return *(reinterpret_cast<float64 *>(&swapped));
        }
        return *(reinterpret_cast<float64 *>(&bin_value));
    } break;
    case FITArchitecture::BigEndian: {
        if constexpr (std::endian::native == std::endian::little) {
            u64 swapped = std::byteswap(bin_value);
            return *(reinterpret_cast<float64 *>(&swapped));
        }
        return *(reinterpret_cast<float64 *>(&bin_value));
    } break;
    }
}

struct RecordHeader {
    struct Definition {
        u8   local_message_type;
        bool has_developer_data;
    };
    struct CompressedTimestamp {
        u8 local_message_type;
        u8 time_offset;
    };

    using HeaderData = std::variant<u8, Definition, CompressedTimestamp>;

    HeaderData header;
    RecordHeader(u8 header_byte);
};

#define FITBASETYPE(S)                \
    S(enum_, 0x00, u8)                \
    S(sint8, 0x01, i8)                \
    S(uint8, 0x02, u8)                \
    S(sint16, 0x03, i16)              \
    S(uint16, 0x04, u16)              \
    S(sint32, 0x05, i32)              \
    S(uint32, 0x06, u32)              \
    S(string, 0x07, std::string_view) \
    S(float32, 0x08, float32)         \
    S(float64, 0x09, float64)         \
    S(uint8z, 0x0A, u8)               \
    S(uint16z, 0x0B, u16)             \
    S(uint32z, 0x0C, u32)             \
    S(byte, 0x0D, std::string_view)   \
    S(sint64, 0x0E, i64)              \
    S(uint64, 0x0F, u64)              \
    S(uint64z, 0x1F, u64)

#define SIMPLE_FITBASETYPE(S) \
    S(enum_, u8)              \
    S(sint8, i8)              \
    S(uint8, u8)              \
    S(sint16, i16)            \
    S(uint16, u16)            \
    S(sint32, i32)            \
    S(uint32, u32)            \
    S(float32, float32)       \
    S(float64, float64)       \
    S(uint8z, u8)             \
    S(uint16z, u16)           \
    S(uint32z, u32)           \
    S(sint64, i64)            \
    S(uint64, u64)            \
    S(uint64z, u64)

enum class FITBaseType : u8 {
#undef S
#define S(Value, Code, Type) Value = Code,
    FITBASETYPE(S)
#undef S
};

char const *tag(FITBaseType t);

struct LocalDefinition {
    struct DeveloperField {
        u8          field_num = 0;
        u8          size = 0;
        u8          developer_data_index = 0;
        FITBaseType base_type = FITBaseType::uint8;

        bool operator==(DeveloperField const &other) const
        {
            return field_num == other.field_num && size == other.size && developer_data_index == other.developer_data_index;
        }
        bool operator!=(DeveloperField const &other) const = default;
    };

    struct DefinitionField {
        u8          field_num = 0;
        u8          size = 0;
        FITBaseType base_type = FITBaseType::uint8;

        bool operator==(DefinitionField const &other) const
        {
            return field_num == other.field_num && size == other.size && base_type == other.base_type;
        }
        bool operator!=(DefinitionField const &other) const = default;
    };

    FITArchitecture arch = FITArchitecture::LittleEndian;
    u16             global_msg_num = 0;
    u8              num_fields = 0;
    DefinitionField fields[256];
    u8              num_developer_fields = 0;
    DeveloperField  developer_fields[256];
    size_t          record_size = 0;
    u32             current_timestamp = 0;

    bool operator==(LocalDefinition const &other)
    {
        if (global_msg_num != other.global_msg_num || arch != other.arch || num_fields != other.num_fields || num_developer_fields != other.num_developer_fields || record_size != other.record_size) {
            return false;
        }
        for (size_t ix = 0; ix < num_fields; ++ix) {
            if (fields[ix] != other.fields[ix]) {
                return false;
            }
        }
        for (size_t ix = 0; ix < num_developer_fields; ++ix) {
            if (developer_fields[ix] != other.developer_fields[ix]) {
                return false;
            }
        }
        return true;
    }
};

struct FITDataField {
    union Options {
#undef S
#define S(Value, Code, Type) Type Value;
        FITBASETYPE(S)
#undef S
    };

    FITBaseType            type;
    std::optional<Options> value;
};

}

template<>
struct std::formatter<ST::FIT::FITDataField> : std::formatter<std::string> {
    template<class FmtContext>
    FmtContext::iterator format(ST::FIT::FITDataField const &val, FmtContext &ctx) const
    {
        std::ostringstream out;
        if (!val.value) {
            out << "(empty)";
        } else {
            switch (val.type) {
            case ST::FIT::FITBaseType::enum_:
                out << static_cast<int>(val.value->enum_);
                break;
            case ST::FIT::FITBaseType::sint8:
                out << static_cast<int>(val.value->sint8);
                break;
            case ST::FIT::FITBaseType::uint8:
                out << static_cast<int>(val.value->uint8);
                break;
            case ST::FIT::FITBaseType::sint16:
                out << val.value->sint16;
                break;
            case ST::FIT::FITBaseType::uint16:
                out << val.value->uint16;
                break;
            case ST::FIT::FITBaseType::sint32:
                out << val.value->sint32;
                break;
            case ST::FIT::FITBaseType::uint32:
                out << val.value->uint32;
                break;
            case ST::FIT::FITBaseType::string:
                out << val.value->string;
                break;
            case ST::FIT::FITBaseType::float32:
                out << val.value->float32;
                break;
            case ST::FIT::FITBaseType::float64:
                out << val.value->float64;
                break;
            case ST::FIT::FITBaseType::uint8z:
                out << val.value->uint8z;
                break;
            case ST::FIT::FITBaseType::uint16z:
                out << val.value->uint16z;
                break;
            case ST::FIT::FITBaseType::uint32z:
                out << val.value->uint32z;
                break;
            case ST::FIT::FITBaseType::byte: {
                out << ios::hex;
                for (auto b : val.value->byte) {
                    out << b << " ";
                }
                out << ios::dec;
                out << val.value->byte;
            } break;
            case ST::FIT::FITBaseType::sint64:
                out << val.value->sint64;
                break;
            case ST::FIT::FITBaseType::uint64:
                out << val.value->uint64;
                break;
            case ST::FIT::FITBaseType::uint64z:
                out << val.value->uint64z;
                break;
            }
        }
        out << " [" << tag(val.type) << ']';
        return std::ranges::copy(std::move(out).str(), ctx.out()).out;
    }
};

namespace ST::FIT {

enum class MetaDataUnits {
    None,
    Semicircles,
    DateTime,
    Lat,
    Long,
};

#define FITERROR(S)               \
    S(LargeHeaderSizeUnsupported) \
    S(HeaderMagicMissing)         \
    S(UnknownDeveloperField)      \
    S(IOError)

enum class FITError {
#undef S
#define S(E) E,
    FITERROR(S)
#undef S
};

char const *tag(FITError e);

struct FITDataRecord {
    struct FITFile           &file;
    size_t                    definition;
    mesg_num                  mesg_num;
    u16                       mesg_number;
    std::vector<FITDataField> fields;

    std::optional<FITDataField> get_field(u8 num) const;
};

struct FieldMetaData {
    char const    name[32];
    u8            num = 0xFF;
    float32       scale = 1;
    float32       offset = 0;
    MetaDataUnits units { MetaDataUnits::None };
    bool          optional { true };
    FITBaseType   base_type { FITBaseType::uint16 };
    size_t        fld_offset;
};

struct TypeMetaData {
    mesg_num      mesg_num;
    size_t        num_fields;
    FieldMetaData fields[50];
};

struct file_id {
    file_type                       type;
    std::optional<manufacturer>     manufacturer = { };
    std::optional<u16>              product = { };
    std::optional<u32>              serial_number = { };
    DateTime                        time_created = { };
    std::optional<u16>              number = { };
    std::optional<std::string_view> product_name = { };
};

struct developer_data_id {
    std::optional<std::string_view> developer_id = { };
    std::optional<std::string_view> application_id = { };
    std::optional<manufacturer>     manufacturer_id = { };
    u8                              developer_data_index = 0;
    std::optional<u32>              application_version = { };
};

struct field_description {
    u8                              developer_data_index;
    u8                              field_definition_number;
    u8                              fit_base_type_id;
    std::optional<std::string_view> field_name = { };
    std::optional<std::string_view> units = { };
    std::optional<u8>               native_field_num = { };
};

template<typename T>
std::expected<T, FITError> make_from_rec(FITDataRecord const &)
{
    std::println("make_from_rec<{}> not implemented", typeid(T).name());
    std::unreachable();
}

template<mesg_num N>
std::ostream &format_message(std::ostream &out, FITDataRecord const &rec);

template<>
std::expected<file_id, FITError> make_from_rec(FITDataRecord const &rec);
template<>
std::expected<developer_data_id, FITError> make_from_rec(FITDataRecord const &rec);
template<>
std::expected<field_description, FITError> make_from_rec(FITDataRecord const &rec);

struct FITFile {
    struct Header {
        u8  header_size = 0;
        u8  protocol_version = 0;
        u16 profile_version = 0;
        u32 data_size = 0;
        u16 crc = 0;
    };

    Header                         header;
    size_t                         current_definitions[16];
    std::vector<LocalDefinition>   definitions;
    std::vector<developer_data_id> developer_data;
    std::vector<field_description> developer_field;
    std::vector<FITDataRecord>     data_records;
    std::vector<std::string>       arrays;
    std::string const              buffer;
    size_t                         offset { 0 };
    size_t                         total_read { 0 };
    size_t                         current { 0 };
    bool                           verbose { false };

    FITFile(std::string_view const &buffer);
    ~FITFile();

    static std::expected<FITFile, FITError>               read(std::string_view file_name);
    std::string_view                                      read_slice(size_t count);
    void                                                  skip(size_t count);
    std::expected<void, FITError>                         read_header();
    std::expected<FITDataField, FITError>                 read_field(FITArchitecture arch, FITBaseType base_type, size_t size);
    std::expected<FITDataField, FITError>                 read_field(LocalDefinition const &def, size_t num);
    std::expected<FITDataField, FITError>                 read_developer_field(LocalDefinition const &def, size_t num);
    std::expected<FITDataRecord, FITError>                read_data_record(RecordHeader record_header);
    void                                                  rewind();
    bool                                                  fully_read();
    bool                                                  exhausted();
    std::optional<FITDataRecord>                          current_record();
    std::expected<void, FITError>                         read();
    std::expected<std::optional<FITDataRecord>, FITError> find_first(mesg_num mesg);
    std::expected<std::optional<FITDataRecord>, FITError> find_next(mesg_num mesg);
    std::expected<std::optional<FITDataRecord>, FITError> first();
    std::expected<std::optional<FITDataRecord>, FITError> next();

    template<typename T>
    T read_value(FITArchitecture arch)
    {
        auto buf = read_slice(sizeof(T));
        return fit_read<T>(arch, buf);
    }

    template<typename Predicate>
        requires std::invocable<Predicate, FITDataRecord const &> || std::same_as<Predicate, std::nullptr_t>
    std::expected<void, FITError> read_until(Predicate const &predicate)
    {
        // std::println("read_until data_records {} current {}", data_records.size(), current);
        for (++current; current < data_records.size(); ++current) {
            auto const &rec = data_records[current];
            if constexpr (std::is_same_v<std::nullptr_t, Predicate>) {
                continue;
            } else {
                if (!predicate(rec)) {
                    return { };
                }
            }
        }

        if (header.header_size > 0 and total_read >= header.data_size) {
            return { };
        }
        if (header.header_size == 0) {
            if (auto err_maybe = read_header(); !err_maybe) {
                std::println("Error reading file header");
                return std::unexpected(err_maybe.error());
            }
        }

        u32 mesg_count = 0;
        while (total_read < header.data_size) {
            auto hdr = read_slice(1);
            if (hdr.empty()) {
                break;
            }
            RecordHeader record_header(hdr[0]);

            auto read_msg = [this, predicate, record_header, &mesg_count]() -> std::expected<bool, FITError> {
                auto const rec = TRY_EVAL(read_data_record(record_header));
                mesg_count += 1;
                current = data_records.size() - 1;
                switch (rec.mesg_num) {
                case mesg_num::developer_data_id: {
                    auto const msg_maybe = make_from_rec<developer_data_id>(rec);
                    if (!msg_maybe) {
                        std::println("Error making developer_data_id: {}", tag(msg_maybe.error()));
                        return std::unexpected(msg_maybe.error());
                    }
                    developer_data.emplace_back(TRY_EVAL(make_from_rec<developer_data_id>(rec)));
                } break;
                case mesg_num::field_description: {
                    auto const msg_maybe = make_from_rec<field_description>(rec);
                    if (!msg_maybe) {
                        std::println("Error making field_description: {}", tag(msg_maybe.error()));
                        return std::unexpected(msg_maybe.error());
                    }
                    developer_field.emplace_back(msg_maybe.value());
                } break;
                default:
                    break;
                }
                if constexpr (!std::is_same_v<std::nullptr_t, Predicate>) {
                    return predicate(rec);
                } else {
                    return true;
                }
            };

            auto read_def = [this, record_header, &mesg_count](RecordHeader::Definition const &def_header) -> std::expected<bool, FITError> {
                auto                  def_buffer = read_slice(5);
                FITArchitecture const arch = static_cast<FITArchitecture>(def_buffer[1]);
                LocalDefinition       def = {
                    .arch = arch,
                    .global_msg_num = fit_read<u16>(arch, def_buffer.substr(2, 2)),
                    .num_fields = static_cast<u8>(def_buffer[4]),
                    .fields = { },
                    .num_developer_fields = 0,
                    .developer_fields = { },
                    .record_size = 0,
                    .current_timestamp = 0,
                };
                if (verbose) {
                    std::println("Definition: local_mesg_num {} global_mesg_num {} {} arch {}",
                        def_header.local_message_type, def.global_msg_num, tag(static_cast<mesg_num>(def.global_msg_num)), tag(def.arch));
                }
                if (def.num_fields > 0) {
                    if (verbose)
                        std::println("  Fields ({}):", def.num_fields);
                    for (auto i = 0; i < std::min(static_cast<int>(def.num_fields), 256); ++i) {
                        def_buffer = read_slice(3);
                        auto &fld = def.fields[i];
                        fld.field_num = def_buffer[0];
                        fld.size = def_buffer[1];
                        fld.base_type = static_cast<FITBaseType>(def_buffer[2] & 0x1F);
                        def.record_size += def_buffer[1];
                        if (verbose)
                            std::println("  {}. field_num {} size {} type {}", i, fld.field_num, fld.size, tag(fld.base_type));
                    }
                } else {
                    if (verbose)
                        std::println("  No fields");
                }

                if (def_header.has_developer_data) {
                    def_buffer = read_slice(1);
                    def.num_developer_fields = def_buffer[0];
                    if (verbose)
                        std::println("  Developer Fields ({}):", def.num_developer_fields);
                    for (auto i = 0; i < def.num_developer_fields && i < 256; ++i) {
                        def_buffer = read_slice(3);
                        auto &fld = def.developer_fields[i];
                        fld.field_num = def_buffer[0];
                        fld.size = def_buffer[1];
                        fld.developer_data_index = def_buffer[2];
                        auto found = false;
                        for (auto const &dev_fld : developer_field) {
                            if (def_buffer[2] == dev_fld.developer_data_index && def_buffer[0] == dev_fld.field_definition_number) {
                                fld.base_type = static_cast<FITBaseType>(dev_fld.fit_base_type_id & 0x0F);
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            std::println("Unknown developer field encountered");
                            return std::unexpected(FITError::UnknownDeveloperField);
                        }
                        if (verbose)
                            std::println("  {}. developer {} field_num {} size {} type {}", i, fld.developer_data_index, fld.field_num, fld.size, tag(fld.base_type));
                    };
                    def.record_size += def_buffer[1];
                } else {
                    if (verbose)
                        std::println("  No developer fields");
                }

                auto found { false };
                for (auto ix = 0; ix < definitions.size(); ++ix) {
                    auto const &defined = definitions[ix];
                    if (defined == def) {
                        current_definitions[def_header.local_message_type] = ix;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    definitions.emplace_back(def);
                    current_definitions[def_header.local_message_type] = definitions.size() - 1;
                }
                if (0) {
                    std::print("  current_definitions: ");
                    for (auto ix = 0; ix < 16; ++ix) {
                        if (current_definitions[ix] >= definitions.size()) {
                            continue;
                        }
                        std::print("{}->{} {} ",
                            current_definitions[ix],
                            definitions[current_definitions[ix]].global_msg_num,
                            tag(static_cast<mesg_num>(definitions[current_definitions[ix]].global_msg_num)));
                    }
                    std::println();
                }
                return false;
            };

            std::expected<bool, FITError> read_record = std::visit(
                overloaded {
                    [this, &read_msg](u8) -> std::expected<bool, FITError> {
                        return TRY_EVAL(read_msg());
                    },
                    [&read_msg](RecordHeader::CompressedTimestamp const &) -> std::expected<bool, FITError> {
                        return TRY_EVAL(read_msg());
                    },
                    [&read_def](RecordHeader::Definition const &def) -> std::expected<bool, FITError> {
                        return TRY_EVAL(read_def(def));
                    },
                },
                record_header.header);
            if (!read_record.has_value()) {
                std::println("read_record failed");
                return std::unexpected(read_record.error());
            }
            if (read_record.value()) {
                break;
            }
        }
        if (total_read >= header.data_size) {
            // TODO check CRC
        }
        return { };
    }
};

template<typename FldType, FieldMetaData def>
bool assign_value(FldType &fld, FITDataField const &val)
{
    if (val.value) {
        if constexpr (def.units == MetaDataUnits::Semicircles) {
            static_assert(def.base_type == FITBaseType::sint32);
            auto v = val.value->sint32;
            fld = static_cast<f32>(v) * (180.0 / static_cast<f32>(1u << 31));
        } else if constexpr (def.units == MetaDataUnits::Lat) { // Implies semicircles
            static_assert(def.base_type == FITBaseType::sint32);
            auto v = val.value->sint32;
            fld.lat = static_cast<f32>(v) * (180.0 / static_cast<f32>(1u << 31));
        } else if constexpr (def.units == MetaDataUnits::Long) { // Implies semicircles
            static_assert(def.base_type == FITBaseType::sint32);
            auto v = val.value->sint32;
            fld.lon = static_cast<f32>(v) * (180.0 / static_cast<f32>(1u << 31));
        } else if constexpr (def.units == MetaDataUnits::DateTime) {
            int32_t d;
            switch (def.base_type) {
#undef S
#define S(Value, Type)        \
    case FITBaseType::Value:  \
        d = val.value->Value; \
        break;
                SIMPLE_FITBASETYPE(S)
#undef S
            default:
                assert(false);
                break;
            }
            fld = DateTime::from_timestamp(d + FIT_TIMESTAMP_OFFSET);
        } else {
#undef S
#define S(Value, Type)                                                             \
    if constexpr (def.base_type == FITBaseType::Value) {                           \
        if constexpr (def.scale != 1.0 || def.offset != 0.0) {                     \
            fld = static_cast<FldType>(val.value->Value / def.scale - def.offset); \
        } else {                                                                   \
            fld = static_cast<FldType>(val.value->Value);                          \
        }                                                                          \
    }
            SIMPLE_FITBASETYPE(S)
#undef S
            if constexpr (def.base_type == FITBaseType::string) {
                fld = static_cast<FldType>(val.value->string);
            }
            if constexpr (def.base_type == FITBaseType::byte) {
                fld = static_cast<FldType>(val.value->byte);
            }
        }
    }
    return true;
}

template<typename FldType, FieldMetaData def>
bool assign_optional(std::optional<FldType> &fld, FITDataField const &val)
{
    FldType f;
    if (fld) {
        f = fld.value();
    }
    auto ret = assign_value<FldType, def>(f, val);
    fld = f;
    return ret;
}

template<typename ObjType, TypeMetaData meta, u8 num, typename FldType>
bool assign_field(ObjType &obj, FITDataRecord const &rec)
{
    constexpr FieldMetaData const &def = meta.fields[num];

    auto const &local_def = rec.file.definitions[rec.definition];
    auto const  val_maybe = rec.get_field(def.num);
    if constexpr (def.optional) {
        std::optional<FldType> *fld = reinterpret_cast<std::optional<FldType> *>(reinterpret_cast<char *>(&obj) + def.fld_offset);
        if (val_maybe && val_maybe->value) {
            assign_optional<FldType, def>(*fld, *val_maybe);
        }
    } else {
        if (!val_maybe) {
            std::println("Field {} of mesg {} not found", def.num, tag(static_cast<mesg_num>(local_def.global_msg_num)));
            return false;
        }
        if (!val_maybe->value) {
            std::println("Field {} of mesg {} empty", num, tag(static_cast<mesg_num>(local_def.global_msg_num)));
            return false;
        }
        FldType *fld = reinterpret_cast<FldType *>(reinterpret_cast<char *>(&obj) + def.fld_offset);
        assign_value<FldType, def>(*fld, *val_maybe);
    }
    return true;
}

template<typename ObjType, TypeMetaData meta, u8 num>
bool assign_fields(ObjType &, FITDataRecord const &)
{
    return true;
}

template<typename ObjType, TypeMetaData meta, u8 num, typename FldType, typename... FldTypes>
bool assign_fields(ObjType &obj, FITDataRecord const &rec)
{
    if (!assign_field<ObjType, meta, num, FldType>(obj, rec)) {
        return false;
    }
    return assign_fields<ObjType, meta, num + 1, FldTypes...>(obj, rec);
}

template<typename T>
std::optional<FITError> on_load(FITDataRecord const &, T &)
{
    return { };
}

template<typename T, TypeMetaData meta, typename... FldTypes>
std::expected<T, FITError> make_from_rec_(FITDataRecord const &rec)
{
    assert(rec.mesg_num == meta.mesg_num);
    T ret;
    if (!assign_fields<T, meta, 0, FldTypes...>(ret, rec)) {
        fatal("Error making FIT message");
    }
    if (auto err = on_load<T>(rec, ret); err) {
        return std::unexpected(err.value());
    }
    return ret;
}

/* ----------------------------------------------------------------------- */

template<TypeMetaData meta, u8 num>
void format_field_value(std::ostream &out, FITDataField const &val)
{
    constexpr FieldMetaData const &def = meta.fields[num];
    if constexpr (def.units == MetaDataUnits::Semicircles) {
        static_assert(def.base_type == FITBaseType::sint32);
        auto v = val.value->sint32;
        out << std::format("{:7.3}º", static_cast<f32>(v) * (180.0 / static_cast<f32>(1 << 31)));
    } else if constexpr (def.units == MetaDataUnits::Lat) { // Implies semicircles
        static_assert(def.base_type == FITBaseType::sint32);
        auto v = val.value->sint32;
        out << std::format("{:7.3}º lat", static_cast<f32>(v) * (180.0 / static_cast<f32>(1 << 31)));
    } else if constexpr (def.units == MetaDataUnits::Long) { // Implies semicircles
        static_assert(def.base_type == FITBaseType::sint32);
        auto v = val.value->sint32;
        out << std::format("{:7.3}º long", static_cast<f32>(v) * (180.0 / static_cast<f32>(1 << 31)));
    } else if constexpr (def.units == MetaDataUnits::DateTime) {
        int32_t d;
        switch (def.base_type) {
#undef S
#define S(Value, Type)        \
    case FITBaseType::Value:  \
        d = val.value->Value; \
        break;
            SIMPLE_FITBASETYPE(S)
#undef S
        default:
            assert(false);
            break;
        }
        out << std::format("{}", DateTime::from_timestamp(d + FIT_TIMESTAMP_OFFSET));
    } else {
        out << std::format("{}", val);
    }
}

template<TypeMetaData meta, u8 num>
std::ostream &format_field(std::ostream &out, FITDataRecord const &rec)
{
    constexpr FieldMetaData const &def = meta.fields[num];

    auto const &local_def = rec.file.definitions[rec.definition];
    auto const  val_maybe = rec.get_field(def.num);
    auto const *name = def.name;
    if constexpr (def.optional) {
        if (val_maybe && val_maybe->value) {
            out << std::format("  {}: ", name);
            format_field_value<meta, num>(out, *val_maybe);
            out << "\n";
        }
    } else {
        if (!val_maybe) {
            std::println("Required field `{}` not found\n", name, tag(static_cast<mesg_num>(local_def.global_msg_num)));
            return out;
        }
        if (!val_maybe->value) {
            out << std::format("Required field `{}` empty\n", name, tag(static_cast<mesg_num>(local_def.global_msg_num)));
            return out;
        }
        out << std::format("  {}: ", name);
        format_field_value<meta, num>(out, *val_maybe);
        out << "\n";
    }
    return out;
}

template<TypeMetaData meta, u8 num>
std::ostream &format_fields(std::ostream &out, FITDataRecord const &rec)
{
    if constexpr (num < meta.num_fields) {
        format_field<meta, num>(out, rec);
        return format_fields<meta, num + 1>(out, rec);
    }
    return out;
}

template<TypeMetaData meta>
std::ostream &format_record_(std::ostream &out, FITDataRecord const &rec)
{
    assert(rec.mesg_num == meta.mesg_num);
    format_fields<meta, 0>(out, rec);
    return out;
}

template<mesg_num N>
std::ostream &format_record(std::ostream &out, FITDataRecord const &rec)
{
    size_t ix = 0;
    for (auto const &fld : rec.fields) {
        out << "  " << ix << ": " << std::format("{}\n", fld);
        ++ix;
    }
    return out;
}

template<>
std::ostream &format_record<mesg_num::file_id>(std::ostream &out, FITDataRecord const &rec);
template<>
std::ostream &format_record<mesg_num::developer_data_id>(std::ostream &out, FITDataRecord const &rec);
template<>
std::ostream &format_record<mesg_num::field_description>(std::ostream &out, FITDataRecord const &rec);

} // namespace ST:FIT

template<>
struct std::formatter<ST::FIT::FITDataRecord> : std::formatter<std::string> {
    template<class FmtContext>
    FmtContext::iterator format(ST::FIT::FITDataRecord const &rec, FmtContext &ctx) const
    {
        std::ostringstream out;
        out << "mesg_num: " << tag(rec.mesg_num) << "\n";
        switch (rec.mesg_num) {
#undef S
#define S(M, V)                                        \
    case ST::FIT::mesg_num::M:                         \
        format_record<ST::FIT::mesg_num::M>(out, rec); \
        break;
            FIT_MESG_NUM(S)
#undef S
        }
        return std::ranges::copy(std::move(out).str(), ctx.out()).out;
    }
};

template<>
struct std::formatter<ST::FIT::mesg_num> : std::formatter<std::string> {
    template<class FmtContext>
    FmtContext::iterator format(ST::FIT::mesg_num const &val, FmtContext &ctx) const
    {
        std::ostringstream out;
        out << ST::FIT::tag(val);
        return std::ranges::copy(std::move(out).str(), ctx.out()).out;
    }
};

template<>
struct std::formatter<ST::FIT::file_type> : std::formatter<std::string> {
    template<class FmtContext>
    FmtContext::iterator format(ST::FIT::file_type const &val, FmtContext &ctx) const
    {
        std::ostringstream out;
        out << ST::FIT::tag(val);
        return std::ranges::copy(std::move(out).str(), ctx.out()).out;
    }
};

template<>
struct std::formatter<ST::FIT::manufacturer> : std::formatter<std::string> {
    template<class FmtContext>
    FmtContext::iterator format(ST::FIT::manufacturer const &val, FmtContext &ctx) const
    {
        std::ostringstream out;
        out << ST::FIT::tag(val);
        return std::ranges::copy(std::move(out).str(), ctx.out()).out;
    }
};
