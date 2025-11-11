/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __DA_H__
#define __DA_H__

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef DA_TEST
#define SLICE_IMPLEMENTATION
#define DA_IMPLEMENTATION
#endif

#include "slice.h"

typedef struct _generic_da {
    void  *items;
    size_t len;
    size_t capacity;
} generic_da_t;

#define DA(T)            \
    struct {             \
        T     *items;    \
        size_t len;      \
        size_t capacity; \
    }

typedef DA(char) sb_t;
OPTDEF(sb_t);
typedef DA(sb_t) strings_t;
typedef DA(slice_t) slices_t;
typedef DA(opt_slice_t) opt_slices_t;
typedef DA(slice_pair_t) slice_pairs_t;
typedef DA(uint64_t) uint64s;
typedef DA(nodeptr) nodeptrs;
typedef char *cstr;
typedef DA(cstr) cstrs;
OPTDEF(nodeptrs);

#define GENDA(da) (generic_da_t *) (da), sizeof(*((da)->items))

#define dynarr_ensure(arr, mincap)               \
    do {                                         \
        generic_da_ensure(GENDA(arr), (mincap)); \
    } while (0)

#define dynarr_copy(A, T, arr)                                              \
    (                                                                       \
        {                                                                   \
            A __copy = {                                                    \
                .items = (T *) allocator_alloc((arr).capacity * sizeof(T)), \
                .len = (arr).len,                                           \
                .capacity = (arr).capacity,                                 \
            };                                                              \
            if (__copy.items == NULL) {                                     \
                fprintf(stderr, "Out of memory.\n");                        \
                abort();                                                    \
            }                                                               \
            memcpy(__copy.items, (arr).items, sizeof(T) * (arr).len);       \
            __copy;                                                         \
        })

#define dynarr_remove_unordered(T, arr, ix)                            \
    (                                                                  \
        {                                                              \
            size_t __ix = (ix);                                        \
            T      __deleted = { 0 };                                  \
            if (__ix >= 0 && __ix < (arr)->len) {                      \
                __deleted = (arr)->items[ix];                          \
                if (__ix < (arr)->len - 1) {                           \
                    (arr)->items[__ix] = (arr)->items[(arr)->len - 1]; \
                }                                                      \
                --((arr)->len);                                        \
            }                                                          \
            (__deleted);                                               \
        })

#define dynarr_remove_ordered(T, arr, ix)                               \
    (                                                                   \
        {                                                               \
            size_t __ix = (ix);                                         \
            T      __deleted = { 0 };                                   \
            if (__ix >= 0 || __ix < (arr)->len) {                       \
                __deleted = (arr)->items[ix];                           \
                if (__ix < (arr)->len - 1) {                            \
                    memcpy((arr)->items + __ix,                         \
                        (arr)->items + (__ix + 1),                      \
                        ((arr)->len - __ix) * sizeof((arr)->items[0])); \
                }                                                       \
                --((arr)->len);                                         \
            }                                                           \
            (__deleted);                                                \
        })

#define dynarr_sort(arr, cmp, thunk)                     \
    do {                                                 \
        void  *__base = (arr)->items;                    \
        size_t __nel = (arr)->len;                       \
        size_t __width = sizeof((arr)->items[0]);        \
        qsort_r(__base, __nel, __width, (thunk), (cmp)); \
    } while (0)

#define dynarr_clear(arr) generic_da_clear(GENDA(arr))
#define dynarr_free(arr) generic_da_free(GENDA(arr))

#define dynarr_append(arr, elem)              \
    do {                                      \
        dynarr_ensure((arr), (arr)->len + 1); \
        (arr)->items[(arr)->len++] = (elem);  \
        ((arr)->len - 1);                     \
    } while (0);

#define dynarr_append_s(T, arr, ...) \
    do {                             \
        T __elem = { __VA_ARGS__ };  \
        dynarr_append(arr, __elem);  \
    } while (0);

#define dynarr_pop(arr)       \
    do {                      \
        if ((arr)->len > 0) { \
            --(arr)->len;     \
        }                     \
    } while (0)

#define dynarr_back(arr)                                                         \
    (                                                                            \
        {                                                                        \
            if ((arr)->len == 0) {                                               \
                fprintf(stderr, "dynarr_back(): Out of bounds array access.\n"); \
                abort();                                                         \
            }                                                                    \
            (arr)->items + ((arr)->len - 1);                                     \
        })

#define dynarr_popback(T, arr)                                                   \
    (                                                                            \
        {                                                                        \
            if ((arr)->len == 0) {                                               \
                fprintf(stderr, "dynarr_back(): Out of bounds array access.\n"); \
                abort();                                                         \
            }                                                                    \
            T __t = (arr)->items[(arr)->len - 1];                                \
            --(arr)->len;                                                        \
            __t;                                                                 \
        })

#define dynarr_cmp(arr1, arr2)                                                            \
    (                                                                                     \
        {                                                                                 \
            size_t __sz_1 = sizeof(*((arr1).items));                                      \
            size_t __sz_2 = sizeof(*((arr2).items));                                      \
            if (__sz_1 != __sz_2) {                                                       \
                fprintf(stderr, "Comparing dynamic arrays of different types\n");         \
                abort();                                                                  \
            }                                                                             \
            (generic_da_cmp((generic_da_t *) &(arr1), (generic_da_t *) &(arr2), __sz_1)); \
        })

#define dynarr_eq(arr1, arr2) (dynarr_cmp((arr1), (arr2)) == 0)
#define dynarr_as_slice(arr) (slice_make((arr).items, (arr).len))
#define dynarr_foreach(T, it, arr) slice_foreach(T, it, arr)
#define dynarr_reverse(T, it, arr) slice_reverse(T, (it), (arr))
#define dynarr_find(T, haystack, needle, eq_fnc) slice_find(T, (haystack), (needle), (eq_fnc))

#define sb_as_slice(sb) dynarr_as_slice(sb)
#define sb_clear(sb) dynarr_clear((sb))
#define sb_free(sb) dynarr_free((sb))
#define sb_append_char(sb, ch)              \
    do {                                    \
        dynarr_ensure((sb), (sb)->len + 2); \
        (sb)->items[(sb)->len] = (ch);      \
        (sb)->items[(sb)->len + 1] = '\0';  \
        (sb)->len++;                        \
    } while (0);
#define sb_append_sb(sb, a) sb_append((sb), (sb_as_slice((a))))
#define sb_append_cstr(sb, a) sb_append((sb), C(a));
#define sb_make(s)                \
    (                             \
        {                         \
            sb_t __s = { 0 };     \
            sb_append(&__s, (s)); \
            (__s);                \
        })

#define sb_make_cstr(s)              \
    (                                \
        {                            \
            sb_t __s = { 0 };        \
            sb_append(&__s, C((s))); \
            (__s);                   \
        })

sb_t *sb_append(sb_t *sb, slice_t slice);
sb_t *sb_unescape(sb_t *sb, slice_t escaped);
sb_t *sb_escape(sb_t *sb, slice_t slice);
sb_t  sb_format(char const *fmt, ...) __attribute__((__format__(printf, 1, 2)));
sb_t  sb_vformat(char const *fmt, va_list args);
sb_t *sb_printf(sb_t *sb, char const *fmt, ...) __attribute__((__format__(printf, 2, 3)));
sb_t *sb_vprintf(sb_t *sb, char const *fmt, va_list args);

void   generic_da_ensure(generic_da_t *arr, size_t elem_size, size_t mincap);
size_t generic_da_append(generic_da_t *arr, size_t elem_size, void *elem);
void   generic_da_clear(generic_da_t *arr, size_t elem_size);
void   generic_da_free(generic_da_t *arr, size_t elem_size);
int    generic_da_cmp(generic_da_t *da1, generic_da_t *da2, size_t elem_size);

#endif /* __DA_H__ */

#if defined(DA_IMPLEMENTATION) || defined(JDV_IMPLEMENTATION)
#undef DA_IMPLEMENTATION
#ifndef DA_IMPLEMENTED
#define DA_IMPLEMENTED

void generic_da_ensure(generic_da_t *arr, size_t elem_size, size_t mincap)
{
    if (arr->capacity >= mincap) {
        return;
    }
    // trace("da_ensure(%zu,%zu,%zu,%zu)", arr->len, elem_size, arr->capacity, mincap);
    size_t cap = (arr->capacity > 0) ? arr->capacity : 16;
    while (cap < mincap) {
        cap *= 1.6;
    }
    void *newitems = allocator_realloc(arr->items, arr->capacity * elem_size, cap * elem_size);
    if (newitems == NULL) {
        fprintf(stderr, "Out of memory.\n");
        abort();
    }
    arr->items = newitems;
    arr->capacity = cap;
}

size_t generic_da_append(generic_da_t *arr, size_t elem_size, void *elem)
{
    generic_da_ensure(arr, elem_size, arr->len + 1);
    memcpy(arr->items + arr->len * elem_size, elem, elem_size);
    ++arr->len;
    return arr->len - 1;
}

void generic_da_clear(generic_da_t *arr, size_t elem_size)
{
    (void) elem_size;
    arr->len = 0;
}

void generic_da_free(generic_da_t *arr, size_t elem_size)
{
    (void) elem_size;
    allocator_free(arr->items);
    arr->len = 0;
    arr->items = NULL;
    arr->capacity = 0;
}

int generic_da_cmp(generic_da_t *da1, generic_da_t *da2, size_t elem_size)
{
    if (da1->len != da2->len) {
        return da1->len - da2->len;
    }
    return memcmp(da1->items, da2->items, da1->len * elem_size);
}

sb_t sb_format(char const *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    sb_t ret = sb_vformat(fmt, args);
    va_end(args);
    return ret;
}

sb_t sb_vformat(char const *fmt, va_list args)
{
    sb_t ret = { 0 };
    sb_vprintf(&ret, fmt, args);
    return ret;
}

sb_t *sb_append(sb_t *sb, slice_t slice)
{
    dynarr_ensure(sb, sb->len + slice.len + 1);
    sb->items[sb->len + slice.len] = 0;
    memcpy(sb->items + sb->len, slice.items, slice.len);
    sb->len += slice.len;
    return sb;
}

sb_t *sb_unescape(sb_t *sb, slice_t escaped)
{
    if (!slice_indexof(escaped, '\\').ok) {
        return sb_append(sb, escaped);
    }
    bool prev_was_backslash = false;
    for (size_t ix = 0; ix < escaped.len; ++ix) {
        int ch = escaped.items[ix];
        if (prev_was_backslash) {
            switch (ch) {
            case 'b':
                sb_append_char(sb, '\b');
                break;
            case 'f':
                sb_append_char(sb, '\f');
                break;
            case 'n':
                sb_append_char(sb, '\n');
                break;
            case 't':
                sb_append_char(sb, '\t');
                break;
            case 'r':
                sb_append_char(sb, '\r');
                break;
            default:
                sb_append_char(sb, ch);
                break;
            }
            prev_was_backslash = false;
            continue;
        }
        if (ch == '\\') {
            prev_was_backslash = true;
            continue;
        }
        sb_append_char(sb, ch);
    }
    return sb;
}

sb_t *sb_escape(sb_t *sb, slice_t slice)
{
    if (!slice_first_of(slice, C("\"\\\b\f\n\t\r")).ok) {
        return sb_append(sb, slice);
    }
    for (size_t ix = 0; ix < slice.len; ++ix) {
        int ch = slice.items[ix];
        switch (ch) {
        case '\b':
            sb_append_cstr(sb, "\\b");
            break;
        case '\f':
            sb_append_cstr(sb, "\\f");
            break;
        case '\n':
            sb_append_cstr(sb, "\\n");
            break;
        case '\t':
            sb_append_cstr(sb, "\\t");
            break;
        case '\r':
            sb_append_cstr(sb, "\\r");
            break;
        case '\\':
            sb_append_cstr(sb, "\\\\");
            break;
        case '"':
            sb_append_cstr(sb, "\\\"");
            break;
        default:
            sb_append_char(sb, ch);
            break;
        }
    }
    return sb;
}

sb_t *sb_vprintf(sb_t *sb, char const *fmt, va_list args)
{
    va_list copy;
    va_copy(copy, args);
    size_t n = vsnprintf(NULL, 0, fmt, copy);
    dynarr_ensure(sb, sb->len + n + 1);
    va_end(copy);
    vsnprintf(sb->items + sb->len, n + 1, fmt, args);
    sb->len += n;
    return sb;
}

sb_t *sb_printf(sb_t *sb, char const *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    sb_vprintf(sb, fmt, args);
    return sb;
}

#endif /* DA_IMPLEMENTED */
#endif /* DA_IMPLEMENTATION */

#ifdef DA_TEST

int main()
{
    sb_t sb = { 0 };
    sb_printf(&sb, "Hello, %s\n", "World");
    assert(strncmp(sb.items, "Hello, World\n", strlen("Hello, World\n")) == 0);
    sb_printf(&sb, "Hello, World\n");
    assert(strncmp(sb.items, "Hello, World\nHello, World\n", 2 * strlen("Hello, World\n")) == 0);
    for (size_t ix = 0; ix < 100; ++ix) {
        sb_printf(&sb, "Hello, World\n");
        assert(sb.len == (ix + 3) * strlen("Hello, World\n"));
        assert(strncmp(sb.items, "Hello, World\nHello, World\n", 2 * strlen("Hello, World\n")) == 0);
    }
    return 0;
}

#endif
