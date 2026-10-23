/*
 * Copyright (c) 2024, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <charconv>
#include <expected>

#include <IO.h>
#include <JSON.h>
#include <Lexer.h>

namespace ST {

using namespace std::literals;

[[nodiscard]] std::string JSONValue::to_string() const
{
    switch (type()) {
    case JSONType::Null:
        return "null";
    case JSONType::String:
        return std::get<std::string>(m_value);
    case JSONType::Integer:
        return std::to_string(std::get<int64_t>(m_value));
    case JSONType::Boolean:
        return std::get<bool>(m_value) ? "true" : "false";
    case JSONType::Double:
        return std::to_string(std::get<double>(m_value));
    case JSONType::Array: {
        auto const &array = std::get<Array>(m_value);
        if (array.empty())
            return "[]";
        std::string ret = "[ ";
        bool        first = true;
        for (auto const &v : array) {
            if (!first)
                ret += ", ";
            first = false;
            ret += v.serialize();
        }
        return ret + " ]";
    }
    case JSONType::Object: {
        auto const &object = std::get<Object>(m_value);
        if (object.empty())
            return "{}";
        std::string ret = "{ ";
        bool        first = true;
        for (auto const &v : object) {
            if (!first)
                ret += ", ";
            first = false;
            ret += "\"" + v.first + "\": " + v.second.serialize();
        }
        return ret + " }";
    }
    }
    UNREACHABLE();
}

[[nodiscard]] std::string JSONValue::serialize(bool pretty, int indent_width, int indent) const
{
    switch (type()) {
    case JSONType::Null:
    case JSONType::Integer:
    case JSONType::Double:
    case JSONType::Boolean:
        return to_string();
    case JSONType::String: {
        auto s = to_string();
        replace_all(s, "\r", R"(\r)");
        replace_all(s, "\n", R"(\n)");
        replace_all(s, "\t", R"(\t)");
        replace_all(s, "\"", R"(\")");
        return "\"" + s + "\"";
    }
    case JSONType::Array: {
        auto const &array = std::get<Array>(m_value);
        if (array.empty())
            return "[]";
        std::string ret = "[";
        bool        first = true;
        for (auto const &v : array) {
            if (!first)
                ret += ",";
            if (pretty) {
                ret += "\n";
                for (auto i = 0; i < indent + indent_width; ++i)
                    ret += ' ';
            } else {
                ret += ' ';
            }
            first = false;
            ret += v.serialize(pretty, indent_width, indent + indent_width);
        }
        if (pretty) {
            ret += "\n";
            for (auto i = 0; i < indent; ++i)
                ret += ' ';
        } else {
            ret += ' ';
        }
        return ret + "]";
    }
    case JSONType::Object: {
        auto const &object = std::get<Object>(m_value);
        if (object.empty())
            return "{}";
        std::string ret = "{";
        bool        first = true;
        for (auto const &v : object) {
            if (!first)
                ret += ",";
            first = false;
            if (pretty) {
                ret += "\n";
                for (auto i = 0; i < indent + indent_width; ++i)
                    ret += ' ';
            } else {
                ret += ' ';
            }
            ret += "\"" + v.first + "\": " + v.second.serialize();
        }
        if (pretty) {
            ret += "\n";
            for (auto i = 0; i < indent; ++i)
                ret += ' ';
        } else {
            ret += ' ';
        }
        return ret + "}";
    }
    }
    UNREACHABLE();
}

#define JSONKEYWORD(S) \
    S(True, "true")    \
    S(False, "false")  \
    S(Null, "null")

enum class JSONKeyword {
#undef S
#define S(kw, str) kw,
    JSONKEYWORD(S)
#undef S
};

std::expected<JSONValue, JSONValue::ReadError> JSONValue::read_file(std::string_view const &file_name)
{
    trace(JSON, "Reading JSON file '{}'", file_name);
    auto json_text_maybe = read_file_by_name(file_name);
    if (!json_text_maybe) {
        log_error("Error reading JSON file '{}': {}", file_name, json_text_maybe.error().to_string());
        return std::unexpected(ReadError { json_text_maybe.error() });
    }
    auto json_maybe = JSONValue::deserialize(json_text_maybe.value());
    if (!json_maybe) {
        log_error("Error parsing JSON file '{}': {}", file_name, json_maybe.error().to_string());
        return std::unexpected(ReadError { json_maybe.error() });
    }
    return json_maybe.value();
}

// The lexer's own numbers have no sign or exponent: it lexes `-80.5` as a
// symbol and a number, and `1.5e3` as a number and an identifier. JSON
// numbers have both, so they're scanned here, before the lexer gets to
// them: -?(0|[1-9][0-9]*)(.[0-9]+)?([eE][+-]?[0-9]+)?
struct JSONKeywords : public EnumKeywords<std::string_view, SimpleKeywordCategory, JSONKeyword> {
    std::optional<std::tuple<Token, size_t>> pre_match(std::string_view const &buffer, size_t index)
    {
        auto       at = index;
        auto const digit = [&buffer](size_t ix) { return ix < buffer.length() && isdigit(buffer[ix]); };
        auto const skip_digits = [&buffer, &at, &digit]() {
            auto const start = at;
            while (digit(at)) {
                ++at;
            }
            return at > start;
        };
        if (at < buffer.length() && buffer[at] == '-') {
            ++at;
        }
        if (!skip_digits()) {
            return { };
        }
        auto type = NumberType::Integer;
        if (at < buffer.length() && buffer[at] == '.' && digit(at + 1)) {
            ++at;
            skip_digits();
            type = NumberType::Decimal;
        }
        if (at < buffer.length() && (buffer[at] == 'e' || buffer[at] == 'E')) {
            auto exponent = at + 1;
            if (exponent < buffer.length() && (buffer[exponent] == '+' || buffer[exponent] == '-')) {
                ++exponent;
            }
            if (digit(exponent)) {
                at = exponent;
                skip_digits();
                type = NumberType::Decimal;
            }
        }
        return std::tuple { Token::number(type), at - index };
    }
};

using JSONLexer = Lexer<std::string_view, JSONKeywords, char>;
using JSONToken = JSONLexer::Token;

std::expected<JSONValue, JSONError> decode_number(JSONToken const &token, std::string_view const &str)
{
    auto const text = str.substr(token.location.index, token.location.length);
    auto const invalid = [&token, &text]() {
        return std::unexpected(JSONError {
            JSONError::Code::SyntaxError,
            std::format("Invalid number '{}'", text),
            static_cast<int>(token.location.line),
            static_cast<int>(token.location.column),
        });
    };
    if (token.number_type() == NumberType::Integer) {
        int64_t integer { 0 };
        auto const [end, ec] = std::from_chars(text.data(), text.data() + text.size(), integer);
        if (ec == std::errc { } && end == text.data() + text.size()) {
            return JSONValue { integer };
        }
        // Too big for an integer: it can still be a double.
    }
    double number { 0 };
    auto const [end, ec] = std::from_chars(text.data(), text.data() + text.size(), number);
    if (ec != std::errc { } || end != text.data() + text.size()) {
        return invalid();
    }
    return JSONValue { number };
}

std::expected<JSONValue, JSONError> decode_value(JSONLexer &lexer, std::string_view const &str)
{
    auto decode_string = [str](JSONToken const &token) -> std::expected<std::string, JSONError> {
        if (token != TokenKind::QuotedString) {
            return std::unexpected(JSONError {
                JSONError::Code::SyntaxError,
                "Expected quoted string",
                static_cast<int>(token.location.line),
                static_cast<int>(token.location.column),
            });
        }
        if (!token.quoted_string().terminated) {
            return std::unexpected(JSONError {
                JSONError::Code::SyntaxError,
                "Unterminated string",
                static_cast<int>(token.location.line),
                static_cast<int>(token.location.column),
            });
        }
        trace(JSON, "decode_string({})", token);
        if (token.location.length > 2) {
            std::string qstr { str.substr(token.location.index + 1, token.location.length - 2) };
            replace_all(qstr, R"(\r)", "\r"sv);
            replace_all(qstr, R"(\n)", "\n"sv);
            replace_all(qstr, R"(\t)", "\t"sv);
            replace_all(qstr, R"(\")", R"(")");
            replace_all(qstr, R"(\')", "'"sv);
            return { qstr };
        }
        return std::string { };
    };

    auto expect_symbol = [&lexer](int sym) -> Error<JSONError> {
        auto err = lexer.expect_symbol(sym);
        if (!err) {
            auto const &error_token = lexer.peek();
            return std::unexpected(JSONError {
                JSONError::Code::SyntaxError,
                std::format("Expected '{:c}'", sym),
                static_cast<int>(error_token.location.line),
                static_cast<int>(error_token.location.column),
            });
        }
        return { };
    };

    auto const &token = lexer.lex();
    trace(JSON, "decode_value token: {}", token);
    switch (token.kind) {
    case TokenKind::Symbol: {
        switch (token.symbol_code()) {
        case '{': {
            auto result = JSONValue::object();
            while (!lexer.accept_symbol('}')) {
                auto const name_token = lexer.lex();
                trace(JSON, "name_token: {}", name_token);
                auto name = TRY_EVAL(decode_string(name_token));
                trace(JSON, "Name: {}", name);
                TRY(expect_symbol(':'));
                auto value = TRY_EVAL(decode_value(lexer, str));
                trace(JSON, "NVP: {}: {}", name, value.to_string());
                result.set(name, value);
                if (!lexer.accept_symbol(',')) {
                    TRY(expect_symbol('}'));
                    break;
                }
            }
            return result;
        }
        case '[': {
            auto result = JSONValue::array();
            while (!lexer.accept_symbol(']')) {
                auto value = TRY_EVAL(decode_value(lexer, str));
                trace(JSON, "Array elem: {}", value.to_string());
                result.append(value);
                if (!lexer.accept_symbol(',')) {
                    TRY(expect_symbol(']'));
                    break;
                }
            }
            return result;
        }
        default:
            return std::unexpected(JSONError {
                JSONError::Code::SyntaxError,
                std::format("Unexpected symbol '{:c}'", token.symbol_code()),
                static_cast<int>(token.location.line),
                static_cast<int>(token.location.column),
            });
        }
    }
    case TokenKind::QuotedString: {
        trace(JSON, "QuotedString: {}", token);
        return JSONValue { TRY_EVAL(decode_string(token)) };
    }
    case TokenKind::Number:
        return decode_number(token, str);
    case TokenKind::Keyword: {
        switch (token.keyword().code) {
        case JSONKeyword::False:
            return JSONValue(false);
        case JSONKeyword::True:
            return JSONValue(true);
        case JSONKeyword::Null:
            return JSONValue();
        default:
            UNREACHABLE();
        }
    }
    default:
        return std::unexpected(JSONError {
            JSONError::Code::SyntaxError,
            std::format("Invalid token '{:}' ({:})", str.substr(token.location.index, token.location.length), TokenKind_name(token.kind)),
            static_cast<int>(token.location.line),
            static_cast<int>(token.location.column),
        });
    }
}

template<>
[[nodiscard]] std::optional<std::tuple<SimpleKeywordCategory, JSONKeyword, MatchType>> match_keyword(std::string const &str)
{
#undef S
#define S(KW, STR)                                                       \
    if (std::string_view(STR).starts_with(str)) {                        \
        return std::tuple {                                              \
            SimpleKeywordCategory::Keyword,                              \
            JSONKeyword::KW,                                             \
            (str == STR) ? MatchType::FullMatch : MatchType::PrefixMatch \
        };                                                               \
    }
    JSONKEYWORD(S)
#undef S
    return { };
}

std::expected<JSONValue, JSONError> JSONValue::deserialize(std::string_view const &str)
{
    JSONLexer lexer;
    lexer.push_source(str);
    return decode_value(lexer, str);
}

}
