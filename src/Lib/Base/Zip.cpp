/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The layout is in PKWARE's APPNOTE.TXT. A ZIP ends with an "end of central
 * directory" record, which says where the central directory is. That has
 * an entry per file, with its name, sizes and compression, and the offset
 * of its local header, which the file's data follows.
 */

#include <format>
#include <optional>

#include <zlib.h>

#include <Logging.h>
#include <Zip.h>

namespace ST {

namespace {

constexpr u32    EndOfCentralDirectorySignature = 0x06054b50;
constexpr u32    CentralDirectorySignature = 0x02014b50;
constexpr u32    LocalHeaderSignature = 0x04034b50;
constexpr size_t EndOfCentralDirectorySize = 22;
constexpr size_t CentralDirectoryEntrySize = 46;
constexpr size_t LocalHeaderSize = 30;
// The end of central directory record can be followed by a comment of at
// most this many bytes.
constexpr size_t MaxCommentSize = 0xFFFF;

constexpr u16 Stored = 0;
constexpr u16 Deflated = 8;

LibCError zip_error(std::string const &what)
{
    return LibCError { std::format("Invalid ZIP archive: {}", what) };
}

// Little-endian numbers at `offset`, or nothing if they'd run past the end.
std::optional<u16> read_u16(std::string_view data, size_t offset)
{
    if (offset + 2 > data.size()) {
        return { };
    }
    return static_cast<u16>(static_cast<u8>(data[offset]) | (static_cast<u8>(data[offset + 1]) << 8));
}

std::optional<u32> read_u32(std::string_view data, size_t offset)
{
    auto const low = read_u16(data, offset);
    auto const high = read_u16(data, offset + 2);
    if (!low || !high) {
        return { };
    }
    return static_cast<u32>(*low) | (static_cast<u32>(*high) << 16);
}

struct Entry {
    std::string name;
    u16         method { 0 };
    u32         crc { 0 };
    u32         compressed_size { 0 };
    u32         size { 0 };
    u32         local_header_offset { 0 };
};

std::expected<std::vector<Entry>, LibCError> read_directory(std::string_view zip)
{
    if (zip.size() < EndOfCentralDirectorySize) {
        return std::unexpected(zip_error("too short"));
    }
    // Search back from the end, past a comment if there is one.
    std::optional<size_t> end_record { };
    auto const            last = zip.size() - EndOfCentralDirectorySize;
    auto const            first = (last > MaxCommentSize) ? last - MaxCommentSize : 0;
    for (auto at = last + 1; at-- > first;) {
        if (read_u32(zip, at) == EndOfCentralDirectorySignature) {
            end_record = at;
            break;
        }
    }
    if (!end_record) {
        return std::unexpected(zip_error("no end of central directory"));
    }
    auto const count = read_u16(zip, *end_record + 10).value_or(0);
    size_t     at = read_u32(zip, *end_record + 16).value_or(0);

    std::vector<Entry> entries;
    for (u16 ix = 0; ix < count; ++ix) {
        if (read_u32(zip, at) != CentralDirectorySignature || at + CentralDirectoryEntrySize > zip.size()) {
            return std::unexpected(zip_error("bad central directory entry"));
        }
        auto const name_length = *read_u16(zip, at + 28);
        auto const extra_length = *read_u16(zip, at + 30);
        auto const comment_length = *read_u16(zip, at + 32);
        if (at + CentralDirectoryEntrySize + name_length > zip.size()) {
            return std::unexpected(zip_error("entry name runs past the end"));
        }
        entries.push_back(Entry {
            .name = std::string { zip.substr(at + CentralDirectoryEntrySize, name_length) },
            .method = *read_u16(zip, at + 10),
            .crc = *read_u32(zip, at + 16),
            .compressed_size = *read_u32(zip, at + 20),
            .size = *read_u32(zip, at + 24),
            .local_header_offset = *read_u32(zip, at + 42),
        });
        at += CentralDirectoryEntrySize + name_length + extra_length + comment_length;
    }
    return entries;
}

std::expected<std::string, LibCError> inflate_raw(std::string_view compressed, size_t size)
{
    std::string out(size, '\0');
    z_stream    stream { };
    // Negative window bits: raw deflate data, without a zlib header.
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        return std::unexpected(zip_error("can't initialize zlib"));
    }
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(compressed.data()));
    stream.avail_in = static_cast<uInt>(compressed.size());
    stream.next_out = reinterpret_cast<Bytef *>(out.data());
    stream.avail_out = static_cast<uInt>(out.size());
    auto const result = inflate(&stream, Z_FINISH);
    auto const written = stream.total_out;
    inflateEnd(&stream);
    if (result != Z_STREAM_END || written != size) {
        return std::unexpected(zip_error("corrupt deflated data"));
    }
    return out;
}

std::expected<std::string, LibCError> extract(std::string_view zip, Entry const &entry)
{
    size_t const header = entry.local_header_offset;
    if (read_u32(zip, header) != LocalHeaderSignature || header + LocalHeaderSize > zip.size()) {
        return std::unexpected(zip_error(std::format("bad local header for `{}`", entry.name)));
    }
    // The local header's name and extra field can differ in length from
    // the central directory's.
    auto const data_offset = header + LocalHeaderSize + *read_u16(zip, header + 26) + *read_u16(zip, header + 28);
    if (data_offset + entry.compressed_size > zip.size()) {
        return std::unexpected(zip_error(std::format("`{}` runs past the end", entry.name)));
    }
    auto const data = zip.substr(data_offset, entry.compressed_size);

    std::string contents;
    switch (entry.method) {
    case Stored:
        contents = std::string { data };
        break;
    case Deflated: {
        auto inflated = inflate_raw(data, entry.size);
        if (!inflated) {
            return std::unexpected(inflated.error());
        }
        contents = std::move(*inflated);
    } break;
    default:
        return std::unexpected(zip_error(std::format("`{}` uses compression method {}", entry.name, entry.method)));
    }
    if (contents.size() != entry.size
        || crc32(0, reinterpret_cast<Bytef const *>(contents.data()), static_cast<uInt>(contents.size())) != entry.crc) {
        return std::unexpected(zip_error(std::format("`{}` fails its checksum", entry.name)));
    }
    return contents;
}

}

std::expected<std::vector<std::string>, LibCError> zip_entry_names(std::string_view zip)
{
    auto entries = read_directory(zip);
    if (!entries) {
        return std::unexpected(entries.error());
    }
    std::vector<std::string> names;
    for (auto &entry : *entries) {
        names.push_back(std::move(entry.name));
    }
    return names;
}

std::expected<std::string, LibCError> unzip(std::string_view zip, std::string_view name)
{
    auto entries = read_directory(zip);
    if (!entries) {
        return std::unexpected(entries.error());
    }
    for (auto const &entry : *entries) {
        if (entry.name == name) {
            return extract(zip, entry);
        }
    }
    return std::unexpected(LibCError { std::format("ZIP archive has no `{}`", name) });
}

}
