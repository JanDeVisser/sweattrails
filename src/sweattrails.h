/*
 * Copyright (c) 2023, 2025 Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <pthread.h>

#include "../fitsdk/fit_example.h"
#include <raylib.h>

#include "da.h"
#include "map.h"
#include "slice.h"

#include "schema.h"

#ifndef __SWEATTRAILS_H__
#define __SWEATTRAILS_H__

typedef struct _fat_pointer {
    sweattrails_entities_t *entities;
    nodeptr                 ptr;
} ptr;

#define get_ptr(p)                                                     \
    (                                                                  \
        {                                                              \
            ptr __p = (p);                                             \
            assert(__p.ptr.ok && (__p.ptr.value < __p.entities->len)); \
            (__p.entities->items + __p.ptr.value);                     \
        })

#define get_p(T, ptr)                                 \
    (                                                 \
        {                                             \
            sweattrails_entity_t *__e = get_ptr(ptr); \
            (&__e->T);                                \
        })

#define make_ptr(other, ix) ((ptr) { .entities = other.entities, .ptr = ix })

typedef enum _import_status {
    ImportStatus_Start,
    ImportStatus_Idle,
    ImportStatus_Processing,
    ImportStatus_Importing,
    ImportStatus_Crashed,
} import_status_t;

typedef struct _import {
    db_t                   *db;
    sweattrails_entities_t *entities;
    slices_t                done;
    slice_pairs_t           errors;
    pthread_t               thread;
    int                     total_imported;
    int                     total_errors;
    path_t                  inbox_d;
    path_t                  done_d;
    path_t                  errors_d;

    struct import_status {
        import_status_t status;
        union {
            slice_t importing;
            struct {
                slice_t filename;
                slice_t message;
            } crashed;
        };
    } import_status;
} import_t;

import_t import_init(db_t *db, sweattrails_entities_t *entities, bool rebuild);
void     import_free(import_t *this);
void     import_start(import_t *this);
void     import_restart(import_t *this);

ptr         activity_import(sweattrails_entities_t *entities, path_t inbox_path);
char const *activity_store(ptr activity, db_t *db);

#endif /* __SWEATTRAILS_H__ */
