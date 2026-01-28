#ifndef STRAVA_API_H
#define STRAVA_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define STRAVA_MAX_ACTIVITIES 200

typedef struct {
    char client_id[32];
    char client_secret[128];
    char access_token[128];
    char refresh_token[128];
    time_t token_expires_at;
} StravaConfig;

typedef struct {
    int64_t id;
    char name[256];
    char type[64];
    char start_date[32];
    int moving_time;      // seconds
    float distance;       // meters
    float average_watts;
    bool has_power;
} StravaActivity;

typedef struct {
    StravaActivity *activities;
    size_t count;
    size_t capacity;
} StravaActivityList;

// Load config from ~/.config/fitpower/config
bool strava_load_config(StravaConfig *config);

// Save config (including tokens) to file
bool strava_save_config(const StravaConfig *config);

// Check if we have valid tokens
bool strava_is_authenticated(const StravaConfig *config);

// Start OAuth flow - opens browser and waits for callback
// Returns true if authentication successful
bool strava_authenticate(StravaConfig *config);

// Refresh access token if expired
bool strava_refresh_token(StravaConfig *config);

// Fetch list of activities
bool strava_fetch_activities(StravaConfig *config, StravaActivityList *list, int page, int per_page);

// Download activity as FIT file to specified path
// Returns true on success
bool strava_download_activity(StravaConfig *config, int64_t activity_id, const char *output_path);

// Free activity list
void strava_activity_list_free(StravaActivityList *list);

#endif // STRAVA_API_H
