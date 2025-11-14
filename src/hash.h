#include <stdint.h>
#include <stdlib.h>

#ifdef HASH_TEST
#define HASH_IMPLEMENTATION
#endif

#ifndef __HASH_H__
#define __HASH_H__

#include "slice.h"

unsigned int hash(void const *, size_t);
unsigned int hashptr(void const *);
unsigned int hashlong(long);
unsigned int hashdouble(double);
unsigned int hashblend(unsigned int, unsigned int);
unsigned int strhash(char const *);
unsigned int hashslice(slice_t);

#endif /* __HASH_H__ */

#ifdef HASH_IMPLEMENTATION
#ifndef HASH_IMPLEMENTED

#undef get16bits
#if (defined(__GNUC__) && defined(__i386__)) || defined(__WATCOMC__) \
    || defined(_MSC_VER) || defined(__BORLANDC__) || defined(__TURBOC__)
#define get16bits(d) (*((const uint16_t *) (d)))
#endif

#if !defined(get16bits)
#define get16bits(d) ((((uint32_t) (((const uint8_t *) (d))[1])) << 8) \
    + (uint32_t) (((const uint8_t *) (d))[0]))
#endif

unsigned int hash(const void *voiddata, size_t len)
{
    char    *data = (char *) voiddata;
    uint32_t hash = len, tmp;
    int      rem;

    if (len <= 0 || !data) {
        return 0;
    }

    rem = len & 3;
    len >>= 2;

    /* Main loop */
    for (; len > 0; len--) {
        hash += get16bits(data);
        tmp = (get16bits(data + 2) << 11) ^ hash;
        hash = (hash << 16) ^ tmp;
        data += 2 * sizeof(uint16_t);
        hash += hash >> 11;
    }

    /* Handle end cases */
    switch (rem) {
    case 3:
        hash += get16bits(data);
        hash ^= hash << 16;
        hash ^= ((signed char) data[sizeof(uint16_t)]) << 18;
        hash += hash >> 11;
        break;
    case 2:
        hash += get16bits(data);
        hash ^= hash << 11;
        hash += hash >> 17;
        break;
    case 1:
        hash += (signed char) *data;
        hash ^= hash << 10;
        hash += hash >> 1;
        break;
    }

    /* Force "avalanching" of final 127 bits */
    hash ^= hash << 3;
    hash += hash >> 5;
    hash ^= hash << 4;
    hash += hash >> 17;
    hash ^= hash << 25;
    hash += hash >> 6;

    return (unsigned int) hash;
}

unsigned int hashptr(void const *ptr)
{
    return hash(&ptr, sizeof(void *));
}

unsigned int hashlong(long val)
{
    // return hash(&val, sizeof(long));
    return (unsigned int) val;
}

unsigned int hashfloat(float val)
{
    return hash(&val, sizeof(float));
}

unsigned int hashdouble(double val)
{
    return hash(&val, sizeof(double));
}

unsigned int hashblend(unsigned int h1, unsigned int h2)
{
    return (h1 << 1) + h1 + h2;
}

unsigned int strhash(char const *s)
{
    return hash(s, strlen(s));
}

unsigned int hashslice(slice_t s)
{
    return hash(s.items, s.len);
}

#define HASH_IMPLEMENTED
#endif /* HASH_IMPLEMENTED */
#endif /* HASH_IMPLEMENTATION */

#ifdef HASH_TEST

int main()
{
    return 0;
}

#endif /* HASH_TEST */
