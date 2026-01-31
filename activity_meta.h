#ifndef ACTIVITY_META_H
#define ACTIVITY_META_H

#include <stdbool.h>
#include <time.h>

#define MAX_GROUP_FILES 32

typedef struct {
    char title[256];
    char description[2048];
    bool title_edited;
    bool description_edited;
} ActivityMeta;

typedef struct {
    char title[256];
    char description[2048];
    bool title_edited;
    bool description_edited;
    char files[MAX_GROUP_FILES][64];  // Filenames in the group
    int file_count;
} GroupMeta;

// Load metadata from .meta.json sidecar file
// Returns true if loaded successfully, false if file doesn't exist or error
bool activity_meta_load(const char *activity_path, ActivityMeta *meta);

// Save metadata to .meta.json sidecar file
// Returns true on success
bool activity_meta_save(const char *activity_path, const ActivityMeta *meta);

// Generate group meta path from month directory and timestamp
void group_meta_path(const char *month_path, time_t timestamp, char *out, size_t out_size);

// Load group metadata
bool group_meta_load(const char *meta_path, GroupMeta *meta);

// Save group metadata
bool group_meta_save(const char *meta_path, const GroupMeta *meta);

#endif // ACTIVITY_META_H
