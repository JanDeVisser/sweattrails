/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <string>
#include <string_view>
#include <vector>

#include <Logging.h>
#include <Zip.h>

#include <TestApp.h>
#include <TestZip.h>

using namespace ST;
using namespace ST::Test;

namespace {

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
    // zip(1) stores empty files, but a deflated one is valid too.
    auto const deflated = make_zip({ { .name = "empty", .contents = "", .deflated = true } });
    CHECK_EQ(unzip(deflated, "empty").value_or("x"), "");
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

// A damaged size, here 4 GB, is an error, not 4 GB of memory.
void test_impossible_size()
{
    auto zip = make_zip({ { .name = "a.fit", .contents = Text, .deflated = true } });
    // The size is at offset 24 in the central directory entry.
    auto const entry = zip.find(std::string { "\x50\x4b\x01\x02", 4 });
    CHECK(entry != std::string::npos);
    for (size_t ix = 0; ix < 4; ++ix) {
        zip[entry + 24 + ix] = '\xFF';
    }
    CHECK(!unzip(zip, "a.fit").has_value());
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
    test_impossible_size();
    return ST::Test::test_result();
}
