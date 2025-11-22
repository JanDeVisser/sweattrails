/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <ctype.h>
#define SLICE_IMPLEMENTATION
#define DA_IMPLEMENTATION
#define FS_IMPLEMENTATION
#define IO_IMPLEMENTATION
#define JSON_IMPLEMENTATION
#define ZORRO_IMPLEMENTATION
#define ZORRO_TYPES_ONLY

#include "da.h"
#include "fs.h"
#include "io.h"
#include "json.h"
#include "slice.h"
#include "zorro.h"

typedef struct _schema_column_def {
    slice_t         name;
    bool            transient;
    bool            optional;
    sql_type_kind_t kind;
    bool            pk;
    union {
        slice_t type;
        struct {
            cardinality_t cardinality;
            slice_t       references;
            slice_t       fk_col;
        } reference;
    };
} schema_column_def_t;

typedef DA(schema_column_def_t) schema_column_defs_t;

typedef struct _schema_type_def {
    slice_t         sql_type;
    slice_t         c_type;
    sql_type_kind_t kind;
    union {
        schema_column_defs_t composite;
    };
} schema_type_def_t;

typedef struct _schema_table_def {
    slice_t              name;
    schema_column_defs_t columns;
    slices_t             indexes;
    bool                 no_id;
} schema_table_def_t;

static slice_t  schema_name = { 0 };
static slices_t schema_includes = { 0 };
static DA(schema_type_def_t) schema_types = { 0 };
static DA(schema_table_def_t) schema_tables = { 0 };

void parse_includes(json_t *json, nodeptr includesptr)
{
    json_value_t *includes = json_get_array(json, includesptr);
    if (includes == NULL) {
        fatal("Schema `includes` value must be a JSON array");
    }
    dynarr_foreach(nodeptr, ptr, &includes->array)
    {
        slice_t inc = json_get_string(json, *ptr);
        if (inc.len > 0) {
            dynarr_append(&schema_includes, inc);
        }
    }
}

void parse_types(json_t *json, nodeptr typesptr)
{
    json_value_t *types = json_get_array(json, typesptr);
    if (types == NULL) {
        fatal("Schema `types` value must be a JSON array");
    }
    dynarr_foreach(nodeptr, ptr, &types->array)
    {
        json_value_t *type = json_get_object(json, *ptr);
        if (type == NULL) {
            fatal("Entry of schema `types` array must be object");
        }
        schema_type_def_t t = { .kind = SQLTypeKind_Builtin };
        dynarr_foreach(json_attrib_t, attr, &type->object)
        {
            if (slice_eq(attr->key, C("sql_type"))) {
                t.sql_type = json_get_string(json, attr->value);
                continue;
            }
            if (slice_eq(attr->key, C("c_type"))) {
                t.c_type = json_get_string(json, attr->value);
                continue;
            }
            if (slice_eq(attr->key, C("composite"))) {
                t.kind = SQLTypeKind_Composite;
                json_value_t *columns = json_get_array(json, attr->value);
                if (columns == NULL) {
                    fatal("Schema type `composite` value must be JSON array");
                }
                dynarr_foreach(nodeptr, colptr, &columns->array)
                {
                    json_value_t *column = json_get_object(json, *colptr);
                    if (column == NULL) {
                        fatal("Schema type `composite` array entry must be JSON object");
                    }
                    schema_column_def_t c = { 0 };
                    dynarr_foreach(json_attrib_t, colattr, &column->object)
                    {
                        if (slice_eq(colattr->key, C("name"))) {
                            c.name = json_get_string(json, colattr->value);
                            continue;
                        }
                        if (slice_eq(colattr->key, C("type"))) {
                            c.type = json_get_string(json, colattr->value);
                            continue;
                        }
                    }
                    if (c.name.len == 0 || c.type.len == 0) {
                        fatal("Schema type composite columns must have a name and a type");
                    }
                    dynarr_append(&t.composite, c);
                }
            }
        }
        if (t.sql_type.len == 0 || t.c_type.len == 0) {
            fatal("Schema composite types must have a name and a c-type");
        }
        dynarr_append(&schema_types, t);
    }
}

slice_t get_sql_type_for_c_type(slice_t c_type)
{
    opt_sql_type_t s = sql_type_from_c_type(c_type);
    if (s.ok) {
        return sql_type_sql(s.value);
    }
    dynarr_foreach(schema_type_def_t, typ, &schema_types)
    {
        if (slice_eq(typ->c_type, c_type)) {
            char *t = temp_sprintf(SL "." SL, SLARG(schema_name), SLARG(typ->sql_type));
            return C(t);
        }
    }
    fatal("Could not determine SQL type for C type `" SL "`", SLARG(c_type));
}

schema_type_def_t *find_type(slice_t c_type)
{
    dynarr_foreach(schema_type_def_t, typ, &schema_types)
    {
        if (slice_eq(typ->c_type, c_type)) {
            return typ;
        }
    }
    return NULL;
}

void parse_tables(json_t *json, nodeptr tablesptr)
{
    json_value_t *tables = json_get_array(json, tablesptr);
    if (tables == NULL) {
        fatal("Schema `tables` value must be a JSON array");
    }
    dynarr_foreach(nodeptr, ptr, &tables->array)
    {
        json_value_t *table = json_get_object(json, *ptr);
        if (table == NULL) {
            fatal("Entry of schema `tables` array must be object");
        }
        schema_table_def_t t = { 0 };
        dynarr_foreach(json_attrib_t, attr, &table->object)
        {
            if (slice_eq(attr->key, C("name"))) {
                t.name = json_get_string(json, attr->value);
                continue;
            }
            if (slice_eq(attr->key, C("no-id"))) {
                t.no_id = true;
                continue;
            }
            if (slice_eq(attr->key, C("columns"))) {
                json_value_t *columns = json_get_array(json, attr->value);
                if (columns == NULL) {
                    fatal("Schema type `columns` value must be JSON array");
                }
                dynarr_foreach(nodeptr, colptr, &columns->array)
                {
                    json_value_t *column = json_get_object(json, *colptr);
                    if (column == NULL) {
                        fatal("Schema type `column` array entry must be JSON object");
                    }
                    schema_column_def_t c = { 0 };
                    dynarr_foreach(json_attrib_t, colattr, &column->object)
                    {
                        if (slice_eq(colattr->key, C("name"))) {
                            c.name = json_get_string(json, colattr->value);
                            continue;
                        }
                        if (slice_eq(colattr->key, C("ref"))) {
                            c.kind = SQLTypeKind_Reference;
                            c.reference.references = json_get_string(json, colattr->value);
                        }
                        if (slice_eq(colattr->key, C("cardinality"))) {
                            c.kind = SQLTypeKind_Reference;
                            slice_t cardinality = json_get_string(json, colattr->value);
                            if (slice_eq(cardinality, C("n-1"))) {
                                c.reference.cardinality = Card_ManyToOne;
                            } else if (slice_eq(cardinality, C("1-n"))) {
                                c.reference.cardinality = Card_OneToMany;
                            } else if (slice_eq(cardinality, C("*"))) {
                                c.reference.cardinality = Card_ManyToMany;
                            } else {
                                fatal("Invalid cardinality value `" SL "`", SLARG(cardinality));
                            }
                        }
                        if (slice_eq(colattr->key, C("fk_column"))) {
                            c.kind = SQLTypeKind_Reference;
                            c.reference.fk_col = json_get_string(json, colattr->value);
                        }
                        if (slice_eq(colattr->key, C("type"))) {
                            c.type = json_get_string(json, colattr->value);
                            schema_type_def_t *type_def = find_type(c.type);
                            if (type_def == NULL) {
                                c.kind = SQLTypeKind_Builtin;
                            } else {
                                c.kind = type_def->kind;
                            }
                        }
                        if (slice_eq(colattr->key, C("transient"))) {
                            c.transient = true;
                            continue;
                        }
                        if (slice_eq(colattr->key, C("optional"))) {
                            c.optional = true;
                            continue;
                        }
                        if (slice_eq(colattr->key, C("pk"))) {
                            c.pk = true;
                            continue;
                        }
                        if (slice_eq(colattr->key, C("indexed"))) {
                            assert(c.name.len > 0);
                            dynarr_append(&t.indexes, c.name);
                        }
                    }
                    if (c.name.len == 0) {
                        fatal("Schema columns must have a name");
                    }
                    if (c.kind == SQLTypeKind_Builtin && (c.name.len == 0 || c.type.len == 0)) {
                        fatal("Schema columns must have a type");
                    }
                    if (c.kind == SQLTypeKind_Reference && c.reference.references.len == 0) {
                        fatal("Schema reference columns must have referenced type");
                    }
                    dynarr_append(&t.columns, c);
                }
            }
        }
        dynarr_append(&schema_tables, t);
    }
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: schemagen <schema json file> <schema header file>\n");
        return 1;
    }
    slice_t              text = sb_as_slice(MUSTOPT(sb_t, slurp_file(slice_from_cstr(argv[1]))));
    json_decode_result_t res = json_decode(text);
    if (!res.ok) {
        fatal("JSON parse failed: %d:%d: " SL, res.error.line, res.error.column, SLARG(res.error.error));
    }
    json_t        json = res.success;
    json_value_t *root = json.values.items + json.root.value;
    if (root->type != JT_Object) {
        fatal("Schema root must be JSON object");
    }
    dynarr_foreach(json_attrib_t, attr, &root->object)
    {
        if (slice_eq(C("name"), attr->key)) {
            schema_name = json_get_string(&json, attr->value);
            continue;
        }
        if (slice_eq(C("includes"), attr->key)) {
            parse_includes(&json, attr->value);
            continue;
        }
        if (slice_eq(C("types"), attr->key)) {
            parse_types(&json, attr->value);
            continue;
        }
        if (slice_eq(C("tables"), attr->key)) {
            parse_tables(&json, attr->value);
            continue;
        }
    }
    path_t p = path_make_relative(argv[2]);
    path_replace_extension(&p, C("h"));
    slice_t basename = path_basename(&p);
    sb_t    basename_upper = { 0 };
    slice_t func_prefix = schema_name;
    if (func_prefix.len == 0) {
        func_prefix = basename;
    }

    for (size_t ix = 0; ix < basename.len; ++ix) {
        sb_append_char(&basename_upper, toupper(basename.items[ix]));
    }
    sb_t out = sb_format(
        "/*\n"
        " * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>\n"
        " *\n"
        " * G E N E R A T E D  C O D E.  M O D I F Y  A T  Y O U R  P E R I L\n"
        " *\n"
        " * SPDX-License-Identifier: MIT\n"
        " */\n\n"
        "#ifndef __" SL "_H__\n"
        "#define __" SL "_H__\n\n"
        "#include <raylib.h>\n"
        "#include \"zorro.h\"\n",
        SLARG(basename_upper), SLARG(basename_upper));

    dynarr_foreach(slice_t, inc, &schema_includes)
    {
        sb_append_cstr(&out, "#include ");
        if (inc->items[0] != '<') {
            sb_append_char(&out, '"');
        }
        sb_append(&out, *inc);
        if (inc->items[0] != '<') {
            sb_append_char(&out, '"');
        }
        sb_append_char(&out, '\n');
    }
    sb_append_char(&out, '\n');

    dynarr_foreach(schema_table_def_t, tbl, &schema_tables)
    {
        sb_append_cstr(&out, "#define ");
        for (size_t ix = 0; ix < tbl->name.len; ++ix) {
            sb_append_char(&out, toupper(tbl->name.items[ix]));
        }
        sb_printf(&out, "_DEF %zu\n", tbl - schema_tables.items);
    }
    sb_append_char(&out, '\n');

    sb_printf(&out, "typedef enum _entity_type {\n");
    dynarr_foreach(schema_table_def_t, tbl, &schema_tables)
    {
        sb_printf(&out, "    EntityType_" SL ",\n", SLARG(tbl->name));
    }
    sb_append_cstr(&out,
        "} entity_type_t;\n\n"
        "typedef struct _dummy_entity {\n"
        "    serial id;\n"
        "} dummy_entity_t;\n\n");

    dynarr_foreach(schema_table_def_t, tbl, &schema_tables)
    {
        sb_printf(&out, "typedef struct _" SL " {\n", SLARG(tbl->name));
        dynarr_foreach(schema_column_def_t, col, &tbl->columns)
        {
            switch (col->kind) {
            case SQLTypeKind_Builtin:
            case SQLTypeKind_Composite:
                sb_append_cstr(&out, "    ");
                if (col->optional) {
                    sb_append_cstr(&out, "opt_");
                }
                sb_printf(&out, SL " " SL ";\n", SLARG(col->type), SLARG(col->name));
                break;
            case SQLTypeKind_Reference:
                switch (col->reference.cardinality) {
                case Card_ManyToOne:
                    sb_printf(&out, "    ref_t");
                    break;
                case Card_OneToMany:
                    sb_printf(&out, "    nodeptrs");
                    break;
                default:
                    UNREACHABLE();
                }
                sb_printf(&out, " " SL ";\n", SLARG(col->name));
                break;
            default:
                UNREACHABLE();
            }
        }
        sb_printf(&out,
            "} " SL "_t;\n\n"
            "typedef DA(" SL "_t) " SL "s_t;\n\n",
            SLARG(tbl->name), SLARG(tbl->name), SLARG(tbl->name));
    }
    sb_append_cstr(
        &out,
        "typedef struct _entity {\n"
        "    entity_type_t type;\n"
        "    unsigned int hash;\n"
        "    union {\n"
        "        dummy_entity_t dummy_entity;\n");
    dynarr_foreach(schema_table_def_t, tbl, &schema_tables)
    {
        sb_printf(&out, "        " SL "_t " SL ";\n", SLARG(tbl->name), SLARG(tbl->name));
    }
    sb_append_cstr(&out, "    };\n} entity_t;\n\n"
                         "typedef DA(entity_t) entities_t;\n\n");
    sb_append_cstr(&out,
        "typedef struct _repo {\n"
        "    entities_t entities;\n");
    dynarr_foreach(schema_table_def_t, tbl, &schema_tables)
    {
        sb_printf(&out, "    nodeptrs " SL "s;\n", SLARG(tbl->name));
    }
    sb_append_cstr(
        &out,
        "} repo_t;\n\n"
        "#include \"schema_common.h\"\n\n");

    sb_printf(&out,
        "void " SL "_init_schema(db_t *db);\n\n"
        "#endif /* __" SL "_H__ */\n\n"
        "#ifdef " SL "_IMPLEMENTATION\n"
        "#ifndef " SL "_IMPLEMENTED\n\n"
        "#include \"schema_impl.h\"\n\n"
        "void " SL "_init_schema(db_t *db)\n"
        "{\n",

        SLARG(func_prefix),
        SLARG(basename_upper), SLARG(basename_upper), SLARG(basename_upper), SLARG(func_prefix));

    if (schema_name.len != 0) {
        sb_printf(&out,
            "    if (!db_exec(db, \"set search_path to " SL "\")) {\n"
            "        PQfinish(db->conn);\n"
            "        fatal(\"Could not set database search path: %%s\", PQerrorMessage(db->conn));\n"
            "    };\n\n"
            "    db->schema.schema = C(\"" SL "\");\n",
            SLARG(schema_name), SLARG(schema_name));
    }

    dynarr_foreach(schema_type_def_t, typ, &schema_types)
    {
        sb_printf(&out,
            "    {\n"
            "        type_def_t type = {\n"
            "            .name = C(\"" SL "\"),\n"
            "            .c_type = C(\"" SL "\"),\n"
            "            .kind = SQLTypeKind_" SL ",\n"
            "        };\n",
            SLARG(typ->sql_type), SLARG(typ->c_type), SLARG(sql_type_kind_name(typ->kind)));
        switch (typ->kind) {
        case SQLTypeKind_Composite: {
            dynarr_foreach(schema_column_def_t, col, &typ->composite)
            {
                if (col->transient) {
                    continue;
                }
                sb_printf(&out,
                    "        dynarr_append_s(\n"
                    "            column_def_t,\n"
                    "            &type.composite,\n"
                    "            .name = C(\"" SL "\"),\n"
                    "            .kind = SQLTypeKind_" SL ",\n"
                    "            .offset = offsetof(" SL ", " SL "),\n",
                    SLARG(col->name), SLARG(sql_type_kind_name(col->kind)),
                    SLARG(typ->c_type), SLARG(col->name));
                switch (col->kind) {
                case SQLTypeKind_Builtin: {
                    if (col->transient) {
                        continue;
                    }
                    sql_type_t sql_type_code = MUSTOPT(sql_type_t, sql_type_from_c_type(col->type));
                    sb_printf(&out, "            .type = SQLType_" SL ");\n",
                        SLARG(sql_type_name(sql_type_code)));
                } break;
                case SQLTypeKind_Composite: {
                    sb_printf(&out,
                        "            .composite = schema_find_type_by_c_type(&db->schema, C(\"" SL "\")));\n",
                        SLARG(col->type));
                } break;
                case SQLTypeKind_Reference:
                    sb_printf(&out,
                        "           .reference.cardinality = " SL ",\n"
                        "           .reference.references = C(\"" SL "\"),\n"
                        "           .reference.fk_col = C(\"" SL "\")\n"
                        "            );\n",
                        SLARG(cardinality_name(col->reference.cardinality)),
                        SLARG(col->reference.references),
                        SLARG(col->reference.fk_col));
                    break;
                default:
                    UNREACHABLE();
                }
            }
        } break;
        default:
            break;
        }
        sb_printf(&out,
            "        dynarr_append(&db->schema.types, type);\n"
            "    }\n");
    }

    dynarr_foreach(schema_table_def_t, tbl, &schema_tables)
    {
        sb_printf(&out,
            "    {\n"
            "        table_def_t table = {.name = C(\"" SL "\")};\n",
            SLARG(tbl->name));
        dynarr_foreach(schema_column_def_t, col, &tbl->columns)
        {
            if (col->transient) {
                continue;
            }
            sb_printf(&out,
                "        dynarr_append_s(\n"
                "            column_def_t,\n"
                "            &table.columns,\n"
                "            .name = C(\"" SL "\"),\n"
                "            .offset = offsetof(" SL "_t, " SL "),\n",
                SLARG(col->name), SLARG(tbl->name), SLARG(col->name));
            if (!col->optional) {
                sb_printf(&out,
                    "            .kind = SQLTypeKind_" SL ",\n",
                    SLARG(sql_type_kind_name(col->kind)));
            } else {
                sb_append_cstr(&out,
                    "            .kind = SQLTypeKind_Optional,\n");
            }
            switch (col->kind) {
            case SQLTypeKind_Builtin: {
                if (col->transient) {
                    continue;
                }
                sql_type_t sql_type_code = MUSTOPT(sql_type_t, sql_type_from_c_type(col->type));
                if (col->optional) {
                    sb_printf(&out,
                        "            .optional = {\n"
                        "                .type = SQLType_" SL ",\n"
                        "                .value_offset = offsetof(opt_" SL ", value),\n"
                        "            });\n",
                        SLARG(sql_type_name(sql_type_code)), SLARG(col->type));
                } else {
                    sb_printf(&out, "            .type = SQLType_" SL ");\n",
                        SLARG(sql_type_name(sql_type_code)));
                }
            } break;
            case SQLTypeKind_Composite:
                sb_printf(&out,
                    "            .composite = schema_find_type_by_c_type(&db->schema, C(\"" SL "\")));\n",
                    SLARG(col->type));
                break;
            case SQLTypeKind_Reference:
                sb_printf(&out,
                    "           .reference.cardinality = " SL ",\n"
                    "           .reference.references = C(\"" SL "\"),\n"
                    "           .reference.reference_tag = EntityType_" SL ",\n"
                    "           .reference.fk_col = C(\"" SL "\")\n"
                    "            );\n",
                    SLARG(cardinality_name(col->reference.cardinality)),
                    SLARG(col->reference.references),
                    SLARG(col->reference.references),
                    SLARG(col->reference.fk_col));
                break;
            default:
                UNREACHABLE();
            }
        }
        sb_printf(&out,
            "        dynarr_append(&db->schema.tables, table);\n"
            "    }\n");
    }

    sb_printf(&out,
        "}\n\n"
        "#define " SL "_IMPLEMENTED\n"
        "#endif /* " SL "_IMPLEMENTED */\n"
        "#endif /* " SL "_IMPLEMENTATION */\n",
        SLARG(basename_upper), SLARG(basename_upper), SLARG(basename_upper));

    if (!write_file(sb_as_slice(p.path), sb_as_slice(out))) {
        fatal("Error writing schema file `" SL "`", SLARG(p.path));
    }

    sb_t sql = { 0 };
    sb_printf(&sql,
        "drop schema if exists " SL " cascade;\n"
        "create schema " SL ";\n"
        "set search_path to " SL ";\n\n",
        SLARG(schema_name), SLARG(schema_name), SLARG(schema_name));
    dynarr_foreach(schema_type_def_t, typ, &schema_types)
    {
        switch (typ->kind) {
        case SQLTypeKind_Composite: {
            sb_printf(&sql,
                "create type " SL "." SL " as (\n",
                SLARG(schema_name), SLARG(typ->sql_type));
            dynarr_foreach(schema_column_def_t, col, &typ->composite)
            {
                if (col != typ->composite.items) {
                    sb_append_cstr(&sql, ",\n");
                }
                sb_printf(&sql,
                    "    " SL " " SL,
                    SLARG(col->name), SLARG(get_sql_type_for_c_type(col->type)));
            }
            sb_append_cstr(&sql, "\n);\n\n");
        } break;
        default:
            break;
        }
    }

    dynarr_foreach(schema_table_def_t, tbl, &schema_tables)
    {
        sb_printf(&sql, "create table " SL "." SL "(\n", SLARG(schema_name), SLARG(tbl->name));
        bool first = true;
        bool has_pk = false;
        dynarr_foreach(schema_column_def_t, col, &tbl->columns)
        {
            if (col->transient) {
                continue;
            }
            if (col->kind == SQLTypeKind_Reference && col->reference.cardinality != Card_ManyToOne) {
                continue;
            }
            has_pk |= col->pk;
            if (!first) {
                sb_append_cstr(&sql, ",\n");
            }
            first = false;
            switch (col->kind) {
            case SQLTypeKind_Builtin:
            case SQLTypeKind_Composite:
                sb_printf(
                    &sql,
                    "    " SL " " SL,
                    SLARG(col->name), SLARG(get_sql_type_for_c_type(col->type)));
                break;
            case SQLTypeKind_Reference: {
                slice_t fk_col = (col->reference.fk_col.len > 0) ? col->reference.fk_col : C("id");
                sb_printf(
                    &sql,
                    "    " SL " int references " SL " ( " SL " )",
                    SLARG(col->name), SLARG(col->reference.references), SLARG(fk_col));
            } break;
            default:
                UNREACHABLE();
            }
        }
        if (has_pk) {
            sb_append_cstr(&sql, ",\n    primary key ( ");
            bool first = true;
            dynarr_foreach(schema_column_def_t, col, &tbl->columns)
            {
                if (!col->pk) {
                    continue;
                }
                if (!first) {
                    sb_append_cstr(&sql, ", ");
                }
                first = false;
                sb_append(&sql, col->name);
            }
            sb_append_cstr(&sql, " )");
        }
        sb_append_cstr(&sql, "\n);\n");
        dynarr_foreach(slice_t, idx, &tbl->indexes)
        {
            sb_printf(&sql, "create index on " SL "." SL " ( " SL " );\n",
                SLARG(schema_name), SLARG(tbl->name), SLARG(*idx));
        }
        sb_append_char(&sql, '\n');
    }

    path_replace_extension(&p, C("sql"));
    if (!write_file(sb_as_slice(p.path), sb_as_slice(sql))) {
        fatal("Error writing SQL schema`" SL "`", SLARG(p.path));
    }
}
