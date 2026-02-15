#ifndef GARMIN_SYNC_H
#define GARMIN_SYNC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GARMIN_MAX_ACTIVITIES 200

typedef struct {
    char email[256];
    char password[256];
} GarminConfig;

typedef struct {
    int64_t id;
    char name[256];
    char type[64];
    char start_time[32];    // "YYYY-MM-DD HH:MM:SS" local time
    float duration;         // seconds
    float distance;         // meters
} GarminActivity;

typedef struct {
    GarminActivity *activities;
    size_t count;
    size_t capacity;
} GarminActivityList;

// Load config from ~/.config/sweattrails/garmin_config
bool garmin_load_config(GarminConfig *config);

// Save config to file
bool garmin_save_config(const GarminConfig *config);

// Check if we have a valid saved session
bool garmin_is_authenticated(void);

// Authenticate using email/password from config (passed via env vars)
bool garmin_authenticate(const GarminConfig *config);

// Fetch list of recent activities
bool garmin_fetch_activities(GarminActivityList *list, int limit);

// Download activity FIT file to specified path
bool garmin_download_fit(int64_t activity_id, const char *output_path);

// Disconnect - remove saved session tokens
bool garmin_disconnect(void);

// Free activity list
void garmin_activity_list_free(GarminActivityList *list);

// Find path to garmin_helper.py (looks next to executable, then cwd)
bool garmin_find_helper(char *path, size_t path_size);

#endif // GARMIN_SYNC_H
