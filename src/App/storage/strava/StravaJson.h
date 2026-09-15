/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Shared JSON-DTO infrastructure for the zig/connect/strava translation
 * (the *.zig files under zig/connect/strava). This is not a translation of a
 * specific zig file: it exists
 * because zig gets `std.json.parseFromSliceLeaky`/`std.json.fmt` "for free"
 * via reflection over every struct's fields, whereas src/JSON.h expects each
 * type to provide its own `static Decoded<T> decode(JSONValue const &)` and
 * `JSONValue encode() const`.
 *
 * Every struct in this directory is a flat bag of `?T` (optional) fields
 * mirroring a Strava API JSON object 1:1, so rather than hand-writing the
 * same decode/encode boilerplate ~250 times, each struct declares its field
 * list once via an X-macro (the same idiom already used for e.g.
 * `ENUMERATE_LOG_LEVELS` in src/Logging.h and `FITBASETYPE` in src/FIT.h) and
 * ST_STRAVA_FIELD_MEMBER/_DECODE/_ENCODE below expand it into the member
 * declarations and the two JSON functions.
 *
 * Strava's enums are JSON *strings* (e.g. `"Run"`), not integers, so they
 * need a name-to-string table; decode_enum()/encode_enum() below is that
 * lookup, generic over any `enum class` + `std::span` of (value, name)
 * pairs. Individual enums (see Enums.h) each specialize `ST::decode<T>` /
 * `ST::encode<T>` in terms of it, which is what lets the same
 * ST_STRAVA_FIELD_* macros treat scalar fields, nested-struct fields, and
 * enum fields identically: they all go through `JSONValue::convert<Target>`
 * and `ST::encode<Target>`.
 */

#pragma once

#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Expected.h>
#include <IO.h>
#include <JSON.h>

#include <Date.h>

namespace ST {

// src/JSON.h has no notion of "the field is JSON itself, don't try to
// convert it to anything", which a couple of Strava responses need (e.g.
// PhotosSummaryPrimary.urls, a `Map<String,String>` Strava doesn't document
// the shape of). JSONValue::convert<Target>'s fully-generic fallback already
// does `t = decode<Target>(*this)`, so specializing decode<JSONValue> to be
// the identity is all a `std::optional<JSONValue>` field needs; encode<T> for
// T = JSONValue already has an identity specialization in src/JSON.h.
template<>
inline Decoded<JSONValue> decode<JSONValue>(JSONValue const &json)
{
    return json;
}

// std::vector<bool> is bit-packed, so its const_reference is a proxy object,
// not bool const&; the generic encode<std::vector<Element>>() in src/JSON.h
// binds to that proxy and then fails to call .encode() on it. Specializing
// for Element = bool avoids that (decode<std::vector<bool>>() does not have
// the same problem: JSONValue::convert<std::vector<T>>() decodes into a
// plain local bool before pushing it, never taking a reference to an
// existing element).
template<>
inline JSONValue encode<bool>(std::vector<bool> const &value)
{
    auto ret = JSONValue::array();
    for (bool v : value) {
        ret.append(JSONValue { v });
    }
    return ret;
}

// Generic string<->enum lookup used by every enum in Enums.h. `table` is a
// `static constexpr std::pair<E, std::string_view>[]` listing every
// enumerator and the exact string Strava's API uses for it (which is not
// always spelled the same as the C++ identifier, e.g. ClimbCategoryDesc).
template<typename E>
Decoded<E> decode_enum(JSONValue const &json, std::span<std::pair<E, std::string_view> const> table, std::string_view const &enum_name)
{
    if (!json.is_string()) {
        return std::unexpected(JSONError::expected(JSONType::String, json.type(), enum_name));
    }
    std::string s;
    TRY(json.convert(s));
    for (auto const &[value, name] : table) {
        if (name == s) {
            return value;
        }
    }
    return std::unexpected(JSONError { JSONError::Code::TypeMismatch, std::format("Unknown {} value `{}`", enum_name, s) });
}

template<typename E>
JSONValue encode_enum(E value, std::span<std::pair<E, std::string_view> const> table)
{
    for (auto const &[v, name] : table) {
        if (v == value) {
            return JSONValue { name };
        }
    }
    return { };
}

}

// Declares `std::optional<Type> Name{};` for one field. `Type` is the
// field's *unwrapped* type: every field in this directory is optional in
// zig (`field: ?T = null`), so the optional wrapper is implicit here.
#define ST_STRAVA_FIELD_MEMBER(Type, Name) std::optional<Type> Name { };

// Decodes one field from `json` (a JSONValue const&) into `ret` (an
// in-scope instance of the enclosing struct), skipping it entirely if the
// key is absent (matching zig's `?T = null` defaults).
#define ST_STRAVA_FIELD_DECODE(Type, Name)               \
    if (auto opt_##Name = json.get(#Name); opt_##Name) { \
        Type value_##Name { };                           \
        TRY(opt_##Name->convert(value_##Name));          \
        ret.Name = std::move(value_##Name);              \
    }

// Encodes one field into `obj` (an in-scope JSONValue object), omitting it
// if unset (ST::set(JSONValue&, key, optional<T> const&) already does this).
#define ST_STRAVA_FIELD_ENCODE(Type, Name) ST::set(obj, #Name, Name);
