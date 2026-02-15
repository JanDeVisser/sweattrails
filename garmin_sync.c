#include "garmin_sync.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>

#define GARMIN_CONFIG_PATH "/.config/sweattrails/garmin_config"
#define GARMIN_TOKENS_DIR "/.config/sweattrails/garmin_tokens"

// JSON helpers (same pattern as wahoo_api.c)
static const char *json_find_key(const char *json, const char *key) {
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = json;

    while ((pos = strstr(pos, search)) != NULL) {
        int depth = 0;
        for (const char *p = json; p < pos; p++) {
            if (*p == '{') depth++;
            else if (*p == '}') depth--;
        }
        if (depth <= 1) {
            return pos;
        }
        pos++;
    }
    return NULL;
}

static bool json_get_string(const char *json, const char *key, char *out, size_t out_size) {
    const char *pos = json_find_key(json, key);
    if (!pos) return false;

    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    pos = strchr(pos + strlen(search), ':');
    if (!pos) return false;

    while (*pos && (*pos == ':' || *pos == ' ' || *pos == '\t')) pos++;
    if (*pos != '"') return false;
    pos++;

    size_t i = 0;
    while (*pos && *pos != '"' && i < out_size - 1) {
        if (*pos == '\\' && *(pos + 1)) {
            pos++;
        }
        out[i++] = *pos++;
    }
    out[i] = '\0';
    return true;
}

static bool json_get_int64(const char *json, const char *key, int64_t *out) {
    const char *pos = json_find_key(json, key);
    if (!pos) return false;

    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    pos = strchr(pos + strlen(search), ':');
    if (!pos) return false;

    while (*pos && (*pos == ':' || *pos == ' ' || *pos == '\t')) pos++;

    char *end;
    *out = strtoll(pos, &end, 10);
    return end != pos;
}

static bool json_get_float(const char *json, const char *key, float *out) {
    const char *pos = json_find_key(json, key);
    if (!pos) return false;

    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    pos = strchr(pos + strlen(search), ':');
    if (!pos) return false;

    while (*pos && (*pos == ':' || *pos == ' ' || *pos == '\t')) pos++;

    char *end;
    *out = strtof(pos, &end);
    return end != pos;
}

// Find garmin_helper.py next to executable or in cwd
bool garmin_find_helper(char *path, size_t path_size) {
    // Try next to executable
    char exe_path[512];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len > 0) {
        exe_path[len] = '\0';
        // Find last /
        char *slash = strrchr(exe_path, '/');
        if (slash) {
            *(slash + 1) = '\0';
            snprintf(path, path_size, "%sgarmin_helper.py", exe_path);
            struct stat st;
            if (stat(path, &st) == 0) return true;
        }
    }

    // Try current working directory
    snprintf(path, path_size, "garmin_helper.py");
    struct stat st;
    if (stat(path, &st) == 0) return true;

    // Try relative to cwd with full path
    char cwd[512];
    if (getcwd(cwd, sizeof(cwd))) {
        snprintf(path, path_size, "%s/garmin_helper.py", cwd);
        if (stat(path, &st) == 0) return true;
    }

    return false;
}

// Run garmin_helper.py with given args, read JSON output
// Credentials are passed via env vars, not command line
static char *run_helper(const GarminConfig *config, const char *args) {
    char helper_path[512];
    if (!garmin_find_helper(helper_path, sizeof(helper_path))) {
        fprintf(stderr, "garmin_helper.py not found\n");
        return NULL;
    }

    // Build command with env vars for credentials
    char cmd[2048];
    if (config && config->email[0] && config->password[0]) {
        snprintf(cmd, sizeof(cmd),
                 "GARMIN_EMAIL='%s' GARMIN_PASSWORD='%s' python3 '%s' %s 2>/dev/null",
                 config->email, config->password, helper_path, args);
    } else {
        snprintf(cmd, sizeof(cmd), "python3 '%s' %s 2>/dev/null", helper_path, args);
    }

    FILE *pipe = popen(cmd, "r");
    if (!pipe) return NULL;

    // Read all output
    size_t capacity = 4096;
    size_t size = 0;
    char *buf = malloc(capacity);
    if (!buf) { pclose(pipe); return NULL; }

    char line[4096];
    while (fgets(line, sizeof(line), pipe)) {
        size_t line_len = strlen(line);
        if (size + line_len >= capacity) {
            capacity *= 2;
            char *newbuf = realloc(buf, capacity);
            if (!newbuf) { free(buf); pclose(pipe); return NULL; }
            buf = newbuf;
        }
        memcpy(buf + size, line, line_len);
        size += line_len;
    }
    buf[size] = '\0';
    pclose(pipe);

    return buf;
}

// Check if helper response has status "ok"
static bool response_ok(const char *json) {
    if (!json) return false;
    char status[32];
    if (json_get_string(json, "status", status, sizeof(status))) {
        return strcmp(status, "ok") == 0;
    }
    return false;
}

bool garmin_load_config(GarminConfig *config) {
    memset(config, 0, sizeof(GarminConfig));

    const char *home = getenv("HOME");
    if (!home) return false;

    char path[512];
    snprintf(path, sizeof(path), "%s%s", home, GARMIN_CONFIG_PATH);

    FILE *f = fopen(path, "r");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *json = malloc(size + 1);
    if (!json) {
        fclose(f);
        return false;
    }

    fread(json, 1, size, f);
    json[size] = '\0';
    fclose(f);

    json_get_string(json, "email", config->email, sizeof(config->email));
    json_get_string(json, "password", config->password, sizeof(config->password));

    free(json);

    return config->email[0] && config->password[0];
}

bool garmin_save_config(const GarminConfig *config) {
    const char *home = getenv("HOME");
    if (!home) return false;

    char path[512];
    snprintf(path, sizeof(path), "%s%s", home, GARMIN_CONFIG_PATH);

    FILE *f = fopen(path, "w");
    if (!f) return false;

    fprintf(f, "{\n");
    fprintf(f, "  \"email\": \"%s\",\n", config->email);
    fprintf(f, "  \"password\": \"%s\"\n", config->password);
    fprintf(f, "}\n");

    fclose(f);
    return true;
}

bool garmin_is_authenticated(void) {
    char *response = run_helper(NULL, "check");
    bool ok = response_ok(response);
    free(response);
    return ok;
}

bool garmin_authenticate(const GarminConfig *config) {
    if (!config->email[0] || !config->password[0]) return false;

    char *response = run_helper(config, "login_env");
    bool ok = response_ok(response);
    if (!ok && response) {
        char msg[256];
        if (json_get_string(response, "message", msg, sizeof(msg))) {
            fprintf(stderr, "Garmin auth failed: %s\n", msg);
        }
    }
    free(response);
    return ok;
}

bool garmin_fetch_activities(GarminActivityList *list, int limit) {
    char args[64];
    snprintf(args, sizeof(args), "list %d", limit);

    char *response = run_helper(NULL, args);
    if (!response || !response_ok(response)) {
        free(response);
        return false;
    }

    // Parse activities array
    // Find "activities" key then parse each object in the array
    const char *arr_start = strstr(response, "\"activities\"");
    if (!arr_start) { free(response); return false; }

    arr_start = strchr(arr_start, '[');
    if (!arr_start) { free(response); return false; }
    arr_start++;

    // Initialize list
    list->capacity = 64;
    list->count = 0;
    list->activities = malloc(list->capacity * sizeof(GarminActivity));
    if (!list->activities) { free(response); return false; }

    // Parse each activity object
    const char *pos = arr_start;
    while (*pos) {
        // Find next object
        const char *obj_start = strchr(pos, '{');
        if (!obj_start) break;

        // Find matching close brace
        int depth = 1;
        const char *obj_end = obj_start + 1;
        while (*obj_end && depth > 0) {
            if (*obj_end == '{') depth++;
            else if (*obj_end == '}') depth--;
            obj_end++;
        }

        // Extract object as string
        size_t obj_len = obj_end - obj_start;
        char *obj = malloc(obj_len + 1);
        if (!obj) break;
        memcpy(obj, obj_start, obj_len);
        obj[obj_len] = '\0';

        // Grow list if needed
        if (list->count >= list->capacity) {
            list->capacity *= 2;
            GarminActivity *newact = realloc(list->activities,
                                              list->capacity * sizeof(GarminActivity));
            if (!newact) { free(obj); break; }
            list->activities = newact;
        }

        GarminActivity *act = &list->activities[list->count];
        memset(act, 0, sizeof(GarminActivity));

        int64_t id;
        if (json_get_int64(obj, "id", &id)) {
            act->id = id;
        }
        json_get_string(obj, "name", act->name, sizeof(act->name));
        json_get_string(obj, "type", act->type, sizeof(act->type));
        json_get_string(obj, "start_time", act->start_time, sizeof(act->start_time));
        json_get_float(obj, "duration", &act->duration);
        json_get_float(obj, "distance", &act->distance);

        list->count++;
        free(obj);
        pos = obj_end;
    }

    free(response);
    return true;
}

bool garmin_download_fit(int64_t activity_id, const char *output_path) {
    char args[1024];
    snprintf(args, sizeof(args), "download %lld '%s'", (long long)activity_id, output_path);

    char *response = run_helper(NULL, args);
    bool ok = response_ok(response);
    if (!ok && response) {
        char msg[256];
        if (json_get_string(response, "message", msg, sizeof(msg))) {
            fprintf(stderr, "Garmin download failed for %lld: %s\n",
                    (long long)activity_id, msg);
        }
    }
    free(response);
    return ok;
}

bool garmin_disconnect(void) {
    const char *home = getenv("HOME");
    if (!home) return false;

    char tokens_dir[512];
    snprintf(tokens_dir, sizeof(tokens_dir), "%s%s", home, GARMIN_TOKENS_DIR);

    // Remove session file
    char session_file[512];
    snprintf(session_file, sizeof(session_file), "%s/session.pkl", tokens_dir);
    remove(session_file);

    // Try to remove the directory
    rmdir(tokens_dir);

    return true;
}

void garmin_activity_list_free(GarminActivityList *list) {
    if (list->activities) {
        free(list->activities);
        list->activities = NULL;
    }
    list->count = 0;
    list->capacity = 0;
}
