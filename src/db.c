/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <libpq-fe.h>

#include "io.h"
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
    void *data = get_p(entity, entity);
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
            sweattrails_entity_t *e = entity.entities->items + ref->entity_id.value;
            assert(e->entity.id.ok);
            dynarr_append(values, temp_sprintf("%d", e->entity.id.value));
            if (!ref->db_id.ok) {
                ref->db_id = nodeptr_ptr(e->entity.id.value);
            } else {
                assert(ref->db_id.value == (size_t) e->entity.id.value);
            }
        }
        break;
    default:
        NYI("sql type kind " SL, SLARG(sql_type_kind_name(col->kind)));
    }
}

char const *entity_store(ptr entity, db_t *db, int def_ix)
{
    void        *data = get_p(entity, entity);
    serial      *id = &((entity_t *) data)->id;
    size_t       cp = temp_save();
    PGresult    *res;
    table_def_t *def = db->schema.tables.items + def_ix;
    sb_t         sql = { 0 };
    cstrs        values = { 0 };

    // trace("Storing ID %zu type " SL, entity.ptr.value, SLARG(def->name));

    if (id->ok) {
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
        sb_printf(&sql, " WHERE id = $%zu RETURNING id", values.len);
        dynarr_append(&values, temp_sprintf("%u", id->value));
    } else {
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
    // trace("SQL: " SL, SLARG(sql));
    //    for (size_t ix = 0; ix < values.len; ++ix) {
    //        trace("value[%zu]: %s", ix + 1, values.items[ix]);
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
    int returned_id = atoi(PQgetvalue(res, 0, 0));
    if (id->ok) {
        assert(id->value == returned_id);
    } else {
        *(id) = OPTVAL(int, returned_id);
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
        *((serial *) value_ptr) = OPTVAL(int, atoi(sql_value));
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

refs_t entity_load_all(db_t *db, sweattrails_entities_t *repo, int def_ix, char const *order_by)
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
        sb_append(&sql, col->name);
    }
    sb_printf(&sql, " FROM " SL "." SL " ORDER BY %s", SLARG(db->schema.schema), SLARG(def->name), (order_by != NULL) ? order_by : "id");
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
        dynarr_append_s(
            sweattrails_entity_t,
            repo,
            .type = def_ix);
        void *data = &repo->items[repo->len - 1].entity;
        int   field_num = 0;
        dynarr_foreach(column_def_t, col, &def->columns)
        {
            if (!must_include(col)) {
                continue;
            }
            assert(unmarshall_value(&db->schema, PQgetvalue(res, ix, field_num), data + col->offset, col) == 0);
            ++field_num;
        }
        dynarr_append_s(
            ref_t,
            &ret,
            .type = def_ix,
            .db_id = nodeptr_ptr(((entity_t *) data)->id.value),
            .entity_id = nodeptr_ptr(repo->len - 1));
    }
    temp_rewind(cp);
    PQclear(res);
    sb_free(&sql);
exit:
    return ret;
}

char const *record_store(ptr record, db_t *db)
{
    return entity_store(record, db, RECORD_DEF);
}

char const *lap_store(ptr lap, db_t *db)
{
    return entity_store(lap, db, LAP_DEF);
}

char const *session_store(ptr session, db_t *db)
{
    char const *ret = entity_store(session, db, SESSION_DEF);
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
        trace("Stored session nodeptr %zu with psql id %d and %zu laps", session.ptr.value, s->id.value, s->laps.len);
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

char const *activity_store(ptr activity, db_t *db)
{
    size_t cp = temp_save();
    allocator_push(&temp_allocator);
    char const *ret = entity_store(activity, db, ACTIVITY_DEF);
    if (ret == NULL) {
        activity_t *a = get_p(activity, activity);
        dynarr_foreach(nodeptr, s, &a->sessions)
        {
            ptr         session = make_ptr(activity, *s);
            char const *ret = session_store(session, db);
            if (ret != NULL) {
                break;
            }
        }
        trace("Stored activity nodeptr %zu with psql id %d and %zu sessions", activity.ptr.value, a->id.value, a->sessions.len);
    }
    allocator_pop();
    temp_rewind(cp);
    return ret;
}

bool reload_everything(sweattrails_entities_t *repo, db_t *db)
{
    refs_t activities = entity_load_all(db, repo, ACTIVITY_DEF, "start_time");
    refs_t sessions = entity_load_all(db, repo, SESSION_DEF, "start_time");
    refs_t laps = entity_load_all(db, repo, LAP_DEF, "session_id, start_time, end_time");
    refs_t records = entity_load_all(db, repo, RECORD_DEF, "session_id, timestamp");

    if (activities.len == 0) {
        assert(sessions.len == 0 && laps.len == 0 && records.len == 0);
        return true;
    }
    assert(sessions.len >= activities.len && records.len > 0);

    size_t activity_ix = 0;
    size_t session_ix = 0;
    size_t lap_ix = 0;
    size_t record_ix = 0;
    do {
        activity_t *a = get_entity(activity, repo, activities.items[activity_ix].entity_id);
        session_t  *s = (session_ix < sessions.len) ? get_entity(session, repo, sessions.items[session_ix].entity_id) : NULL;
        while (s != NULL && (s->activity_id.db_id.value == activities.items[activity_ix].db_id.value)) {
            s->activity_id = activities.items[activity_ix];
            dynarr_append(&a->sessions, sessions.items[session_ix].entity_id);
            if (s->route_area.ok) {
                s->atlas = atlas_for_box(s->route_area.value, 3, 3);
            }

            size_t lap_offset = lap_ix;
            lap_t *l = (lap_ix < laps.len) ? get_entity(lap, repo, laps.items[lap_ix].entity_id) : NULL;
            while (l != NULL && (l->session_id.db_id.value == sessions.items[session_ix].db_id.value)) {
                l->session_id = sessions.items[session_ix];
                dynarr_append(&s->laps, laps.items[lap_ix].entity_id);
                ++lap_ix;
                l = (lap_ix < laps.len) ? get_entity(lap, repo, laps.items[lap_ix].entity_id) : NULL;
            }

            record_t *r = (record_ix < records.len) ? get_entity(record, repo, records.items[record_ix].entity_id) : NULL;
            while (r != NULL && r->timestamp < s->start_time) {
                ++record_ix;
                r = (record_ix < records.len) ? get_entity(record, repo, records.items[record_ix].entity_id) : NULL;
            }
            while (r != NULL && (r->session_id.db_id.value == sessions.items[session_ix].db_id.value)) {
                r->session_id = sessions.items[session_ix];
                dynarr_append(&s->records, records.items[record_ix].entity_id);

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
        }
        ++activity_ix;
    } while (activity_ix < activities.len);

    dynarr_free(&activities);
    dynarr_free(&sessions);
    dynarr_free(&laps);
    dynarr_free(&records);
    return true;
}
