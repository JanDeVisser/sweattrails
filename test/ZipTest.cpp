/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <string>
#include <string_view>
#include <vector>

#include <zlib.h>

#include <Logging.h>
#include <Zip.h>

#include <TestApp.h>

using namespace ST;

namespace {

void put_u16(std::string &out, u16 v)
{
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>(v >> 8));
}

void put_u32(std::string &out, u32 v)
{
    put_u16(out, static_cast<u16>(v & 0xFFFF));
    put_u16(out, static_cast<u16>(v >> 16));
}

std::string deflate_raw(std::string_view data)
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
std::string make_zip(std::vector<ZipFile> const &files, std::string_view comment = { })
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

std::string const Text = "A FIT file would be binary, but any bytes will do. Repeat, repeat, repeat, repeat.";

std::string const TwoFiles = make_zip({
    { .name = "notes.txt", .contents = "Just stored." },
    { .name = "24790584050_ACTIVITY.fit", .contents = Text, .deflated = true },
});

void test_entry_names()
{
    auto const names = zip_entry_names(TwoFiles);
    CHECK(names.has_value());
    if (names) {
        CHECK_EQ(names->size(), 2uz);
        CHECK_EQ(names->at(0), "notes.txt");
        CHECK_EQ(names->at(1), "24790584050_ACTIVITY.fit");
    }
}

void test_stored_and_deflated()
{
    auto const stored = unzip(TwoFiles, "notes.txt");
    CHECK(stored.has_value());
    CHECK_EQ(stored.value_or(""), "Just stored.");
    auto const deflated = unzip(TwoFiles, "24790584050_ACTIVITY.fit");
    CHECK(deflated.has_value());
    CHECK_EQ(deflated.value_or(""), Text);
}

void test_empty_entry()
{
    auto const zip = make_zip({ { .name = "empty", .contents = "" } });
    auto const empty = unzip(zip, "empty");
    CHECK(empty.has_value());
    CHECK_EQ(empty.value_or("x"), "");
}

// The end of central directory record is found behind a comment.
void test_archive_with_a_comment()
{
    auto const zip = make_zip({ { .name = "a", .contents = "b", .deflated = true } }, "Downloaded from Garmin Connect");
    CHECK_EQ(unzip(zip, "a").value_or(""), "b");
}

void test_missing_entry()
{
    CHECK(!unzip(TwoFiles, "other.fit").has_value());
}

void test_not_a_zip()
{
    CHECK(!zip_entry_names("").has_value());
    CHECK(!zip_entry_names("<html>Service unavailable</html>").has_value());
}

// Cut off anywhere, or with its data damaged, an archive is an error, not
// a crash or garbage.
void test_truncated_and_corrupt()
{
    for (size_t length = 0; length < TwoFiles.size(); ++length) {
        auto const cut = std::string_view { TwoFiles }.substr(0, length);
        auto const contents = unzip(cut, "24790584050_ACTIVITY.fit");
        CHECK(!contents.has_value() || *contents == Text);
    }
    auto corrupt = TwoFiles;
    // The deflated data starts after notes.txt's header, data, and its own
    // header: flip a byte in the middle of it.
    auto const at = corrupt.find("24790584050_ACTIVITY.fit") + 24 + 4 + 5;
    corrupt[at] = static_cast<char>(corrupt[at] ^ 0x55);
    auto const damaged = unzip(corrupt, "24790584050_ACTIVITY.fit");
    CHECK(!damaged.has_value());
}

}

int main()
{
    test_entry_names();
    test_stored_and_deflated();
    test_empty_entry();
    test_archive_with_a_comment();
    test_missing_entry();
    test_not_a_zip();
    test_truncated_and_corrupt();
    return ST::Test::test_result();
}
