
#pragma once

#include <format>
#include <sstream>
#include <string>

#include <Error.h>
#include <Logging.h>

namespace ST {

struct Box;
struct Tile;

struct Coordinates {
    float32 lat { 0 };
    float32 lon { 0 };

    [[nodiscard]] bool        in_box(Box const &box) const;
    [[nodiscard]] std::string to_string() const;

    bool operator==(Coordinates const &) const = default;
};

struct Box {
    Coordinates sw { };
    Coordinates ne { };

    [[nodiscard]] Box         with_margins(float32 margin) const;
    [[nodiscard]] Coordinates center() const;
    [[nodiscard]] float32     north() const { return ne.lat; }
    [[nodiscard]] float32     south() const { return sw.lat; }
    [[nodiscard]] float32     east() const { return ne.lon; }
    [[nodiscard]] float32     west() const { return sw.lon; }
    [[nodiscard]] float32     width() const;
    [[nodiscard]] float32     height() const;
    [[nodiscard]] bool        contains(Box const &other) const;
    [[nodiscard]] bool        has(Coordinates const &point) const;
    void                      extend(Coordinates const &point);
    [[nodiscard]] std::string to_string() const;

    bool operator==(Box const &) const = default;
};

}

template<>
struct std::formatter<ST::Coordinates> : std::formatter<std::string> {
    template<class FmtContext>
    FmtContext::iterator format(ST::Coordinates const &val, FmtContext &ctx) const
    {
        std::ostringstream out;
        out << val.to_string();
        return std::ranges::copy(std::move(out).str(), ctx.out()).out;
    }
};

template<>
struct std::formatter<ST::Box> : std::formatter<std::string> {
    template<class FmtContext>
    FmtContext::iterator format(ST::Box const &val, FmtContext &ctx) const
    {
        std::ostringstream out;
        out << val.to_string();
        return std::ranges::copy(std::move(out).str(), ctx.out()).out;
    }
};
