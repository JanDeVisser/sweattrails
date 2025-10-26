/*
 * Copyright (c) 2023, 2025 Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __IO_H__
#define __IO_H__

// #define IO_TEST
#ifdef IO_TEST
#define IO_IMPLEMENTATION
#define SLICE_IMPLEMENTATION
#define DA_IMPLEMENTATION
#endif

#include "da.h"
#include <stddef.h>

extern opt_sb_t slurp_file(slice_t path);
extern bool     write_file(slice_t path, slice_t data);

#endif /* __IO_H__ */

#ifdef IO_IMPLEMENTATION
#ifndef IO_IMPLEMENTED

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

opt_sb_t slurp_file(slice_t path)
{
    opt_sb_t    ret = { 0 };
    size_t      cp = temp_save();
    char const *p = temp_slice_to_cstr(path);

    int fh = open(p, O_RDONLY);
    if (fh < 0) {
        goto done;
    }
    ssize_t sz = (ssize_t) lseek(fh, 0, SEEK_END);
    if (sz < 0) {
        goto done_close;
    }
    if (lseek(fh, 0, SEEK_SET) < 0) {
        goto done_close;
    }

    sb_t contents = { 0 };
    dynarr_ensure(&contents, sz + 1);
    contents.items[sz] = 0;
    ssize_t num_read = read(fh, contents.items, sz);
    if (num_read < 0) {
        fatal("slurp_file(" SL "): %s", SLARG(path), strerror(errno));
    } else if (num_read < sz) {
        fatal("write_file(" SL "): short read: %zu < %zu", SLARG(path), num_read, sz);
    }
    contents.len = sz;
    ret = OPTVAL(sb_t, contents);

done_close:
    close(fh);

done:
    temp_rewind(cp);
    return ret;
}

bool write_file(slice_t path, slice_t data)
{
    bool        ret = false;
    size_t      cp = temp_save();
    char const *p = temp_slice_to_cstr(path);

    int fh = open(p, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fh < 0) {
        goto done;
    }
    ssize_t written = write(fh, data.items, data.len);
    if (written < 0) {
        fatal("write_file(" SL "): %s", SLARG(path), strerror(errno));
    } else if ((size_t) written < data.len) {
        fatal("write_file(" SL "): short write: %zu < %zu", SLARG(path), written, data.len);
    }
    close(fh);
    ret = true;

done:
    temp_rewind(cp);
    return ret;
}

#endif /* IO_IMPLEMENTED */
#endif /* IO_IMPLEMENTATION */

#ifdef IO_TEST

#define TMP_COPY "/tmp/fs.h.test"

int main()
{
    opt_sb_t contents = slurp_file(C("src/io.h"));
    assert(contents.ok);

    assert(write_file(C(TMP_COPY), sb_as_slice(contents.value)));
    opt_sb_t contents_copy = slurp_file(C(TMP_COPY));
    assert(contents_copy.ok);
    assert(slice_eq(sb_as_slice(contents.value), sb_as_slice(contents_copy.value)));
    unlink(TMP_COPY);
    return 0;
}

#endif /* IO_TEST */
