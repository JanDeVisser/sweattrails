/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Builds ZIP archives in memory, for tests of code that reads them.
 */

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <zlib.h>

#include <Logging.h>

namespace ST::Test {

inline void put_u16(std::string &out, u16 v)
{
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>(v >> 8));
}

inline void put_u32(std::string &out, u32 v)
{
    put_u16(out, static_cast<u16>(v & 0xFFFF));
    put_u16(out, static_cast<u16>(v >> 16));
}

inline std::string deflate_raw(std::string_view data)
{
    z_stream stream { };
    deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    std::string out(deflateBound(&stream, static_cast<uLong>(data.size())), '\0');
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.data()));
    stream.avail_in = static_cast<uInt>(data.size());
    stream.next_out = reinterpret_cast<Bytef *>(out.data());
    stream.avail_out = static_cast<uInt>(out.size());
    deflate(&stream, Z_FINISH);
    out.resize(stream.total_out);
    deflateEnd(&stream);
    return out;
}

struct ZipFile {
    std::string name;
    std::string contents;
    bool        deflated { false };
};

// A ZIP archive as zip(1) writes one: local headers and data, the central
// directory, and the end of central directory record, with `comment`.
inline std::string make_zip(std::vector<ZipFile> const &files, std::string_view comment = { })
{
    std::string zip;
    std::string directory;
    for (auto const &file : files) {
        auto const data = file.deflated ? deflate_raw(file.contents) : file.contents;
        auto const crc = static_cast<u32>(crc32(0, reinterpret_cast<Bytef const *>(file.contents.data()), static_cast<uInt>(file.contents.size())));
        auto const method = static_cast<u16>(file.deflated ? 8 : 0);
        auto const offset = static_cast<u32>(zip.size());

        put_u32(zip, 0x04034b50);
        put_u16(zip, 20);
        put_u16(zip, 0);
        put_u16(zip, method);
        put_u32(zip, 0); // Time and date
        put_u32(zip, crc);
        put_u32(zip, static_cast<u32>(data.size()));
        put_u32(zip, static_cast<u32>(file.contents.size()));
        put_u16(zip, static_cast<u16>(file.name.size()));
        put_u16(zip, 4); // An extra field, which the reader has to skip
        zip += file.name;
        zip += "XXXX";
        zip += data;

        put_u32(directory, 0x02014b50);
        put_u16(directory, 20);
        put_u16(directory, 20);
        put_u16(directory, 0);
        put_u16(directory, method);
        put_u32(directory, 0);
        put_u32(directory, crc);
        put_u32(directory, static_cast<u32>(data.size()));
        put_u32(directory, static_cast<u32>(file.contents.size()));
        put_u16(directory, static_cast<u16>(file.name.size()));
        put_u16(directory, 0);
        put_u16(directory, 0);
        put_u16(directory, 0);
        put_u16(directory, 0);
        put_u32(directory, 0);
        put_u32(directory, offset);
        directory += file.name;
    }
    auto const directory_offset = static_cast<u32>(zip.size());
    zip += directory;
    put_u32(zip, 0x06054b50);
    put_u16(zip, 0);
    put_u16(zip, 0);
    put_u16(zip, static_cast<u16>(files.size()));
    put_u16(zip, static_cast<u16>(files.size()));
    put_u32(zip, static_cast<u32>(directory.size()));
    put_u32(zip, directory_offset);
    put_u16(zip, static_cast<u16>(comment.size()));
    zip += comment;
    return zip;
}

}
