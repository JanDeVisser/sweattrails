#ifndef ZWIFT_SYNC_H
#define ZWIFT_SYNC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define ZWIFT_MAX_IMPORTED 2000

typedef struct {
    char source_folder[512];
    char remote_host[256];  // If set, sync via SCP (e.g., "user@192.168.1.100" or just "192.168.1.100")
    bool auto_sync;
} ZwiftConfig;

typedef struct {
    time_t activity_timestamp;
    size_t file_size;
    char source_filename[256];
} ZwiftImportedEntry;

typedef struct {
    ZwiftImportedEntry *entries;
    size_t count;
    size_t capacity;
} ZwiftImportedList;

typedef struct {
    int files_found;
    int files_imported;
    int files_skipped;
    char current_file[256];
} ZwiftSyncProgress;

// Load config from ~/.config/sweattrails/zwift_config
bool zwift_load_config(ZwiftConfig *config);

// Save config to file
bool zwift_save_config(const ZwiftConfig *config);

// Get default Zwift Activities folder path
// Returns path like ~/Documents/Zwift/Activities/
void zwift_get_default_folder(char *path, size_t path_size);

// Load imported list from ~/.config/sweattrails/zwift_imported.json
bool zwift_load_imported(ZwiftImportedList *list);

// Save imported list
bool zwift_save_imported(const ZwiftImportedList *list);

// Free imported list
void zwift_imported_list_free(ZwiftImportedList *list);

// Check if activity already imported (by timestamp + filesize)
bool zwift_is_imported(const ZwiftImportedList *list, time_t timestamp, size_t file_size);

// Add entry to imported list
void zwift_add_imported(ZwiftImportedList *list, time_t timestamp, size_t file_size, const char *filename);

// Sync activities from Zwift folder to data_dir
// Returns number of files imported
int zwift_sync_activities(const ZwiftConfig *config, const char *data_dir, ZwiftSyncProgress *progress);

#endif // ZWIFT_SYNC_H
