/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <libpq-fe.h>

#include "da.h"
#include "schema.h"
#include "sweattrails.h"

bool must_include(column_def_t *col)
{
    switch (col->kind) {
    case SQLTypeKind_Builtin:
    case SQLTypeKind_Optional:
    case SQLTypeKind_Composite:
        return true;
    case SQLTypeKind_Reference:
        return col->reference.cardinality == Card_ManyToOne;
    default:
        NYI("sql type kind " SL, SLARG(sql_type_kind_name(col->kind)));
    }
}

char *render_parameter(void *value_ptr, sql_type_t type)
{
    switch (type) {
    case SQLType_Bool:
        return temp_sprintf("%s", *((bool *) value_ptr) ? "TRUE" : "FALSE");
        break;
    case SQLType_Int32:
        return temp_sprintf("%d", *((int *) value_ptr));
        break;
    case SQLType_UInt32:
        return temp_sprintf("%u", *((uint32_t *) value_ptr));
        break;
    case SQLType_Float:
        return temp_sprintf("%f", *((float *) value_ptr));
        break;
    case SQLType_String:
        return temp_sprintf("'" SL "'", SLARG(*((slice_t *) value_ptr)));
        break;
    case SQLType_Point: {
        Vector2 v = *((Vector2 *) value_ptr);
        return temp_sprintf("(%f,%f)", v.x, v.y);
    } break;
    case SQLType_Coordinates: {
        coordinates_t c = *((coordinates_t *) value_ptr);
        return temp_sprintf("(%f,%f)", c.lon, c.lat);
    } break;
    case SQLType_Box: {
        box_t box = *((box_t *) value_ptr);
        return temp_sprintf("((%f,%f),(%f,%f))", box.sw.lon, box.sw.lat, box.ne.lon, box.ne.lat);
    } break;
    default:
        NYI("builtin sql type " SL, SLARG(sql_type_name(type)));
    }
}

void assign_parameter(ptr entity, schema_def_t *schema, column_def_t *col, cstrs *values)
{
    void *data = get_p(dummy_entity, entity);
    void *value_ptr = data + col->offset;
    switch (col->kind) {
    case SQLTypeKind_Builtin:
        dynarr_append(values, render_parameter(value_ptr, col->type));
        break;
    case SQLTypeKind_Optional:
        if (*((bool *) value_ptr)) {
            dynarr_append(values, render_parameter(value_ptr + col->optional.value_offset, col->optional.type));
        } else {
            dynarr_append(values, NULL);
        }
        break;
    case SQLTypeKind_Composite: {
        type_def_t *type = schema->types.items + col->composite.value;
        sb_t        v = sb_make_cstr("(");
        bool        first = true;
        dynarr_foreach(column_def_t, type_col, &type->composite)
        {
            if (!first) {
                sb_append_char(&v, ',');
            }
            first = false;
            assert(type_col->kind == SQLTypeKind_Builtin);
            sb_append_cstr(&v, render_parameter(value_ptr + type_col->offset, type_col->type));
        }
        sb_append_char(&v, ')');
        sb_append_char(&v, 0);
        dynarr_append(values, temp_strdup(v.items));
    } break;
    case SQLTypeKind_Reference:
        if (col->reference.cardinality == Card_ManyToOne) {
            ref_t *ref = (ref_t *) value_ptr;
            assert(ref->entity_id.ok);
            if (!ref->db_id.ok) {
                dummy_entity_t *target = &(entity.repo->entities.items + ref->entity_id.value)->dummy_entity;
                assert(target->id.entity_id.value == ref->entity_id.value);
                assert(target->id.db_id.ok);
                *ref = target->id;
            }
            dynarr_append(values, temp_sprintf("%zu", ref->db_id.value));
        }
        break;
    default:
        NYI("sql type kind " SL, SLARG(sql_type_kind_name(col->kind)));
    }
}

bool entity_delete(ptr entity, db_t *db)
{
    int          def_ix = get_ptr(entity)->type;
    void        *data = get_p(dummy_entity, entity);
    serial      *id = &((dummy_entity_t *) data)->id;
    size_t       cp = temp_save();
    PGresult    *res;
    table_def_t *def = db->schema.tables.items + def_ix;
    char        *sql = temp_sprintf("DELETE FROM " SL " WHERE id = $1", SLARG(def->name));
    cstrs        values = { 0 };
    bool         ret = true;

    dynarr_append(&values, temp_sprintf("%zu", id->db_id.value));
    res = PQexecParams(db->conn, sql, values.len, NULL, (char const *const *) values.items, NULL, NULL, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        sb_t msg = sb_format(SL "_delete() failed: %s", SLARG(def->name), PQerrorMessage(db->conn));
        trace("PQ error: " SL, SLARG(msg));
        PQclear(res);
        PQfinish(db->conn);
        ret = false;
    } else {
        id->entity_id = id->db_id = nullptr;
    }
    dynarr_free(&values);
    temp_rewind(cp);
    return ret;
}

char const *entity_store(ptr entity, db_t *db)
{
    int          def_ix = get_ptr(entity)->type;
    void        *data = get_p(dummy_entity, entity);
    serial      *id = &((dummy_entity_t *) data)->id;
    size_t       cp = temp_save();
    PGresult    *res;
    table_def_t *def = db->schema.tables.items + def_ix;
    sb_t         sql = { 0 };
    cstrs        values = { 0 };

    // trace("Storing " SL " db_id %zu/%d entity_id %zu/%d", SLARG(def->name), id->db_id.value, id->db_id.ok, id->entity_id.value, id->entity_id.ok);

    if (id->db_id.ok) {
        assert(id->entity_id.ok);
        if (id->entity_id.value != (size_t) -1) {
            if (hash_ptr(entity)) {
                return NULL;
            }
            sb_printf(&sql, "UPDATE sweattrails." SL " SET ", SLARG(def->name));
            dynarr_foreach(column_def_t, col, &def->columns)
            {
                if (slice_eq(col->name, C("id"))) {
                    continue;
                }
                if (!must_include(col)) {
                    continue;
                }
                if (values.len > 0) {
                    sb_append_cstr(&sql, ", ");
                }
                sb_printf(&sql, SL " = $%zu::" SL, SLARG(col->name), values.len + 1, SLARG(sql_type_sql(col->type)));
                assign_parameter(entity, &db->schema, col, &values);
            }
            sb_printf(&sql, " WHERE id = $%zu RETURNING id", values.len + 1);
            dynarr_append(&values, temp_sprintf("%zu", id->db_id.value));
        } else {
            entity_delete(entity, db);
        }
    } else {
        if (!id->entity_id.ok) {
            return NULL;
        }
        sb_printf(&sql, "INSERT INTO sweattrails." SL " ( ", SLARG(def->name));
        dynarr_foreach(column_def_t, col, &def->columns)
        {
            if (slice_eq(col->name, C("id"))) {
                continue;
            }
            if (!must_include(col)) {
                continue;
            }
            if (values.len > 0) {
                sb_append_cstr(&sql, ", ");
            }
            sb_append(&sql, col->name);
            assign_parameter(entity, &db->schema, col, &values);
        }
        sb_append_cstr(&sql, " ) VALUES ( ");
        for (size_t ix = 0; ix < values.len; ++ix) {
            if (ix > 0) {
                sb_append_cstr(&sql, ", ");
            }
            sb_printf(&sql, "$%zu", ix + 1);
        }
        sb_append_cstr(&sql, " ) RETURNING id");
    }
    //    trace("SQL: " SL, SLARG(sql));
    //    for (size_t ix = 0; ix < values.len; ++ix) {
    //	      trace("value[%zu]: %s", ix + 1, values.items[ix]);
    //    }
    res = PQexecParams(db->conn, sql.items, values.len, NULL, (char const *const *) values.items, NULL, NULL, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        sb_t msg = sb_format(SL "_store() failed: %s", SLARG(def->name), PQerrorMessage(db->conn));
        trace("PQ error: " SL, SLARG(msg));
        PQclear(res);
        PQfinish(db->conn);
        return msg.items;
    }
    assert(atoi(PQcmdTuples(res)) == 1);
    size_t returned_id = atoi(PQgetvalue(res, 0, 0));
    if (id->db_id.ok) {
        assert(id->db_id.value == returned_id);
    } else {
        id->db_id = nodeptr_ptr(returned_id);
        // trace("ID: %zu %d %d", entity.ptr.value, get_p(entity, entity)->id.ok, get_p(entity, entity)->id.value);
    }
    PQclear(res);
    sb_free(&sql);
    dynarr_free(&values);
    temp_rewind(cp);
    return NULL;
}

Vector2 parse_point(slice_t *point)
{
    size_t cp = temp_save();
    slice_token(point, '(');
    float x = atof(temp_slice_to_cstr(slice_token(point, ',')));
    float y = atof(temp_slice_to_cstr(slice_token(point, ')')));
    temp_rewind(cp);
    return (Vector2) { .x = x, .y = y };
}

box_t parse_box(slice_t *box)
{
    trace("parse_box( -" SL "- )", SLARG(*box));
    // slice_token(box, '(');
    Vector2 sw = parse_point(box);
    slice_token(box, ',');
    Vector2 ne = parse_point(box);
    // slice_token(box, ')');
    trace("sw: lon: %f, lat: %f; ne: lon: %f, lat: %f", sw.x, sw.y, ne.x, ne.y);
    return (box_t) {
        .sw = (coordinates_t) { .lon = sw.x, .lat = sw.y },
        .ne = (coordinates_t) { .lon = ne.x, .lat = ne.y },
    };
}

int unmarshall_builtin(char const *sql_value, void *value_ptr, sql_type_t type)
{
    switch (type) {
    case SQLType_Bool:
        *((bool *) value_ptr) = sql_value[0] == 't';
        break;
    case SQLType_Int32:
        *((int *) value_ptr) = atoi(sql_value);
        break;
    case SQLType_UInt32:
        *((uint32_t *) value_ptr) = (uint32_t) atol(sql_value);
        break;
    case SQLType_Float:
        *((float *) value_ptr) = atof(sql_value);
        break;
    case SQLType_Serial:
        ((serial *) value_ptr)->db_id = nodeptr_ptr(atol(sql_value));
        break;
    case SQLType_String:
        *((slice_t *) value_ptr) = C(strdup(sql_value));
        break;
    case SQLType_Point: {
        slice_t s = C(sql_value);
        *((Vector2 *) value_ptr) = parse_point(&s);
    } break;
    case SQLType_Coordinates: {
        slice_t s = C(sql_value);
        Vector2 p = parse_point(&s);
        *((coordinates_t *) value_ptr) = (coordinates_t) { .lon = p.x, .lat = p.y };
    } break;
    case SQLType_Box: {
        slice_t s = C(sql_value);
        *((box_t *) value_ptr) = parse_box(&s);
    } break;
    default:
        NYI("builtin sql type " SL, SLARG(sql_type_name(type)));
    }
    return 0;
}

int unmarshall_value(schema_def_t *schema, char const *sql_value, void *value_ptr, column_def_t *col)
{
    switch (col->kind) {
    case SQLTypeKind_Builtin:
        return unmarshall_builtin(sql_value, value_ptr, col->type);
    case SQLTypeKind_Optional: {
        slice_t v = C(sql_value);
        if (slice_trim(v).len != 0) {
            *((bool *) value_ptr) = true;
            return unmarshall_builtin(sql_value, value_ptr + col->optional.value_offset, col->optional.type);
        } else {
            *((bool *) value_ptr) = false;
        }
    } break;
    case SQLTypeKind_Composite: {
        type_def_t *type = schema->types.items + col->composite.value;
        slice_t     v = C(sql_value);
        slice_token(&v, ')');
        dynarr_foreach(column_def_t, type_col, &type->composite)
        {
            char    sep = (type_col == type->composite.items + type->composite.len - 1) ? ')' : ',';
            slice_t field_value = slice_token(&v, sep);
            assert(unmarshall_value(schema, temp_slice_to_cstr(field_value), value_ptr + type_col->offset, type_col) == 0);
        }
    } break;
    case SQLTypeKind_Reference: {
        *((ref_t *) value_ptr) = (ref_t) { .type = col->reference.reference_tag, .db_id = nodeptr_ptr(atol(sql_value)) };
    } break;
    default:
        NYI("sql type kind " SL, SLARG(sql_type_kind_name(col->kind)));
    }
    return 0;
}

refs_t entity_load_all(db_t *db, repo_t *repo, int def_ix, char const *join, char const *order_by)
{
    refs_t       ret = { 0 };
    PGresult    *res;
    table_def_t *def = db->schema.tables.items + def_ix;
    sb_t         sql = sb_make_cstr("SELECT ");

    dynarr_foreach(column_def_t, col, &def->columns)
    {
        if (!must_include(col)) {
            continue;
        }
        if (col != def->columns.items) {
            sb_append_cstr(&sql, ", ");
        }
        sb_printf(&sql, SL "." SL "." SL, SLARG(db->schema.schema), SLARG(def->name), SLARG(col->name));
    }
    sb_printf(&sql, " FROM " SL "." SL, SLARG(db->schema.schema), SLARG(def->name));
    if (join != NULL) {
        sb_append_char(&sql, ' ');
        sb_append_cstr(&sql, join);
    }
    sb_printf(&sql, " ORDER BY %s", (order_by != NULL) ? order_by : "id");
    res = PQexec(db->conn, sql.items);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        trace("PQ error: entity_load_all(" SL ") failed: %s", SLARG(def->name), PQerrorMessage(db->conn));
        PQclear(res);
        PQfinish(db->conn);
        goto exit;
    }
    size_t cp = temp_save();
    for (int ix = 0; ix < PQntuples(res); ++ix) {
        temp_rewind(cp);
        entity_t e = { .type = def_ix };
        void    *data = &e.dummy_entity;
        int      field_num = 0;
        dynarr_foreach(column_def_t, col, &def->columns)
        {
            if (!must_include(col)) {
                continue;
            }
            assert(unmarshall_value(&db->schema, PQgetvalue(res, ix, field_num), data + col->offset, col) == 0);
            ++field_num;
        }
        nodeptr p = entity_append(repo, e);
        dynarr_append_s(
            ref_t,
            &ret,
            .type = def_ix,
            .db_id = nodeptr_ptr(e.dummy_entity.id.db_id.value),
            .entity_id = p);
    }
    temp_rewind(cp);
    PQclear(res);
    sb_free(&sql);
exit:
    trace("Loaded %zu " SL " entities", ret.len, SLARG(def->name));
    return ret;
}

char const *record_store(ptr record, db_t *db)
{
    return entity_store(record, db);
}

char const *lap_store(ptr lap, db_t *db)
{
    return entity_store(lap, db);
}

char const *session_store(ptr session, db_t *db)
{
    char const *ret = entity_store(session, db);
    if (ret == NULL) {
        session_t *s = get_p(session, session);
        dynarr_foreach(nodeptr, l, &s->laps)
        {
            ptr         lap = make_ptr(session, *l);
            char const *ret = lap_store(lap, db);
            if (ret != NULL) {
                break;
            }
        }
        trace("Stored session nodeptr %zu with psql id %zu and %zu laps", session.ptr.value, s->id.db_id.value, s->laps.len);
    }
    if (ret == NULL) {
        session_t *s = get_p(session, session);
        dynarr_foreach(nodeptr, r, &s->records)
        {
            ptr         record = make_ptr(session, *r);
            char const *ret = record_store(record, db);
            if (ret != NULL) {
                break;
            }
        }
    }
    return ret;
}

char const *file_store(ptr file, db_t *db)
{
    size_t cp = temp_save();
    allocator_push(&temp_allocator);
    char const *ret = entity_store(file, db);
    if (ret == NULL) {
        file_t *f = get_p(file, file);
        dynarr_foreach(nodeptr, s, &f->sessions)
        {
            ptr         session = make_ptr(file, *s);
            char const *ret = session_store(session, db);
            if (ret != NULL) {
                break;
            }
        }
        trace("Stored file nodeptr %zu with psql id %zu and %zu sessions", file.ptr.value, f->id.db_id.value, f->sessions.len);
    }
    allocator_pop();
    temp_rewind(cp);
    return ret;
}

char const *activity_store(ptr activity, db_t *db)
{
    size_t cp = temp_save();
    allocator_push(&temp_allocator);
    char const *ret = entity_store(activity, db);
    if (ret == NULL) {
        activity_t *a = get_p(activity, activity);
        dynarr_foreach(nodeptr, f, &a->files)
        {
            ptr         file = make_ptr(activity, *f);
            char const *ret = file_store(file, db);
            if (ret != NULL) {
                break;
            }
        }
        trace("Stored activity nodeptr %zu with psql id %zu and %zu files", activity.ptr.value, a->id.db_id.value, a->files.len);
    }
    allocator_pop();
    temp_rewind(cp);
    return ret;
}

bool store_everything(repo_t *repo, db_t *db)
{
    for (size_t ix = 0; ix < repo->entities.len; ++ix) {
        entity_store((ptr) { .repo = repo, .ptr = nodeptr_ptr(ix) }, db);
    }
    return true;
}

bool reload_everything(repo_t *repo, db_t *db)
{
    refs_t activities = entity_load_all(db, repo, ACTIVITY_DEF, NULL, "start_time");
    refs_t files = entity_load_all(db, repo, FILE_DEF, NULL, "start_time");
    refs_t sessions = entity_load_all(db, repo, SESSION_DEF, NULL, "start_time");
    refs_t laps = entity_load_all(db, repo, LAP_DEF,
        "join sweattrails.session on sweattrails.lap.session_id = sweattrails.session.id",
        "sweattrails.session.start_time, sweattrails.lap.start_time, sweattrails.lap.end_time");
    refs_t records = entity_load_all(db, repo, RECORD_DEF,
        "join sweattrails.session on sweattrails.record.session_id = sweattrails.session.id",
        "sweattrails.session.start_time, sweattrails.record.timestamp");

    if (activities.len == 0) {
        assert(sessions.len == 0 && laps.len == 0 && records.len == 0);
        return true;
    }
    assert(sessions.len >= activities.len && records.len > 0);

    size_t  activity_ix = 0;
    size_t  file_ix = 0;
    size_t  session_ix = 0;
    size_t  lap_ix = 0;
    size_t  record_ix = 0;
    file_t *f = (file_ix < files.len) ? get_entity(file, repo, files.items[file_ix].entity_id) : NULL;
    do {
        activity_t *a = get_entity(activity, repo, activities.items[activity_ix].entity_id);
        while (f != NULL && (f->activity_id.db_id.value == a->id.db_id.value)) {
            f->activity_id = a->id;
            dynarr_append(&a->files, f->id.entity_id);

            trace("file_ix %zu f->id %zu:%zu session_ix %zu", file_ix, f->id.entity_id.value, f->id.db_id.value, session_ix);
            session_t *s = (session_ix < sessions.len) ? get_entity(session, repo, sessions.items[session_ix].entity_id) : NULL;
            trace("session_ix %zu len %zu", session_ix, sessions.len);
            if (s != NULL) {
                trace("session_ix %zu s->id %zu:%zu s->file_id %zu", session_ix, s->id.entity_id.value, s->id.db_id.value, s->file_id.db_id.value);
            }
            while (s != NULL && (s->file_id.db_id.value == f->id.db_id.value)) {
                trace("session match");
                s->file_id = files.items[file_ix];
                dynarr_append(&f->sessions, s->id.entity_id);
                if (s->route_area.ok) {
                    s->atlas = atlas_for_box(s->route_area.value, 3, 3);
                }

                size_t lap_offset = lap_ix;
                lap_t *l = (lap_ix < laps.len) ? get_entity(lap, repo, laps.items[lap_ix].entity_id) : NULL;
                while (l != NULL && (l->session_id.db_id.value == s->id.db_id.value)) {
                    l->session_id = s->id;
                    dynarr_append(&s->laps, l->id.entity_id);
                    ++lap_ix;
                    l = (lap_ix < laps.len) ? get_entity(lap, repo, laps.items[lap_ix].entity_id) : NULL;
                }

                record_t *r = (record_ix < records.len) ? get_entity(record, repo, records.items[record_ix].entity_id) : NULL;
                while (r != NULL && r->timestamp < s->start_time) {
                    ++record_ix;
                    r = (record_ix < records.len) ? get_entity(record, repo, records.items[record_ix].entity_id) : NULL;
                }
                while (r != NULL && (r->session_id.db_id.value == s->id.db_id.value)) {
                    r->session_id = s->id;
                    dynarr_append(&s->records, r->id.entity_id);

                    for (size_t ix = lap_offset; ix < lap_ix; ++ix) {
                        lap_t *curlap = get_entity(lap, repo, laps.items[lap_offset].entity_id);
                        assert(curlap->start_time <= r->timestamp);
                        if (curlap->end_time < r->timestamp) {
                            if (lap_offset == ix) {
                                lap_offset = ix + 1;
                            }
                            continue;
                        }
                        dynarr_append(&curlap->records, records.items[record_ix].entity_id);
                    }

                    ++record_ix;
                    r = (record_ix < records.len) ? get_entity(record, repo, records.items[record_ix].entity_id) : NULL;
                }
                ++session_ix;
                s = (session_ix < sessions.len) ? get_entity(session, repo, sessions.items[session_ix].entity_id) : NULL;
                if (s != NULL) {
                    trace("session_ix %zu s->id %zu:%zu s->file_id %zu", session_ix, s->id.entity_id.value, s->id.db_id.value, s->file_id.db_id.value);
                }
            }
            ++file_ix;
            f = (file_ix < files.len) ? get_entity(file, repo, files.items[file_ix].entity_id) : NULL;
        }
        ++activity_ix;
    } while (activity_ix < activities.len);

    dynarr_free(&activities);
    dynarr_free(&sessions);
    dynarr_free(&laps);
    dynarr_free(&records);
    return true;
}
