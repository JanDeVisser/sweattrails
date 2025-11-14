/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <pthread.h>
#include <time.h>

#include "fs.h"
#include "schema.h"
#include "zorro.h"

#include "sweattrails.h"

import_t import_init(db_t *db, repo_t *repo, bool rebuild)
{
    char *home = getenv("HOME");
    assert(home != NULL);
    path_t app_dir = path_make_relative(home, ".sweattrails");
    path_mkdirs(app_dir);
    path_t inbox = path_extend(app_dir, C("inbox"));
    path_t done = path_extend(app_dir, C("done"));
    path_t errors = path_extend(app_dir, C("errors"));

    path_mkdirs(done);
    if (rebuild) {
        printf("Reloading all .fit files\n");
        assert(path_deltree(inbox) == 0);
        assert(path_rename(done, inbox) == 0);
    }
    path_mkdirs(inbox);
    path_mkdirs(done);
    path_mkdirs(errors);

    return (import_t) {
        .db = db,
        .repo = repo,
        .inbox_d = inbox,
        .done_d = done,
        .errors_d = errors,
    };
}

void import_free(import_t *this)
{
    dynarr_free(&this->done);
    dynarr_free(&this->errors);
    path_free(&this->inbox_d);
    path_free(&this->done_d);
    path_free(&this->errors_d);
    switch (this->import_status.status) {
    case ImportStatus_Processing:
        allocator_free(this->import_status.importing.items);
        break;
    case ImportStatus_Crashed:
        allocator_free(this->import_status.crashed.filename.items);
        allocator_free(this->import_status.crashed.message.items);
        break;
    default:
        break;
    }
}

bool import_file(import_t *this, path_t inbox_path)
{
    slice_t filename = path_basename(&inbox_path);
    this->import_status = (struct import_status) { .status = ImportStatus_Importing, .importing = filename };
    ptr activity = activity_import(this->repo, inbox_path);
    db_begin(this->db);
    char const *err = activity_store(activity, this->db);
    if (err != NULL) {
        db_rollback(this->db);
        dynarr_append_s(
            slice_pair_t,
            &this->errors,
            .key = sb_as_slice(sb_make(sb_as_slice(inbox_path.path))),
            .value = sb_as_slice(sb_make_cstr(err)));
        path_t err_file = path_extend(this->errors_d, filename);
        path_replace_extension(&err_file, C(".fit"));
        path_t error_path = path_extend(this->errors_d, filename);
        assert(path_rename(inbox_path, error_path));
        path_free(&error_path);
        this->import_status = (struct import_status) { .status = ImportStatus_Processing, .importing = (slice_t) { 0 } };
        return false;
    };
    db_commit(this->db);
    dynarr_append(&this->done, sb_as_slice(sb_make(filename)));
    path_t done_path = path_extend(this->done_d, filename);
    path_replace_extension(&done_path, C(".fit"));
    path_rename(inbox_path, done_path);
    path_free(&done_path);
    this->import_status = (struct import_status) { .status = ImportStatus_Processing, .importing = (slice_t) { 0 } };
    return true;
}

void *import_run(void *import)
{
    import_t *this = (import_t *) import;
    while (true) {
        if (this->import_status.status != ImportStatus_Idle && this->import_status.status != ImportStatus_Start) {
            continue;
        }
        this->import_status.status = ImportStatus_Processing;
        paths_t files = path_file_listing(this->inbox_d);
        dynarr_foreach(path_t, p, &files)
        {
            slice_t ext = path_extension(p);
            if (slice_eq(ext, C(".fit")) || slice_eq(ext, C(".FIT"))) {
                if (import_file(this, *p)) {
                    ++this->total_imported;
                } else {
                    ++this->total_errors;
                }
            }
        }
        this->import_status.status = ImportStatus_Idle;
        struct timespec ts = { .tv_sec = 1, .tv_nsec = 0 };
        nanosleep(&ts, NULL);
    }
    return import;
}

void import_start(import_t *this)
{
    int result = pthread_create(&this->thread, NULL, import_run, (void *) this);
    if (result != 0) {
        fatal("Error starting import thread: %d", result);
    }
    pthread_detach(this->thread);
}

void import_restart(import_t *this)
{
    switch (this->import_status.status) {
    case ImportStatus_Processing:
        allocator_free(this->import_status.importing.items);
        break;
    case ImportStatus_Crashed:
        allocator_free(this->import_status.crashed.filename.items);
        allocator_free(this->import_status.crashed.message.items);
        break;
    default:
        break;
    }
    this->import_status.status = ImportStatus_Idle;
}
