/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cstdint>
#include <string_view>

#include <JSON.h>

#include <TestApp.h>

using namespace ST;

namespace {

JSONValue number(std::string_view text)
{
    auto const json = JSONValue::deserialize(text);
    CHECK(json.has_value());
    return json.value_or(JSONValue { });
}

void test_integers()
{
    CHECK(number("42").is_integer());
    int64_t value { 0 };
    CHECK(number("-80").convert(value).has_value());
    CHECK_EQ(value, -80);
    CHECK(number("24790584050").convert(value).has_value());
    CHECK_EQ(value, 24790584050);
    i8 small { 0 };
    CHECK(number("-128").convert(small).has_value());
    CHECK_EQ(small, -128);
    CHECK(!number("-129").convert(small).has_value());
    CHECK(!number("128").convert(small).has_value());
    CHECK(!number("-1.5e3").convert(small).has_value());
}

// Garmin Connect's activity lists have negative coordinates, and doubles
// with exponents.
void test_decimals()
{
    double value { 0 };
    CHECK(number("-80.4925").convert(value).has_value());
    CHECK_EQ(value, -80.4925);
    CHECK(number("1.5e3").convert(value).has_value());
    CHECK_EQ(value, 1500.0);
    CHECK(number("2.5E-2").convert(value).has_value());
    CHECK_EQ(value, 0.025);
    CHECK(number("-1e+2").convert(value).has_value());
    CHECK_EQ(value, -100.0);
    // Too big for an integer.
    CHECK(number("123456789012345678901234567890").convert(value).has_value());
    CHECK(value > 1e29);
}

void test_in_objects_and_arrays()
{
    auto const json = number(R"({"startLongitude": -80.4925, "list": [-1, 2.5e1, -0.5]})");
    CHECK_EQ(json.try_get<double>("startLongitude").value_or(0), -80.4925);
    auto const list = json.try_get_array<double>("list");
    CHECK(list.has_value() && list->size() == 3);
    if (list && list->size() == 3) {
        CHECK_EQ(list->at(0), -1.0);
        CHECK_EQ(list->at(1), 25.0);
        CHECK_EQ(list->at(2), -0.5);
    }
}

void test_not_numbers()
{
    for (auto const *text : { "-", "[-]", "[1.]", "[.5]", "[1e]", "[--1]" }) {
        CHECK(!JSONValue::deserialize(text).has_value());
    }
}

}

int main()
{
    test_integers();
    test_decimals();
    test_in_objects_and_arrays();
    test_not_numbers();
    return ST::Test::test_result();
}
