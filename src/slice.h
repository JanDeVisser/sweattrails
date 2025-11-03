/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

// #define SLICE_TEST
#if defined(SLICE_TEST) || defined(JDV_IMPLEMENTATION)
#define SLICE_IMPLEMENTATION
#endif

#ifndef __SLICE_H__
#define __SLICE_H__

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIN(a, b) ((a < b) ? (a) : (b))
#define MAX(a, b) ((a > b) ? (a) : (b))
#define ALIGNAT(bytes, align) ((bytes + (align - 1)) & ~(align - 1))

extern bool do_trace;

#define _fatal(file, line, prefix, msg, ...)    \
    do {                                        \
        fprintf(stderr, "%s:%d: ", file, line); \
        fputs(prefix, stderr);                  \
        fprintf(stderr, msg, ##__VA_ARGS__);    \
        fputc('\n', stderr);                    \
        abort();                                \
    } while (0)

#define UNREACHABLE_MSG(msg, ...) _fatal(__FILE__, __LINE__, "Unreachable", msg, ##__VA_ARGS__)
#define UNREACHABLE() _fatal(__FILE__, __LINE__, "Unreachable: ", "")
#define TODO(msg, ...) _fatal(__FILE__, __LINE__, "Not Yet Implemented: ", msg, ##__VA_ARGS__)
#define NYI(msg, ...) TODO(msg, ##__VA_ARGS__)
#define fatal(msg, ...) _fatal(__FILE__, __LINE__, "", msg, ##__VA_ARGS__);
#define fatal_file_line(f, l, msg, ...) _fatal(f, l, "", msg, ##__VA_ARGS__);
#ifdef NDEBUG
#define trace(msg, ...)
#else
#define trace(msg, ...)                                     \
    do {                                                    \
        if (do_trace) {                                     \
            fprintf(stderr, "%s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, msg, ##__VA_ARGS__);            \
            fputc('\n', stderr);                            \
        }                                                   \
    } while (0)
#endif

#define ALLOCATOR_VTABLE                                                                \
    void *(*alloc)(allocator_t * alloc, size_t size);                                   \
    void *(*realloc)(allocator_t * alloc, void *ptr, size_t old_size, size_t new_size); \
    void (*free)(allocator_t * alloc, void *ptr);                                       \
    void (*destroy)(allocator_t * alloc);                                               \
    allocator_t *prev_allocator;

typedef struct _allocator allocator_t;
struct _allocator {
    ALLOCATOR_VTABLE;
};

void *stdc_alloc(allocator_t *alloc, size_t size);
void *stdc_realloc(allocator_t *alloc, void *ptr, size_t old_size, size_t new_size);
void  stdc_free(allocator_t *alloc, void *ptr);

static allocator_t stdc_allocator = {
    .alloc = stdc_alloc,
    .realloc = stdc_realloc,
    .free = stdc_free,
    .destroy = NULL,
    .prev_allocator = NULL,
};

void *temp_allocator_alloc(allocator_t *alloc, size_t size);

static allocator_t temp_allocator = {
    .alloc = temp_allocator_alloc,
    .realloc = NULL,
    .free = NULL,
    .destroy = NULL,
    .prev_allocator = NULL,
};

static allocator_t *current_allocator = &stdc_allocator;

void  allocator_push(allocator_t *alloc);
void  allocator_pop();
char *allocator_alloc(size_t size);
char *allocator_realloc(char *ptr, size_t old_size, size_t new_size);
void  allocator_free(char *ptr);

typedef struct _arena arena_t;
struct _arena {
    arena_t *prev;
    arena_t *next;
    char    *buffer;
    size_t   len;
    size_t   capacity;
};

typedef struct _slab_allocator {
    ALLOCATOR_VTABLE;
    size_t   arena_capacity;
    arena_t  head;
    arena_t *tail;
    struct {
        arena_t *arena;
        size_t   pos;
    } mark;
} slab_allocator_t;

#ifndef SLAB_CAPACITY
#define SLAB_CAPACITY 8 * 1024 * 1024
#endif /* SLAB_CAPACITY */

slab_allocator_t *slab_allocator_init(size_t arena_capacity);
void              slab_allocator_checkpoint(slab_allocator_t *alloc);
void              slab_allocator_rewind(slab_allocator_t *alloc);
void             *slab_allocator_alloc(allocator_t *a, size_t size);
void              slab_allocator_destroy(allocator_t *a);

#define OPT(T) opt_##T
#define OPTDEF(T)                  \
    typedef struct _##T##_option { \
        bool ok;                   \
        T    value;                \
    } OPT(T)

#define OPTVAL(T, V) ((OPT(T)) { .ok = true, .value = (V) })
#define OPTNULL(T) ((OPT(T)) { 0 })

#define UNWRAP_T(T, Expr)                            \
    (                                                \
        {                                            \
            T __value = (Expr);                      \
            if (!__value.ok) {                       \
                fprintf(stderr, #Expr " is null\n"); \
                abort();                             \
            }                                        \
            (__value.value);                         \
        })

#define UNWRAP(T, Expr) UNWRAP_T(opt_##T, (Expr))

#define ORELSE(T, V, E)                                      \
    (                                                        \
        {                                                    \
            opt_##T __val = (V);                             \
            (T)((__val.ok) ? ((T) __val.value) : ((T) (E))); \
        })

#define TRYOPT_T(T, V)            \
    (                             \
        {                         \
            T __val = (V);        \
            if (!__val.ok) {      \
                return (T) { 0 }; \
            }                     \
            (__val.value);        \
        })

#define TRYOPT(T, V) TRYOPT_T(opt_##T, (V))

#define TRYOPT_ADAPT_T(T, V, Ret) \
    (                             \
        {                         \
            T __val = (V);        \
            if (!__val.ok) {      \
                return (Ret);     \
            }                     \
            (__val.value);        \
        })

#define TRYOPT_ADAPT(T, V, Ret) \
    TRYOPT_ADAPT_T(opt_##T, (V), (Ret))

#define MUSTOPT(T, Expr)                  \
    (                                     \
        {                                 \
            opt_##T __res = (Expr);       \
            if (!__res.ok) {              \
                fatal(#Expr " failed\n"); \
            }                             \
            (__res.value);                \
        })

#define RES(T, E)      \
    struct {           \
        bool ok;       \
        union {        \
            T success; \
            E error;   \
        };             \
    }

#define RESVAL(T, V) \
    (T) { .ok = true, .success = (V) }
#define RESERR(T, E) \
    (T) { .ok = false, .error = (E) }

#define TRY(T, Expr)          \
    (                         \
        {                     \
            T __res = (Expr); \
            if (!__res.ok) {  \
                return __res; \
            }                 \
            (__res.success);  \
        })

#define TRY_TO(T, U, Expr)                     \
    (                                          \
        {                                      \
            T __res = (Expr);                  \
            if (!__res.ok) {                   \
                return RESERR(U, __res.error); \
            }                                  \
            (__res.success);                   \
        })

#define MUST(T, Expr)                               \
    (                                               \
        {                                           \
            T __res = (Expr);                       \
            if (!__res.ok) {                        \
                fprintf(stderr, #Expr " failed\n"); \
                abort();                            \
            }                                       \
            (__res.success);                        \
        })

OPTDEF(int);
OPTDEF(uint32_t);
OPTDEF(size_t);
OPTDEF(long);
typedef unsigned long ulong;
OPTDEF(ulong);
OPTDEF(float);
OPTDEF(double);

typedef opt_size_t nodeptr;
#ifndef __cplusplus
extern nodeptr nullptr;
#else
#warning "Can't compile elrond with C++ yet"
#endif
#define nodeptr_ptr(v) ((nodeptr) { .ok = true, .value = (v) })
#define nodeptr_offset(p, offset) ((p.ok) ? (nodeptr) { .ok = true, .value = (p.value + (offset)) } : (nodeptr) { 0 })

typedef struct slice {
    char  *items;
    size_t len;
} slice_t;

typedef struct _slice_pair {
    slice_t key;
    slice_t value;
} slice_pair_t;

OPTDEF(slice_t);

typedef struct array {
    void  *items;
    size_t size;
} array_t;

#define slice_make(s, l) ((slice_t) { .items = (s), .len = (l) })
#define slice_from_cstr(s) ((slice_t) { .items = ((char *) s), .len = strlen((s)) })
#define C(s) slice_from_cstr(s)
#define slice_is_cstr(s) ((s).items[(s).len] == '\0')
#define SL "%.*s"
#define SLARG(s) (int) (s).len, (s).items
#define slice_foreach(T, it, slice) for (T *it = (slice)->items; it < (slice)->items + (slice)->len; ++it)
#define slice_reverse(T, it, slice) for (T *it = (slice)->items + ((slice)->len - 1); it >= (slice)->items; --it)

#define slice_search(T, haystack, needle, eq_fnc) \
    (                                             \
        {                                         \
            T  __needle = (needle);               \
            *T __found = NULL;                    \
            slice_foreach(T, __it, (slice))       \
            {                                     \
                if ((eq_fnc) (*__it, needle)) {   \
                    __found = __it;               \
                    break;                        \
                }                                 \
            }                                     \
            (__found);                            \
        })

#define slice_fwrite(s, f)                    \
    do {                                      \
        slice_t __s = (s);                    \
        if (__s.len > 0) {                    \
            fwrite(__s.items, 1, __s.len, f); \
            fputc('\n', f);                   \
        }                                     \
    } while (0)

intptr_t   align_at(intptr_t alignment, intptr_t value);
intptr_t   words_needed(intptr_t word_size, intptr_t bytes);
slice_t    slice_head(slice_t slice, size_t from_back);
slice_t    slice_first(slice_t slice, size_t num);
slice_t    slice_tail(slice_t slice, size_t from_start);
slice_t    slice_last(slice_t slice, size_t num);
slice_t    slice_sub(slice_t slice, size_t start, size_t end);
slice_t    slice_sub_by_length(slice_t slice, size_t start, size_t num);
bool       slice_startswith(slice_t slice, slice_t head);
bool       slice_endswith(slice_t slice, slice_t tail);
bool       slice_contains(slice_t haystack, slice_t needle);
opt_size_t slice_find_sub(slice_t haystack, slice_t needle);
opt_size_t slice_rfind(slice_t haystack, slice_t needle);
opt_size_t slice_indexof(slice_t haystack, char needle);
opt_size_t slice_last_indexof(slice_t haystack, char needle);
opt_size_t slice_first_of(slice_t haystack, slice_t needles);
int        slice_cmp(slice_t s1, slice_t s2);
bool       slice_eq(slice_t s1, slice_t s2);
slice_t    slice_trim(slice_t s);
slice_t    slice_rtrim(slice_t s);
slice_t    slice_ltrim(slice_t s);
slice_t    slice_token(slice_t *s, char separator);
slice_t    slice_csv_token(slice_t *s);
opt_ulong  slice_to_ulong(slice_t s, unsigned int base);
opt_long   slice_to_long(slice_t s, unsigned int base);
opt_double slice_to_double(slice_t s);

#ifndef TEMP_CAPACITY
#define TEMP_CAPACITY SLAB_CAPACITY
#endif /* TEMP_CAPACITY */
void        temp_reset(void);
size_t      temp_save(void);
void        temp_rewind(size_t checkpoint);
char       *temp_strdup(char const *cstr);
void       *temp_alloc(size_t size);
char       *temp_sprintf(char const *format, ...) __attribute__((__format__(printf, 1, 2)));
char const *temp_slice_to_cstr(slice_t slice);

#endif /* __SLICE_H__ */

#ifdef SLICE_IMPLEMENTATION
#undef SLICE_IMPLEMENTATION
#ifndef SLICE_IMPLEMENTED
#define SLICE_IMPLEMENTED

bool                        do_trace = false;
_Thread_local static size_t temp_size = 0;
_Thread_local static char   temp_buffer[TEMP_CAPACITY] = { 0 };
nodeptr nullptr = { 0 };

intptr_t align_at(intptr_t alignment, intptr_t bytes)
{
    assert(alignment > 0 && (alignment & (alignment - 1)) == 0); // Align must be power of 2
    return (bytes + (alignment - 1)) & ~(alignment - 1);
}

intptr_t words_needed(intptr_t word_size, intptr_t bytes)
{
    size_t ret = bytes / word_size;
    return (bytes % word_size != 0) ? ret + 1 : ret;
}

void *stdc_alloc(allocator_t *alloc, size_t size)
{
    (void) alloc;
    void *ret = calloc(size, 1);
    return ret;
}

void *stdc_realloc(allocator_t *alloc, void *ptr, size_t old_size, size_t new_size)
{
    (void) alloc;
    void *ret = realloc(ptr, new_size);
    memset(ret + old_size, 0, new_size - old_size);
    return ret;
}

void stdc_free(allocator_t *alloc, void *ptr)
{
    (void) alloc;
    free(ptr);
}

void *temp_allocator_alloc(allocator_t *alloc, size_t size)
{
    (void) alloc;
    (void) temp_allocator;
    void *ret = temp_alloc(size);
    return ret;
}

void allocator_push(allocator_t *alloc)
{
    alloc->prev_allocator = current_allocator;
    current_allocator = alloc;
}

void allocator_pop()
{
    assert(current_allocator->prev_allocator != NULL);
    allocator_t *alloc = current_allocator;
    current_allocator = alloc->prev_allocator;
    alloc->prev_allocator = NULL;
    if (alloc->destroy) {
        alloc->destroy(alloc);
    }
}

char *allocator_alloc(size_t size)
{
    assert(current_allocator != NULL);
    char *ret = (char *) current_allocator->alloc(current_allocator, size);
    if (ret == NULL) {
        fatal("Allocator exhausted");
    }
    return ret;
}

char *allocator_realloc(char *ptr, size_t old_size, size_t new_size)
{
    assert(current_allocator != NULL);
    assert((ptr == NULL && old_size == 0) || (ptr != NULL && old_size != 0));
    if (new_size <= old_size) {
        return ptr;
    }
    char *ret = NULL;
    if (current_allocator->realloc != NULL) {
        ret = (char *) current_allocator->realloc(current_allocator, ptr, old_size, new_size);
    } else {
        ret = (char *) current_allocator->alloc(current_allocator, new_size);
        if (ret != NULL && old_size > 0) {
            memcpy(ret, ptr, old_size);
        }
        allocator_free(ptr);
    }
    if (ret == NULL) {
        fatal("Allocator exhausted");
    }
    return ret;
}

void allocator_free(char *ptr)
{
    assert(current_allocator != NULL);
    if (current_allocator->free != NULL) {
        current_allocator->free(current_allocator, ptr);
    }
}

slab_allocator_t *slab_allocator_init(size_t arena_capacity)
{
    if (arena_capacity == 0) {
        arena_capacity = 8 * 1024;
    }
    assert(arena_capacity > (size_t) align_at(8, sizeof(slab_allocator_t)));
    char *buffer = (char *) calloc(arena_capacity, 1);
    if (buffer == NULL) {
        fatal("Allocator exhausted");
    }
    slab_allocator_t *alloc = (slab_allocator_t *) buffer;
    alloc->alloc = slab_allocator_alloc;
    alloc->destroy = slab_allocator_destroy;
    alloc->prev_allocator = current_allocator;
    alloc->head.capacity = arena_capacity;
    alloc->head.buffer = buffer;
    alloc->head.len = align_at(8, sizeof(slab_allocator_t));
    alloc->tail = &alloc->head;
    current_allocator = (allocator_t *) alloc;
    return alloc;
}

void slab_allocator_checkpoint(slab_allocator_t *alloc)
{
    alloc->mark.arena = alloc->tail;
    alloc->mark.pos = alloc->tail->len;
}

void slab_allocator_rewind(slab_allocator_t *alloc)
{
    assert(alloc->mark.arena != NULL);
    alloc->mark.arena->len = alloc->mark.pos;
    for (arena_t *arena = alloc->mark.arena->next; arena != NULL; arena = arena->next) {
        alloc->mark.arena->len = align_at(8, sizeof(arena_t));
    }
    alloc->mark.arena = NULL;
    alloc->mark.pos = 0;
}

void slab_allocator_destroy(allocator_t *a)
{
    slab_allocator_t *alloc = (slab_allocator_t *) a;
    arena_t          *n = NULL;
    for (arena_t *arena = &alloc->head; arena != NULL; arena = n) {
        n = arena->next;
        free(arena->buffer);
    }
}

void *slab_allocator_alloc(allocator_t *a, size_t size)
{
    slab_allocator_t *alloc = (slab_allocator_t *) a;
    for (arena_t *arena = &alloc->head; arena != NULL; arena = arena->next) {
        if (arena->len + size <= arena->capacity) {
            void *ret = arena->buffer + arena->len;
            arena->len += align_at(8, size);
            return ret;
        }
    }
    size_t cap = MAX(alloc->arena_capacity, size + align_at(8, sizeof(arena_t)));
    char  *buffer = (char *) calloc(cap, 1);
    if (buffer == NULL) {
        return NULL;
    }
    arena_t *arena = (arena_t *) buffer;
    arena->buffer = buffer;
    arena->capacity = cap;
    arena->len = align_at(8, sizeof(arena_t));
    arena->prev = alloc->tail;
    alloc->tail->next = arena;
    alloc->tail = arena;
    void *ret = arena->buffer + arena->len;
    arena->len += align_at(8, size);
    return ret;
}

slice_t slice_head(slice_t slice, size_t from_back)
{
    assert(from_back <= slice.len);
    return slice_make(slice.items, slice.len - from_back);
}

slice_t slice_first(slice_t slice, size_t num)
{
    assert(num <= slice.len);
    return slice_make(slice.items, num);
}

slice_t slice_tail(slice_t slice, size_t from_start)
{
    if (from_start > slice.len) {
        return C("");
    }
    return slice_make(slice.items + from_start, slice.len - from_start);
}

slice_t slice_last(slice_t slice, size_t num)
{
    assert(num <= slice.len);
    return slice_make(slice.items + (slice.len - num), num);
}

slice_t slice_sub(slice_t slice, size_t start, size_t end)
{
    assert(start <= slice.len && end <= slice.len && start <= end);
    return slice_make(slice.items + start, end - start);
}

slice_t slice_sub_by_length(slice_t slice, size_t start, size_t num)
{
    assert(start <= slice.len && start + num <= slice.len);
    return slice_make(slice.items + start, num);
}

bool slice_startswith(slice_t slice, slice_t head)
{
    if (slice.len < head.len) {
        return false;
    }
    return strncmp(slice.items, head.items, head.len) == 0;
}

bool slice_endswith(slice_t slice, slice_t tail)
{
    if (slice.len < tail.len) {
        return false;
    }
    return strncmp(slice.items + (slice.len - tail.len), tail.items, tail.len) == 0;
}

bool slice_contains(slice_t haystack, slice_t needle)
{
    if (haystack.len < needle.len) {
        return false;
    }
    for (size_t i = 0; i < haystack.len - needle.len; ++i) {
        if (strncmp(haystack.items + i, needle.items, needle.len) == 0) {
            return true;
        }
    }
    return false;
}

opt_size_t slice_find_sub(slice_t haystack, slice_t needle)
{
    if (haystack.len < needle.len) {
        return OPTNULL(size_t);
    }
    for (size_t i = 0; i <= haystack.len - needle.len; ++i) {
        if (strncmp(haystack.items + i, needle.items, needle.len) == 0) {
            return OPTVAL(size_t, i);
        }
    }
    return OPTNULL(size_t);
}

opt_size_t slice_rfind(slice_t haystack, slice_t needle)
{
    if (haystack.len < needle.len) {
        return OPTNULL(size_t);
    }
    size_t i = haystack.len - needle.len;
    do {
        if (strncmp(haystack.items + i, needle.items, needle.len) == 0) {
            return OPTVAL(size_t, i);
        }
    } while (i-- != 0);
    return OPTNULL(size_t);
}

opt_size_t slice_indexof(slice_t haystack, char needle)
{
    for (size_t i = 0; i < haystack.len; ++i) {
        if (haystack.items[i] == needle) {
            return OPTVAL(size_t, i);
        }
    }
    return OPTNULL(size_t);
}

opt_size_t slice_last_indexof(slice_t haystack, char needle)
{
    if (haystack.len > 0) {
        size_t i = haystack.len - 1;
        do {
            if (haystack.items[i] == needle) {
                return OPTVAL(size_t, i);
            }
        } while (i-- != 0);
    }
    return OPTNULL(size_t);
}

opt_size_t slice_first_of(slice_t haystack, slice_t needles)
{
    for (size_t i = 0; i < haystack.len; ++i) {
        if (slice_indexof(needles, haystack.items[i]).ok) {
            return OPTVAL(size_t, i);
        }
    }
    return OPTNULL(size_t);
}

slice_t slice_token(slice_t *s, char separator)
{
    opt_size_t ix = slice_indexof(*s, separator);
    if (!ix.ok) {
        slice_t ret = slice_trim(*s);
        *s = C("");
        return ret;
    }
    slice_t ret = slice_trim(slice_first(*s, ix.value));
    *s = slice_tail(*s, ix.value + 1);
    return ret;
}

slice_t slice_csv_token(slice_t *s)
{
    enum {
        CSVTokenState_None,
        CSVTokenState_Unquoted,
        CSVTokenState_Quoted,
    } state
        = CSVTokenState_None;
    size_t start = 0;
    bool   esc = false;
    int    quote = 0;
    for (size_t ix = 0; ix < s->len; ++ix) {
        int ch = s->items[ix];
        int space = isspace(s->items[ix]);
        int is_quote = s->items[ix] == quote;
        int is_comma = s->items[ix] == ',';
        int is_backslash = s->items[ix] == '\\';
        switch (state) {
        case CSVTokenState_None: {
            if (space) {
                continue;
            }
            switch (ch) {
            case '"':
            case '\'':
                state = CSVTokenState_Quoted;
                quote = ch;
                start = ix + 1;
                break;
            case ',':
                *s = slice_tail(*s, ix + 1);
                return C("");
            default:
                state = CSVTokenState_Unquoted;
                start = ix;
                break;
            }
        } break;
        case CSVTokenState_Unquoted:
            if (is_comma) {
                slice_t ret = slice_rtrim(slice_sub(*s, start, ix));
                *s = slice_tail(*s, ix + 1);
                return ret;
            }
            break;
        case CSVTokenState_Quoted:
            if (esc) {
                esc = false;
                continue;
            }
            if (is_backslash) {
                esc = true;
                continue;
            }
            if (is_quote) {
                slice_t ret = slice_sub(*s, start, ix);
                while (ix < s->len && s->items[ix] != ',') {
                    ++ix;
                }
                *s = slice_tail(*s, ix + 1);
                return ret;
            }
            break;
        }
    }
    if (CSVTokenState_Unquoted == state) {
        slice_t ret = slice_tail(*s, start);
        *s = C("");
        return ret;
    }
    printf(" *** " SL " ***\n", SLARG(*s));
    UNREACHABLE();
}

int slice_cmp(slice_t s1, slice_t s2)
{
    if (s1.len != s2.len) {
        return s1.len - s2.len;
    }
    return memcmp(s1.items, s2.items, s1.len);
}

bool slice_eq(slice_t s1, slice_t s2)
{
    return slice_cmp(s1, s2) == 0;
}

slice_t slice_trim(slice_t s)
{
    return slice_rtrim(slice_ltrim(s));
}

slice_t slice_ltrim(slice_t s)
{
    char *ptr = s.items;
    while (isspace(*ptr) && ptr < s.items + s.len) {
        ++ptr;
    }
    if (ptr == s.items + s.len) {
        return (slice_t) { 0 };
    }
    return slice_make(ptr, s.len - (ptr - s.items));
}

slice_t slice_rtrim(slice_t s)
{
    size_t l = s.len;
    while (l > 0 && isspace(s.items[l - 1])) {
        --l;
    }
    if (l == 0) {
        return (slice_t) { 0 };
    }
    return slice_make(s.items, l);
}

opt_int digit_for_base(unsigned int digit, unsigned int base)
{
    if (digit >= '0' && digit < '0' + base) {
        return OPTVAL(int, digit - '0');
    } else if (digit >= 'A' && digit < 'A' + (base - 10)) {
        return OPTVAL(int, 10 + digit - 'A');
    } else if (digit >= 'a' && digit < 'a' + (base - 10)) {
        return OPTVAL(int, 10 + digit - 'a');
    }
    return (opt_int) { 0 };
}

opt_ulong slice_to_ulong(slice_t s, unsigned int base)
{
    if (s.len == 0) {
        return (opt_ulong) { 0 };
    }

    size_t ix = 0;
    while (ix < s.len && isspace(s.items[ix])) {
        ++ix;
    }
    if (ix == s.len) {
        return (opt_ulong) { 0 };
    }

    if (s.len > ix + 2 && s.items[ix] == '0') {
        if (s.items[ix + 1] == 'x' || s.items[ix + 1] == 'X') {
            if (base != 0 && base != 16) {
                return (opt_ulong) { 0 };
            }
            base = 16;
            ix = 2;
        }
        if (s.items[ix + 1] == 'b' || s.items[ix + 1] == 'B') {
            if (base != 0 && base != 2) {
                return (opt_ulong) { 0 };
            }
            base = 2;
            ix = 2;
        }
    }
    if (base == 0) {
        base = 10;
    }
    if (base > 36) {
        return (opt_ulong) { 0 };
    }

    size_t first = ix;
    ulong  val = 0;
    while (ix < s.len) {
        opt_int d = digit_for_base(s.items[ix], base);
        if (!d.ok) {
            break;
        }
        val = (val * base) + d.value;
        ++ix;
    }
    if (ix == first) {
        return (opt_ulong) { 0 };
    }
    while (ix < s.len && isspace(s.items[ix])) {
        ++ix;
    }
    if (ix < s.len) {
        return (opt_ulong) { 0 };
    }
    return OPTVAL(ulong, val);
}

opt_long slice_to_long(slice_t s, unsigned int base)
{
    if (s.len == 0) {
        return (opt_long) { 0 };
    }

    size_t ix = 0;
    while (ix < s.len && isspace(s.items[ix])) {
        ++ix;
    }
    if (ix == s.len) {
        return (opt_long) { 0 };
    }
    long    sign = 1;
    slice_t tail = slice_tail(s, ix);
    if (s.len > ix + 1 && (s.items[ix] == '-' || s.items[ix] == '+')) {
        sign = s.items[ix] == '-' ? -1 : 1;
        tail = slice_tail(s, ix + 1);
    }
    opt_ulong val = slice_to_ulong(tail, base);
    if (!val.ok) {
        return (opt_long) { 0 };
    }
    if (sign == 1) {
        if (val.value > (ulong) LONG_MAX) {
            return (opt_long) { 0 };
        }
        return OPTVAL(long, ((long) val.value));
    }
    if (val.value > ((ulong) (-(LONG_MIN + 1))) + 1) {
        return (opt_long) { 0 };
    }
    return OPTVAL(long, ((long) (~val.value + 1)));
}

opt_double slice_to_double(slice_t s)
{
    size_t      cp = temp_save();
    char const *cstr = temp_slice_to_cstr(s);
    char       *endptr;
    double      d = strtod(cstr, &endptr);
    if (endptr == cstr) {
        return OPTNULL(double);
    }
    temp_rewind(cp);
    return OPTVAL(double, d);
}

char *temp_strdup(char const *cstr)
{
    size_t n = strlen(cstr);
    char  *result = (char *) temp_alloc(n + 1);
    assert(result != NULL && "Increase TEMP_CAPACITY");
    memcpy(result, cstr, n);
    result[n] = '\0';
    return result;
}

#define WORD_SIZE sizeof(uintptr_t)

void *temp_alloc(size_t requested_size)
{
    size_t size = align_at(WORD_SIZE, requested_size);
    if (temp_size + size > TEMP_CAPACITY) {
        return NULL;
    }
    void *result = &temp_buffer[temp_size];
    temp_size += size;
    return result;
}

char *temp_sprintf(char const *format, ...)
{
    va_list args;
    va_start(args, format);
    int n = vsnprintf(NULL, 0, format, args);
    va_end(args);

    assert(n >= 0);
    char *result = (char *) temp_alloc(n + 1);
    assert(result != NULL && "Extend the size of the temporary allocator");
    // TODO: use proper arenas for the temporary allocator;
    va_start(args, format);
    vsnprintf(result, n + 1, format, args);
    va_end(args);

    return result;
}

void temp_reset(void)
{
    temp_size = 0;
}

size_t temp_save(void)
{
    return temp_size;
}

void temp_rewind(size_t checkpoint)
{
    temp_size = checkpoint;
}

char const *temp_slice_to_cstr(slice_t slice)
{
    char *result = (char *) temp_alloc(slice.len + 1);
    assert(result != NULL && "Extend the size of the temporary allocator");
    memcpy(result, slice.items, slice.len);
    result[slice.len] = '\0';
    return result;
}

#endif /* SLICE_IMPLEMENTED */
#endif /* SLICE_IMPLEMENTATION */

#ifdef SLICE_TEST

slice_t X = C("X");

int main()
{
    assert(X.len == 1);
    slice_t s = C("Hello");
    assert(s.len == 5);
    assert(memcmp(s.items, "Hello", 5) == 0);
    assert(slice_startswith(s, C("He")));
    assert(slice_endswith(s, C("lo")));
    assert(!slice_startswith(s, C("he")));
    assert(!slice_endswith(s, C("la")));
    slice_t spaces = C("   Hello   ");
    assert(slice_eq(slice_ltrim(spaces), C("Hello   ")));
    assert(slice_eq(slice_rtrim(spaces), C("   Hello")));
    assert(slice_eq(slice_trim(spaces), s));
    slice_t tabs = C(" \t Hello \t ");
    assert(slice_eq(slice_ltrim(tabs), C("Hello \t ")));
    assert(slice_eq(slice_rtrim(tabs), C(" \t Hello")));
    assert(slice_eq(slice_trim(tabs), s));
    assert(slice_find_sub(s, C("lo")).ok);

    slice_t csv = C("foo,  bar   ,baz");
    assert(slice_eq(slice_token(&csv, ','), C("foo")));
    assert(slice_eq(slice_token(&csv, ','), C("bar")));
    assert(slice_eq(slice_token(&csv, ','), C("baz")));
    assert(csv.len == 0);

    csv = C("foo,  bar   ,  \"baz\",\"quux\",\"1,2,3\"");
    assert(slice_eq(slice_csv_token(&csv), C("foo")));
    assert(slice_eq(slice_csv_token(&csv), C("bar")));
    assert(slice_eq(slice_csv_token(&csv), C("baz")));
    assert(slice_eq(slice_csv_token(&csv), C("quux")));
    assert(slice_eq(slice_csv_token(&csv), C("1,2,3")));
    assert(csv.len == 0);
}

#endif /* SLICE_TEST */
