#ifndef FIT_PARSER_H
#define FIT_PARSER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define FIT_MAX_FIELDS 256
#define FIT_MAX_POWER_SAMPLES 100000

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
} FitPowerSample;

typedef struct {
    FitPowerSample *samples;
    size_t count;
    size_t capacity;
    uint16_t max_power;
    uint16_t min_power;
    double avg_power;
} FitPowerData;

// Parse a FIT file and extract power data
// Returns true on success, false on failure
bool fit_parse_file(const char *filename, FitPowerData *data);

// Free power data resources
void fit_power_data_free(FitPowerData *data);

#endif // FIT_PARSER_H
