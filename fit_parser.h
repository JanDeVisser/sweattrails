#ifndef FIT_PARSER_H
#define FIT_PARSER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>

#define FIT_MAX_FIELDS 256
#define FIT_MAX_POWER_SAMPLES 100000

// GPS coordinate conversion: FIT uses semicircles (2^31 = 180 degrees)
#define FIT_SEMICIRCLE_TO_DEGREES (180.0 / 2147483648.0)

typedef struct {
    uint8_t field_def_num;
    uint8_t size;
    uint8_t base_type;
} FitFieldDef;

typedef struct {
    bool defined;
    uint8_t reserved;
    uint8_t arch;  // 0 = little endian, 1 = big endian
    uint16_t global_msg_num;
    uint8_t num_fields;
    FitFieldDef fields[FIT_MAX_FIELDS];
    size_t record_size;
} FitDefinition;

typedef struct {
    uint32_t timestamp;
    uint16_t power;
    bool has_power;
    int32_t latitude;    // semicircles (raw FIT format)
    int32_t longitude;   // semicircles (raw FIT format)
    bool has_gps;
    uint8_t heart_rate;  // bpm (0 = invalid)
    bool has_heart_rate;
    uint8_t cadence;     // rpm (0 = invalid)
    bool has_cadence;
} FitPowerSample;

typedef struct {
    FitPowerSample *samples;
    size_t count;
    size_t capacity;
    uint16_t max_power;
    uint16_t min_power;
    double avg_power;
    bool has_gps_data;       // true if any sample has GPS
    size_t gps_sample_count;
    double min_lat, max_lat; // bounding box (decimal degrees)
    double min_lon, max_lon;

    // Activity metadata (from JSON or calculated from FIT)
    char title[256];
    char description[2048];
    char activity_type[64];       // "Ride", "Run", etc.
    time_t start_time;            // Unix timestamp
    int elapsed_time;             // seconds (from samples or JSON)
    int moving_time;              // seconds (from JSON if available)
    float total_distance;         // meters

    // Heart rate stats
    uint8_t max_heart_rate;
    uint8_t avg_heart_rate;
    bool has_heart_rate_data;

    // Cadence stats
    uint8_t max_cadence;
    uint8_t avg_cadence;
    bool has_cadence_data;

    // Source tracking
    char source_file[512];        // Path to original file
} FitPowerData;

// Parse a FIT file and extract power data
// Returns true on success, false on failure
bool fit_parse_file(const char *filename, FitPowerData *data);

// Parse a JSON activity file (from Strava download) and extract power data
// Returns true on success, false on failure
bool json_parse_activity(const char *filename, FitPowerData *data);

// Free power data resources
void fit_power_data_free(FitPowerData *data);

#endif // FIT_PARSER_H
