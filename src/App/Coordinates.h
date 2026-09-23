
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

    [[nodiscard]] bool in_box(Box const &box) const;

    bool operator==(Coordinates const &) const = default;
};

struct Box {
    Coordinates sw { };
    Coordinates ne { };

    [[nodiscard]] Box         with_margins(float32 margin) const;
    [[nodiscard]] Coordinates center() const;
    [[nodiscard]] float32     width() const;
    [[nodiscard]] float32     height() const;
    [[nodiscard]] bool        contains(Box const &other) const;
    [[nodiscard]] bool        has(Coordinates const &point) const;
    void                      extend(Coordinates const &point);

    bool operator==(Box const &) const = default;
};

}

template<>
struct std::formatter<ST::Coordinates> : std::formatter<std::string> {
    template<class FmtContext>
    FmtContext::iterator format(ST::Coordinates const &val, FmtContext &ctx) const
    {
        std::ostringstream out;
        out << "(" << val.lat << "º, " << val.lon << "º)";
        return std::ranges::copy(std::move(out).str(), ctx.out()).out;
    }
};
