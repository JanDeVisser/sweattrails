/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <bit>
#include <concepts>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <Error.h>
#include <Expected.h>
#include <JSON.h>
#include <fit/FIT.h>

#include <Date.h>
#include <Format.h>
#include <Map.h>
#include <Range.h>

namespace ST {

namespace fs = std::filesystem;

using namespace ST::FIT;

struct Storage;
struct Activity;
struct Activities;

// Zig returns `!T`, i.e. the inferred error set of everything that can go wrong
// downstream. Here that is spelled out as a variant of the error types the
// storage layer can produce.
struct StorageError {
    using ErrorVariant = std::variant<LibCError, JSONError, FITError>;

    ErrorVariant error;

    StorageError(StorageError const &) = default;
    StorageError(StorageError &&) = default;
    StorageError &operator=(StorageError const &) = default;
    StorageError &operator=(StorageError &&) = default;

    // Constrained (and not explicit) so that copy/move construction is not
    // hijacked and `std::unexpected(some_lib_c_error)` just works.
    template<typename E>
        requires std::constructible_from<ErrorVariant, E>
    StorageError(E const &e)
        : error(e)
    {
    }

    [[nodiscard]] std::string to_string() const
    {
        return std::visit(
            overloaded {
                [](LibCError const &e) { return e.to_string(); },
                [](JSONError const &e) { return e.to_string(); },
                [](FITError const &e) { return std::format("FIT error {}", static_cast<int>(e)); },
            },
            error);
    }
};

template<typename T>
using StorageResult = std::expected<T, StorageError>;

// union(enum) { Root, Year: i16, Month: Month }
struct RootIndex {
    bool operator==(RootIndex const &) const = default;
};

struct YearIndex {
    i16 year { 0 };

    bool operator==(YearIndex const &) const = default;
};

struct Year {
    size_t                              year { 0 };
    std::optional<std::array<bool, 12>> months { };

    static bool less_than(Year const &y1, Year const &y2)
    {
        return y1.year < y2.year;
    }
};

using Years = std::vector<Year>;

struct Month {
    size_t          year { 0 };
    DateTime::Month month { DateTime::Month::January };

    Month() = default;
    Month(size_t the_year, DateTime::Month the_month)
        : year(the_year)
        , month(the_month)
    {
    }

    auto                                    operator<=>(Month const &) const = default;
    [[nodiscard]] std::optional<fs::path>   dir(Storage const &storage) const;
    [[nodiscard]] StorageResult<bool>       has_activities(Storage const &storage) const;
    [[nodiscard]] StorageResult<Activities> list(Storage &storage) const;
    [[nodiscard]] std::string               dir_name() const;
};

using Index = std::variant<RootIndex, YearIndex, Month>;

enum class ActivityFile : u8 {
    Fit = 0,
    Strava,
    SweatTrails,
    Count,
};

// std.EnumSet(ActivityFiles)
struct FileTypes {
    u8 bits { 0 };

    static constexpr u8 mask(ActivityFile f)
    {
        return static_cast<u8>(1 << static_cast<u8>(f));
    }

    static FileTypes one(ActivityFile f)
    {
        return FileTypes { mask(f) };
    }

    [[nodiscard]] bool contains(ActivityFile f) const
    {
        return (bits & mask(f)) != 0;
    }

    void insert(ActivityFile f)
    {
        bits |= mask(f);
    }

    void remove(ActivityFile f)
    {
        bits &= static_cast<u8>(~mask(f));
    }

    void set_union(FileTypes other)
    {
        bits |= other.bits;
    }

    [[nodiscard]] size_t count() const
    {
        return static_cast<size_t>(std::popcount(bits));
    }

    auto operator<=>(FileTypes const &) const = default;
};

enum class LoadingDepth {
    None,
    Shallow,
    Deep,
};

struct ActivityID {
    Month       month { };
    std::string name { };
    FileTypes   files { };

    ActivityID() = default;
    ActivityID(Month const &the_month, std::string_view const &the_name, FileTypes the_files)
        : month(the_month)
        , name(the_name)
        , files(the_files)
    {
    }

    auto operator<=>(ActivityID const &) const = default;

    static ActivityID dummy(ActivityFile file_type)
    {
        return ActivityID { Month { 2020, DateTime::Month::January }, "dummy", FileTypes::one(file_type) };
    }

    [[nodiscard]] StorageResult<std::optional<Activity>> load(Storage &storage, LoadingDepth depth) const;
};

}

inline std::ostream &operator<<(std::ostream &os, ST::Month const &month)
{
    os << month.year << "-" << static_cast<int>(month.month);
    return os;
}

inline std::ostream &operator<<(std::ostream &os, ST::ActivityID const &activity)
{
    os << activity.month << " " << activity.name;
    return os;
}

template<>
struct std::formatter<ST::ActivityID> : public std::formatter<std::string> {
    template<class FmtContext>
    typename FmtContext::iterator format(ST::ActivityID const &value, FmtContext &ctx) const
    {
        std::ostringstream out;
        out << value.month.year << "-" << ST::month_name(value.month.month) << " `" << value.name << "`";
        return std::ranges::copy(std::move(out).str(), ctx.out()).out;
    }
};
