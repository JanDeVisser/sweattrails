#include "activity_meta.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void json_escape_string(const char *src, char *dest, size_t dest_size) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j < dest_size - 2; i++) {
        char c = src[i];
        if (c == '"' || c == '\\') {
            if (j < dest_size - 3) {
                dest[j++] = '\\';
                dest[j++] = c;
            }
        } else if (c == '\n') {
            if (j < dest_size - 3) {
                dest[j++] = '\\';
                dest[j++] = 'n';
            }
        } else if (c == '\r') {
            if (j < dest_size - 3) {
                dest[j++] = '\\';
                dest[j++] = 'r';
            }
        } else if (c == '\t') {
            if (j < dest_size - 3) {
                dest[j++] = '\\';
                dest[j++] = 't';
            }
        } else {
            dest[j++] = c;
        }
    }
    dest[j] = '\0';
}

static void json_unescape_string(const char *src, char *dest, size_t dest_size) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j < dest_size - 1; i++) {
        if (src[i] == '\\' && src[i + 1]) {
            i++;
            switch (src[i]) {
                case 'n': dest[j++] = '\n'; break;
                case 'r': dest[j++] = '\r'; break;
                case 't': dest[j++] = '\t'; break;
                case '"': dest[j++] = '"'; break;
                case '\\': dest[j++] = '\\'; break;
                default: dest[j++] = src[i]; break;
            }
        } else {
            dest[j++] = src[i];
        }
    }
    dest[j] = '\0';
}

static bool json_get_string_value(const char *json, const char *key, char *out, size_t out_size) {
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = strstr(json, search);
    if (!pos) return false;

    pos = strchr(pos + strlen(search), ':');
    if (!pos) return false;

    while (*pos && (*pos == ':' || *pos == ' ' || *pos == '\t' || *pos == '\n')) pos++;
    if (*pos != '"') return false;
    pos++;

    // Read until closing quote, handling escapes
    char escaped[4096];
    size_t i = 0;
    while (*pos && *pos != '"' && i < sizeof(escaped) - 1) {
        if (*pos == '\\' && *(pos + 1)) {
            escaped[i++] = *pos++;
        }
        escaped[i++] = *pos++;
    }
    escaped[i] = '\0';

    json_unescape_string(escaped, out, out_size);
    return true;
}

static bool json_get_bool_value(const char *json, const char *key) {
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = strstr(json, search);
    if (!pos) return false;

    pos = strchr(pos + strlen(search), ':');
    if (!pos) return false;

    while (*pos && (*pos == ':' || *pos == ' ' || *pos == '\t' || *pos == '\n')) pos++;
    return strncmp(pos, "true", 4) == 0;
}

bool activity_meta_load(const char *activity_path, ActivityMeta *meta) {
    memset(meta, 0, sizeof(ActivityMeta));

    char meta_path[520];
    snprintf(meta_path, sizeof(meta_path), "%s.meta.json", activity_path);

    FILE *f = fopen(meta_path, "r");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > 65536) {
        fclose(f);
        return false;
    }

    char *json = malloc(size + 1);
    if (!json) {
        fclose(f);
        return false;
    }

    size_t read = fread(json, 1, size, f);
    json[read] = '\0';
    fclose(f);

    json_get_string_value(json, "title", meta->title, sizeof(meta->title));
    json_get_string_value(json, "description", meta->description, sizeof(meta->description));
    meta->title_edited = json_get_bool_value(json, "title_edited");
    meta->description_edited = json_get_bool_value(json, "description_edited");

    free(json);
    return true;
}

bool activity_meta_save(const char *activity_path, const ActivityMeta *meta) {
    char meta_path[520];
    snprintf(meta_path, sizeof(meta_path), "%s.meta.json", activity_path);

    FILE *f = fopen(meta_path, "w");
    if (!f) return false;

    char escaped_title[512];
    char escaped_desc[4096];
    json_escape_string(meta->title, escaped_title, sizeof(escaped_title));
    json_escape_string(meta->description, escaped_desc, sizeof(escaped_desc));

    fprintf(f, "{\n");
    fprintf(f, "  \"title\": \"%s\",\n", escaped_title);
    fprintf(f, "  \"description\": \"%s\",\n", escaped_desc);
    fprintf(f, "  \"title_edited\": %s,\n", meta->title_edited ? "true" : "false");
    fprintf(f, "  \"description_edited\": %s\n", meta->description_edited ? "true" : "false");
    fprintf(f, "}\n");

    fclose(f);
    return true;
}
