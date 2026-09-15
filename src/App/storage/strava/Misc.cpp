/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/connect/strava/misc.zig.
 */

#include "Misc.h"

namespace ST::strava {

Decoded<Waypoint> Waypoint::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "Waypoint"));
    }
    Waypoint ret { };
    ST_STRAVA_FIELDS_Waypoint(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue Waypoint::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_Waypoint(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<Route> Route::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "Route"));
    }
    Route ret { };
    ST_STRAVA_FIELDS_Route(ST_STRAVA_FIELD_DECODE) if (auto opt_private = json.get("private"); opt_private)
    {
        bool value_private = false;
        TRY(opt_private->convert(value_private));
        ret.is_private = value_private;
    }
    return ret;
}

JSONValue Route::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_Route(ST_STRAVA_FIELD_ENCODE)
        ST::set(obj, "private", is_private);
    return obj;
}

Decoded<Comment> Comment::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "Comment"));
    }
    Comment ret { };
    ST_STRAVA_FIELDS_Comment(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue Comment::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_Comment(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<Upload> Upload::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "Upload"));
    }
    Upload ret { };
    ST_STRAVA_FIELDS_Upload(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue Upload::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_Upload(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<Error> Error::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "Error"));
    }
    Error ret { };
    ST_STRAVA_FIELDS_Error(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue Error::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_Error(ST_STRAVA_FIELD_ENCODE) return obj;
}

Decoded<Fault> Fault::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError::expected(JSONType::Object, json.type(), "Fault"));
    }
    Fault ret { };
    ST_STRAVA_FIELDS_Fault(ST_STRAVA_FIELD_DECODE) return ret;
}

JSONValue Fault::encode() const
{
    auto obj = JSONValue::object();
    ST_STRAVA_FIELDS_Fault(ST_STRAVA_FIELD_ENCODE) return obj;
}

}
