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

#define SPORTS(S)                                                   \
    S(FIT_SPORT_INVALID, "INVALID")                                 \
    S(FIT_SPORT_GENERIC, "GENERIC")                                 \
    S(FIT_SPORT_RUNNING, "RUNNING")                                 \
    S(FIT_SPORT_CYCLING, "CYCLING")                                 \
    S(FIT_SPORT_TRANSITION, "TRANSITION")                           \
    S(FIT_SPORT_FITNESS_EQUIPMENT, "FITNESS EQUIPMENT")             \
    S(FIT_SPORT_SWIMMING, "SWIMMING")                               \
    S(FIT_SPORT_BASKETBALL, "BASKETBALL")                           \
    S(FIT_SPORT_SOCCER, "SOCCER")                                   \
    S(FIT_SPORT_TENNIS, "TENNIS")                                   \
    S(FIT_SPORT_AMERICAN_FOOTBALL, "AMERICAN FOOTBALL")             \
    S(FIT_SPORT_TRAINING, "TRAINING")                               \
    S(FIT_SPORT_WALKING, "WALKING")                                 \
    S(FIT_SPORT_CROSS_COUNTRY_SKIING, "CROSS-COUNTRY SKIING")       \
    S(FIT_SPORT_ALPINE_SKIING, "ALPINE SKIING")                     \
    S(FIT_SPORT_SNOWBOARDING, "SNOWBOARDING")                       \
    S(FIT_SPORT_ROWING, "ROWING")                                   \
    S(FIT_SPORT_MOUNTAINEERING, "MOUNTAINEERING")                   \
    S(FIT_SPORT_HIKING, "HIKING")                                   \
    S(FIT_SPORT_MULTISPORT, "MULTISPORT")                           \
    S(FIT_SPORT_PADDLING, "PADDLING")                               \
    S(FIT_SPORT_FLYING, "FLYING")                                   \
    S(FIT_SPORT_E_BIKING, "E-BIKING")                               \
    S(FIT_SPORT_MOTORCYCLING, "MOTORCYCLING")                       \
    S(FIT_SPORT_BOATING, "BOATING")                                 \
    S(FIT_SPORT_DRIVING, "DRIVING")                                 \
    S(FIT_SPORT_GOLF, "GOLF")                                       \
    S(FIT_SPORT_HANG_GLIDING, "HANG GLIDING")                       \
    S(FIT_SPORT_HORSEBACK_RIDING, "HORSEBACK RIDING")               \
    S(FIT_SPORT_HUNTING, "HUNTING")                                 \
    S(FIT_SPORT_FISHING, "FISHING")                                 \
    S(FIT_SPORT_INLINE_SKATING, "INLINE SKATING")                   \
    S(FIT_SPORT_ROCK_CLIMBING, "ROCK CLIMBING")                     \
    S(FIT_SPORT_SAILING, "SAILING")                                 \
    S(FIT_SPORT_ICE_SKATING, "ICE SKATING")                         \
    S(FIT_SPORT_SKY_DIVING, "SKY_DIVING")                           \
    S(FIT_SPORT_SNOWSHOEING, "SNOWSHOEING")                         \
    S(FIT_SPORT_SNOWMOBILING, "SNOWMOBILING")                       \
    S(FIT_SPORT_STAND_UP_PADDLEBOARDING, "STAND-UP PADDLEBOARDING") \
    S(FIT_SPORT_SURFING, "SURFING")                                 \
    S(FIT_SPORT_WAKEBOARDING, "WAKEBOARDING")                       \
    S(FIT_SPORT_WATER_SKIING, "WATER SKIING")                       \
    S(FIT_SPORT_KAYAKING, "KAYAKING")                               \
    S(FIT_SPORT_RAFTING, "RAFTING")                                 \
    S(FIT_SPORT_WINDSURFING, "WINDSURFING")                         \
    S(FIT_SPORT_KITESURFING, "KITESURFING")                         \
    S(FIT_SPORT_TACTICAL, "TACTICAL")                               \
    S(FIT_SPORT_JUMPMASTER, "JUMPMASTER")                           \
    S(FIT_SPORT_BOXING, "BOXING")                                   \
    S(FIT_SPORT_FLOOR_CLIMBING, "FLOOR CLIMBING")                   \
    S(FIT_SPORT_BASEBALL, "BASEBALL")                               \
    S(FIT_SPORT_DIVING, "DIVING")                                   \
    S(FIT_SPORT_HIIT, "HIIT")                                       \
    S(FIT_SPORT_RACKET, "RACKET")                                   \
    S(FIT_SPORT_WHEELCHAIR_PUSH_WALK, "WHEELCHAIR PUSH WALK")       \
    S(FIT_SPORT_WHEELCHAIR_PUSH_RUN, "WHEELCHAIR PUSH RUN")         \
    S(FIT_SPORT_MEDITATION, "MEDITATION")                           \
    S(FIT_SPORT_DISC_GOLF, "DISC GOLF")                             \
    S(FIT_SPORT_CRICKET, "CRICKET")                                 \
    S(FIT_SPORT_RUGBY, "RUGBY")                                     \
    S(FIT_SPORT_HOCKEY, "HOCKEY")                                   \
    S(FIT_SPORT_LACROSSE, "LACROSSE")                               \
    S(FIT_SPORT_VOLLEYBALL, "VOLLEYBALL")                           \
    S(FIT_SPORT_WATER_TUBING, "WATER TUBING")                       \
    S(FIT_SPORT_WAKESURFING, "WAKESURFING")                         \
    S(FIT_SPORT_MIXED_MARTIAL_ARTS, "MIXED MARTIAL ARTS")           \
    S(FIT_SPORT_SNORKELING, "SNORKELING")                           \
    S(FIT_SPORT_DANCE, "DANCE")                                     \
    S(FIT_SPORT_JUMP_ROPE, "JUMP ROPE")

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

slice_t  sport_name(FIT_SPORT sport);

import_t import_init(db_t *db, sweattrails_entities_t *entities, bool rebuild);
void     import_free(import_t *this);
void     import_start(import_t *this);
void     import_restart(import_t *this);

ptr         activity_import(sweattrails_entities_t *entities, path_t inbox_path);
char const *activity_store(ptr activity, db_t *db);

#endif /* __SWEATTRAILS_H__ */
