#ifndef WAHOO_API_H
#define WAHOO_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define WAHOO_MAX_WORKOUTS 200

typedef struct {
    char client_id[64];
    char client_secret[128];
    char access_token[256];
    char refresh_token[256];
    time_t token_expires_at;
} WahooConfig;

typedef struct {
    int64_t id;
    char name[256];
    char starts[32];          // ISO timestamp
    int minutes;              // duration in minutes
    float distance_meters;
    float ascent_meters;
    int avg_heart_rate;
    int avg_power;
    char fit_file_url[512];   // URL to download FIT file
} WahooWorkout;

typedef struct {
    WahooWorkout *workouts;
    size_t count;
    size_t capacity;
} WahooWorkoutList;

// Load config from ~/.config/sweattrails/wahoo_config
bool wahoo_load_config(WahooConfig *config);

// Save config (including tokens) to file
bool wahoo_save_config(const WahooConfig *config);

// Check if we have valid tokens
bool wahoo_is_authenticated(const WahooConfig *config);

// Start OAuth flow - opens browser and waits for callback
// Returns true if authentication successful
bool wahoo_authenticate(WahooConfig *config);

// Refresh access token if expired
bool wahoo_refresh_token(WahooConfig *config);

// Fetch list of workouts
bool wahoo_fetch_workouts(WahooConfig *config, WahooWorkoutList *list, int page, int per_page);

// Download workout FIT file to specified path
// Returns true on success
bool wahoo_download_fit(WahooConfig *config, const char *fit_url, const char *output_path);

// Free workout list
void wahoo_workout_list_free(WahooWorkoutList *list);

#endif // WAHOO_API_H
