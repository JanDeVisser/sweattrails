/*
 * Copyright (c) 2024, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <expected>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>

#include <Logging.h>

namespace ST {

struct NoSuchEnumValue {
    std::string enum_name;
    std::string value;
};

template<typename ResultType>
using EnumResult = std::expected<ResultType, NoSuchEnumValue>;

#define VALUE_TOKENKINDS(S) \
    S(Symbol)               \
    S(Keyword)              \
    S(Number)               \
    S(QuotedString)         \
    S(Comment)

#define TOKENKINDS(S)   \
    S(Unknown)          \
    VALUE_TOKENKINDS(S) \
    S(EndOfFile)        \
    S(EndOfLine)        \
    S(Identifier)       \
    S(Tab)              \
    S(Whitespace)       \
    S(Program)          \
    S(Module)

enum class TokenKind {
#undef S
#define S(kind) kind,
    TOKENKINDS(S)
#undef S
};

inline std::string TokenKind_name(TokenKind kind)
{
    switch (kind) {
#undef S
#define S(K)           \
    case TokenKind::K: \
        return std::format("{} *{}*", #K, static_cast<int>(kind));
        TOKENKINDS(S)
#undef S
    default:
        UNREACHABLE();
    }
}

inline EnumResult<TokenKind> TokenKind_from_string(std::string_view const &kind)
{
#undef S
#define S(K)        \
    if (kind == #K) \
        return TokenKind::K;
    TOKENKINDS(S)
#undef S
    return std::unexpected(NoSuchEnumValue { "TokenKind", std::string(kind) });
}

#define QUOTETYPES(S)    \
    S(SingleQuote, '\'') \
    S(DoubleQuote, '"')  \
    S(BackQuote, '`')

enum class QuoteType : char {
#undef S
#define S(T, Q) T = (Q),
    QUOTETYPES(S)
#undef S
};

extern std::string           QuoteType_name(QuoteType quote);
extern EnumResult<QuoteType> QuoteType_from_string(std::string_view quote);

#define COMMENTTYPES(S) \
    S(Block)            \
    S(Line)

enum class CommentType {
#undef S
#define S(T) T,
    COMMENTTYPES(S)
#undef S
};

extern std::string             CommentType_name(CommentType quote);
extern EnumResult<CommentType> CommentType_from_string(std::string_view comment);

#define NUMBERTYPES(S) \
    S(Integer)         \
    S(Decimal)         \
    S(HexNumber)       \
    S(BinaryNumber)

enum class NumberType : int {
#undef S
#define S(T) T,
    NUMBERTYPES(S)
#undef S
};

extern std::string            NumberType_name(NumberType quote);
extern EnumResult<NumberType> NumberType_from_string(std::string_view comment);

struct TokenLocation {
    TokenLocation() = default;
    TokenLocation(TokenLocation const &) = default;

    size_t index { 0 };
    size_t length { 0 };
    size_t line { 0 };
    size_t column { 0 };
};

struct QuotedString {
    QuoteType quote_type;
    bool      triple;
    bool      terminated;
};

struct CommentText {
    CommentType comment_type;
    bool        terminated;
};

enum class NoKeywordCategory {
};

enum class NoKeywordCode {
};

template<typename KeywordCategoryType = NoKeywordCategory, typename KeywordCodeType = NoKeywordCode, typename Char = char>
struct Token {
    using Keywords = KeywordCodeType;
    using Categories = KeywordCategoryType;

    struct Keyword {
        Categories category;
        Keywords   code;
    };

    using TokenValue = std::variant<std::monostate, NumberType, QuotedString, CommentText, Keyword, Char>;

    Token() = default;
    Token(Token const &) = default;

    TokenKind     kind { TokenKind::Unknown };
    TokenLocation location { };
    TokenValue    value;

    static Token number(NumberType type)
    {
        Token ret;
        ret.kind = TokenKind::Number;
        ret.value = type;
        return ret;
    }

    static Token symbol(Char sym)
    {
        Token ret;
        ret.kind = TokenKind::Symbol;
        ret.value = TokenValue { std::in_place_index<5>, sym };
        return ret;
    }

    static Token keyword(KeywordCategoryType cat, KeywordCodeType code)
    {
        Token ret;
        ret.kind = TokenKind::Keyword;
        ret.value = TokenValue { std::in_place_index<4>, Keyword { cat, code } };
        return ret;
    }

    static Token whitespace()
    {
        Token ret;
        ret.kind = TokenKind::Whitespace;
        return ret;
    }

    static Token tab()
    {
        Token ret;
        ret.kind = TokenKind::Tab;
        return ret;
    }

    static Token identifier()
    {
        Token ret;
        ret.kind = TokenKind::Identifier;
        return ret;
    }

    static Token comment(CommentType type, bool terminated = true)
    {
        Token ret;
        ret.kind = TokenKind::Comment;
        ret.value = CommentText { .comment_type = type, .terminated = terminated };
        return ret;
    }

    static Token end_of_line()
    {
        Token ret;
        ret.kind = TokenKind::EndOfLine;
        return ret;
    }

    static Token end_of_file()
    {
        Token ret;
        ret.kind = TokenKind::EndOfFile;
        return ret;
    }

    static Token string(QuoteType type, bool terminated = true, bool triple = false)
    {
        Token ret;
        ret.kind = TokenKind::QuotedString;
        ret.value = QuotedString {
            .quote_type = type,
            .triple = triple,
            .terminated = terminated
        };
        return ret;
    }

    [[nodiscard]] NumberType number_type() const
    {
        assert(kind == TokenKind::Number);
        return std::get<1>(value);
    }

    [[nodiscard]] Char symbol_code() const
    {
        assert(kind == TokenKind::Symbol);
        return std::get<5>(value);
    }

    [[nodiscard]] Keyword const &keyword() const
    {
        assert(kind == TokenKind::Keyword);
        return std::get<4>(value);
    }

    [[nodiscard]] KeywordCodeType keyword_code() const
    {
        assert(kind == TokenKind::Keyword);
        auto kw = keyword();
        return kw.code;
    }

    [[nodiscard]] QuotedString const &quoted_string() const
    {
        assert(kind == TokenKind::QuotedString);
        return std::get<QuotedString>(value);
    }

    [[nodiscard]] CommentText const &comment_text() const
    {
        assert(kind == TokenKind::Comment);
        return std::get<CommentText>(value);
    }

    bool operator==(TokenKind const &k) const
    {
        return k == kind;
    }

    bool operator!=(TokenKind const &k) const
    {
        return k != kind;
    }

    bool operator==(Char s) const
    {
        return matches(s);
    }

    bool operator!=(Char s) const
    {
        return !matches(s);
    }

    bool operator==(KeywordCodeType const &code) const
    {
        return matches(code);
    }

    bool operator!=(KeywordCodeType const &code) const
    {
        return !matches(code);
    }

    [[nodiscard]] bool matches(TokenKind k) const { return kind == k; }
    [[nodiscard]] bool matches_symbol(Char symbol) const { return matches(TokenKind::Symbol) && this->symbol_code() == symbol; }
    [[nodiscard]] bool matches_keyword(KeywordCategoryType cat, KeywordCodeType code) const { return matches(TokenKind::Keyword) && this->keyword().category == cat && this->keyword().code == code; }
    [[nodiscard]] bool matches_keyword(KeywordCodeType code) const { return matches(TokenKind::Keyword) && this->keyword().code == code; }
    [[nodiscard]] bool is_identifier() const { return matches(TokenKind::Identifier); }
};

}

template<>
struct std::formatter<ST::TokenKind, char> {
    template<class ParseContext>
    constexpr typename ParseContext::iterator parse(ParseContext &ctx)
    {
        auto it = ctx.begin();
        if (it != ctx.end() && *it != '}') {
            throw std::format_error("Invalid format args for Token.");
        }
        return it;
    }

    template<class FmtContext>
    typename FmtContext::iterator format(ST::TokenKind kind, FmtContext &ctx) const
    {
        std::ostringstream out;
        switch (kind) {
#undef S
#define S(K)               \
    case ST::TokenKind::K: \
        out << #K;         \
        break;
            TOKENKINDS(S)
#undef S
        default:
            UNREACHABLE();
        }
        return std::ranges::copy(std::move(out).str(), ctx.out()).out;
    }
};

template<typename KeywordCategoryType, typename KeywordCodeType>
struct std::formatter<ST::Token<KeywordCategoryType, KeywordCodeType>, char> {
    using Token = ST::Token<KeywordCategoryType, KeywordCodeType>;

    template<class ParseContext>
    constexpr typename ParseContext::iterator parse(ParseContext &ctx)
    {
        auto it = ctx.begin();
        if (it != ctx.end() && *it != '}') {
            throw std::format_error("Invalid format args for Token.");
        }
        return it;
    }

    template<class FmtContext>
    typename FmtContext::iterator format(Token const &token, FmtContext &ctx) const
    {
        std::ostringstream out;
        out << "[" << ST::TokenKind_name(token.kind) << "] (" << token.location.index << ", " << token.location.length << ")";
        return std::ranges::copy(std::move(out).str(), ctx.out()).out;
    }
};
