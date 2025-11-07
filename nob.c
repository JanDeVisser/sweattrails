
#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX

#include "nob.h"

Nob_Cmd cmd = { 0 };

#define FIT_DIR "fitsdk/"
#define BUILD_DIR "build/"
#define SRC_DIR "src/"
#define TEST_DIR "test/"

#define STB_HEADERS(S)  \
    S(slice, SLICE)     \
    S(da, DA)           \
    S(io, IO)           \
    S(lexer, LEXER)     \
    S(json, JSON)       \
    S(cmdline, CMDLINE) \
    S(fs, FS)           \
    S(process, PROCESS)

#define FIT_SOURCES(S) \
    S(fit)             \
    S(fit_convert)     \
    S(fit_crc)         \
    S(fit_example)     \
    S(fit_product)     \
    S(fit_ram)

#define APP_HEADERS(S) \
    S(map)             \
    S(sweattrails)

#define APP_SOURCES(S) \
    S(import)          \
    S(map)             \
    S(sweattrails)

int format_sources()
{
    cmd_append(&cmd, "clang-format", "-i", "nob.c");
    if (!cmd_run(&cmd)) {
        return 1;
    }
#undef S
#define S(HDR, NAME)                                           \
    cmd_append(&cmd, "clang-format", "-i", SRC_DIR #HDR ".h"); \
    if (!cmd_run(&cmd)) {                                      \
        return 1;                                              \
    }
    STB_HEADERS(S)
#undef S
#define S(SRC)                                                 \
    cmd_append(&cmd, "clang-format", "-i", SRC_DIR #SRC ".c"); \
    if (!cmd_run(&cmd)) {                                      \
        return 1;                                              \
    }
    APP_SOURCES(S)
#undef S
#define S(HDR)                                                 \
    cmd_append(&cmd, "clang-format", "-i", SRC_DIR #HDR ".h"); \
    if (!cmd_run(&cmd)) {                                      \
        return 1;                                              \
    }
    APP_HEADERS(S)
#undef S
    return 0;
}

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);

    bool        rebuild = false;
    char const *script = NULL;
    bool        run_all = true;
    bool        format = false;
    bool        build_displaymap = false;

    for (int ix = 1; ix < argc; ++ix) {
        if (strcmp(argv[ix], "-B") == 0) {
            rebuild = true;
        }
        if (strcmp(argv[ix], "format") == 0) {
            format = true;
        }
        if (strcmp(argv[ix], "map") == 0) {
            build_displaymap = true;
        }
    }

    if (format) {
        return format_sources();
    }

    if (!nob_file_exists("build")) {
        mkdir_if_not_exists("build");
    }
    char *cc = getenv("CC");
    if (cc == NULL) {
        cc = "cc";
    }

    bool fit_sources_updated = false;
#undef S
#define S(SRC)                                                                                              \
    if (rebuild || nob_needs_rebuild1(BUILD_DIR "libfit.a", FIT_DIR #SRC ".c")) {                           \
        cmd_append(&cmd, cc, "-Wall", "-Wextra", "-c", "-g", "-o", BUILD_DIR #SRC ".o", FIT_DIR #SRC ".c"); \
        if (!cmd_run(&cmd)) {                                                                               \
            return 1;                                                                                       \
        }                                                                                                   \
        fit_sources_updated = true;                                                                         \
    }
    FIT_SOURCES(S)
    if (fit_sources_updated) {
        cmd_append(&cmd, "ar", "r", BUILD_DIR "libfit.a",
#undef S
#define S(SRC) BUILD_DIR #SRC ".o",
            FIT_SOURCES(S));
        if (!cmd_run(&cmd)) {
            return 1;
        }
    }

    bool headers_updated = rebuild;
#undef S
#define S(H, T)                                                                                                          \
    if (headers_updated || nob_needs_rebuild1(BUILD_DIR #H, SRC_DIR #H ".h")) {                                          \
        cmd_append(&cmd, cc, "-D" #T "_TEST", "-Wall", "-Wextra", "-g", "-x", "c", "-o", BUILD_DIR #H, SRC_DIR #H ".h"); \
        if (!cmd_run(&cmd)) {                                                                                            \
            return 1;                                                                                                    \
        }                                                                                                                \
        cmd_append(&cmd, BUILD_DIR #H);                                                                                  \
        if (!cmd_run(&cmd)) {                                                                                            \
            return 1;                                                                                                    \
        }                                                                                                                \
        headers_updated = true;                                                                                          \
    }
    STB_HEADERS(S)

    char const *sources[] = {
        "",
#undef S
#define S(H) SRC_DIR #H ".h",
        APP_HEADERS(S)
    };

#if 0    
    bool profile_updated = false;
    char const *profile_sources[] = {
                (char const *) "src/profile.c",
                (char const *) "messages.txt",
                (char const *) "profile-types.csv",
                (char const *) "profile-messages.csv",
    };
    if (nob_needs_rebuild("src/fittypes.c", profile_sources, 4)) {
        cmd_append(&cmd, cc, "-Wall", "-Wextra", "-g", 
            "-o", BUILD_DIR "profile", "src/profile.c");
        if (!cmd_run(&cmd)) {
            return 1;
        }
        cmd_append(&cmd, BUILD_DIR "profile");
        if (!cmd_run(&cmd)) {
            return 1;
        }
	profile_updated = true;
    }
#endif

    bool        schema_updated = false;
    char const *schema_sources[] = {
        (char const *) "src/schemagen.c",
        (char const *) "db/schema.json",
    };
    if (rebuild || nob_needs_rebuild("src/schema.h", schema_sources, 2)) {
        cmd_append(&cmd, cc, "-Wall", "-Wextra", "-g",
            "-o", BUILD_DIR "schemagen", "src/schemagen.c");
        if (!cmd_run(&cmd)) {
            return 1;
        }
        cmd_append(&cmd, BUILD_DIR "schemagen", "db/schema.json", "src/schema.h");
        if (!cmd_run(&cmd)) {
            return 1;
        }
        schema_updated = true;
    }

    if (!build_displaymap) {
        bool sources_updated = false;
#undef S
#define S(SRC)                                                                                                              \
    sources[0] = SRC_DIR #SRC ".c";                                                                                         \
    if (headers_updated                                                                                                     \
        || fit_sources_updated || schema_updated                                                                            \
        || /* || profile_updated || */ nob_needs_rebuild(BUILD_DIR #SRC ".o", sources, sizeof(sources) / sizeof(char *))) { \
        cmd_append(&cmd, cc, "-Wall", "-Wextra",                                                                            \
            "-I/opt/homebrew/include", "-I../fitsdk", "-I/opt/homebrew/include/postgresql",                                 \
            "-c", "-g", "-o", BUILD_DIR #SRC ".o", SRC_DIR #SRC ".c");                                                      \
        if (!cmd_run(&cmd)) {                                                                                               \
            return 1;                                                                                                       \
        }                                                                                                                   \
        sources_updated = true;                                                                                             \
    }
        APP_SOURCES(S)
        if (sources_updated) {
            cmd_append(&cmd, cc, "-o", BUILD_DIR "sweattrails",
#undef S
#define S(SRC) BUILD_DIR #SRC ".o",
                APP_SOURCES(S) "-Lbuild", "-lfit", "-lraylib", "-lcurl", "-L/opt/homebrew/lib/postgresql", "-lpq", "-lm");
            if (!cmd_run(&cmd)) {
                return 1;
            }
        }

    } else {
        cmd_append(&cmd, cc, "-Wall", "-Wextra",
            "-I/opt/homebrew/include", "-c", "-g", "-o", BUILD_DIR "map.o", SRC_DIR "map.c");
        if (!cmd_run(&cmd)) {
            return 1;
        }
        cmd_append(&cmd, cc, "-Wall", "-Wextra",
            "-I/opt/homebrew/include", "-c", "-g", "-o", BUILD_DIR "displaymap.o", SRC_DIR "displaymap.c");
        if (!cmd_run(&cmd)) {
            return 1;
        }
        cmd_append(&cmd, cc, "-o", BUILD_DIR "displaymap", BUILD_DIR "displaymap.o", BUILD_DIR "map.o", "-lraylib", "-lcurl", "-lm");
        if (!cmd_run(&cmd)) {
            return 1;
        }
    }
    return 0;
}
