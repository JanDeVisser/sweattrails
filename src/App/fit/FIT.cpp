
#include "FIT.h"
#include <algorithm>
#include <cstddef>
#include <ctime>
#include <expected>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <Expected.h>
#include <IO.h>
#include <Logging.h>

#include <fit/FIT.h>

namespace ST::FIT {

char const *tag(mesg_num m)
{
    switch (m) {
#undef S
#define S(M, N)       \
    case mesg_num::M: \
        return #M;
        FIT_MESG_NUM(S)
#undef S
    default:
        break;
    }
    return "Unknown";
}

char const *tag(file_type t)
{
    switch (t) {
#undef S
#define S(T, N)        \
    case file_type::T: \
        return #T;
        FIT_FILE_TYPE(S)
#undef S
    default:
        break;
    }
    return "Unknown";
}

char const *tag(manufacturer m)
{
    switch (m) {
#undef S
#define S(M, N)           \
    case manufacturer::M: \
        return #M;
        FIT_MANUFACTURER(S)
#undef S
    default:
        break;
    }
    return "Unknown";
}

char const *tag(FITError e)
{
    switch (e) {
#undef S
#define S(E)          \
    case FITError::E: \
        return #E;
        FITERROR(S)
#undef S
    default:
        break;
    }
    return "Unknown";
}

char const *tag(FITBaseType t)
{
    switch (t) {
#undef S
#define S(FT, C, CT)      \
    case FITBaseType::FT: \
        return #FT;
        FITBASETYPE(S)
#undef S
    default:
        break;
    }
    return "Unknown";
}

char const *tag(FITArchitecture a)
{
    switch (a) {
#undef S
#define S(A, N)              \
    case FITArchitecture::A: \
        return #A;
        FITARCHITECTURE(S)
#undef S
    default:
        break;
    }
    return "Unknown";
}

RecordHeader::RecordHeader(u8 header_byte)
{
    // std::println("reading header {:x}", header_byte);
    u8 mask = static_cast<u8>(header_byte & 0x0F);
    switch (header_byte >> 6 & 0x03) {
    case 0:
        header = mask;
        break;
    case 1:
        header = (Definition) {
            .local_message_type = mask,
            .has_developer_data = (header_byte & 0x20) == 0x20,
        };
        break;
    default:
        header = (CompressedTimestamp) {
            .local_message_type = static_cast<u8>(header_byte >> 5 & 0x03),
            .time_offset = static_cast<u8>(header_byte & 0x1F),
        };
        break;
    }
}

std::optional<FITDataField> FITDataRecord::get_field(u8 num) const
{
    auto const &local_def = file.definitions[definition];
    for (auto ix = 0; ix < local_def.num_fields; ++ix) {
        if (local_def.fields[ix].field_num == num) {
            return fields[ix];
        }
    }
    for (auto ix = 0; ix < local_def.num_developer_fields; ++ix) {
        if (local_def.developer_fields[ix].field_num == num) {
            return fields[local_def.num_fields + ix];
        }
    }
    return { };
}

FITFile::FITFile(std::string_view const &buffer)
    : buffer(buffer)
{
    memset(current_definitions, 0xFF, 16 * sizeof(size_t));
}

FITFile::~FITFile()
{
}

std::expected<FITFile, FITError> FITFile::read(std::string_view file_name)
{
    auto res = read_file_by_name(file_name);
    if (!res) {
        return std::unexpected(FITError::IOError);
    }
    return FITFile { res.value() };
}

std::string_view FITFile::read_slice(size_t count)
{
    assert(offset + count <= buffer.length());
    std::string_view data { buffer };
    data = data.substr(offset, count);
    // std::print("reading {} bytes from ", count);
    // for (auto ix = 0ul; ix < std::min(data.length(), 16ul); ++ix) {
    //     std::print("0x{:02x} ", data[ix]);
    // }
    // std::println();
    auto ret = data.substr(0, count);
    data = data.substr(count);
    offset += count;
    total_read += count;
    // std::println("read: {} total_read: {} remaining: {}", count, total_read, data.length());
    return ret;
}

void FITFile::skip(size_t count)
{
    offset = std::min(offset + count, buffer.size());
}

std::expected<void, FITError> FITFile::read_header()
{
    if (header.protocol_version != 0) {
        return { };
    }

    std::string_view header_data = read_slice(14);
    u8 const         header_size = header_data[0];
    if (header_size > 14) {
        std::println("read_header: large header {}", header_size);
        return std::unexpected(FITError::LargeHeaderSizeUnsupported);
    }
    if (header_data.substr(8, 4) != ".FIT") {
        std::println("read_header: header magic missing");
        return std::unexpected(FITError::HeaderMagicMissing);
    }

    header = {
        .header_size = header_size,
        .protocol_version = static_cast<u8>(header_data[1]),
        .profile_version = fit_read<u16>(FITArchitecture::LittleEndian, header_data.substr(2)),
        .data_size = fit_read<u32>(FITArchitecture::LittleEndian, header_data.substr(4)),
    };
    if (header_size >= 14) {
        header.crc = fit_read<u16>(FITArchitecture::LittleEndian, header_data.substr(12));
    }
    if (verbose) {
        std::println("file header: header_size: {} version: {}.{} data_size: {} crc: {}",
            header.header_size, header.protocol_version, header.profile_version, header.data_size, header.crc);
    }
    total_read = 0;
    return { };
}

std::expected<FITDataField, FITError> FITFile::read_field(FITArchitecture arch, FITBaseType base_type, size_t size)
{
    FITDataField ret = { .type = base_type, .value = std::optional<FITDataField::Options> { } };
    switch (base_type) {
    case FITBaseType::enum_: {
        auto v = read_value<u8>(arch);
        if (v != 0xFF) {
            ret.value = (FITDataField::Options) { .enum_ = v };
        }
    } break;
    case FITBaseType::sint8: {
        auto v = read_value<i8>(arch);
        if (v != 0x7F) {
            ret.value = (FITDataField::Options) { .sint8 = v };
        }
    } break;
    case FITBaseType::uint8: {
        auto v = read_value<u8>(arch);
        if (v != 0xFF) {
            ret.value = (FITDataField::Options) { .uint8 = v };
        }
    } break;
    case FITBaseType::uint8z: {
        auto v = read_value<u8>(arch);
        if (v != 0x00) {
            ret.value = (FITDataField::Options) { .uint8z = v };
        }
    } break;
    case FITBaseType::sint16: {
        auto v = read_value<i16>(arch);
        if (v != 0x7FFF) {
            ret.value = (FITDataField::Options) { .sint16 = v };
        }
    } break;
    case FITBaseType::uint16: {
        auto v = read_value<u16>(arch);
        if (v != 0xFFFF) {
            ret.value = (FITDataField::Options) { .uint16 = v };
        }
    } break;
    case FITBaseType::uint16z: {
        auto v = read_value<u16>(arch);
        if (v != 0x00) {
            ret.value = (FITDataField::Options) { .uint16z = v };
        }
    } break;
    case FITBaseType::sint32: {
        auto v = read_value<i32>(arch);
        if (v != 0x7FFFFFFF) {
            ret.value = (FITDataField::Options) { .sint32 = v };
        }
    } break;
    case FITBaseType::uint32: {
        auto v = read_value<u32>(arch);
        if (v != 0xFFFFFFFF) {
            ret.value = (FITDataField::Options) { .uint32 = v };
        }
    } break;
    case FITBaseType::uint32z: {
        auto v = read_value<u32>(arch);
        if (v != 0x00) {
            ret.value = (FITDataField::Options) { .uint32z = v };
        }
    } break;
    case FITBaseType::sint64: {
        auto v = read_value<i64>(arch);
        if (v != 0x7FFFFFFFFFFFFFFF) {
            ret.value = (FITDataField::Options) { .sint64 = v };
        }
    } break;
    case FITBaseType::uint64: {
        auto v = read_value<u64>(arch);
        if (v != 0xFFFFFFFFFFFFFFFF) {
            ret.value = (FITDataField::Options) { .uint64 = v };
        }
    } break;
    case FITBaseType::uint64z: {
        auto v = read_value<u64>(arch);
        if (v != 0x00) {
            ret.value = (FITDataField::Options) { .uint64z = v };
        }
    } break;
    case FITBaseType::float32: {
        auto v = read_value<float32>(arch);
        if (*(reinterpret_cast<u32 *>(&v)) != 0xFFFFFFFF) {
            ret.value = (FITDataField::Options) { .float32 = v };
        }
    } break;
    case FITBaseType::float64: {
        auto v = read_value<float64>(arch);
        if (*(reinterpret_cast<u64 *>(&v)) != 0xFFFFFFFFFFFFFFFF) {
            ret.value = (FITDataField::Options) { .float64 = v };
        }
    } break;
    case FITBaseType::string: {
        size_t strlen = size;
        for (size_t ix = 0; ix < size; ++ix) {
            if (buffer[offset + ix] == 0) {
                strlen = ix;
                break;
            }
        }
        if (strlen > 0) {
            arrays.emplace_back(read_slice(strlen));
            ret.value = (FITDataField::Options) { .string = arrays.back() };
        }
        if (strlen < size) {
            read_slice(size - strlen);
        }
    } break;
    case FITBaseType::byte: {
        size_t len = size;
        for (size_t ix = 0; ix < size; ++ix) {
            if (static_cast<u8>(buffer[offset + ix]) == 0xFF) {
                len = ix;
                break;
            }
        }
        if (len > 0) {
            ret.value = (FITDataField::Options) { .string = read_slice(len) };
        }
        if (len < size) {
            read_slice(size - len);
        }
    } break;
    }
    return ret;
}

std::expected<FITDataField, FITError> FITFile::read_field(LocalDefinition const &def, size_t num)
{
    assert(num < def.num_fields);
    auto const &fld = def.fields[num];
    assert(fld.size > 0);
    return TRY_EVAL(read_field(def.arch, fld.base_type, fld.size));
}

std::expected<FITDataField, FITError> FITFile::read_developer_field(LocalDefinition const &def, size_t num)
{
    assert(num < def.num_developer_fields);
    auto const &fld = def.developer_fields[num];
    assert(fld.size > 0);
    return TRY_EVAL(read_field(def.arch, fld.base_type, fld.size));
}

std::expected<FITDataRecord, FITError> FITFile::read_data_record(RecordHeader record_header)
{
    size_t def_ix = std::visit(
        overloaded {
            [this](u8 const &data_header) -> size_t {
                if (verbose) {
                    std::println("message {} -> {} {}",
                        data_header,
                        definitions[current_definitions[data_header]].global_msg_num,
                        tag(static_cast<mesg_num>(definitions[current_definitions[data_header]].global_msg_num)));
                }
                return current_definitions[data_header];
            },
            [this](RecordHeader::CompressedTimestamp const &ts_header) -> size_t {
                if (verbose) {
                    std::println("compressed timestamp message {} -> {} {}",
                        ts_header.local_message_type,
                        definitions[current_definitions[ts_header.local_message_type]].global_msg_num,
                        tag(static_cast<mesg_num>(definitions[current_definitions[ts_header.local_message_type]].global_msg_num)));
                }
                auto  ret = current_definitions[ts_header.local_message_type];
                auto &def = definitions[ret];
                def.current_timestamp = ((def.current_timestamp & 0x1F) >= ts_header.time_offset)
                    ? (def.current_timestamp & 0xFFFFFFE0) + ts_header.time_offset
                    : (def.current_timestamp & 0xFFFFFFE0) + ts_header.time_offset + 0x20;
                return ret;
            },
            [](auto const &) -> size_t {
                std::unreachable();
            },
        },
        record_header.header);
    LocalDefinition &def = definitions[def_ix];

    std::vector<FITDataField> fields;
    for (size_t fld_num = 0; fld_num < def.num_fields; ++fld_num) {
        auto data_fld = TRY_EVAL(read_field(def, fld_num));
        if (def.fields[fld_num].field_num == FIT_FIELD_TIMESTAMP) {
            assert(data_fld.type == FITBaseType::uint32 && data_fld.value);
            data_fld.value = { .uint32 = data_fld.value->uint32 + def.current_timestamp };
            def.current_timestamp = data_fld.value->uint32;
        }
        if (verbose) {
            std::println("  {}. {} {}", fld_num, def.fields[fld_num].field_num, data_fld);
        }
        fields.emplace_back(data_fld);
    }
    for (size_t dev_fld_num = 0; dev_fld_num < def.num_developer_fields; ++dev_fld_num) {
        fields.emplace_back(TRY_EVAL(read_developer_field(def, dev_fld_num)));
        auto const &data_fld { fields.back() };
        if (verbose)
            std::println("  {}. {} {}", dev_fld_num, def.developer_fields[dev_fld_num].field_num, data_fld);
    }
    data_records.emplace_back((FITDataRecord) {
        .file = *this,
        .definition = def_ix,
        .mesg_num = (def.global_msg_num < 0xFF00) ? static_cast<mesg_num>(def.global_msg_num) : mesg_num::vendor,
        .mesg_number = def.global_msg_num,
        .fields = std::move(fields),
    });
    return data_records.back();
}

void FITFile::rewind()
{
    current = 0;
}

bool FITFile::fully_read()
{
    return header.header_size > 0 and total_read >= buffer.length();
}

bool FITFile::exhausted()
{
    return fully_read() && offset >= buffer.size();
}

std::optional<FITDataRecord> FITFile::current_record()
{
    if (current >= data_records.size()) {
        return { };
    }
    return data_records[current];
}

std::expected<void, FITError> FITFile::read()
{
    return read_until(nullptr);
}

std::expected<std::optional<FITDataRecord>, FITError> FITFile::find_first(mesg_num mesg)
{
    rewind();
    return find_next(mesg);
}

std::expected<std::optional<FITDataRecord>, FITError> FITFile::find_next(mesg_num mesg)
{
    TRY(read_until([mesg](FITDataRecord const &rec) -> bool {
        return rec.mesg_num == mesg;
    }));
    return current_record();
}

std::expected<std::optional<FITDataRecord>, FITError> FITFile::first()
{
    rewind();
    return next();
}

std::expected<std::optional<FITDataRecord>, FITError> FITFile::next()
{
    TRY(read_until(nullptr));
    return current_record();
}

constexpr static TypeMetaData file_id_meta {
    .mesg_num = mesg_num::file_id,
    .num_fields = 7,
    .fields = {
        { .name = "type", .num = 0, .optional = false, .base_type = FITBaseType::enum_, .fld_offset = offsetof(file_id, type) },
        { .name = "manufacturer", .num = 1, .optional = true, .base_type = FITBaseType::enum_, .fld_offset = offsetof(file_id, manufacturer) },
        { .name = "product", .num = 2, .optional = true, .base_type = FITBaseType::uint16, .fld_offset = offsetof(file_id, product) },
        { .name = "serial number", .num = 3, .optional = true, .base_type = FITBaseType::uint32, .fld_offset = offsetof(file_id, serial_number) },
        { .name = "time created", .num = 4, .units = MetaDataUnits::DateTime, .optional = false, .base_type = FITBaseType::sint32, .fld_offset = offsetof(file_id, time_created) },
        { .name = "number", .num = 5, .optional = true, .base_type = FITBaseType::uint16, .fld_offset = offsetof(file_id, number) },
        { .name = "product name", .num = 8, .optional = true, .base_type = FITBaseType::string, .fld_offset = offsetof(file_id, product_name) },
    },
};

template<>
std::expected<file_id, FITError> make_from_rec(FITDataRecord const &rec)
{
    return make_from_rec_<
        file_id,
        file_id_meta,
        file_type,
        manufacturer,
        u16,
        u32,
        DateTime,
        u16,
        std::string_view>(rec);
}

template<>
std::ostream &format_record<mesg_num::file_id>(std::ostream &out, FITDataRecord const &rec)
{
    return format_record_<file_id_meta>(out, rec);
}

constexpr static TypeMetaData developer_data_id_meta {
    .mesg_num = mesg_num::developer_data_id,
    .num_fields = 5,
    .fields = {
        { .name = "developer id", .num = 0, .optional = true, .base_type = FITBaseType::string, .fld_offset = offsetof(developer_data_id, developer_id) },
        { .name = "application id", .num = 1, .optional = true, .base_type = FITBaseType::string, .fld_offset = offsetof(developer_data_id, application_id) },
        { .name = "manufacturer id", .num = 2, .optional = true, .base_type = FITBaseType::uint16, .fld_offset = offsetof(developer_data_id, manufacturer_id) },
        { .name = "developer data index", .num = 3, .optional = false, .base_type = FITBaseType::uint8, .fld_offset = offsetof(developer_data_id, developer_data_index) },
        { .name = "application version", .num = 4, .optional = true, .base_type = FITBaseType::uint32, .fld_offset = offsetof(developer_data_id, application_version) },
    },
};

template<>
std::expected<developer_data_id, FITError> make_from_rec(FITDataRecord const &rec)
{
    return make_from_rec_<
        developer_data_id,
        developer_data_id_meta,
        std::string_view,
        std::string_view,
        manufacturer,
        u8,
        u32>(rec);
}

template<>
std::ostream &format_record<mesg_num::developer_data_id>(std::ostream &out, FITDataRecord const &rec)
{
    return format_record_<developer_data_id_meta>(out, rec);
}

constexpr static TypeMetaData field_description_meta {
    .mesg_num = mesg_num::field_description,
    .num_fields = 6,
    .fields = {
        { .name = "developer data index", .num = 0, .optional = false, .base_type = FITBaseType::uint8, .fld_offset = offsetof(field_description, developer_data_index) },
        { .name = "field definition number", .num = 1, .optional = false, .base_type = FITBaseType::uint8, .fld_offset = offsetof(field_description, field_definition_number) },
        { .name = "FIT base type id", .num = 2, .optional = false, .base_type = FITBaseType::uint8, .fld_offset = offsetof(field_description, fit_base_type_id) },
        { .name = "field name", .num = 3, .optional = true, .base_type = FITBaseType::string, .fld_offset = offsetof(field_description, field_name) },
        { .name = "units", .num = 8, .optional = true, .base_type = FITBaseType::string, .fld_offset = offsetof(field_description, units) },
        { .name = "native field number", .num = 15, .optional = true, .base_type = FITBaseType::uint8, .fld_offset = offsetof(field_description, native_field_num) },
    },
};

template<>
std::expected<field_description, FITError> make_from_rec(FITDataRecord const &rec)
{
    return make_from_rec_<
        field_description,
        field_description_meta,
        u8,
        u8,
        u8,
        std::string_view,
        std::string_view,
        u8>(rec);
}

template<>
std::ostream &format_record<mesg_num::field_description>(std::ostream &out, FITDataRecord const &rec)
{
    return format_record_<field_description_meta>(out, rec);
}

}
