#include <Coordinates.h>
#include <cstdint>
#include <fit/FIT.h>
#include <fit/Profile.h>
#include <string>

namespace ST::FIT {

constexpr static TypeMetaData activity_meta {
    .mesg_num = mesg_num::activity,
    .num_fields = 4,
    .fields = {
        { .name = "timestamp", .num = 253, .units = MetaDataUnits::DateTime, .optional = false, .base_type = FITBaseType::sint32, .fld_offset = offsetof(activity, timestamp) },
        { .name = "total timer time", .num = 0, .scale = 1000, .optional = false, .base_type = FITBaseType::uint32, .fld_offset = offsetof(activity, total_timer_time) },
        { .name = "num_sessions", .num = 1, .optional = false, .base_type = FITBaseType::uint16, .fld_offset = offsetof(activity, num_sessions) },
        { .name = "local timestamp", .num = 5, .units = MetaDataUnits::DateTime, .optional = true, .base_type = FITBaseType::sint32, .fld_offset = offsetof(activity, local_timestamp) },
    }
};

template<>
std::expected<activity, FITError> make_from_rec(FITDataRecord const &rec)
{
    return make_from_rec_<
        activity,
        activity_meta,
        DateTime,
        f32,
        u16,
        DateTime>(rec);
}

template<>
std::ostream &format_record<mesg_num::activity>(std::ostream &out, FITDataRecord const &rec)
{
    return format_record_<activity_meta>(out, rec);
}

constexpr static TypeMetaData session_meta {
    .mesg_num = mesg_num::session,
    .num_fields = 11,
    .fields = {
        { .name = "timestamp", .num = 253, .units = MetaDataUnits::DateTime, .optional = false, .base_type = FITBaseType::sint32, .fld_offset = offsetof(session, timestamp) },
        { .name = "start time", .num = 2, .units = MetaDataUnits::DateTime, .optional = false, .base_type = FITBaseType::sint32, .fld_offset = offsetof(session, start_time) },
        { .name = "sport", .num = 5, .optional = true, .base_type = FITBaseType::enum_, .fld_offset = offsetof(session, sport) },
        { .name = "sub sport", .num = 6, .optional = true, .base_type = FITBaseType::enum_, .fld_offset = offsetof(session, sub_sport) },
        { .name = "total elapsed time", .num = 7, .scale = 1000, .optional = false, .base_type = FITBaseType::uint32, .fld_offset = offsetof(session, total_elapsed_time) },
        { .name = "total timer time", .num = 8, .scale = 1000, .optional = false, .base_type = FITBaseType::uint32, .fld_offset = offsetof(session, total_timer_time) },
        { .name = "total distance", .num = 9, .scale = 100, .optional = true, .base_type = FITBaseType::uint32, .fld_offset = offsetof(session, total_distance) },
        { .name = "total calories", .num = 11, .optional = true, .base_type = FITBaseType::uint16, .fld_offset = offsetof(session, total_calories) },
        { .name = "first lap index", .num = 25, .optional = true, .base_type = FITBaseType::uint16, .fld_offset = offsetof(session, first_lap_index) },
        { .name = "num laps", .num = 26, .optional = true, .base_type = FITBaseType::uint16, .fld_offset = offsetof(session, num_laps) },
        { .name = "total moving time", .num = 59, .scale = 1000, .optional = true, .base_type = FITBaseType::uint32, .fld_offset = offsetof(session, total_moving_time) },
    },
};

template<>
std::expected<session, FITError> make_from_rec(FITDataRecord const &rec)
{
    return make_from_rec_<
        session,
        session_meta,
        DateTime,
        DateTime,
        sport,
        sub_sport,
        f32,
        f32,
        f32,
        u16,
        u16,
        u16,
        f32>(rec);
}

template<>
std::ostream &format_record<mesg_num::session>(std::ostream &out, FITDataRecord const &rec)
{
    return format_record_<session_meta>(out, rec);
}

constexpr static TypeMetaData lap_meta {
    .mesg_num = mesg_num::lap,
    .num_fields = 10,
    .fields = {
        { .name = "message idx", .num = 254, .optional = true, .base_type = FITBaseType::uint16, .fld_offset = offsetof(lap, message_index) },
        { .name = "timestamp", .num = 253, .units = MetaDataUnits::DateTime, .optional = false, .base_type = FITBaseType::sint32, .fld_offset = offsetof(lap, timestamp) },
        { .name = "start time", .num = 2, .units = MetaDataUnits::DateTime, .optional = false, .base_type = FITBaseType::sint32, .fld_offset = offsetof(lap, start_time) },
        { .name = "total elapsed time", .num = 7, .scale = 1000, .optional = false, .base_type = FITBaseType::uint32, .fld_offset = offsetof(lap, total_elapsed_time) },
        { .name = "total timer time", .num = 8, .scale = 1000, .optional = false, .base_type = FITBaseType::uint32, .fld_offset = offsetof(lap, total_timer_time) },
        { .name = "total distance", .num = 9, .scale = 100, .optional = true, .base_type = FITBaseType::uint32, .fld_offset = offsetof(lap, total_distance) },
        { .name = "total calories", .num = 11, .optional = true, .base_type = FITBaseType::uint16, .fld_offset = offsetof(lap, total_calories) },
        { .name = "sport", .num = 25, .optional = true, .base_type = FITBaseType::enum_, .fld_offset = offsetof(lap, sport) },
        { .name = "sub sport", .num = 39, .optional = true, .base_type = FITBaseType::enum_, .fld_offset = offsetof(lap, sub_sport) },
        { .name = "total moving time", .num = 52, .scale = 1000, .optional = true, .base_type = FITBaseType::uint32, .fld_offset = offsetof(lap, total_moving_time) },
    },
};

template<>
std::expected<lap, FITError> make_from_rec(FITDataRecord const &rec)
{
    return make_from_rec_<
        lap,
        lap_meta,
        message_index,
        DateTime,
        DateTime,
        f32,
        f32,
        f32,
        u16,
        sport,
        sub_sport,
        f32>(rec);
}

template<>
std::ostream &format_record<mesg_num::lap>(std::ostream &out, FITDataRecord const &rec)
{
    return format_record_<lap_meta>(out, rec);
}

constexpr static TypeMetaData record_meta {
    .mesg_num = mesg_num::record,
    .num_fields = 15,
    .fields = {
        { .name = "timestamp", .num = 253, .units = MetaDataUnits::DateTime, .optional = false, .base_type = FITBaseType::sint32, .fld_offset = offsetof(record, timestamp) },
        { .name = "position_lat", .num = 0, .units = MetaDataUnits::Lat, .optional = true, .base_type = FITBaseType::sint32, .fld_offset = offsetof(record, position) },
        { .name = "position_lon", .num = 1, .units = MetaDataUnits::Long, .optional = true, .base_type = FITBaseType::sint32, .fld_offset = offsetof(record, position) },
        { .name = "altitude", .num = 2, .scale = 5, .offset = 500, .optional = true, .base_type = FITBaseType::uint16, .fld_offset = offsetof(record, altitude) },
        { .name = "heart rate", .num = 3, .optional = true, .base_type = FITBaseType::uint8, .fld_offset = offsetof(record, heart_rate) },
        { .name = "cadence", .num = 4, .optional = true, .base_type = FITBaseType::uint8, .fld_offset = offsetof(record, cadence) },
        { .name = "distance", .num = 5, .scale = 100, .optional = true, .base_type = FITBaseType::uint32, .fld_offset = offsetof(record, distance) },
        { .name = "speed", .num = 6, .scale = 1000, .optional = true, .base_type = FITBaseType::uint16, .fld_offset = offsetof(record, speed) },
        { .name = "power", .num = 7, .optional = true, .base_type = FITBaseType::uint16, .fld_offset = offsetof(record, power) },
        { .name = "grade", .num = 9, .optional = true, .base_type = FITBaseType::sint16, .fld_offset = offsetof(record, grade) },
        { .name = "temperature", .num = 13, .optional = true, .base_type = FITBaseType::sint8, .fld_offset = offsetof(record, temperature) },
        { .name = "left/right balance", .num = 30, .optional = true, .base_type = FITBaseType::uint8, .fld_offset = offsetof(record, left_right_balance) },
        { .name = "calories", .num = 33, .optional = true, .base_type = FITBaseType::uint16, .fld_offset = offsetof(record, calories) },
        { .name = "speed enh", .num = 73, .scale = 1000, .optional = true, .base_type = FITBaseType::uint32, .fld_offset = offsetof(record, speed) },
        { .name = "altitude enh", .num = 78, .scale = 5, .offset = 500, .optional = true, .base_type = FITBaseType::uint32, .fld_offset = offsetof(record, altitude) },
    },
};

template<>
std::expected<record, FITError> make_from_rec(FITDataRecord const &rec)
{
    return make_from_rec_<
        record,
        record_meta,
        DateTime,
        Coordinates,
        Coordinates,
        f32,
        u8,
        u8,
        f32,
        f32,
        u16,
        f32,
        i8,
        u8,
        u16,
        f32,
        f32>(rec);
}

template<>
std::optional<FITError> on_load(FITDataRecord const &fitrec, record &rec)
{
    if (auto compressed_speed_distance_maybe = fitrec.get_field(8); compressed_speed_distance_maybe) {
        FITDataField compressed_speed_distance = { *compressed_speed_distance_maybe };
        if (compressed_speed_distance.value) {
            assert(compressed_speed_distance.type == FITBaseType::byte);
            auto spd_dst { compressed_speed_distance.value.value().byte };
            u16  spd = (static_cast<u16>(spd_dst[0]) << 4) | ((static_cast<u16>(spd_dst[1]) & 0x00F0) >> 4);
            u16  dst = (static_cast<u16>(spd_dst[1]) & 0x000F << 8) | static_cast<u16>(spd_dst[2]);
            rec.speed = static_cast<f32>(spd) / 100.0;
            rec.distance = static_cast<f32>(dst) / 16.0;
        }
    }
    return { };
}

template<>
std::ostream &format_record<mesg_num::record>(std::ostream &out, FITDataRecord const &rec)
{
    return format_record_<record_meta>(out, rec);
}

constexpr static TypeMetaData workout_meta {
    .mesg_num = mesg_num::workout,
    .num_fields = 4,
    .fields = {
        { .name = "message idx", .num = 254, .optional = true, .base_type = FITBaseType::uint16, .fld_offset = offsetof(workout, message_index) },
        { .name = "sport", .num = 4, .optional = true, .base_type = FITBaseType::enum_, .fld_offset = offsetof(workout, sport) },
        { .name = "sub sport", .num = 11, .optional = true, .base_type = FITBaseType::enum_, .fld_offset = offsetof(workout, sub_sport) },
        { .name = "workout name", .num = 8, .optional = true, .base_type = FITBaseType::string, .fld_offset = offsetof(workout, wkt_name) },
    },
};

template<>
std::expected<workout, FITError> make_from_rec(FITDataRecord const &rec)
{
    auto ret = make_from_rec_<
        workout,
        workout_meta,
        message_index,
        sport,
        sub_sport,
        std::string>(rec);
    return ret;
}

template<>
std::ostream &format_record<mesg_num::workout>(std::ostream &out, FITDataRecord const &rec)
{
    return format_record_<workout_meta>(out, rec);
}

}
