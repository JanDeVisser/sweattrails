/*
 * Copyright (c) 2023, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cctype>
#include <format>
#include <string>
#include <string_view>

#include <Result.h>
#include <StringUtil.h>
#include <Token.h>
#include <Utf8.h>

namespace ST {

struct LexerErrorMessage {
    TokenLocation location;
    std::string   message;

    [[nodiscard]] std::string const &to_string() const
    {
        return message;
    }
};

template<typename Buffer>
struct NoKeywords {
    using KeywordCategoryType = NoKeywordCategory;
    using KeywordCodeType = NoKeywordCode;
    using Token = Token<KeywordCategoryType, KeywordCodeType>;
    using Keyword = typename Token::Keyword;

    std::optional<std::tuple<Token, size_t>> pre_match(Buffer const &, size_t)
    {
        return { };
    }

    std::optional<std::tuple<KeywordCategoryType, KeywordCodeType>> match(std::string const &)
    {
        return { };
    }

    std::optional<std::tuple<KeywordCategoryType, KeywordCodeType, size_t>> match(Buffer const &, size_t)
    {
        return { };
    }
};

enum class MatchType {
    PrefixMatch,
    FullMatch,
};

enum class SimpleKeywordCategory {
    Keyword,
};

template<typename CategoryType, typename CodeType>
std::optional<std::tuple<CategoryType, CodeType, MatchType>> match_keyword(std::string const &str)
{
    return { };
}

template<typename Buffer, typename CategoryType, typename CodeType>
struct EnumKeywords {
    using Token = Token<CategoryType, CodeType>;
    using Keyword = typename Token::Keyword;
    using Keywords = CodeType;
    using Categories = CategoryType;

    std::optional<std::tuple<Token, size_t>> pre_match(Buffer const &, size_t)
    {
        return { };
    }

    std::optional<std::tuple<CategoryType, CodeType>> match(std::string const &str)
    {
        if (auto m = match_keyword<CategoryType, CodeType>(str); m && std::get<MatchType>(*m) == MatchType::FullMatch) {
            return std::tuple { std::get<CategoryType>(*m), std::get<CodeType>(*m) };
        }
        return { };
    }

    std::optional<std::tuple<CategoryType, CodeType, size_t>> match(Buffer const &buffer, size_t index)
    {
        std::string scanned;
        for (auto ix = index; ix < buffer.length(); ++ix) {
            scanned += buffer[ix];
            if (auto m = match_keyword<CategoryType, CodeType>(scanned)) {
                if (std::get<MatchType>(*m) == MatchType::FullMatch) {
                    return std::tuple { std::get<CategoryType>(*m), std::get<CodeType>(*m), scanned.length() };
                }
            } else {
                return { };
            }
        }
        return { };
    }

    std::string_view get_scope(Token const &)
    {
        return "identifier";
    }
};

template<bool Whitespace = false, bool Comments = false, bool BackquotedStrings = false>
struct LexerConfig {
    bool whitespace { Whitespace };
    bool comments { Comments };
    bool backquotedStrings { BackquotedStrings };
};

template<typename Buffer, typename Matcher = NoKeywords<Buffer>, typename Char = wchar_t, bool Whitespace = false, bool Comments = false, bool BackquotedStrings = false>
class Lexer {
public:
    using LexerError = Error<LexerErrorMessage>;
    using Keywords = typename Matcher::Keywords;
    using Categories = typename Matcher::Categories;
    using Token = Token<Categories, Keywords>;
    using LexerResult = Result<Token, LexerErrorMessage>;

    Lexer() = default;

    void push_source(Buffer source)
    {
        m_sources.emplace_back(this, source);
        if constexpr (!BackquotedStrings) {
            m_sources.back().quote_chars = "\"'";
        }
    }

    std::basic_string_view<Char> text(Token const &token) const
    {
        return m_sources.back().substr(token.location.index, token.location.length);
    }

    std::string text_utf8(Token const &token) const
    {
        return as_utf8(text(token));
    }

    Token const &peek()
    {
        if (m_current.has_value()) {
            trace(LEXER, "lexer.peek() -> {} [cached]", *m_current);
            return m_current.value();
        }
        while (!exhausted()) {
            TokenKind k;
            while (true) {
                m_current = m_sources.back().peek_next();
                trace(LEXER, "lexer.peek() -> {} [source.peek_next()]", *m_current);
                k = m_current->kind;
                if constexpr (!Whitespace) {
                    if (k == TokenKind::Whitespace || k == TokenKind::Tab || k == TokenKind::EndOfLine) {
                        lex();
                        trace(LEXER, "skip it");
                        continue;
                    }
                }
                if constexpr (!Comments) {
                    if (k == TokenKind::Comment) {
                        lex();
                        trace(LEXER, "skip it");
                        continue;
                    }
                }
                break;
            }
            if (k != TokenKind::EndOfFile) {
                break;
            }
            m_sources.pop_back();
        }
        trace(LEXER, "lexer.peek() -> {} [caching it]", *m_current);
        return m_current.value();
    }

    Token lex()
    {
        auto ret = peek();
        if (!m_sources.empty()) {
            m_sources.back().lex();
        }
        m_current.reset();
        return ret;
    }

    LexerResult expect(TokenKind kind)
    {
        if (auto ret = peek(); !ret.matches(kind)) {
            return LexerErrorMessage { location(),
                std::format("Expected '{}'", TokenKind_name(kind)) };
        }
        return lex();
    }

    bool accept(TokenKind kind)
    {
        if (auto ret = peek(); ret.matches(kind)) {
            lex();
            return true;
        }
        return false;
    }

    LexerError expect_keyword(Keywords code)
    {
        if (auto ret = peek(); !ret.matches_keyword(code)) {
            return LexerErrorMessage {
                location(),
                std::format("Expected keyword"), // FIXME need KW code -> text mechanism
            };
        }
        lex();
        return { };
    }

    bool accept_keyword(Keywords code)
    {
        if (auto ret = peek(); ret.matches_keyword(code)) {
            lex();
            return true;
        }
        return false;
    }

    LexerError expect_symbol(int symbol)
    {
        if (auto ret = peek(); !ret.matches_symbol(symbol)) {
            return LexerErrorMessage { location(), std::format("Expected '{}' but got '{}'", static_cast<char>(symbol), text_utf8(ret)) };
        }
        lex();
        return { };
    }

    bool accept_symbol(int symbol)
    {
        if (auto ret = peek(); ret.matches_symbol(symbol)) {
            lex();
            return true;
        }
        return false;
    }

    LexerResult expect_identifier()
    {
        if (auto ret = peek(); !ret.is_identifier()) {
            return LexerErrorMessage { location(), "Expected identifier" };
        }
        return lex();
    }

    bool next_matches(TokenKind kind)
    {
        auto n = peek();
        return n.matches(kind);
    }

    bool next_matches(int symbol)
    {
        auto n = peek();
        return n.matches_symbol(symbol);
    }

    [[nodiscard]] bool exhausted() const
    {
        return m_sources.empty();
    }

private:
    std::optional<Token> m_current { };

    [[nodiscard]] TokenLocation const &location() const
    {
        assert(!m_sources.empty());
        return m_sources.back().location();
    }

    class Source {
    public:
        char const *quote_chars;

        [[nodiscard]] TokenLocation const &location() const
        {
            return m_location;
        }

        Source(Lexer *lexer, Buffer const &src)
            : quote_chars("\"'`")
            , m_buffer(src)
            , m_lexer(lexer)
        {
        }

        Token const &peek_next()
        {
            if (m_current.has_value()) {
                return *m_current;
            }

            m_current = peek();
            m_location.length = m_index - m_location.index;
            m_current->location = m_location;
            return *m_current;
        }

        Token peek()
        {
            if (m_index >= m_buffer.length()) {
                return Token::end_of_file();
            }
            m_scanned.clear();
            auto cur = m_buffer[m_index];
            if (m_in_comment) {
                if (cur == '\n') {
                    ++m_index;
                    return Token::end_of_line();
                }
                return block_comment();
            }
            if (cur == '/') {
                switch (m_buffer[m_index + 1]) {
                case '/': {
                    m_index += 2;
                    for (; m_index < m_buffer.length() && m_buffer[m_index] != '\n'; ++m_index)
                        ;
                    return Token::comment(CommentType::Line);
                }
                case '*': {
                    return block_comment();
                }
                default:
                    break;
                }
            }
            if (auto t = matcher.pre_match(m_buffer, m_index)) {
                auto token = std::get<Token>(*t);
                m_index += std::get<size_t>(*t);
                return token;
            }
            if (isdigit(cur)) {
                return scan_number();
            }
            if (strchr(quote_chars, cur)) {
                ++m_index;
                while (m_index < m_buffer.length() && m_buffer[m_index] != cur) {
                    m_index += (m_buffer[m_index] == '\\') ? 2 : 1;
                }
                ++m_index;
                return { Token::string(static_cast<QuoteType>(cur), m_index < m_buffer.length()) };
            }
            switch (cur) {
            case '\n':
                ++m_index;
                return Token::end_of_line();
            case '\t':
                ++m_index;
                return Token::tab();
            case ' ':
                while (m_index < m_buffer.length() && m_buffer[m_index] == ' ') {
                    ++m_index;
                }
                return Token::whitespace();
            default:
                break;
            }
            if (isalpha(cur) || cur == '_') {
                for (; isalnum(m_buffer[m_index]) || m_buffer[m_index] == '_'; ++m_index) {
                    m_scanned += m_buffer[m_index];
                }
                if (auto kw = matcher.match(m_scanned); kw) {
                    return Token::keyword(std::get<typename Matcher::Categories>(*kw), std::get<typename Matcher::Keywords>(*kw));
                }
                return Token::identifier();
            }

            if (auto kw = matcher.match(m_buffer, m_index); kw) {
                m_index += std::get<size_t>(*kw);
                return Token::keyword(std::get<typename Matcher::Categories>(*kw), std::get<typename Matcher::Keywords>(*kw));
            }
            ++m_index;
            return Token::symbol(static_cast<int>(cur));
        }

        void lex()
        {
            if (!m_current) {
                return;
            }
            m_location.index = m_index;
            if (m_current->kind == TokenKind::EndOfLine) {
                ++m_location.line;
                m_location.column = 0;
            } else {
                m_location.column += m_location.length;
            }
            m_location.length = 0;
            m_current.reset();
        }

        Token block_comment()
        {
            for (; m_index < m_buffer.length() && m_buffer[m_index] != '\n' && (m_buffer[m_index - 1] != '*' || m_buffer[m_index] != '/'); ++m_index)
                ;
            if (m_index >= m_buffer.length()) {
                return Token::comment(CommentType::Block, false);
            }
            if (m_buffer[m_index] == '\n') {
                m_in_comment = true;
                return Token::comment(CommentType::Block, false);
            }
            m_in_comment = false;
            ++m_index;
            return Token::comment(CommentType::Block, true);
        }

        Token scan_number()
        {
            auto type = NumberType::Integer;
            auto cur = m_buffer[m_index];
            int  ix = m_index;
            int (*predicate)(int) = isdigit;
            if (m_index < m_buffer.length() - 1 && cur == '0') {
                if (m_buffer[m_index + 1] == 'x' || m_buffer[m_index + 1] == 'X') {
                    if (m_index == m_buffer.length() - 2 || !isxdigit(m_buffer[m_index + 2])) {
                        m_index += 1;
                        return Token::number(NumberType::Integer);
                    }
                    type = NumberType::HexNumber;
                    predicate = isxdigit;
                    ix = m_index + 2;
                } else if (m_buffer[m_index + 1] == 'b' || m_buffer[m_index + 1] == 'B') {
                    if (m_index == m_buffer.length() - 2 || !isbdigit(m_buffer[m_index + 2])) {
                        m_index += 1;
                        return Token::number(NumberType::Integer);
                    }
                    type = NumberType::BinaryNumber;
                    predicate = isbdigit;
                    ix = m_index + 2;
                }
            }
            while (ix < m_buffer.length()) {
                Char const ch = m_buffer[ix];
                if (!predicate(ch) && ((ch != '.') || (type == NumberType::Decimal))) {
                    // FIXME lex '1..10' as '1', '..', '10'. It will now lex as '1.', '.', '10'
                    break;
                }
                if (ch == '.') {
                    if (type != NumberType::Integer) {
                        break;
                    }
                    type = NumberType::Decimal;
                }
                ++ix;
            }
            m_index = ix;
            return Token::number(type);
        }

        Char operator*() const
        {
            return m_buffer[m_index];
        }

        Char operator[](size_t ix) const
        {
            return m_buffer[ix];
        }

        [[nodiscard]] std::basic_string_view<Char> substr(size_t pos, size_t len = std::basic_string_view<Char>::npos) const
        {
            return m_buffer.substr(pos, len);
        }

        [[nodiscard]] size_t length() const
        {
            return m_buffer.length();
        }

        [[nodiscard]] bool exhausted() const
        {
            return m_index >= length();
        }

        Source &operator++()
        {
            ++m_index;
            return *this;
        }

        Source &operator++(int)
        {
            ++m_index;
            return *this;
        }

    private:
        Buffer               m_buffer;
        size_t               m_index { 0 };
        Lexer               *m_lexer;
        TokenLocation        m_location { };
        std::optional<Token> m_current { };
        bool                 m_in_comment { false };
        std::string          m_scanned;
        Matcher              matcher { };
    };

    std::vector<Source> m_sources { };
};

}
