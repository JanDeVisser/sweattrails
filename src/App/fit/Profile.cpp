#include <Coordinates.h>
#include <fit/FIT.h>
#include <fit/Profile.h>

namespace ST::FIT {

constexpr static TypeMetaData activity_meta {
    .mesg_num = mesg_num::activity,
    .num_fields = 4,
    .fields = {
        { .num = 253, .units = MetaDataUnits::DateTime, .optional = false, .base_type = FITBaseType::sint32, .fld_offset = offsetof(activity, timestamp) },
        { .num = 0, .scale = 1000, .optional = false, .base_type = FITBaseType::uint32, .fld_offset = offsetof(activity, total_timer_time) },
        { .num = 1, .optional = false, .base_type = FITBaseType::uint16, .fld_offset = offsetof(activity, num_sessions) },
        { .num = 5, .units = MetaDataUnits::DateTime, .optional = true, .base_type = FITBaseType::sint32, .fld_offset = offsetof(activity, local_timestamp) },
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

constexpr static TypeMetaData session_meta {
    .mesg_num = mesg_num::session,
    .num_fields = 11,
    .fields = {
        { .num = 253, .units = MetaDataUnits::DateTime, .optional = false, .base_type = FITBaseType::sint32, .fld_offset = offsetof(session, timestamp) },
        { .num = 2, .units = MetaDataUnits::DateTime, .optional = false, .base_type = FITBaseType::sint32, .fld_offset = offsetof(session, start_time) },
        { .num = 5, .optional = true, .base_type = FITBaseType::enum_, .fld_offset = offsetof(session, sport) },
        { .num = 6, .optional = true, .base_type = FITBaseType::enum_, .fld_offset = offsetof(session, sub_sport) },
        { .num = 7, .scale = 1000, .optional = false, .base_type = FITBaseType::uint32, .fld_offset = offsetof(session, total_elapsed_time) },
        { .num = 8, .scale = 1000, .optional = false, .base_type = FITBaseType::uint32, .fld_offset = offsetof(session, total_timer_time) },
        { .num = 9, .scale = 100, .optional = true, .base_type = FITBaseType::uint32, .fld_offset = offsetof(session, total_distance) },
        { .num = 11, .optional = true, .base_type = FITBaseType::uint16, .fld_offset = offsetof(session, total_calories) },
        { .num = 25, .optional = true, .base_type = FITBaseType::uint16, .fld_offset = offsetof(session, first_lap_index) },
        { .num = 26, .optional = true, .base_type = FITBaseType::uint16, .fld_offset = offsetof(session, num_laps) },
        { .num = 59, .scale = 1000, .optional = true, .base_type = FITBaseType::uint32, .fld_offset = offsetof(session, total_moving_time) },
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

constexpr static TypeMetaData lap_meta {
    .mesg_num = mesg_num::lap,
    .num_fields = 10,
    .fields = {
        { .num = 254, .optional = true, .base_type = FITBaseType::uint16, .fld_offset = offsetof(lap, message_index) },
        { .num = 253, .units = MetaDataUnits::DateTime, .optional = false, .base_type = FITBaseType::sint32, .fld_offset = offsetof(lap, timestamp) },
        { .num = 2, .units = MetaDataUnits::DateTime, .optional = false, .base_type = FITBaseType::sint32, .fld_offset = offsetof(lap, start_time) },
        { .num = 7, .scale = 1000, .optional = false, .base_type = FITBaseType::uint32, .fld_offset = offsetof(lap, total_elapsed_time) },
        { .num = 8, .scale = 1000, .optional = false, .base_type = FITBaseType::uint32, .fld_offset = offsetof(lap, total_timer_time) },
        { .num = 9, .scale = 100, .optional = true, .base_type = FITBaseType::uint32, .fld_offset = offsetof(lap, total_distance) },
        { .num = 11, .optional = true, .base_type = FITBaseType::uint16, .fld_offset = offsetof(lap, total_calories) },
        { .num = 25, .optional = true, .base_type = FITBaseType::enum_, .fld_offset = offsetof(lap, sport) },
        { .num = 39, .optional = true, .base_type = FITBaseType::enum_, .fld_offset = offsetof(lap, sub_sport) },
        { .num = 52, .scale = 1000, .optional = true, .base_type = FITBaseType::uint32, .fld_offset = offsetof(lap, total_moving_time) },
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

constexpr static TypeMetaData record_meta {
    .mesg_num = mesg_num::record,
    .num_fields = 15,
    .fields = {
        { .num = 253, .units = MetaDataUnits::DateTime, .optional = false, .base_type = FITBaseType::sint32, .fld_offset = offsetof(record, timestamp) },
        { .num = 0, .units = MetaDataUnits::Lat, .optional = true, .base_type = FITBaseType::sint32, .fld_offset = offsetof(record, position) },
        { .num = 1, .units = MetaDataUnits::Long, .optional = true, .base_type = FITBaseType::sint32, .fld_offset = offsetof(record, position) },
        { .num = 2, .scale = 5, .offset = 500, .optional = true, .base_type = FITBaseType::uint16, .fld_offset = offsetof(record, altitude) },
        { .num = 3, .optional = true, .base_type = FITBaseType::uint8, .fld_offset = offsetof(record, heart_rate) },
        { .num = 4, .optional = true, .base_type = FITBaseType::uint8, .fld_offset = offsetof(record, cadence) },
        { .num = 5, .scale = 100, .optional = true, .base_type = FITBaseType::uint32, .fld_offset = offsetof(record, distance) },
        { .num = 6, .scale = 1000, .optional = true, .base_type = FITBaseType::uint32, .fld_offset = offsetof(record, speed) },
        { .num = 7, .optional = true, .base_type = FITBaseType::uint16, .fld_offset = offsetof(record, power) },
        { .num = 9, .optional = true, .base_type = FITBaseType::sint16, .fld_offset = offsetof(record, grade) },
        { .num = 13, .optional = true, .base_type = FITBaseType::sint8, .fld_offset = offsetof(record, temperature) },
        { .num = 30, .optional = true, .base_type = FITBaseType::uint8, .fld_offset = offsetof(record, left_right_balance) },
        { .num = 33, .optional = true, .base_type = FITBaseType::uint16, .fld_offset = offsetof(record, calories) },
        { .num = 73, .scale = 1000, .optional = true, .base_type = FITBaseType::uint32, .fld_offset = offsetof(record, speed) },
        { .num = 78, .scale = 5, .offset = 500, .optional = true, .base_type = FITBaseType::uint32, .fld_offset = offsetof(record, altitude) },
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

}
