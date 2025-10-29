/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef ZORRO_TYPES_ONLY
#include <libpq-fe.h>
#endif /* ZORRO_TYPES_ONLY */

#include "da.h"
#include "slice.h"

#ifndef __ZORRO_H__
#define __ZORRO_H__

typedef opt_int serial;

#define SQLTYPES(S)                     \
    S(Int32, integer, int32_t)          \
    S(Serial, serial, serial)           \
    S(UInt32, integer, uint32_t)        \
    S(Float, real, float)               \
    S(Double, double precision, double) \
    S(String, text, slice_t)            \
    S(Point, point, Vector2)            \
    S(Box, box, box_t)

typedef enum _sql_type {
#undef S
#define S(T, Sql, C) SQLType_##T,
    SQLTYPES(S)
#undef S
} sql_type_t;

OPTDEF(sql_type_t);

#define SQLTYPEKINDS(S) \
    S(Builtin)          \
    S(Composite)        \
    S(Reference)

typedef enum _sql_type_kind {
#undef S
#define S(K) SQLTypeKind_##K,
    SQLTYPEKINDS(S)
#undef S
} sql_type_kind_t;

typedef enum _cardinality {
    Card_OneToMany,
    Card_ManyToOne,
    Card_ManyToMany,
} cardinality_t;

typedef struct _column_def {
    bool            optional;
    slice_t         name;
    sql_type_kind_t kind;
    union {
        sql_type_t type;
        slice_t    composite;
        struct {
            cardinality_t cardinality;
            slice_t       references;
            slice_t       fk_col;
        } reference;
    };
} column_def_t;

typedef DA(column_def_t) column_defs_t;

typedef struct _table_def {
    slice_t       name;
    column_defs_t columns;
} table_def_t;

typedef DA(table_def_t) table_defs_t;

#ifndef ZORRO_TYPES_ONLY

typedef struct _db_result {
    PGresult *res;
} db_result_t;

typedef DA(db_result_t) db_results_t;

typedef struct _db {
    table_defs_t schema;
    PGconn      *conn;
    db_results_t results;
} db_t;

OPTDEF(db_result_t);

slice_t        sql_type_kind_name(sql_type_kind_t kind);
slice_t        sql_type_name(sql_type_t t);
slice_t        sql_type_sql(sql_type_t t);
opt_sql_type_t sql_type_from_c_type(slice_t c_type);
db_t           db_make(slice_t dbname, slice_t user, slice_t passwd, slice_t hostname, int port);
bool           db_exec(db_t *db, char const *sql);
nodeptr        db_query(db_t *db, char const *sql);
void           db_close(db_t *db);
void           db_result_close(db_t *db, nodeptr result);
#define pg_result(db, result) ((db)->results.items[(result).value].res)
#define pg_status(db, result) PQresultStatus(pg_result((db), (result)))

#endif /* ZORRO_TYPES_ONLY */

#endif /* __ZORRO_H__ */

#if defined(ZORRO_IMPLEMENTATION) || defined(JDV_IMPLEMENTATION)
#ifndef ZORRO_IMPLEMENTED
#define ZORRO_IMPLEMENTED

slice_t sql_type_kind_name(sql_type_kind_t kind)
{
    switch (kind) {
#undef S
#define S(K)              \
    case SQLTypeKind_##K: \
        return C(#K);
        SQLTYPEKINDS(S)
#undef S
    default:
        UNREACHABLE();
    }
}

slice_t sql_type_name(sql_type_t t)
{
    switch (t) {
#undef S
#define S(T, Sql, CType) \
    case SQLType_##T:    \
        return C(#T);
        SQLTYPES(S)
#undef S
    default:
        UNREACHABLE();
    }
}

slice_t sql_type_sql(sql_type_t t)
{
    switch (t) {
#undef S
#define S(T, Sql, CType) \
    case SQLType_##T:    \
        return C(#Sql);
        SQLTYPES(S)
#undef S
    default:
        UNREACHABLE();
    }
}

opt_sql_type_t sql_type_from_c_type(slice_t c_type)
{
#undef S
#define S(T, Sql, CType)                        \
    if (slice_eq(c_type, C(#CType))) {          \
        return OPTVAL(sql_type_t, SQLType_##T); \
    }
    SQLTYPES(S)
#undef S
    return OPTNULL(sql_type_t);
}

#ifndef ZORRO_TYPES_ONLY

bool db_exec(db_t *db, char const *sql)
{
    nodeptr res = db_query(db, sql);
    if (!res.ok) {
        return false;
    }
    ExecStatusType result_status = pg_status(db, res);
    bool           ret = result_status == PGRES_COMMAND_OK
        || result_status == PGRES_TUPLES_OK
        || result_status == PGRES_SINGLE_TUPLE;
    db_result_close(db, res);
    return ret;
}

nodeptr db_query(db_t *db, char const *sql)
{
    assert(db->conn != NULL);
    nodeptr ret = nullptr;
    dynarr_foreach(db_result_t, r, &db->results)
    {
        if (r->res == NULL) {
            ret = nodeptr_ptr(r - db->results.items);
            break;
        }
    }
    if (!ret.ok) {
        dynarr_append(&db->results, (db_result_t) { 0 });
        ret = nodeptr_ptr(db->results.len - 1);
    }
    db->results.items[ret.value].res = PQexec(db->conn, sql);
    return ret;
}

db_t db_make(slice_t dbname, slice_t user, slice_t passwd, slice_t hostname, int port)
{
    db_t ret = { 0 };

    size_t      cp = temp_save();
    char const *conninfo;
    if (passwd.len == 0) {
        conninfo = temp_sprintf(
            "dbname=" SL " user=" SL " host=" SL " port=%d",
            SLARG(dbname), SLARG(user), SLARG(hostname), port);
    } else {
        conninfo = temp_sprintf(
            "dbname=" SL " user=" SL "passwd=" SL " host=" SL " port=%d",
            SLARG(dbname), SLARG(user), SLARG(passwd), SLARG(hostname), port);
    }

    // Establish a connection to the PostgreSQL database
    ret.conn = PQconnectdb(conninfo);

    // Check if the connection was successful
    if (PQstatus(ret.conn) != CONNECTION_OK) {
        PQfinish(ret.conn);
        fatal("Connection to database failed: %s\n", PQerrorMessage(ret.conn));
    }
    temp_rewind(cp);
    return ret;
}

void db_close(db_t *db)
{
    if (db->conn != NULL) {
        while (db->results.len > 0) {
            db_result_close(db, nodeptr_ptr(db->results.len - 1));
        }
        PQfinish(db->conn);
        db->conn = NULL;
    }
}

void db_result_close(db_t *db, nodeptr res)
{
    assert(res.ok && res.value < db->results.len);
    db_result_t result = db->results.items[res.value];
    if (result.res != NULL) {
        PQclear(result.res);
        result.res = NULL;
    }
    (void) dynarr_remove_unordered(db_result_t, &db->results, res.value);
}

#endif /* ZORRO_TYPES_ONLY */

#endif /* ZORRO_IMPLEMENTED */
#endif /* ZORRO_IMPLEMENTATION */
