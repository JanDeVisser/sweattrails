/*
 * Copyright (c) 2023, 2025 Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __LEXER_H__
#define __LEXER_H__

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef LEXER_TEST
#define SLICE_IMPLEMENTATION
#define DA_IMPLEMENTATION
#define LEXER_IMPLEMENTATION
#endif

#include "da.h"
#include "slice.h"

#ifdef LEXER_TEST

typedef enum {
    KW_If,
    KW_Then,
    KW_Else,
    KW_While,
} testkeyword_t;

slice_t test_keywords[] = {
    C("if"),
    C("then"),
    C("else"),
    C("while"),
    C(""),
};

#define keywordcode testkeyword_t
#define keywords test_keywords

#endif

#ifndef keywordcode
#define keywordcode size_t
#endif

#ifndef keywords
#define keywords no_keywords
#endif

#define VALUE_TOKENKINDS(S) \
    S(Symbol)               \
    S(Number)               \
    S(String)               \
    S(Comment)              \
    S(Raw)

#define TOKENKINDS(S)   \
    S(Unknown)          \
    VALUE_TOKENKINDS(S) \
    S(Keyword)          \
    S(EndOfFile)        \
    S(EndOfLine)        \
    S(Identifier)       \
    S(Tab)              \
    S(Whitespace)       \
    S(Program)          \
    S(Module)

typedef enum {
#undef S
#define S(kind) TK_##kind,
    TOKENKINDS(S)
#undef S
} tokenkind_t;

OPTDEF(tokenkind_t);

typedef enum : char {
    QT_SingleQuote = '\'',
    QT_DoubleQuote = '"',
    QT_BackQuote = '`',
} quotetype_t;

typedef enum {
    CT_Block,
    CT_Line,
} commenttype_t;

typedef enum {
    NUM_Integer,
    NUM_Decimal,
    NUM_HexNumber,
    NUM_BinaryNumber,
} numbertype_t;

typedef struct _tokenlocation {
    size_t index;
    size_t length;
    size_t line;
    size_t column;
} tokenlocation_t;

typedef struct _quotedstring {
    quotetype_t quote_type;
    bool        triple;
    bool        terminated;
} quotedstring_t;

typedef struct _rawtext {
    slice_t marker;
    bool    terminated;
} rawtext_t;

typedef struct _commenttext {
    commenttype_t comment_type;
    bool          terminated;
} commenttext_t;

typedef keywordcode keywordcode_t;
OPTDEF(keywordcode_t);

typedef struct _token {
    tokenlocation_t location;
    tokenkind_t     kind;
    union {
        numbertype_t   number;
        quotedstring_t quoted_string;
        commenttext_t  comment_text;
        rawtext_t      rawtext;
        char           symbol;
        keywordcode_t  keyword;
    };
} token_t;

OPTDEF(token_t);

typedef struct _keyword {
    char const   *keyword;
    keywordcode_t code;
} keyword_t;

typedef enum _matchtype {
    MT_FullMatch,
    MT_PrefixMatch,
} matchtype_t;

typedef struct _keywordmatch {
    keywordcode_t keyword;
    matchtype_t   match_type;
} keywordmatch_t;

OPTDEF(keywordmatch_t);

typedef enum _scanresult_type {
    SRT_Token = 0,
    SRT_Buffer,
    SRT_Skip,
} scanresult_type_t;

typedef struct _scanresult {
    size_t            matched;
    scanresult_type_t result;
    union {
        token_t     token;
        char const *buffer;
        size_t      skip_index;
    };
} scanresult_t;

OPTDEF(scanresult_t);

typedef opt_scanresult_t (*scanner_t)(void *ctx, slice_t buffer);

typedef struct _scanner_def {
    scanner_t scanner;
    void     *ctx;
} scanner_def_t;

typedef struct _scannerpack {
    scanner_def_t *scanners;
} scannerpack_t;

typedef struct _linecomment {
    slice_t marker;
} linecomment_t;

typedef struct _blockcomment {
    slice_t begin;
    slice_t end;
    bool    in_comment;
} blockcomment_t;

typedef struct _rawscanner {
    slice_t begin;
    slice_t end;
} rawscanner_t;

typedef struct _stringscanner {
    slice_t quotes;
} stringscanner_t;

extern linecomment_t   slash_slash;
extern linecomment_t   hashmark;
extern blockcomment_t  c_block_comment;
extern stringscanner_t default_quotes;
extern stringscanner_t single_double_quotes;
extern stringscanner_t double_quotes;
extern scannerpack_t   c_style_comments;
extern scannerpack_t   c_scanner_pack;
extern scanner_def_t   c_scanner;
extern slice_t         no_keywords[];

// typedef char const *string_t;

typedef enum {
    LE_UnexpectedKeyword,
    LE_UnexpectedSymbol,
    LE_UnexpectedTokenKind,
} lexererror_t;

typedef RES(token_t, lexererror_t) lexerresult_t;

typedef struct _lexer {
    DA(token_t)
    tokens;
    slice_t buffer;
    size_t  cursor;
} lexer_t;

extern char const      *tokenkind_name(tokenkind_t kind);
extern opt_tokenkind_t  tokenkind_from_string(char const *kind);
extern tokenlocation_t  tokenlocation_merge(tokenlocation_t first, tokenlocation_t second);
extern token_t          token_make_comment(commenttype_t type, bool terminated);
extern token_t          token_make_end_of_file();
extern token_t          token_make_end_of_line();
extern token_t          token_make_identifier();
extern token_t          token_make_keyword(keywordcode_t kw);
extern token_t          token_make_number(numbertype_t type);
extern token_t          token_make_raw(slice_t marker, bool terminated);
extern token_t          token_make_string(quotetype_t type, bool terminated, bool triple);
extern token_t          token_make_symbol(char symbol);
extern token_t          token_make_tab();
extern token_t          token_make_whitespace();
extern bool             token_matches(token_t token, tokenkind_t k);
extern bool             token_matches_symbol(token_t token, char symbol);
extern bool             token_matches_keyword(token_t token, keywordcode_t kw);
extern bool             token_is_identifier(token_t token);
extern scanresult_t     make_token_result(token_t token, size_t matched);
extern scanresult_t     make_buffer_result(char const *buffer, size_t matched);
extern scanresult_t     make_skip_result(size_t skip, size_t matched);
extern opt_scanresult_t scannerpack(void *ctx, slice_t buffer);
extern opt_scanresult_t linecomment(void *ctx, slice_t buffer);
extern opt_scanresult_t blockcomment(void *ctx, slice_t buffer);
extern opt_scanresult_t identifierscanner(void *, slice_t buffer);
extern opt_scanresult_t keywordscanner(void *ctx, slice_t buffer);
extern opt_scanresult_t numberscanner(void *ctx, slice_t buffer);
extern opt_scanresult_t rawscanner(void *ctx, slice_t buffer);
extern opt_scanresult_t stringscanner(void *ctx, slice_t buffer);
extern opt_scanresult_t whitespacescanner(void *, slice_t buffer);
extern opt_scanresult_t symbolmuncher(void *, slice_t buffer);
extern slice_t          lexer_token_text(lexer_t *lexer, token_t token);
extern void             lexer_push_source(lexer_t *lexer, slice_t src, scanner_def_t scanner);
extern token_t          lexer_peek(lexer_t *lexer);
extern token_t          lexer_lex(lexer_t *lexer);
extern lexerresult_t    lexer_expect(lexer_t *lexer, tokenkind_t kind);
extern bool             lexer_accept(lexer_t *lexer, tokenkind_t kind);
extern lexerresult_t    lexer_expect_keyword(lexer_t *lexer, keywordcode_t code);
extern bool             lexer_accept_keyword(lexer_t *lexer, keywordcode_t code);
extern lexerresult_t    lexer_expect_symbol(lexer_t *lexer, int symbol);
extern bool             lexer_accept_symbol(lexer_t *lexer, int symbol);
extern lexerresult_t    lexer_expect_identifier(lexer_t *lexer);
extern opt_token_t      lexer_accept_identifier(lexer_t *lexer);
extern void             lexer_push_back(lexer_t *lexer);
extern bool             lexer_exhausted(lexer_t *lexer);
extern bool             lexer_matches_keyword(lexer_t *lexer, keywordcode_t keyword);
extern bool             lexer_matches_symbol(lexer_t *lexer, int sym);
extern bool             lexer_matches(lexer_t *lexer, tokenkind_t kind);
extern bool             lexer_has_lookback(lexer_t *, size_t);
extern token_t          lexer_lookback(lexer_t *, size_t);

#endif /* __LEXER_H__ */

#ifdef LEXER_IMPLEMENTATION
#undef LEXER_IMPLEMENTATION
#ifndef LEXER_IMPLEMENTED
#define LEXER_IMPLEMENTED

linecomment_t slash_slash = (linecomment_t) { .marker = C("//") };
linecomment_t hashmark = (linecomment_t) { .marker = C("#") };

blockcomment_t c_block_comment = (blockcomment_t) {
    .begin = C("/*"),
    .end = C("*/"),
};

scanner_def_t c_style_comments_def[] = {
    { .scanner = linecomment, .ctx = &slash_slash },
    { .scanner = blockcomment, .ctx = &c_block_comment },
    { .scanner = NULL, .ctx = NULL },
};

scannerpack_t c_style_comments = (scannerpack_t) {
    .scanners = c_style_comments_def,
};

scanner_def_t c_scanner_pack_def[] = {
    { .scanner = scannerpack, .ctx = (void *) &c_style_comments },
    { .scanner = numberscanner, .ctx = NULL },
    { .scanner = stringscanner, .ctx = (void *) &single_double_quotes },
    { .scanner = whitespacescanner, .ctx = (void *) true },
    { .scanner = identifierscanner, .ctx = NULL },
    { .scanner = keywordscanner, .ctx = NULL },
    { .scanner = symbolmuncher, .ctx = NULL },
};

scannerpack_t c_scanner_pack = {
    .scanners = c_scanner_pack_def,
};

scanner_def_t c_scanner = {
    .scanner = scannerpack,
    .ctx = (void *) &c_scanner_pack,
};

stringscanner_t default_quotes = (stringscanner_t) {
    .quotes = C("\"'`"),
};

stringscanner_t single_double_quotes = (stringscanner_t) {
    .quotes = C("\"'"),
};

stringscanner_t double_quotes = (stringscanner_t) {
    .quotes = C("\""),
};

slice_t no_keywords[] = {
    C(""),
};

char const *tokenkind_name(tokenkind_t kind)
{
    switch (kind) {
#undef S
#define S(K)     \
    case TK_##K: \
        return #K;
        TOKENKINDS(S)
#undef S
    default:
        UNREACHABLE();
    }
}

opt_tokenkind_t tokenkind_from_string(char const *kind)
{
#undef S
#define S(K)              \
    if (strcmp(kind, #K)) \
        return OPTVAL(tokenkind_t, TK_##K);
    TOKENKINDS(S)
#undef S
    return OPTNULL(tokenkind_t);
}

tokenlocation_t tokenlocation_merge(tokenlocation_t first, tokenlocation_t second)
{
    size_t index = MIN(first.index, second.index);
    return (tokenlocation_t) {
        .index = index,
        .length = MAX(first.index + first.length, second.index + second.length) - index,
        .line = MIN(first.line, second.line),
        .column = MIN(first.column, second.column),
    };
}

token_t token_make_comment(commenttype_t type, bool terminated)
{
    return (token_t) {
        .kind = TK_Comment,
        .comment_text = (commenttext_t) {
            .comment_type = type,
            .terminated = terminated,
        },
    };
}

token_t token_make_end_of_file()
{
    return (token_t) {
        .kind = TK_EndOfFile,
    };
}

token_t token_make_end_of_line()
{
    return (token_t) {
        .kind = TK_EndOfLine,
    };
}

token_t token_make_identifier()
{
    return (token_t) {
        .kind = TK_Identifier,
    };
}

token_t token_make_keyword(keywordcode_t kw)
{
    return (token_t) {
        .kind = TK_Keyword,
        .keyword = kw,
    };
}

token_t token_make_number(numbertype_t type)
{
    return (token_t) {
        .kind = TK_Number,
        .number = type,
    };
}

token_t token_make_raw(slice_t marker, bool terminated)
{
    return (token_t) {
        .kind = TK_Raw,
        .rawtext = (rawtext_t) {
            .marker = marker,
            .terminated = terminated,
        },
    };
}

token_t token_make_string(quotetype_t type, bool terminated, bool triple)
{
    return (token_t) {
        .kind = TK_String,
        .quoted_string = (quotedstring_t) {
            .quote_type = type,
            .terminated = terminated,
            .triple = triple,
        },
    };
}

token_t token_make_symbol(char sym)
{
    return (token_t) {
        .kind = TK_Symbol,
        .symbol = sym,
    };
}

token_t token_make_tab()
{
    return (token_t) {
        .kind = TK_Tab,
    };
}

token_t token_make_whitespace()
{
    return (token_t) {
        .kind = TK_Whitespace,
    };
}

bool token_matches(token_t token, tokenkind_t k)
{
    return token.kind == k;
}

bool token_matches_symbol(token_t token, char symbol)
{
    return token_matches(token, TK_Symbol) && token.symbol == symbol;
}

bool token_matches_keyword(token_t token, keywordcode_t kw)
{
    return token_matches(token, TK_Keyword) && token.keyword == kw;
}

bool token_is_identifier(token_t token)
{
    return token_matches(token, TK_Identifier);
}

extern scanresult_t make_token_result(token_t token, size_t matched)
{
    return (scanresult_t) {
        .matched = matched,
        .result = SRT_Token,
        .token = token,
    };
}

extern scanresult_t make_buffer_result(char const *buffer, size_t matched)
{
    return (scanresult_t) {
        .matched = matched,
        .result = SRT_Buffer,
        .buffer = buffer,
    };
}

extern scanresult_t make_skip_result(size_t skip_index, size_t matched)
{
    return (scanresult_t) {
        .matched = matched,
        .result = SRT_Skip,
        .skip_index = skip_index,
    };
}

opt_scanresult_t scannerpack(void *ctx, slice_t buffer)
{
    scannerpack_t *config = (scannerpack_t *) ctx;
    for (scanner_def_t *def = config->scanners; def->scanner != NULL; ++def) {
        opt_scanresult_t res = def->scanner(def->ctx, buffer);
        if (res.ok) {
            return res;
        }
    }
    return OPTNULL(scanresult_t);
}

opt_scanresult_t linecomment(void *ctx, slice_t buffer)
{
    linecomment_t *config = (linecomment_t *) ctx;
    if (!slice_startswith(buffer, config->marker)) {
        return OPTNULL(scanresult_t);
    }
    size_t ix = config->marker.len;
    for (; ix < buffer.len && buffer.items[ix] != '\n'; ++ix)
        ;
#ifdef COMMENT_IGNORE
    return OPTVAL(scanresult_t, make_skip_result(0, ix));
#else
    return OPTVAL(scanresult_t, make_token_result(token_make_comment(CT_Line, true), ix));
#endif
}

opt_scanresult_t block_comment_line(blockcomment_t *config, slice_t buffer)
{
    opt_size_t end_maybe = slice_find(buffer, config->end);
    opt_size_t nl_maybe = slice_indexof(buffer, '\n');
    if (nl_maybe.ok && (!end_maybe.ok || end_maybe.value > nl_maybe.value)) {
#ifdef COMMENT_IGNORE
        return OPTVAL(scanresult_t, make_skip_result(0, nl_maybe.value + 1));
#else
        return OPTVAL(scanresult_t, make_token_result(token_make_comment(CT_Block, false), nl_maybe.value + 1));
#endif
    }
    config->in_comment = false;
    if (end_maybe.ok) {
#ifdef COMMENT_IGNORE
        return OPTVAL(scanresult_t, make_skip_result(0, end_maybe.value + config->end.len));
#else
        return OPTVAL(scanresult_t, make_token_result(token_make_comment(CT_Block, true), end_maybe.value + config->end.len));
#endif
    }
#ifdef COMMENT_IGNORE
    return OPTVAL(scanresult_t, make_skip_result(0, buffer.len));
#else
    return OPTVAL(scanresult_t, make_token_result(token_make_comment(CT_Block, true), buffer.len));
#endif
}

opt_scanresult_t blockcomment(void *ctx, slice_t buffer)
{
    blockcomment_t *config = (blockcomment_t *) ctx;
    if (config->in_comment) {
        return block_comment_line(config, buffer);
    }
    if (!slice_startswith(buffer, config->begin)) {
        return OPTNULL(scanresult_t);
    }
    config->in_comment = true;
    return block_comment_line(config, buffer);
}

opt_scanresult_t rawscanner(void *ctx, slice_t buffer)
{
    rawscanner_t *config = (rawscanner_t *) ctx;
    if (!slice_startswith(buffer, config->begin)) {
        return OPTNULL(scanresult_t);
    }
    opt_size_t end_maybe = slice_find(buffer, config->end);
    size_t     scanned = (end_maybe.ok) ? end_maybe.value + config->end.len : buffer.len;
    return OPTVAL(scanresult_t, make_token_result(token_make_raw(config->begin, end_maybe.ok), scanned));
}

int isbdigit(int ch)
{
    return (ch == '0') || (ch == '1');
}

opt_scanresult_t numberscanner(void *ctx, slice_t buffer)
{
    (void) ctx;
    numbertype_t type = NUM_Integer;
    size_t       ix = 0;
    char         cur = buffer.items[0];
    if (!isdigit(cur)) {
        return OPTNULL(scanresult_t);
    }
    int (*predicate)(int) = isdigit;
    if (ix < buffer.len - 1 && cur == '0') {
        if (buffer.items[ix + 1] == 'x' || buffer.items[ix + 1] == 'X') {
            if (ix == buffer.len - 2 || !isxdigit(buffer.items[ix + 2])) {
                return OPTVAL(scanresult_t, make_token_result(token_make_number(NUM_Integer), ix + 1));
            }
            type = NUM_HexNumber;
            predicate = isxdigit;
            ix = ix + 2;
        } else if (buffer.items[ix + 1] == 'b' || buffer.items[ix + 1] == 'B') {
            if (ix == buffer.len - 2 || !isbdigit(buffer.items[ix + 2])) {
                return OPTVAL(scanresult_t, make_token_result(token_make_number(NUM_Integer), ix + 1));
            }
            type = NUM_BinaryNumber;
            predicate = isbdigit;
            ix = ix + 2;
        }
    }
    for (; ix < buffer.len; ++ix) {
        char ch = buffer.items[ix];
        if (!predicate(ch) && ((ch != '.') || (type == NUM_Decimal))) {
            // FIXME lex '1..10' as '1', '..', '10'. It will now lex as '1.', '.', '10'
            break;
        }
        if (ch == '.') {
            if (type != NUM_Integer) {
                break;
            }
            type = NUM_Decimal;
        }
    }
    return OPTVAL(scanresult_t, make_token_result(token_make_number(type), ix));
}

opt_scanresult_t stringscanner(void *ctx, slice_t buffer)
{
    stringscanner_t *config = (stringscanner_t *) ctx;
    if (ORELSE(size_t, slice_first_of(buffer, config->quotes), buffer.len) == 0) {
        char   quote = buffer.items[0];
        size_t ix = 1;
        while (ix < buffer.len && buffer.items[ix] != quote) {
            ix += (buffer.items[ix] == '\\') ? 2 : 1;
        }
        return OPTVAL(scanresult_t, make_token_result(token_make_string(quote, ix < buffer.len, false), ix + 1));
    }
    return OPTNULL(scanresult_t);
}

opt_scanresult_t whitespacescanner(void *ctx, slice_t buffer)
{
    (void) ctx;
    size_t ix = 0;
    char   cur = buffer.items[ix];
    switch (cur) {
    case '\n':
#ifdef WS_IGNORE
        return OPTVAL(scanresult_t, make_skip_result(0, 1));
#else
        return OPTVAL(scanresult_t, make_token_result(token_make_end_of_line(), 1));
#endif
    case '\t':
#ifdef WS_IGNORE
        return OPTVAL(scanresult_t, make_skip_result(0, 1));
#else
        return OPTVAL(scanresult_t, make_token_result(token_make_tab(), 1));
#endif
    case ' ':
        while (ix < buffer.len && buffer.items[ix] == ' ') {
            ++ix;
        }
#ifdef WS_IGNORE
        return OPTVAL(scanresult_t, make_skip_result(0, ix));
#else
        return OPTVAL(scanresult_t, make_token_result(token_make_whitespace(), ix));
#endif
    default:
        return OPTNULL(scanresult_t);
    }
}

opt_keywordmatch_t keyword_match(slice_t s)
{
    opt_keywordcode_t prefix;
    for (size_t ix = 0; keywords[ix].len > 0; ++ix) {
        slice_t       kw = keywords[ix];
        keywordcode_t current = (keywordcode_t) ix;
        if (slice_startswith(kw, s)) {
            if (slice_eq(kw, s)) {
                return OPTVAL(keywordmatch_t, ((keywordmatch_t) {
                                                  .keyword = current,
                                                  .match_type = MT_FullMatch,
                                              }));
            }
            prefix = OPTVAL(keywordcode_t, current);
        }
    }
    if (prefix.ok) {
        return OPTVAL(keywordmatch_t, ((keywordmatch_t) {
                                          .keyword = prefix.value,
                                          .match_type = MT_PrefixMatch,
                                      }));
    }
    return OPTNULL(keywordmatch_t);
}

opt_scanresult_t identifierscanner(void *ctx, slice_t buffer)
{
    (void) ctx;
    size_t ix = 0;
    char   cur = buffer.items[ix];
    if (isalpha(cur) || cur == '_') {
        for (; (ix < buffer.len) && (isalnum(buffer.items[ix]) || buffer.items[ix] == '_'); ++ix) {
            ;
        }
        opt_keywordmatch_t kw_match = keyword_match(slice_first(buffer, ix));
        if (kw_match.ok && kw_match.value.match_type == MT_FullMatch) {
            return OPTVAL(scanresult_t, make_token_result(token_make_keyword(kw_match.value.keyword), ix));
        } else {
            return OPTVAL(scanresult_t, make_token_result(token_make_identifier(), ix));
        }
    }
    return OPTNULL(scanresult_t);
}

opt_scanresult_t keywordscanner(void *ctx, slice_t buffer)
{
    (void) ctx;
    for (size_t ix = 1; ix <= buffer.len; ++ix) {
        opt_keywordmatch_t kw_match = keyword_match(slice_first(buffer, ix));
        if (!kw_match.ok) {
            break;
        }
        if (kw_match.value.match_type == MT_FullMatch) {
            return OPTVAL(scanresult_t, make_token_result(token_make_keyword(kw_match.value.keyword), ix));
        }
    }
    return OPTNULL(scanresult_t);
}

opt_scanresult_t symbolmuncher(void *ctx, slice_t buffer)
{
    (void) ctx;
    if (buffer.len == 0) {
        return OPTVAL(scanresult_t, make_token_result(token_make_end_of_file(), 0));
    }
    return OPTVAL(scanresult_t, make_token_result(token_make_symbol(buffer.items[0]), 1));
}

slice_t lexer_token_text(lexer_t *lexer, token_t token)
{
    return (slice_t) {
        .items = lexer->buffer.items + token.location.index,
        .len = token.location.length,
    };
}

void lexer_push_source(lexer_t *lexer, slice_t src, scanner_def_t scanner)
{
    lexer->buffer = src;
    dynarr_clear(&lexer->tokens);
    size_t          index = 0;
    tokenlocation_t loc = { 0 };
    while (src.len > 0) {
        scanresult_t ret = UNWRAP(scanresult_t, scanner.scanner(scanner.ctx, src));
        index += ret.matched;
        src = slice_tail(src, ret.matched);
        loc.length = ret.matched;
        switch (ret.result) {
        case SRT_Token: {
            ret.token.location = loc;
            // trace("%s", tokenkind_name(ret.token.kind));
            dynarr_append(&(lexer->tokens), ret.token);
        } break;
        default:
            break;
        }
        while (loc.index < index) {
            if (lexer->buffer.items[loc.index] == '\n') {
                loc.line += 1;
                loc.column = 0;
            } else {
                loc.column += 1;
            }
            loc.index += 1;
        }
        loc.length = 0;
    }
    dynarr_append(&lexer->tokens, token_make_end_of_file());
    lexer->cursor = 0;
}

token_t lexer_peek(lexer_t *lexer)
{
    if (lexer->cursor < lexer->tokens.len) {
        return lexer->tokens.items[lexer->cursor];
    }
    return token_make_end_of_file();
}

token_t lexer_lex(lexer_t *lexer)
{
    token_t ret = lexer_peek(lexer);
    if (lexer->cursor < lexer->tokens.len) {
        lexer->cursor += 1;
    }
    return ret;
}

lexerresult_t lexer_expect(lexer_t *lexer, tokenkind_t kind)
{
    token_t ret = lexer_peek(lexer);
    if (!token_matches(ret, kind)) {
        return RESERR(lexerresult_t, LE_UnexpectedTokenKind);
    }
    return RESVAL(lexerresult_t, lexer_lex(lexer));
}

bool lexer_accept(lexer_t *lexer, tokenkind_t kind)
{
    token_t ret = lexer_peek(lexer);
    if (!token_matches(ret, kind)) {
        return false;
    }
    lexer_lex(lexer);
    return true;
}

lexerresult_t lexer_expect_keyword(lexer_t *lexer, keywordcode_t code)
{
    token_t ret = lexer_peek(lexer);
    if (!token_matches(ret, TK_Keyword)) {
        return RESERR(lexerresult_t, LE_UnexpectedTokenKind);
    }
    if (!token_matches_keyword(ret, code)) {
        return RESERR(lexerresult_t, LE_UnexpectedKeyword);
    }
    return RESVAL(lexerresult_t, lexer_lex(lexer));
}

bool lexer_accept_keyword(lexer_t *lexer, keywordcode_t code)
{
    token_t ret = lexer_peek(lexer);
    if (!token_matches_keyword(ret, code)) {
        return false;
    }
    lexer_lex(lexer);
    return true;
}

lexerresult_t lexer_expect_symbol(lexer_t *lexer, int symbol)
{
    token_t ret = lexer_peek(lexer);
    if (!token_matches(ret, TK_Symbol)) {
        return RESERR(lexerresult_t, LE_UnexpectedTokenKind);
    }
    if (!token_matches_symbol(ret, symbol)) {
        return RESERR(lexerresult_t, LE_UnexpectedSymbol);
    }
    return RESVAL(lexerresult_t, lexer_lex(lexer));
}

bool lexer_accept_symbol(lexer_t *lexer, int symbol)
{
    token_t ret = lexer_peek(lexer);
    if (!token_matches_symbol(ret, symbol)) {
        return false;
    }
    lexer_lex(lexer);
    return true;
}

lexerresult_t lexer_expect_identifier(lexer_t *lexer)
{
    token_t ret = lexer_peek(lexer);
    if (!token_is_identifier(ret)) {
        return RESERR(lexerresult_t, LE_UnexpectedTokenKind);
    }
    lexer_lex(lexer);
    return RESVAL(lexerresult_t, ret);
}

opt_token_t lexer_accept_identifier(lexer_t *lexer)
{
    token_t ret = lexer_peek(lexer);
    if (!token_is_identifier(ret)) {
        return OPTNULL(token_t);
    }
    return OPTVAL(token_t, lexer_lex(lexer));
}

bool lexer_matches(lexer_t *lexer, tokenkind_t kind)
{
    return token_matches(*(lexer->tokens.items + lexer->cursor), kind);
}

bool lexer_matches_symbol(lexer_t *lexer, int sym)
{
    return token_matches_symbol(*(lexer->tokens.items + lexer->cursor), sym);
}

bool lexer_matches_keyword(lexer_t *lexer, keywordcode_t keyword)
{
    return token_matches_keyword(*(lexer->tokens.items + lexer->cursor), keyword);
}

bool lexer_exhausted(lexer_t *lexer)
{
    return lexer->cursor < lexer->tokens.len;
}

void lexer_push_back(lexer_t *lexer)
{
    assert(lexer->cursor > 0 && lexer->cursor < lexer->tokens.len);
    lexer->cursor -= 1;
}

bool lexer_has_lookback(lexer_t *lexer, size_t lookback)
{
    return lexer->cursor > lookback;
}

token_t lexer_lookback(lexer_t *lexer, size_t lookback)
{
    assert(lexer->cursor > lookback);
    return lexer->tokens.items[lexer->cursor - lookback];
}

#endif /* LEXER_IMPLEMENTED */
#endif /* LEXER_IMPLEMENTATION */

#ifdef LEXER_TEST

void test_line_comment_scanner()
{
    opt_scanresult_t res = linecomment(
        &slash_slash,
        C("// Well hello there\n"
          "foo bar"));
    assert(res.ok);
    scanresult_t r = res.value;
    assert(r.result == SRT_Token);
    token_t t = r.token;
    assert(token_matches(t, TK_Comment));
    assert(t.comment_text.comment_type == CT_Line);
    assert(t.comment_text.terminated);
    assert(r.matched == strlen("// Well hello there"));
}

void test_block_comment_scanner()
{
    slice_t          text = C("/* Well hello there\n"
                                       "Sailor */ more stuff\n"
                                       "foo bar");
    opt_scanresult_t res = blockcomment(&c_block_comment, text);
    assert(res.ok);
    scanresult_t r = res.value;
    assert(r.result == SRT_Token);
    token_t t = r.token;
    assert(token_matches(t, TK_Comment));
    assert(t.comment_text.comment_type == CT_Block);
    assert(!t.comment_text.terminated);
    assert(r.matched == strlen("/* Well hello there\n"));

    text = slice_tail(text, r.matched);
    res = blockcomment(&c_block_comment, text);
    assert(res.ok);
    r = res.value;
    assert(r.result == SRT_Token);
    t = r.token;
    assert(token_matches(t, TK_Comment));
    assert(t.comment_text.comment_type == CT_Block);
    assert(t.comment_text.terminated);
    assert(r.matched == strlen("Sailor */"));
}

void test_raw_scanner()
{
    slice_t      text = C("@begin\n"
                               "  Well hello there\n"
                               "  Sailor\n"
                               "@end bla bla bla\n");
    rawscanner_t raw = (rawscanner_t) {
        .begin = C("@begin"),
        .end = C("@end"),
    };
    opt_scanresult_t res = rawscanner(&raw, text);
    assert(res.ok);
    scanresult_t r = res.value;
    assert(r.result == SRT_Token);
    token_t t = r.token;
    assert(token_matches(t, TK_Raw));
    assert(slice_eq(t.rawtext.marker, C("@begin")));
    assert(t.rawtext.terminated);
    assert(r.matched == strlen("@begin\n"
                               "  Well hello there\n"
                               "  Sailor\n"
                               "@end"));

    text = slice_first(text, r.matched - raw.end.len);
    res = rawscanner(&raw, text);
    assert(res.ok);
    r = res.value;
    assert(r.result == SRT_Token);
    t = r.token;
    assert(token_matches(t, TK_Raw));
    assert(slice_eq(t.rawtext.marker, C("@begin")));
    assert(!t.rawtext.terminated);
    assert(r.matched == text.len);
}

void test_number_scanner()
{
    slice_t      numbers = C("4 3.14 0xBABECAFE 0b0110");
    size_t       lengths[] = { 1, 4, 10, 6 };
    numbertype_t types[] = { NUM_Integer, NUM_Decimal, NUM_HexNumber, NUM_BinaryNumber };
    for (int ix = 0; ix < 4; ++ix) {
        opt_scanresult_t res = numberscanner(NULL, numbers);
        assert(res.ok);
        scanresult_t r = res.value;
        assert(r.result == SRT_Token);
        token_t t = r.token;
        assert(token_matches(t, TK_Number));
        assert(r.matched == lengths[ix]);
        assert(t.number == types[ix]);
        numbers = slice_tail(numbers, r.matched + 1);
    }
}

void test_quoted_string_scanner()
{
    stringscanner_t config = {
        .quotes = C("\"'`"),
    };
    slice_t     strings = C("\"Hello\" 'Hello' `Hello`");
    quotetype_t quotes[] = { QT_DoubleQuote, QT_SingleQuote, QT_BackQuote };
    for (int ix = 0; ix < 3; ++ix) {
        opt_scanresult_t res = stringscanner(&config, strings);
        assert(res.ok);
        scanresult_t r = res.value;
        assert(r.result == SRT_Token);
        token_t t = r.token;
        assert(token_matches(t, TK_String));
        assert(r.matched == 7);
        assert(t.quoted_string.quote_type == quotes[ix]);
        assert(t.quoted_string.terminated);
        assert(!t.quoted_string.triple);
        strings = slice_tail(strings, r.matched + 1);
    }
}

void test_whitespace_scanner()
{
    slice_t     ws = C("    x\nx\tx");
    tokenkind_t kinds[] = { TK_Whitespace, TK_EndOfLine, TK_Tab };
    for (size_t ix = 0; ix < sizeof(kinds) / sizeof(kinds[0]); ++ix) {
        opt_scanresult_t res = whitespacescanner(NULL, ws);
        assert(res.ok);
        scanresult_t r = res.value;
        assert(r.result == SRT_Token);
        token_t t = r.token;
        assert(token_matches(t, kinds[ix]));
        assert(r.matched == ((kinds[ix] == TK_Whitespace) ? 4 : 1));
        ws = slice_tail(ws, r.matched + 1);
    }
}

void test_identifier_scanner()
{
    slice_t idents = C("ident ide_t ide9t iden9 _dent");
    for (int ix = 0; ix < 5; ++ix) {
        opt_scanresult_t res = identifierscanner(NULL, idents);
        assert(res.ok);
        scanresult_t r = res.value;
        assert(r.result == SRT_Token);
        token_t t = r.token;
        assert(token_matches(t, TK_Identifier));
        assert(r.matched == 5);
        idents = slice_tail(idents, r.matched + 1);
    }
}

void test_keyword_scanner()
{
    slice_t       kws = C("if while else");
    testkeyword_t kw_codes[] = { KW_If, KW_While, KW_Else };
    for (int ix = 0; ix < 3; ++ix) {
        opt_scanresult_t res = keywordscanner(NULL, kws);
        assert(res.ok);
        scanresult_t r = res.value;
        assert(r.result == SRT_Token);
        token_t t = r.token;
        assert(token_matches(t, TK_Keyword));
        assert(t.keyword == kw_codes[ix]);
        assert(r.matched == test_keywords[t.keyword].len);
        kws = slice_tail(kws, r.matched + 1);
    }
}

slice_t test_string = C(
    " if(x == 12) {\n"
    "   // Success\n"
    "   print(\"Boo!\");\n"
    " } else {\n"
    "   /* Failure */\n"
    "   print(\"Error\");\n"
    " }\n");

void test_lexer()
{
    lexer_t lexer = { 0 };
    lexer_push_source(&lexer, test_string, c_scanner);

    tokenkind_t expected[] = {
        TK_Whitespace,
        TK_Keyword,
        TK_Symbol,
        TK_Identifier,
        TK_Whitespace,
        TK_Symbol,
        TK_Symbol,
        TK_Whitespace,
        TK_Number,
        TK_Symbol,
        TK_Whitespace,
        TK_Symbol,
        TK_EndOfLine,
        TK_Whitespace,
        TK_Comment,
        TK_EndOfLine,
        TK_Whitespace,
        TK_Identifier,
        TK_Symbol,
        TK_String,
        TK_Symbol,
        TK_Symbol,
        TK_EndOfLine,
        TK_Whitespace,
        TK_Symbol,
        TK_Whitespace,
        TK_Keyword,
        TK_Whitespace,
        TK_Symbol,
        TK_EndOfLine,
        TK_Whitespace,
        TK_Comment,
        TK_EndOfLine,
        TK_Whitespace,
        TK_Identifier,
        TK_Symbol,
        TK_String,
        TK_Symbol,
        TK_Symbol,
        TK_EndOfLine,
        TK_Whitespace,
        TK_Symbol,
        TK_EndOfLine,
        TK_EndOfFile,
    };

    for (size_t ix = 0; ix < sizeof(expected) / sizeof(tokenkind_t); ++ix) {
        token_t t = lexer_peek(&lexer);
        assert(t.kind == expected[ix]);
        lexer_lex(&lexer);
    }
}

int main()
{
    test_line_comment_scanner();
    test_block_comment_scanner();
    test_raw_scanner();
    test_number_scanner();
    test_quoted_string_scanner();
    test_whitespace_scanner();
    test_identifier_scanner();
    test_keyword_scanner();
    test_lexer();
    return 0;
}

#endif
