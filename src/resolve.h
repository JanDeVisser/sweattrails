/*
 * Copyright (c) 2023, 2025 Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifdef RESOLVE_TEST
#define SLICE_IMPLEMENTATION
#define DA_IMPLEMENTATION
#define FS_IMPLEMENTATION
#define RESOLVE_IMPLEMENTATION
#endif

#include "config.h"
#include "da.h"
#include "fs.h"
#include "slice.h"

#ifndef __RESOLVE_H__
#define __RESOLVE_H__

#define ENVVAR_ELROND_DIR "ELROND_DIR"
#define ELROND_INIT "_elrond_init"

typedef void (*void_t)();
typedef void *lib_handle_t;

typedef struct _dl_error {
    slice_t message;
} dl_error_t;

typedef RES(lib_handle_t, dl_error_t) lib_handle_result_t;
typedef RES(void_t, dl_error_t) function_result_t;

typedef struct _resolve_function {
    slice_t name;
    void_t  function;
} resolve_function_t;

typedef DA(resolve_function_t) resolve_functions_t;

typedef struct _library {
    lib_handle_result_t handle;
    slice_t             image;
    resolve_functions_t functions;
} library_t;

typedef DA(library_t) libraries_t;
typedef libraries_t resolve_t;

slice_t           library_to_string(library_t *lib);
bool              library_is_valid(library_t *lib);
function_result_t library_get_function(library_t *lib, slice_t name);
resolve_t        *get_resolver();
nodeptr           resolve_open(slice_t lib_name);
function_result_t resolve_function(slice_t name);

#endif /* __RESOLVE_H__ */

#ifdef RESOLVE_IMPLEMENTATION
#ifndef RESOLVE_IMPLEMENTED

#include <dlfcn.h>

#include "type.h"

/* ------------------------------------------------------------------------ */

path_t platform_image(slice_t image)
{
    if (image.len == 0) {
        return (path_t) { 0 };
    }
    path_t platform_image = path_parse(image);
#ifdef __APPLE__
    path_replace_extension(&platform_image, C("dylib"));
#else
    path_replace_extension(&platform_image, C("so"));
#endif
    return platform_image;
}

static lib_handle_result_t library_try_open(library_t *lib, path_t dir)
{
    slice_t path_string = { 0 };
    if (lib->image.len != 0 && dir.path.items != NULL) {
        path_t path = path_append(dir, platform_image(lib->image));
        path_string = sb_as_slice(path.path);
    }
    dlerror();
    lib_handle_t lib_handle = dlopen(path_string.items, RTLD_NOW | RTLD_GLOBAL);
    if (lib_handle != NULL) {
        dlerror();
        return RESVAL(lib_handle_result_t, lib_handle);
    }
    char const *dlerr = dlerror();
    return RESERR(lib_handle_result_t, (dl_error_t) { .message = C(dlerr) });
}

static lib_handle_result_t library_open(library_t *lib)
{
    lib_handle_result_t ret = RESERR(lib_handle_result_t, (dl_error_t) { 0 });
    if (lib->image.len != 0) {
        path_t elrond_dir = path_parse(getenv(ENVVAR_ELROND_DIR) ? C(getenv(ENVVAR_ELROND_DIR)) : C(ELROND_DIR));
        if (elrond_dir.path.len == 0) {
            elrond_dir = path_parse(C("/usr/share/elrond"));
        }
        ret = library_try_open(lib, path_extend(path_copy(elrond_dir), C("lib")));
        if (!ret.ok) {
            ret = library_try_open(lib, path_extend(path_copy(elrond_dir), C("bin")));
        }
        if (!ret.ok) {
            ret = library_try_open(lib, path_extend(path_copy(elrond_dir), C("build")));
        }
        if (!ret.ok) {
            ret = library_try_open(lib, elrond_dir);
        }
        if (!ret.ok) {
            ret = library_try_open(lib, path_extend(path_copy(elrond_dir), C("share/lib")));
        }
        if (!ret.ok) {
            ret = library_try_open(lib, path_parse(C("lib")));
        }
        if (!ret.ok) {
            ret = library_try_open(lib, path_parse(C("bin")));
        }
        if (!ret.ok) {
            ret = library_try_open(lib, path_parse(C("build")));
        }
        if (!ret.ok) {
            ret = library_try_open(lib, path_parse(C("share/lib")));
        }
        if (!ret.ok) {
            ret = library_try_open(lib, path_parse(C(".")));
        }
    } else {
        ret = library_try_open(lib, (path_t) { 0 });
    }
    if (ret.ok) {
        lib->handle = ret;
        if (lib->image.len != 0) {
            function_result_t result = library_get_function(lib, C(ELROND_INIT));
            if (result.ok) {
                void_t func_ptr = result.success;
                if (func_ptr != NULL) {
                    (func_ptr)();
                }
            } else {
                fprintf(stderr, "resolve_open('" SL "') Error finding initializer: " SL "\n",
                    SLARG(library_to_string(lib)), SLARG(result.error.message));
                lib->handle = RESERR(lib_handle_result_t, result.error);
                return lib->handle;
            }
        }
    } else {
        fprintf(stderr, "resolve_open('" SL "') FAILED: " SL "\n",
            SLARG(library_to_string(lib)), SLARG(ret.error.message));
        lib->handle = ret;
    }
    return ret;
}

slice_t library_to_string(library_t *lib)
{
    return (lib->image.len != 0) ? lib->image : C("Main Program Image");
}

bool library_is_valid(library_t *lib)
{
    return lib->handle.ok;
}

function_result_t library_get_function(library_t *lib, slice_t function_name)
{
    if (!lib->handle.ok) {
        return RESERR(function_result_t, lib->handle.error);
    }
    for (size_t ix = 0; ix < lib->functions.len; ++ix) {
        resolve_function_t *f = lib->functions.items + ix;
        if (slice_eq(f->name, function_name)) {
            return RESVAL(function_result_t, f->function);
        }
    }
    dlerror();
    size_t      cp = temp_save();
    char const *fnc = function_name.items;
    if (fnc[function_name.len] != 0) {

        fnc = temp_slice_to_cstr(function_name);
    }
    void_t function = dlsym(lib->handle.success, fnc);
    temp_rewind(cp);
    if (function == NULL) {
        // 'undefined symbol' is returned with an empty result pointer
        char   *dl_error = dlerror();
        slice_t err = (dl_error != NULL) ? C(dl_error) : C("");
#ifdef __APPLE__
        if (err.len > 0 && !slice_find(err, C("symbol not found")).ok) {
#else
        if (err.len > 0 && !slice_find(err, C("undefined symbol")).ok) {
#endif
            return RESERR(function_result_t, (dl_error_t) { .message = err });
        }
    }
    dynarr_append_s(resolve_function_t, &lib->functions, .name = function_name, .function = function);
    return RESVAL(function_result_t, function);
}

/* ------------------------------------------------------------------------ */

nodeptr resolve_open_library(resolve_t *resolve, slice_t img)
{
    library_t lib = {
        .image = img,
    };
    lib.handle = library_open(&lib);
    dynarr_append(resolve, lib);
    return nodeptr_ptr(resolve->len - 1);
}

void resolve_destroy(resolve_t *resolve)
{
    for (size_t ix = 0; ix < resolve->len; ++ix) {
        library_t *lib = resolve->items + ix;
        if (lib->handle.ok) {
            dlclose(lib->handle.success);
        }
    }
    resolve->len = 0;
}

nodeptr resolve_open(slice_t lib_name)
{
    resolve_t *resolve = get_resolver();
    for (size_t ix = 0; ix < resolve->len; ++ix) {
        library_t *lib = resolve->items + ix;
        if (slice_eq(lib->image, lib_name)) {
            return nodeptr_ptr(ix);
        }
    }
    return resolve_open_library(resolve, lib_name);
}

function_result_t resolve_function(slice_t func_name)
{
    slice_t    s = slice_trim(func_name);
    opt_size_t paren = slice_indexof(s, '(');
    if (paren.ok) {
        s = slice_first(s, paren.value);
    }

    slice_t    lib_name = { 0 };
    slice_t    function = s;
    opt_size_t colon = slice_indexof(s, ':');
    if (colon.ok) {
        lib_name = slice_first(s, colon.value);
        function = slice_tail(s, colon.value + 1);
    }

    resolve_t *resolve = get_resolver();
    nodeptr    p = resolve_open(lib_name);
    assert(p.ok);
    library_t *lib = resolve->items + p.value;
    if (!lib->handle.ok) {
        return RESERR(function_result_t, lib->handle.error);
    }
    return library_get_function(lib, function);
}

resolve_t *get_resolver()
{
    static resolve_t resolver = { 0 };
    return &resolver;
}

#define RESOLVE_IMPLEMENTED
#endif /* RESOLVE_IMPLEMENTED */
#endif /* RESOLVE_IMPLEMENTATION */

#ifdef RESOLVE_TEST

int main()
{
    function_result_t res = resolve_function(C("libelrrt:elrond$putln"));
    assert(res.ok && res.success != NULL);
}

#endif
