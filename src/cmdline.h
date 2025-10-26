/*
 * Copyright (c) 2015, 2025 Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifdef CMDLINE_TEST
#define SLICE_IMPLEMENTATION
#define DA_IMPLEMENTATION
#define CMDLINE_IMPLEMENTATION
#endif

#ifndef __CMDLINE_H__
#define __CMDLINE_H__

#include "da.h"
#include "slice.h"

typedef enum _cmdline_option_type {
    COT_Boolean = 0x00,
    COT_String = 0x01,
    COT_Int = 0x02,
} cmdline_option_type_t;

typedef enum _cmdline_option_cardinality {
    COC_Set = 0,
    COC_Single,
    COC_Multiple,
} cmdline_option_cardinality_t;

typedef struct _cmdline_option_value {
    cmdline_option_type_t type;
    union {
        slice_t  str_value;
        uint64_t int_value;
    };
} cmdline_option_value_t;

typedef DA(cmdline_option_value_t) cmdline_option_values_t;

typedef struct _cmdline_option_def {
    char                         option;
    char                        *longopt;
    char                        *description;
    bool                         value_required;
    cmdline_option_cardinality_t cardinality;
    cmdline_option_type_t        type;
} cmdline_option_def_t;

typedef struct _cmdline_option {
    cmdline_option_def_t *opt_def;
    union {
        slice_t                 str_value;
        uint64_t                int_value;
        bool                    bool_value;
        cmdline_option_values_t values;
    };
} cmdline_option_t;

typedef DA(cmdline_option_t) cmdline_options_t;

typedef struct _app_description {
    char                *name;
    char                *shortdescr;
    char                *description;
    char                *legal;
    cmdline_option_def_t options[];
} app_description_t;

typedef struct _cmdline {
    app_description_t *descr;
    int                argc;
    slices_t           argv;
    slice_t            executable;
    slices_t           errors;
    cmdline_options_t  option_values;
    slices_t           arguments;
} cmdline_t;

cmdline_t cmdline_parse_args(app_description_t *descr, int argc, char const **argv);

void     parse_cmdline_args(app_description_t *descr, int argc, char const **argv);
slice_t  cmdline_value(char *opt);
bool     cmdline_is_set(char *opt);
slices_t cmdline_arguments();

#endif /* __CMDLINE_H__ */

#ifdef CMDLINE_IMPLEMENTATION
#ifndef CMDLINE_IMPLEMENTED

static void help(cmdline_t *cmdline)
{
    if (cmdline->descr->name) {
        fprintf(stderr, "%s", cmdline->descr->name);
        if (cmdline->descr->shortdescr) {
            fprintf(stderr, " - %s", cmdline->descr->shortdescr);
        }
        fprintf(stderr, "\n\n");
    } else {
        fprintf(stderr, SL "\n\n", SLARG(cmdline->executable));
    }
    if (cmdline->descr->description) {
        fprintf(stderr, "%s\n\n", cmdline->descr->description);
    }
    if (cmdline->descr->legal) {
        fprintf(stderr, "%s\n\n", cmdline->descr->legal);
    }
    for (cmdline_option_def_t *optdef = cmdline->descr->options; optdef->longopt != NULL; ++optdef) {
        fprintf(stderr, "\t--%s", optdef->longopt);
        if (optdef->option != 0) {
            fprintf(stderr, ", -%c", optdef->option);
        }
        if (optdef->description) {
            fprintf(stderr, "\t%s", optdef->description);
        }
        fprintf(stderr, "\n");
    }
    fprintf(stderr,
        "\t--help\tThis message\n\n");
    exit(1);
}

cmdline_option_def_t *find_longopt(cmdline_t *cmdline, char const *opt)
{
    for (cmdline_option_def_t *optdef = cmdline->descr->options; optdef->longopt != NULL; ++optdef) {
        if (strcmp(optdef->longopt, opt) == 0) {
            return optdef;
        }
    }
    return NULL;
}

cmdline_option_def_t *find_shortopt(cmdline_t *cmdline, char opt)
{
    for (cmdline_option_def_t *optdef = cmdline->descr->options; optdef->longopt != NULL; ++optdef) {
        if (optdef->option == opt) {
            return optdef;
        }
    }
    return NULL;
}

int parse_option(cmdline_t *cmdline, cmdline_option_def_t *opt, int ix)
{
    slice_t arg = cmdline->argv.items[ix];

    cmdline_option_t *val = NULL;
    for (size_t ix = 0; ix < cmdline->option_values.len; ++ix) {
        if (cmdline->option_values.items[ix].opt_def == opt) {
            if (opt->cardinality != COC_Multiple) {
                dynarr_append(&cmdline->errors, sb_as_slice(sb_format("Option '--%s' is allowed only one time", opt->longopt)));
                return ix;
            }
            val = cmdline->option_values.items + ix;
            break;
        }
    }
    if (val == NULL) {
        dynarr_append_s(
            cmdline_option_t,
            &cmdline->option_values,
            .opt_def = opt);
        val = dynarr_back(&cmdline->option_values);
    }

    if ((ix == (cmdline->argc - 1)) ||                     /* Option is last arg OR                 */
        ((arg.len > 2) && (arg.items[1] != '-')) ||        /* Arg is a sequence of short options OR */
        ((cmdline->argv.items[ix + 1]).items[0] == '-') || /* Next arg is an option OR              */
        (opt->cardinality == COC_Set)) {                   /* Option doesn't allow args             */
        val->bool_value = true;
        return ix;
    }

    if ((opt->value_required) &&                              /* Option value required AND */
        ((ix == (cmdline->argc - 1)) ||                       /* 1. Option is last argument OR */
            (cmdline->argv.items[ix + 1].items[0] == '-'))) { /* 2. Next argument is another option */
        dynarr_append(&cmdline->errors, sb_as_slice(sb_format("Option '--%s' requires an argument", opt->longopt)));
        return ix;
    }

    switch (opt->cardinality) {
    case COC_Multiple: {
        for (++ix; (ix < cmdline->argc) && (cmdline->argv.items[ix].items[0] != '-'); ++ix) {
            // FIXME Convert to int
            dynarr_append_s(
                cmdline_option_value_t,
                &val->values,
                .type = opt->type,
                .str_value = cmdline->argv.items[ix]);
        }
        --ix;
    } break;
    case COC_Set: {
        val->bool_value = true;
    } break;
    case COC_Single:
        val->str_value = cmdline->argv.items[++ix];
        break;
    default:
        UNREACHABLE();
    }
    return ix;
}

cmdline_t cmdline_parse_args(app_description_t *descr, int argc, char const **argv)
{
    cmdline_t             ret = { 0 };
    cmdline_option_def_t *opt;

    ret.descr = descr;
    ret.argc = argc;

    for (int ix = 0; ix < ret.argc; ix++) {
        dynarr_append(&ret.argv, slice_make((char *) argv[ix], strlen(argv[ix])));
    }

    ret.executable = ret.argv.items[0];

    int ix = 1;
    for (ix = 1; ix < ret.argc; ++ix) {
        char const *arg = argv[ix];
        if (!strcmp(arg, "--help")) {
            help(&ret);
            //        } else if (!strcmp(arg, "--debug") || !strcmp(arg, "-d")) {
            //            if (ix < (app->argc - 1)) {
            //                _app_debug(app, argv[++ix]);
            //            } else {
            //                app->error = data_exception(ErrorCommandLine,
            //                    "Option '--debug' requires an argument");
            //            }
            //        } else if (!strcmp(arg, "--loglevel") || !strncmp(arg, "-v", 2)) {
            //            if (ix < (app->argc - 1)) {
            //                logging_set_level(argv[++ix]);
            //            } else {
            //                app->error = data_exception(ErrorCommandLine,
            //                    "Option '--loglevel' requires an argument");
            //            }
            //        } else if (!strcmp(arg, "--logfile")) {
            //            if (ix < (app->argc - 1)) {
            //                logging_set_file(argv[++ix]);
            //            } else {
            //                app->error = data_exception(ErrorCommandLine,
            //                    "Option '--logfile' requires an argument");
            //            }
        } else if ((strlen(arg) > 1) && (arg[0] == '-')) {
            if ((strlen(arg) > 2) && (arg[1] == '-')) {
                opt = find_longopt(&ret, arg + 2);
                if (!opt) {
                    dynarr_append(&ret.errors, sb_as_slice(sb_format("Unrecognized option `%s`", arg)));
                    continue;
                } else {
                    ix = parse_option(&ret, opt, ix);
                }
            } else if ((strlen(arg) == 2) && (arg[1] == '-')) {
                ix++;
                break;
            } else {
                for (size_t ixx = 1; ixx < strlen(arg); ++ixx) {
                    opt = find_shortopt(&ret, arg[ixx]);
                    if (!opt) {
                        dynarr_append(&ret.errors, sb_as_slice(sb_format("Unrecognized option `-%c`", arg[ixx])));
                    } else if ((strlen(arg) > 2) && opt->value_required) {
                        dynarr_append(&ret.errors, sb_as_slice(sb_format("Short option '-%c' requires an argument", arg[ixx])));
                    } else {
                        ix = parse_option(&ret, opt, ix);
                    }
                }
            }
        } else {
            break;
        }
    }

    if (ret.errors.len > 0) {
        for (size_t err_ix = 0; err_ix < ret.errors.len; ++err_ix) {
            fprintf(stderr, "Error: " SL "\n", SLARG(ret.errors.items[err_ix]));
        }
        exit(1);
    }

    for (; ix < ret.argc; ++ix) {
        dynarr_append(&ret.arguments, ret.argv.items[ix]);
    }

    return ret;
}

static cmdline_t _cmdline_args = { 0 };

void parse_cmdline_args(app_description_t *descr, int argc, char const **argv)
{
    _cmdline_args = cmdline_parse_args(descr, argc, argv);
}

slice_t cmdline_value(char *opt)
{
    for (size_t ix = 0; ix < _cmdline_args.option_values.len; ++ix) {
        cmdline_option_t *optval = _cmdline_args.option_values.items + ix;
        if (strcmp(optval->opt_def->longopt, opt) == 0) {
            if (optval->opt_def->cardinality == COC_Single) {
                return optval->str_value;
            } else {
                fprintf(stderr, "Command line option `--%s` does not take single arguments\n", opt);
                abort();
            }
        }
    }
    return slice_make(NULL, 0);
}

bool cmdline_is_set(char *opt)
{
    for (size_t ix = 0; ix < _cmdline_args.option_values.len; ++ix) {
        cmdline_option_t *optval = _cmdline_args.option_values.items + ix;
        if (strcmp(optval->opt_def->longopt, opt) == 0) {
            if (optval->opt_def->cardinality == COC_Set) {
                return optval->bool_value;
            } else {
                fprintf(stderr, "Command line option `--%s` is not a setter\n", opt);
                abort();
            }
        }
    }
    return false;
}

slices_t cmdline_arguments()
{
    return _cmdline_args.arguments;
}

#define CMDLINE_IMPLEMENTED
#endif /* !CMDLINE_IMPLEMENTED */

#endif /* CMDLINE_IMPLEMENTATION */

#ifdef CMDLINE_TEST

static app_description_t app_descr = {
    .name = "cmdline_test",
    .shortdescr = "Testing cmdline",
    .description = "Tests the awesome cmdline library\n"
                   "Cool huh?\n",
    .legal = "(c) finiandarcy.com",
    .options = {
        {
            .option = 'x',
            .longopt = "longx",
            .description = "The x option",
            .value_required = true,
            .cardinality = COC_Single,
            .type = COT_String,
        },
        {
            .option = 'y',
            .longopt = "longy",
            .description = "The y option",
            .value_required = false,
            .cardinality = COC_Set,
            .type = COT_Boolean,
        },
        { 0 } }
};

int main(int argc, char const **argv)
{
    cmdline_t args = cmdline_parse_args(&app_descr, argc, argv);
    assert(args.option_values.len == 0);

    char const *x_argv[] = {
        "cmdline",
        "-x",
        "x_value",
    };
    args = cmdline_parse_args(&app_descr, 3, x_argv);
    assert(args.option_values.len == 1);
    assert(args.option_values.items[0].opt_def->option == 'x');
    slice_t val = args.option_values.items[0].str_value;
    assert(slice_eq(val, C("x_value")));

    char const *y_argv[] = {
        "cmdline",
        "-y",
    };
    args = cmdline_parse_args(&app_descr, 3, y_argv);
    assert(args.option_values.len == 1);
    assert(args.option_values.items[0].opt_def->option == 'y');
    assert(args.option_values.items[0].bool_value);
}

#endif
