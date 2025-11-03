/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __FS_H__
#define __FS_H__

#ifdef FS_TEST
#define SLICE_IMPLEMENTATION
#define DA_IMPLEMENTATION
#define FS_IMPLEMENTATION
#endif /* FS_TEST */

#include "da.h"
#include "slice.h"

typedef enum _path_kind {
    PATH_UNDEFINED = 0,
    PATH_RELATIVE,
    PATH_ABSOLUTE,
} path_kind_t;

typedef struct _path {
    path_kind_t kind;
    sb_t        path;
    slices_t    components;
} path_t;

typedef DA(path_t) paths_t;

path_t  path_initialize(path_kind_t kind, ...);
path_t  path_parse(slice_t p);
path_t  path_append(path_t p, path_t sub);
path_t  path_extend(path_t p, slice_t sub);
slice_t path_extension(path_t *path);
slice_t path_basename(path_t *path);
path_t *path_replace_extension(path_t *path, slice_t ext);
path_t *path_strip_extension(path_t *path);
void    path_mkdirs(path_t path);
bool    path_exists(path_t path);
bool    path_is_dir(path_t path);
bool    path_is_file(path_t path);
bool    path_is_readable(path_t path);
paths_t path_file_listing(path_t path);
int     path_del(path_t path);
int     path_rmdir(path_t path);
int     path_deltree(path_t path);
int     path_rename(path_t old, path_t new);
void    path_free(path_t *path);
void    paths_free(paths_t *paths);

#define path_copy(p) (path_parse(sb_as_slice((p).path)))
#define path_make_relative(...) path_initialize(PATH_RELATIVE, ##__VA_ARGS__, NULL);

#endif /* __FS_H__ */

// #define FS_IMPLEMENTATION
#if defined(FS_IMPLEMENTATION) || defined(JDV_IMPLEMENTATION)
#ifndef FS_IMPLEMENTED

#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

static void path_reparse(path_t *path)
{
    dynarr_clear(&path->components);
    path->kind = PATH_UNDEFINED;
    if (path->path.len == 0) {
        return;
    }
    slice_t p = sb_as_slice(path->path);
    path->kind = PATH_RELATIVE;
    if (p.items[0] == '/') {
        path->kind = PATH_ABSOLUTE;
        p = slice_tail(p, 1);
        if (p.len == 0) {
            return;
        }
    }
    opt_size_t slash = slice_indexof(p, '/');
    while (p.len > 0 && slash.ok) {
        slice_t c = slice_first(p, slash.value);
        dynarr_append(&path->components, c);
        p = slice_tail(p, slash.value + 1);
        slash = slice_indexof(p, '/');
    }
    if (p.len > 0) {
        dynarr_append(&path->components, p);
    }
}

path_t path_initialize(path_kind_t kind, ...)
{
    path_t ret = { 0 };
    ret.kind = (kind != PATH_ABSOLUTE) ? PATH_RELATIVE : PATH_ABSOLUTE;
    if (ret.kind == PATH_ABSOLUTE) {
        sb_append_char(&ret.path, '/');
    }
    va_list args;
    va_start(args, kind);
    for (char *c = va_arg(args, char *); c != NULL; c = va_arg(args, char *)) {
        size_t len = strlen(c);
        if (len == 0) {
            continue;
        }
        if (ret.path.len > 1 || (ret.path.len == 1 && ret.path.items[0] != '/')) {
            sb_append_char(&ret.path, '/');
        }
        sb_append_cstr(&ret.path, c);
    }
    va_end(args);
    path_reparse(&ret);
    return ret;
}

path_t path_parse(slice_t p)
{
    path_t ret = { 0 };
    sb_append(&ret.path, p);
    path_reparse(&ret);
    return ret;
}

path_t path_append(path_t p, path_t sub)
{
    assert(sub.kind == PATH_RELATIVE);
    return path_extend(p, sb_as_slice(sub.path));
}

path_t path_extend(path_t p, slice_t sub)
{
    path_t ret = { 0 };
    ret.kind = p.kind;
    sb_append_sb(&ret.path, p.path);
    if (ret.path.len > 1 || (ret.path.len == 1 && ret.path.items[0] != '/')) {
        sb_append_char(&ret.path, '/');
    }
    sb_append(&ret.path, sub);
    path_reparse(&ret);
    return ret;
}

void path_free(path_t *p)
{
    sb_free(&p->path);
    *p = (path_t) { 0 };
}

slice_t path_extension(path_t *path)
{
    if (path->components.len == 0) {
        return (slice_t) { 0 };
    }
    slice_t last = *dynarr_back(&path->components);
    assert(last.len > 0);
    opt_size_t dot = slice_last_indexof(last, '.');
    if (dot.ok) {
        return slice_tail(last, dot.value);
    }
    return (slice_t) { 0 };
}

slice_t path_basename(path_t *path)
{
    if (path->components.len == 0) {
        return (slice_t) { 0 };
    }
    slice_t last = *dynarr_back(&path->components);
    assert(last.len > 0);
    opt_size_t dot = slice_last_indexof(last, '.');
    if (dot.ok) {
        return slice_first(last, dot.value);
    }
    return last;
}

path_t *path_replace_extension(path_t *path, slice_t ext)
{
    if (path->components.len == 0 || ext.len == 0) {
        return path;
    }
    slice_t last = *dynarr_back(&path->components);
    assert(last.len > 0);
    opt_size_t dot = slice_last_indexof(last, '.');
    if (dot.ok) {
        path->path.len = (last.items - path->path.items) + dot.value;
    }
    if (ext.items[0] != '.') {
        sb_append_char(&path->path, '.');
    }
    sb_append(&path->path, ext);
    path_reparse(path);
    return path;
}

path_t *path_strip_extension(path_t *path)
{
    if (path->components.len == 0) {
        return path;
    }
    slice_t last = *dynarr_back(&path->components);
    assert(last.len > 0);
    opt_size_t dot = slice_last_indexof(last, '.');
    if (dot.ok) {
        path->path.len = (last.items - path->path.items) + dot.value;
    }
    path_reparse(path);
    return path;
}

void path_mkdirs(path_t path)
{
    if (path.components.len == 0) {
        return;
    }
    size_t cp = temp_save();
    char  *p = temp_alloc(path.path.len + 5);
    memset(p, 0, path.path.len + 5);
    if (path.kind == PATH_ABSOLUTE) {
        strcpy(p, "/");
    }
    dynarr_foreach(slice_t, c, &path.components)
    {
        strncat(p, c->items, c->len);
        strcat(p, "/");
        struct stat sb;
        if (stat(p, &sb) < 0) {
            if (errno == ENOENT) {
                if (mkdir(p, 0777) < 0) {
                    fatal("path_mkdir(" SL "): mkdir(%p) failed: %s", SLARG(path.path), p, strerror(errno));
                }
                continue;
            }
            fatal("path_mkdir(" SL "): stat(%p) failed: %s", SLARG(path.path), p, strerror(errno));
        }
        if ((sb.st_mode & S_IFDIR) == 0) {
            fatal("path_mkdir(" SL "): `%s` exists but is not a directory", SLARG(path.path), p);
        }
    }
    temp_rewind(cp);
}

bool path_exists(path_t path)
{
    bool   ret = false;
    size_t cp = temp_save();
    if (access(temp_slice_to_cstr(sb_as_slice(path.path)), F_OK) == 0) {
        ret = true;
    }
    temp_rewind(cp);
    return ret;
}

bool path_is_dir(path_t path)
{
    bool        ret = false;
    size_t      cp = temp_save();
    struct stat sb;
    if (stat(temp_slice_to_cstr(sb_as_slice(path.path)), &sb) != 0) {
        ret = false;
        goto exit;
    }
    ret = (sb.st_mode & S_IFDIR) == S_IFDIR;
exit:
    temp_rewind(cp);
    return ret;
}

bool path_is_file(path_t path)
{
    bool        ret = false;
    size_t      cp = temp_save();
    struct stat sb;
    if (stat(temp_slice_to_cstr(sb_as_slice(path.path)), &sb) != 0) {
        ret = false;
        goto exit;
    }
    ret = (sb.st_mode & S_IFREG) == S_IFREG;
exit:
    temp_rewind(cp);
    return ret;
}

bool path_is_readable(path_t path)
{
    bool   ret = false;
    size_t cp = temp_save();
    if (access(temp_slice_to_cstr(sb_as_slice(path.path)), R_OK) == 0) {
        ret = true;
    }
    temp_rewind(cp);
    return ret;
}

paths_t path_file_listing(path_t path)
{
    assert(path_is_dir(path));
    paths_t        ret = { 0 };
    size_t         cp = temp_save();
    char const    *c_str = temp_slice_to_cstr(sb_as_slice(path.path));
    DIR           *d = opendir(c_str);
    struct dirent *dp;
    while ((dp = readdir(d)) != NULL) {
        path_t entry = path_extend(path, slice_make(dp->d_name, dp->d_namlen));
        dynarr_append(&ret, entry);
    }
    closedir(d);
    temp_rewind(cp);
    return ret;
}

int path_del(path_t path)
{
    size_t      cp = temp_save();
    char const *c_str = temp_slice_to_cstr(sb_as_slice(path.path));
    int         ret = 0;
    if (unlink(c_str) != 0) {
        printf("deleting " SL " failed: %s\n", SLARG(path.path), strerror(errno));
        ret = errno;
    }
    temp_rewind(cp);
    return ret;
}

int path_rmdir(path_t path)
{
    size_t      cp = temp_save();
    char const *c_str = temp_slice_to_cstr(sb_as_slice(path.path));
    int         ret = 0;
    if (rmdir(c_str) != 0) {
        printf("rmdir(`" SL "`) failed: %s\n", SLARG(path.path), strerror(errno));
        ret = errno;
    }
    temp_rewind(cp);
    return ret;
}

int path_deltree(path_t path)
{
    paths_t all = { 0 };
    dynarr_append(&all, path_copy(path));
    if (path_is_file(path)) {
        paths_t subdirs = { 0 };
        dynarr_append(&subdirs, path);
        while (subdirs.len > 0) {
            path_t  cur = dynarr_remove_ordered(path_t, &subdirs, 0);
            paths_t entries = path_file_listing(cur);
            dynarr_foreach(path_t, e, &entries)
            {
                path_t copy = path_copy(*e);
                dynarr_append(&all, copy);
                if (path_is_dir(copy)) {
                    dynarr_append(&subdirs, copy);
                }
            }
            paths_free(&entries);
        }
    }
    int ret = 0;
    dynarr_reverse(path_t, p, &all)
    {
        ret = (path_is_dir(*p)) ? path_rmdir(*p) : path_del(*p);
        if (ret != 0) {
            break;
        }
    }
    paths_free(&all);
    return ret;
}

int path_rename(path_t old, path_t new)
{
    size_t      cp = temp_save();
    char const *old_str = temp_slice_to_cstr(sb_as_slice(old.path));
    char const *new_str = temp_slice_to_cstr(sb_as_slice(new.path));
    int         ret = 0;
    if (rename(old_str, new_str) != 0) {
        ret = errno;
    }
    temp_rewind(cp);
    return ret;
}

void paths_free(paths_t *paths)
{
    dynarr_foreach(path_t, p, paths)
    {
        path_free(p);
    }
    dynarr_free(paths);
}

#define FS_IMPLEMENTED
#endif /* FS_IMPLEMENTED */
#endif /* FS_IMPLEMENTATION */

#ifdef FS_TEST

int main()
{
    path_t p = path_make_relative("foo", "bar", "baz");
    assert(p.components.len == 3);
    assert(p.kind == PATH_RELATIVE);
    p = path_parse(C("a/b/c/d"));
    assert(p.components.len == 4);
    assert(p.kind == PATH_RELATIVE);
    p = path_parse(C("/a/b/c/d/e"));
    assert(p.components.len == 5);
    assert(p.kind == PATH_ABSOLUTE);

    p = path_make_relative("foo", "bar.c");
    assert(slice_eq(p.components.items[1], C("bar.c")));
    assert(slice_eq(path_extension(&p), C(".c")));
    path_replace_extension(&p, C(".h"));
    assert(slice_eq(path_extension(&p), C(".h")));
    path_replace_extension(&p, C("o"));
    assert(slice_eq(path_extension(&p), C(".o")));

    p = path_make_relative("src", "fs.h");
    assert(path_exists(p));
    assert(path_is_readable(p));
    assert(path_is_file(p));

    p = path_make_relative("build");
    assert(path_exists(p));
    assert(path_is_dir(p));
}

#endif /* FS_TEST */
