#ifndef ACTIVITY_META_H
#define ACTIVITY_META_H

#include <stdbool.h>

typedef struct {
    char title[256];
    char description[2048];
    bool title_edited;
    bool description_edited;
} ActivityMeta;

// Load metadata from .meta.json sidecar file
// Returns true if loaded successfully, false if file doesn't exist or error
bool activity_meta_load(const char *activity_path, ActivityMeta *meta);

// Save metadata to .meta.json sidecar file
// Returns true on success
bool activity_meta_save(const char *activity_path, const ActivityMeta *meta);

#endif // ACTIVITY_META_H
