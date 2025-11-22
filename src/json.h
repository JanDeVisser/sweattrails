/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#if defined(JSON_TEST) || defined(ELROND_IMPLEMENTATION)
#define JSON_IMPLEMENTATION
#define SLICE_IMPLEMENTATION
#define DA_IMPLEMENTATION
#endif

#ifndef __JSON_H__
#define __JSON_H__

#include "da.h"
#include "slice.h"

#define JSONTYPES(S) \
    S(Array)         \
    S(Boolean)       \
    S(Null)          \
    S(Number)        \
    S(Object)        \
    S(String)

typedef enum _json_value_type {
#undef S
#define S(T) JT_##T,
    JSONTYPES(S)
#undef S
} json_value_type_h;

typedef struct _json_attrib {
    slice_t key;
    nodeptr value;
} json_attrib_t;
typedef DA(json_attrib_t) json_object_t;

typedef struct _json_value {
    json_value_type_h type;
    union {
        nodeptrs      array;
        bool          boolean;
        double        number;
        json_object_t object;
        slice_t       string;
    };
} json_value_t;

typedef DA(json_value_t) json_values_t;

typedef struct _json {
    json_values_t values;
    nodeptr       root;
} json_t;

typedef struct _json_decode_error {
    int     line;
    int     column;
    slice_t error;
} json_decode_error_t;

OPTDEF(json_decode_error_t);
typedef RES(nodeptr, json_decode_error_t) json_deserialize_result_t;
typedef RES(json_t, json_decode_error_t) json_decode_result_t;

json_decode_result_t json_decode(slice_t jsontext);
sb_t                 json_encode(json_t json);

#define json_get(json, ptr)                               \
    (                                                     \
        {                                                 \
            nodeptr __p = (ptr);                          \
            json_t *__j = (json);                         \
            assert(__p.ok && __p.value < _j->values.len); \
            return (__j)->values.items + (__p).value;     \
        })

#define json_get_object(json, ptr)                                        \
    (                                                                     \
        {                                                                 \
            nodeptr __p = (ptr);                                          \
            json_t *__j = (json);                                         \
            assert(__p.ok && __p.value < __j->values.len);                \
            json_value_t *__val = (__j)->values.items + (__p).value;      \
            (json_value_t *) ((__val->type == JT_Object) ? __val : NULL); \
        })

#define json_get_array(json, ptr)                                        \
    (                                                                    \
        {                                                                \
            nodeptr __p = (ptr);                                         \
            json_t *__j = (json);                                        \
            assert(__p.ok && __p.value < __j->values.len);               \
            json_value_t *__val = (__j)->values.items + (__p).value;     \
            (json_value_t *) ((__val->type == JT_Array) ? __val : NULL); \
        })

#define json_get_string(json, ptr)                                         \
    (                                                                      \
        {                                                                  \
            nodeptr __p = (ptr);                                           \
            json_t *__j = (json);                                          \
            assert(__p.ok && __p.value < __j->values.len);                 \
            json_value_t *__val = (__j)->values.items + (__p).value;       \
            (slice_t)((__val->type != JT_String) ? C("") : __val->string); \
        })

#endif /* __JSON_H__ */

#ifdef JSON_IMPLEMENTATION
#ifndef JSON_IMPLEMENTED

typedef enum _jsonkeyword {
    JSON_KW_False = 0,
    JSON_KW_Null,
    JSON_KW_True,
    JSON_KW_Max,
} jsonkeyword_t;

slice_t json_keywords[] = {
    [JSON_KW_False] = { .items = "false", .len = 5 },
    [JSON_KW_Null] = { .items = "null", .len = 4 },
    [JSON_KW_True] = { .items = "true", .len = 4 },
    [JSON_KW_Max] = { .items = NULL, .len = 0 },
};

#define keywordcode jsonkeyword_t
#define keywords json_keywords
#define WS_IGNORE
#define COMMENT_IGNORE
#define LEXER_IMPLEMENTATION

#include "lexer.h"

static void json_serialize(json_t json, nodeptr n, sb_t *sb)
{
    assert(n.ok);
    json_value_t *value = json.values.items + n.value;
    switch (value->type) {
    case JT_Array:
        sb_append_char(sb, '[');
        for (size_t ix = 0; ix < value->array.len; ++ix) {
            if (ix > 0) {
                sb_append_char(sb, ',');
            }
            json_serialize(json, value->array.items[ix], sb);
        }
        sb_append_char(sb, ']');
        break;
    case JT_Boolean:
        sb_append_cstr(sb, (value->boolean) ? "true" : "false");
        break;
    case JT_Null:
        sb_append_cstr(sb, "null");
        break;
    case JT_Number:
        sb_printf(sb, "%lf", value->number);
        break;
    case JT_Object:
        for (size_t ix = 0; ix < value->object.len; ++ix) {
        }
        sb_append_char(sb, '{');
        for (size_t ix = 0; ix < value->object.len; ++ix) {
            if (ix > 0) {
                sb_append_char(sb, ',');
            }
            sb_append_char(sb, '"');
            sb_escape(sb, value->object.items[ix].key);
            sb_append_cstr(sb, "\":");
            json_serialize(json, value->object.items[ix].value, sb);
        }
        sb_append_char(sb, '}');
        break;
    case JT_String:
        sb_append_char(sb, '"');
        sb_escape(sb, value->string);
        sb_append_char(sb, '"');
        break;
    }
}

sb_t json_encode(json_t json)
{
    sb_t ret = { 0 };
    if (json.root.ok) {
        json_serialize(json, json.root, &ret);
    }
    return ret;
}

scanner_def_t json_scanner_with_comments_pack_def[] = {
    { .scanner = scannerpack, .ctx = (void *) &c_style_comments },
    { .scanner = numberscanner, .ctx = NULL },
    { .scanner = stringscanner, .ctx = (void *) &double_quotes },
    { .scanner = whitespacescanner, .ctx = (void *) true },
    { .scanner = keywordscanner, .ctx = NULL },
    { .scanner = symbolmuncher, .ctx = NULL },
};

scannerpack_t json_scanner_with_comments_pack = {
    .scanners = json_scanner_with_comments_pack_def
};

scanner_def_t json_with_comments_scanner = {
    .scanner = scannerpack, .ctx = (void *) &json_scanner_with_comments_pack
};

scanner_def_t json_scanner_pack_def[] = {
    { .scanner = numberscanner, .ctx = NULL },
    { .scanner = stringscanner, .ctx = (void *) &double_quotes },
    { .scanner = whitespacescanner, .ctx = (void *) true },
    { .scanner = keywordscanner, .ctx = NULL },
    { .scanner = symbolmuncher, .ctx = NULL },
};

scannerpack_t json_scanner_pack = { .scanners = json_scanner_pack_def };

scanner_def_t json_scanner = { .scanner = scannerpack,
    .ctx = (void *) &json_scanner_pack };

json_decode_error_t make_decode_error(token_t t, slice_t error)
{
    return (json_decode_error_t) {
        .line = t.location.line,
        .column = t.location.column,
        .error = error,
    };
}

json_deserialize_result_t json_deserialize(json_t *json, lexer_t *lexer)
{
    token_t      t = lexer_peek(lexer);
    json_value_t ret = { 0 };
    switch (t.kind) {
    case TK_Symbol:
        switch (t.symbol) {
        case '{': {
            lexer_lex(lexer);
            ret.type = JT_Object;
            while (!lexer_accept_symbol(lexer, '}')) {
                lexerresult_t name_res = lexer_expect(lexer, TK_String);
                if (!name_res.ok) {
                    return RESERR(json_deserialize_result_t,
                        make_decode_error(t, C("Expected object member name")));
                }
                sb_t    name = { 0 };
                slice_t s = lexer_token_text(lexer, name_res.success);
                sb_unescape(&name, slice_sub(s, 1, s.len - 1));
                if (!lexer_expect_symbol(lexer, ':').ok) {
                    return RESERR(json_deserialize_result_t,
                        make_decode_error(t, C("Expected `:`")));
                }
                nodeptr value = TRY(json_deserialize_result_t, json_deserialize(json, lexer));
                dynarr_append_s(json_attrib_t, &ret.object, .key = sb_as_slice(name), .value = value);
                if (lexer_accept_symbol(lexer, '}')) {
                    break;
                }
                if (!lexer_expect_symbol(lexer, ',').ok) {
                    return RESERR(json_deserialize_result_t,
                        make_decode_error(t, C("Expected `,` in object")));
                }
            }
            for (size_t ix = 0; ix < ret.object.len; ++ix) {
            }
        } break;
        case '[': {
            lexer_lex(lexer);
            ret.type = JT_Array;
            while (!lexer_accept_symbol(lexer, ']')) {
                json_deserialize_result_t value_res = json_deserialize(json, lexer);
                if (!value_res.ok) {
                    return value_res;
                }
                dynarr_append(&ret.array, value_res.success);
                if (lexer_accept_symbol(lexer, ']')) {
                    break;
                }
                if (!lexer_expect_symbol(lexer, ',').ok) {
                    return RESERR(json_deserialize_result_t,
                        make_decode_error(t, C("Expected `,` in array")));
                }
            }
        } break;
        default:
            return RESERR(json_deserialize_result_t,
                make_decode_error(t, C("Unexpected symbol")));
        }
        break;
    case TK_Keyword:
        lexer_lex(lexer);
        switch (t.keyword) {
        case JSON_KW_False:
            ret = (json_value_t) { .type = JT_Boolean, .boolean = false };
            break;
        case JSON_KW_Null:
            ret = (json_value_t) { .type = JT_Null };
            break;
        case JSON_KW_True:
            ret = (json_value_t) { .type = JT_Boolean, .boolean = true };
            break;
        default:
            UNREACHABLE();
        }
        break;
    case TK_String: {
        lexer_lex(lexer);
        sb_t    unescaped = { 0 };
        slice_t s = lexer_token_text(lexer, t);
        sb_unescape(&unescaped, slice_sub(s, 1, s.len - 1));
        ret = (json_value_t) { .type = JT_String, .string = sb_as_slice(unescaped) };
    } break;
    case TK_Number:
        lexer_lex(lexer);
        opt_long num = slice_to_long(lexer_token_text(lexer, t), 0);
        assert(num.ok);
        // TODO floating point numbers
        ret = (json_value_t) { .type = JT_Number, .number = num.value };
        break;
    default:
        UNREACHABLE();
    }
    dynarr_append(&json->values, ret);
    return RESVAL(json_deserialize_result_t, nodeptr_ptr(json->values.len - 1));
}

json_decode_result_t json_decode(slice_t jsontext)
{
    lexer_t lexer = { 0 };
    json_t  ret = { 0 };
    lexer_push_source(&lexer, jsontext, json_scanner);

    json_deserialize_result_t res = json_deserialize(&ret, &lexer);
    if (!res.ok) {
        return RESERR(json_decode_result_t, res.error);
    }
    token_t t = lexer_peek(&lexer);
    if (t.kind != TK_EndOfFile) {
        return RESERR(json_decode_result_t, make_decode_error(t, C("Unexpected text at end of JSON value")));
    }
    ret.root = res.success;
    return RESVAL(json_decode_result_t, ret);
}

#define JSON_IMPLEMENTED
#endif /* JSON_IMPLEMENTED */
#endif /* JSON_IMPLEMENTATION */

#ifdef JSON_TEST

int main()
{
    char const jsontext[] = "{\"hello\":42,\"foo\":[true,false,null],\"s\":\"Hello, World!\"}";

    do_trace = true;
    json_decode_result_t res = json_decode(C(jsontext));
    if (!res.ok) {
        printf(SL "\n", SLARG(res.error.error));
    }
    assert(res.ok);
    json_t json = res.success;
    sb_t   serialized = json_encode(json);
    assert(serialized.len > 0);
    assert(serialized.items[0] == '{');
    assert(serialized.items[serialized.len - 1] == '}');
}

#endif /* JSON_TEST */
