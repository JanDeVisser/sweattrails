#include "zwift_sync.h"
#include "file_organizer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#define ZWIFT_CONFIG_PATH "/.config/sweattrails/zwift_config"
#define ZWIFT_IMPORTED_PATH "/.config/sweattrails/zwift_imported.json"

// Simple JSON parsing helpers (same pattern as wahoo_api.c)
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

static bool json_get_bool(const char *json, const char *key, bool *out) {
    const char *pos = json_find_key(json, key);
    if (!pos) return false;

    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    pos = strchr(pos + strlen(search), ':');
    if (!pos) return false;

    while (*pos && (*pos == ':' || *pos == ' ' || *pos == '\t')) pos++;

    if (strncmp(pos, "true", 4) == 0) {
        *out = true;
        return true;
    } else if (strncmp(pos, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
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

void zwift_get_default_folder(char *path, size_t path_size) {
    const char *home = getenv("HOME");
    if (!home) {
        path[0] = '\0';
        return;
    }
    snprintf(path, path_size, "%s/Documents/Zwift/Activities", home);
}

bool zwift_load_config(ZwiftConfig *config) {
    memset(config, 0, sizeof(ZwiftConfig));
    config->auto_sync = true;  // Default to auto-sync enabled

    const char *home = getenv("HOME");
    if (!home) return false;

    char path[512];
    snprintf(path, sizeof(path), "%s%s", home, ZWIFT_CONFIG_PATH);

    FILE *f = fopen(path, "r");
    if (!f) {
        // Config doesn't exist, use defaults
        zwift_get_default_folder(config->source_folder, sizeof(config->source_folder));
        return true;
    }

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

    json_get_string(json, "source_folder", config->source_folder, sizeof(config->source_folder));
    json_get_string(json, "remote_host", config->remote_host, sizeof(config->remote_host));
    json_get_bool(json, "auto_sync", &config->auto_sync);

    free(json);

    // If source_folder is empty, use default
    if (!config->source_folder[0]) {
        zwift_get_default_folder(config->source_folder, sizeof(config->source_folder));
    }

    return true;
}

bool zwift_save_config(const ZwiftConfig *config) {
    const char *home = getenv("HOME");
    if (!home) return false;

    char path[512];
    snprintf(path, sizeof(path), "%s%s", home, ZWIFT_CONFIG_PATH);

    FILE *f = fopen(path, "w");
    if (!f) return false;

    fprintf(f, "{\n");
    fprintf(f, "  \"source_folder\": \"%s\",\n", config->source_folder);
    fprintf(f, "  \"remote_host\": \"%s\",\n", config->remote_host);
    fprintf(f, "  \"auto_sync\": %s\n", config->auto_sync ? "true" : "false");
    fprintf(f, "}\n");

    fclose(f);
    return true;
}

bool zwift_load_imported(ZwiftImportedList *list) {
    memset(list, 0, sizeof(ZwiftImportedList));

    const char *home = getenv("HOME");
    if (!home) return false;

    char path[512];
    snprintf(path, sizeof(path), "%s%s", home, ZWIFT_IMPORTED_PATH);

    FILE *f = fopen(path, "r");
    if (!f) {
        // No imported list yet, that's fine
        return true;
    }

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

    // Parse JSON array of imported entries
    // Format: {"imported": [{"timestamp": 123, "file_size": 456, "filename": "..."}, ...]}
    const char *array_start = strstr(json, "\"imported\"");
    if (!array_start) {
        free(json);
        return true;
    }

    const char *pos = strchr(array_start, '[');
    if (!pos) {
        free(json);
        return true;
    }

    // Initialize list
    list->capacity = 64;
    list->entries = malloc(list->capacity * sizeof(ZwiftImportedEntry));
    list->count = 0;

    pos++;  // Skip '['
    while ((pos = strchr(pos, '{')) != NULL) {
        // Find end of this object
        int depth = 1;
        const char *start = pos;
        pos++;
        bool in_string = false;
        while (*pos && depth > 0) {
            if (*pos == '"' && *(pos - 1) != '\\') {
                in_string = !in_string;
            } else if (!in_string) {
                if (*pos == '{') depth++;
                else if (*pos == '}') depth--;
            }
            pos++;
        }

        if (depth != 0) break;

        // Extract this object
        size_t obj_len = pos - start;
        char *obj = malloc(obj_len + 1);
        strncpy(obj, start, obj_len);
        obj[obj_len] = '\0';

        // Parse entry
        if (list->count >= list->capacity) {
            list->capacity *= 2;
            list->entries = realloc(list->entries, list->capacity * sizeof(ZwiftImportedEntry));
        }

        ZwiftImportedEntry *entry = &list->entries[list->count];
        memset(entry, 0, sizeof(ZwiftImportedEntry));

        int64_t ts = 0, sz = 0;
        if (json_get_int64(obj, "timestamp", &ts)) {
            entry->activity_timestamp = (time_t)ts;
            if (json_get_int64(obj, "file_size", &sz)) {
                entry->file_size = (size_t)sz;
            }
            json_get_string(obj, "filename", entry->source_filename, sizeof(entry->source_filename));
            list->count++;
        }

        free(obj);
    }

    free(json);
    return true;
}

bool zwift_save_imported(const ZwiftImportedList *list) {
    const char *home = getenv("HOME");
    if (!home) return false;

    char path[512];
    snprintf(path, sizeof(path), "%s%s", home, ZWIFT_IMPORTED_PATH);

    FILE *f = fopen(path, "w");
    if (!f) return false;

    fprintf(f, "{\n  \"imported\": [\n");

    for (size_t i = 0; i < list->count; i++) {
        const ZwiftImportedEntry *entry = &list->entries[i];
        fprintf(f, "    {\"timestamp\": %ld, \"file_size\": %zu, \"filename\": \"%s\"}",
                (long)entry->activity_timestamp, entry->file_size, entry->source_filename);
        if (i < list->count - 1) {
            fprintf(f, ",");
        }
        fprintf(f, "\n");
    }

    fprintf(f, "  ]\n}\n");
    fclose(f);
    return true;
}

void zwift_imported_list_free(ZwiftImportedList *list) {
    free(list->entries);
    list->entries = NULL;
    list->count = 0;
    list->capacity = 0;
}

bool zwift_is_imported(const ZwiftImportedList *list, time_t timestamp, size_t file_size) {
    for (size_t i = 0; i < list->count; i++) {
        if (list->entries[i].activity_timestamp == timestamp &&
            list->entries[i].file_size == file_size) {
            return true;
        }
    }
    return false;
}

// Check if a file with this filename and size was already imported
static bool zwift_is_filename_imported(const ZwiftImportedList *list, const char *filename, size_t file_size) {
    for (size_t i = 0; i < list->count; i++) {
        if (list->entries[i].file_size == file_size &&
            strcmp(list->entries[i].source_filename, filename) == 0) {
            return true;
        }
    }
    return false;
}

void zwift_add_imported(ZwiftImportedList *list, time_t timestamp, size_t file_size, const char *filename) {
    if (!list->entries) {
        list->capacity = 64;
        list->entries = malloc(list->capacity * sizeof(ZwiftImportedEntry));
        list->count = 0;
    }

    if (list->count >= list->capacity) {
        list->capacity *= 2;
        list->entries = realloc(list->entries, list->capacity * sizeof(ZwiftImportedEntry));
    }

    ZwiftImportedEntry *entry = &list->entries[list->count];
    entry->activity_timestamp = timestamp;
    entry->file_size = file_size;
    strncpy(entry->source_filename, filename, sizeof(entry->source_filename) - 1);
    entry->source_filename[sizeof(entry->source_filename) - 1] = '\0';
    list->count++;
}

static bool copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return false;

    FILE *out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return false;
    }

    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            unlink(dst);
            return false;
        }
    }

    fclose(in);
    fclose(out);
    return true;
}

// Remote file info structure
typedef struct {
    char filename[256];
    size_t file_size;
} RemoteFileInfo;

// Get list of FIT files from remote host via SSH
// Returns allocated array of RemoteFileInfo, caller must free
static RemoteFileInfo *ssh_list_fit_files(const char *host, const char *folder, int *count) {
    *count = 0;

    // Single SSH command to get filenames and sizes
    // Try macOS stat format first, fall back to Linux format
    // macOS: stat -f "%z %N" *.fit
    // Linux: stat -c "%s %n" *.fit
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "ssh -o BatchMode=yes -o ConnectTimeout=10 '%s' '"
             "cd \"%s\" 2>/dev/null && "
             "(stat -f \"%%z %%N\" *.fit 2>/dev/null || stat -c \"%%s %%n\" *.fit 2>/dev/null)'",
             host, folder);

    FILE *pipe = popen(cmd, "r");
    if (!pipe) return NULL;

    char line[512];
    int capacity = 64;
    RemoteFileInfo *files = malloc(capacity * sizeof(RemoteFileInfo));
    int file_count = 0;

    while (fgets(line, sizeof(line), pipe)) {
        // Remove trailing newline
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';

        // Parse "size filename" format
        char *space = strchr(line, ' ');
        if (!space) continue;

        size_t file_size = (size_t)atoll(line);
        const char *filename = space + 1;

        // Extract just the basename if it's a full path
        const char *basename = strrchr(filename, '/');
        if (basename) {
            basename++;
        } else {
            basename = filename;
        }

        len = strlen(basename);
        if (len <= 4 || strcasecmp(basename + len - 4, ".fit") != 0) continue;

        if (file_count >= capacity) {
            capacity *= 2;
            files = realloc(files, capacity * sizeof(RemoteFileInfo));
        }

        strncpy(files[file_count].filename, basename, sizeof(files[file_count].filename) - 1);
        files[file_count].filename[sizeof(files[file_count].filename) - 1] = '\0';
        files[file_count].file_size = file_size;
        file_count++;
    }

    pclose(pipe);

    *count = file_count;
    return files;
}

// Copy file from remote host via SCP
static bool scp_copy_file(const char *host, const char *remote_path, const char *local_path) {
    char cmd[1024];
    // Use double quotes for remote path (interpreted by remote shell for spaces)
    // Use single quotes for local path (interpreted by local shell)
    snprintf(cmd, sizeof(cmd),
             "scp -o BatchMode=yes -o ConnectTimeout=10 %s:\"%s\" '%s' >/dev/null 2>&1",
             host, remote_path, local_path);

    int ret = system(cmd);
    return ret == 0;
}

// Sync from local folder
static int zwift_sync_local(const ZwiftConfig *config, const char *data_dir,
                            ZwiftSyncProgress *progress, ZwiftImportedList *imported) {
    struct stat st;
    if (stat(config->source_folder, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return 0;
    }

    DIR *dir = opendir(config->source_folder);
    if (!dir) {
        return 0;
    }

    int imported_count = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        size_t len = strlen(entry->d_name);
        if (len <= 4 || strcasecmp(entry->d_name + len - 4, ".fit") != 0) {
            continue;
        }

        progress->files_found++;
        strncpy(progress->current_file, entry->d_name, sizeof(progress->current_file) - 1);

        char src_path[1024];
        snprintf(src_path, sizeof(src_path), "%s/%s", config->source_folder, entry->d_name);

        // Get file size
        if (stat(src_path, &st) != 0) {
            continue;
        }
        size_t file_size = (size_t)st.st_size;

        // Get activity timestamp from FIT file
        time_t timestamp = fit_get_activity_timestamp(src_path);
        if (timestamp == 0) {
            progress->files_skipped++;
            continue;
        }

        // Check if already imported
        if (zwift_is_imported(imported, timestamp, file_size)) {
            progress->files_skipped++;
            continue;
        }

        // Create destination directory: data_dir/activity/YYYY/MM
        struct tm *tm_info = localtime(&timestamp);
        if (!tm_info) {
            progress->files_skipped++;
            continue;
        }

        char dest_dir[512];
        snprintf(dest_dir, sizeof(dest_dir), "%s/activity/%04d/%02d",
                 data_dir, tm_info->tm_year + 1900, tm_info->tm_mon + 1);

        if (!create_directory_path(dest_dir)) {
            progress->files_skipped++;
            continue;
        }

        // Create destination filename: zwift_<timestamp>.fit
        char dest_path[512];
        snprintf(dest_path, sizeof(dest_path), "%s/zwift_%ld.fit", dest_dir, (long)timestamp);

        // Check if destination already exists
        if (stat(dest_path, &st) == 0) {
            zwift_add_imported(imported, timestamp, file_size, entry->d_name);
            progress->files_skipped++;
            continue;
        }

        // Copy the file
        if (copy_file(src_path, dest_path)) {
            zwift_add_imported(imported, timestamp, file_size, entry->d_name);
            imported_count++;
            progress->files_imported++;
            printf("Imported Zwift activity: %s -> %s\n", entry->d_name, dest_path);
            fflush(stdout);
        } else {
            progress->files_skipped++;
        }
    }

    closedir(dir);
    return imported_count;
}

// Sync from remote host via SSH/SCP
static int zwift_sync_remote(const ZwiftConfig *config, const char *data_dir,
                             ZwiftSyncProgress *progress, ZwiftImportedList *imported) {
    // Get list of files from remote host
    int file_count = 0;
    RemoteFileInfo *files = ssh_list_fit_files(config->remote_host, config->source_folder, &file_count);
    if (!files || file_count == 0) {
        free(files);
        return 0;
    }

    // Create temp directory for downloads
    const char *home = getenv("HOME");
    if (!home) {
        free(files);
        return 0;
    }

    char tmp_dir[512];
    snprintf(tmp_dir, sizeof(tmp_dir), "%s/.cache/sweattrails/zwift_tmp", home);
    create_directory_path(tmp_dir);

    int imported_count = 0;
    struct stat st;

    for (int i = 0; i < file_count; i++) {
        progress->files_found++;
        strncpy(progress->current_file, files[i].filename, sizeof(progress->current_file) - 1);

        // Quick check: if this exact filename+size was already imported, skip
        if (zwift_is_filename_imported(imported, files[i].filename, files[i].file_size)) {
            progress->files_skipped++;
            continue;
        }

        // Build remote path
        char remote_path[1024];
        snprintf(remote_path, sizeof(remote_path), "%s/%s", config->source_folder, files[i].filename);

        // Download to temp location
        char tmp_path[1024];
        snprintf(tmp_path, sizeof(tmp_path), "%s/%s", tmp_dir, files[i].filename);

        if (!scp_copy_file(config->remote_host, remote_path, tmp_path)) {
            progress->files_skipped++;
            continue;
        }

        // Get actual file size from downloaded file
        if (stat(tmp_path, &st) != 0) {
            unlink(tmp_path);
            progress->files_skipped++;
            continue;
        }
        size_t file_size = (size_t)st.st_size;

        // Get activity timestamp from FIT file
        time_t timestamp = fit_get_activity_timestamp(tmp_path);
        if (timestamp == 0) {
            unlink(tmp_path);
            progress->files_skipped++;
            continue;
        }

        // Check if already imported
        if (zwift_is_imported(imported, timestamp, file_size)) {
            unlink(tmp_path);
            progress->files_skipped++;
            continue;
        }

        // Create destination directory: data_dir/activity/YYYY/MM
        struct tm *tm_info = localtime(&timestamp);
        if (!tm_info) {
            unlink(tmp_path);
            progress->files_skipped++;
            continue;
        }

        char dest_dir[512];
        snprintf(dest_dir, sizeof(dest_dir), "%s/activity/%04d/%02d",
                 data_dir, tm_info->tm_year + 1900, tm_info->tm_mon + 1);

        if (!create_directory_path(dest_dir)) {
            unlink(tmp_path);
            progress->files_skipped++;
            continue;
        }

        // Create destination filename: zwift_<timestamp>.fit
        char dest_path[512];
        snprintf(dest_path, sizeof(dest_path), "%s/zwift_%ld.fit", dest_dir, (long)timestamp);

        // Check if destination already exists
        if (stat(dest_path, &st) == 0) {
            zwift_add_imported(imported, timestamp, file_size, files[i].filename);
            unlink(tmp_path);
            progress->files_skipped++;
            continue;
        }

        // Move temp file to final destination
        if (rename(tmp_path, dest_path) == 0) {
            zwift_add_imported(imported, timestamp, file_size, files[i].filename);
            imported_count++;
            progress->files_imported++;
            printf("Imported Zwift activity from %s: %s -> %s\n",
                   config->remote_host, files[i].filename, dest_path);
            fflush(stdout);
        } else {
            // rename failed (maybe cross-device), try copy
            if (copy_file(tmp_path, dest_path)) {
                zwift_add_imported(imported, timestamp, file_size, files[i].filename);
                imported_count++;
                progress->files_imported++;
                printf("Imported Zwift activity from %s: %s -> %s\n",
                       config->remote_host, files[i].filename, dest_path);
                fflush(stdout);
            } else {
                progress->files_skipped++;
            }
            unlink(tmp_path);
        }
    }

    free(files);
    return imported_count;
}

int zwift_sync_activities(const ZwiftConfig *config, const char *data_dir, ZwiftSyncProgress *progress) {
    memset(progress, 0, sizeof(ZwiftSyncProgress));

    // Load imported list
    ZwiftImportedList imported;
    zwift_load_imported(&imported);

    int imported_count;

    if (config->remote_host[0]) {
        // Remote sync via SSH/SCP
        printf("Syncing Zwift activities from %s:%s\n", config->remote_host, config->source_folder);
        fflush(stdout);
        imported_count = zwift_sync_remote(config, data_dir, progress, &imported);
    } else {
        // Local sync
        imported_count = zwift_sync_local(config, data_dir, progress, &imported);
    }

    // Save updated imported list
    zwift_save_imported(&imported);
    zwift_imported_list_free(&imported);

    return imported_count;
}
