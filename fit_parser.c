#define _GNU_SOURCE
#include "fit_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

// Haversine distance between two lat/lon points (in meters)
static double haversine_distance(double lat1, double lon1, double lat2, double lon2) {
    const double R = 6371000.0;  // Earth radius in meters
    double dlat = (lat2 - lat1) * M_PI / 180.0;
    double dlon = (lon2 - lon1) * M_PI / 180.0;
    double a = sin(dlat / 2) * sin(dlat / 2) +
               cos(lat1 * M_PI / 180.0) * cos(lat2 * M_PI / 180.0) *
               sin(dlon / 2) * sin(dlon / 2);
    double c = 2 * atan2(sqrt(a), sqrt(1 - a));
    return R * c;
}

// FIT Global Message Numbers
#define FIT_MESG_RECORD 20

// FIT Record Field Definition Numbers
#define FIT_FIELD_POSITION_LAT 0
#define FIT_FIELD_POSITION_LONG 1
#define FIT_FIELD_HEART_RATE 3
#define FIT_FIELD_CADENCE 4
#define FIT_FIELD_POWER 7
#define FIT_FIELD_TIMESTAMP 253

// FIT base types
#define FIT_BASE_TYPE_ENUM    0x00
#define FIT_BASE_TYPE_SINT8   0x01
#define FIT_BASE_TYPE_UINT8   0x02
#define FIT_BASE_TYPE_SINT16  0x83
#define FIT_BASE_TYPE_UINT16  0x84
#define FIT_BASE_TYPE_SINT32  0x85
#define FIT_BASE_TYPE_UINT32  0x86
#define FIT_BASE_TYPE_STRING  0x07
#define FIT_BASE_TYPE_FLOAT32 0x88
#define FIT_BASE_TYPE_FLOAT64 0x89
#define FIT_BASE_TYPE_UINT8Z  0x0A
#define FIT_BASE_TYPE_UINT16Z 0x8B
#define FIT_BASE_TYPE_UINT32Z 0x8C
#define FIT_BASE_TYPE_BYTE    0x0D
#define FIT_BASE_TYPE_SINT64  0x8E
#define FIT_BASE_TYPE_UINT64  0x8F
#define FIT_BASE_TYPE_UINT64Z 0x90

__attribute__((unused)) static size_t get_base_type_size(uint8_t base_type) {
    switch (base_type & 0x1F) {
        case 0x00: return 1;  // enum
        case 0x01: return 1;  // sint8
        case 0x02: return 1;  // uint8
        case 0x03: return 2;  // sint16
        case 0x04: return 2;  // uint16
        case 0x05: return 4;  // sint32
        case 0x06: return 4;  // uint32
        case 0x07: return 1;  // string
        case 0x08: return 4;  // float32
        case 0x09: return 8;  // float64
        case 0x0A: return 1;  // uint8z
        case 0x0B: return 2;  // uint16z
        case 0x0C: return 4;  // uint32z
        case 0x0D: return 1;  // byte
        case 0x0E: return 8;  // sint64
        case 0x0F: return 8;  // uint64
        case 0x10: return 8;  // uint64z
        default: return 1;
    }
}

static uint16_t read_uint16(const uint8_t *data, bool big_endian) {
    if (big_endian) {
        return (uint16_t)data[0] << 8 | data[1];
    }
    return (uint16_t)data[1] << 8 | data[0];
}

static uint32_t read_uint32(const uint8_t *data, bool big_endian) {
    if (big_endian) {
        return (uint32_t)data[0] << 24 | (uint32_t)data[1] << 16 |
               (uint32_t)data[2] << 8 | data[3];
    }
    return (uint32_t)data[3] << 24 | (uint32_t)data[2] << 16 |
           (uint32_t)data[1] << 8 | data[0];
}

static int32_t read_sint32(const uint8_t *data, bool big_endian) {
    return (int32_t)read_uint32(data, big_endian);
}

static uint64_t read_field_value(const uint8_t *data, uint8_t size, uint8_t base_type __attribute__((unused)), bool big_endian) {
    switch (size) {
        case 1:
            return data[0];
        case 2:
            return read_uint16(data, big_endian);
        case 4:
            return read_uint32(data, big_endian);
        case 8: {
            if (big_endian) {
                return ((uint64_t)read_uint32(data, big_endian) << 32) |
                       read_uint32(data + 4, big_endian);
            }
            return ((uint64_t)read_uint32(data + 4, big_endian) << 32) |
                   read_uint32(data, big_endian);
        }
        default:
            return 0;
    }
}

static bool add_sample(FitPowerData *data, uint32_t timestamp, uint16_t power, bool has_power,
                       int32_t latitude, int32_t longitude, bool has_gps,
                       uint8_t heart_rate, bool has_heart_rate,
                       uint8_t cadence, bool has_cadence) {
    // Only add sample if it has power, GPS, heart rate, or cadence data
    if (!has_power && !has_gps && !has_heart_rate && !has_cadence) {
        return true;
    }

    if (data->count >= data->capacity) {
        size_t new_capacity = data->capacity == 0 ? 1024 : data->capacity * 2;
        if (new_capacity > FIT_MAX_POWER_SAMPLES) {
            new_capacity = FIT_MAX_POWER_SAMPLES;
            if (data->count >= new_capacity) {
                return false;
            }
        }
        FitPowerSample *new_samples = realloc(data->samples, new_capacity * sizeof(FitPowerSample));
        if (!new_samples) {
            return false;
        }
        data->samples = new_samples;
        data->capacity = new_capacity;
    }

    FitPowerSample *sample = &data->samples[data->count];
    sample->timestamp = timestamp;
    sample->power = power;
    sample->has_power = has_power;
    sample->latitude = latitude;
    sample->longitude = longitude;
    sample->has_gps = has_gps;
    sample->heart_rate = heart_rate;
    sample->has_heart_rate = has_heart_rate;
    sample->cadence = cadence;
    sample->has_cadence = has_cadence;
    data->count++;

    return true;
}

bool fit_parse_file(const char *filename, FitPowerData *data) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "Error: Cannot open file %s\n", filename);
        return false;
    }

    // Initialize power data
    memset(data, 0, sizeof(FitPowerData));
    data->min_power = UINT16_MAX;

    // Read FIT header
    uint8_t header[14];
    if (fread(header, 1, 1, file) != 1) {
        fclose(file);
        return false;
    }

    uint8_t header_size = header[0];
    if (header_size != 12 && header_size != 14) {
        fprintf(stderr, "Error: Invalid FIT header size: %d\n", header_size);
        fclose(file);
        return false;
    }

    // Read rest of header
    if (fread(header + 1, 1, header_size - 1, file) != (size_t)(header_size - 1)) {
        fclose(file);
        return false;
    }

    // Verify FIT signature
    if (header[8] != '.' || header[9] != 'F' || header[10] != 'I' || header[11] != 'T') {
        fprintf(stderr, "Error: Invalid FIT signature\n");
        fclose(file);
        return false;
    }

    uint32_t data_size = read_uint32(header + 4, false);

    // Local message definitions (0-15)
    FitDefinition definitions[16] = {0};

    // Timestamp accumulator (for compressed timestamps)
    uint32_t timestamp = 0;

    size_t bytes_read = 0;

    while (bytes_read < data_size) {
        uint8_t record_header;
        if (fread(&record_header, 1, 1, file) != 1) {
            break;
        }
        bytes_read++;

        // Check if this is a compressed timestamp header
        if (record_header & 0x80) {
            // Compressed timestamp header
            uint8_t local_msg = (record_header >> 5) & 0x03;
            uint8_t time_offset = record_header & 0x1F;

            // Update timestamp
            timestamp = (timestamp & 0xFFFFFFE0) | time_offset;
            if (time_offset < (timestamp & 0x1F)) {
                timestamp += 0x20;
            }

            FitDefinition *def = &definitions[local_msg];
            if (!def->defined) {
                // Skip unknown message
                continue;
            }

            uint8_t *record_data = malloc(def->record_size);
            if (!record_data || fread(record_data, 1, def->record_size, file) != def->record_size) {
                free(record_data);
                break;
            }
            bytes_read += def->record_size;

            // Process record message
            if (def->global_msg_num == FIT_MESG_RECORD) {
                uint16_t power = 0;
                bool has_power = false;
                int32_t latitude = 0x7FFFFFFF;  // Invalid value
                int32_t longitude = 0x7FFFFFFF;
                bool has_gps = false;
                uint8_t heart_rate = 0;
                bool has_heart_rate = false;
                uint8_t cadence = 0;
                bool has_cadence = false;
                size_t offset = 0;

                for (int i = 0; i < def->num_fields; i++) {
                    FitFieldDef *field = &def->fields[i];

                    if (field->field_def_num == FIT_FIELD_POWER && field->size >= 2) {
                        power = (uint16_t)read_field_value(record_data + offset, field->size,
                                                          field->base_type, def->arch == 1);
                        if (power != 0xFFFF) {
                            has_power = true;
                        }
                    } else if (field->field_def_num == FIT_FIELD_TIMESTAMP && field->size >= 4) {
                        timestamp = (uint32_t)read_field_value(record_data + offset, field->size,
                                                               field->base_type, def->arch == 1);
                    } else if (field->field_def_num == FIT_FIELD_POSITION_LAT && field->size >= 4) {
                        latitude = read_sint32(record_data + offset, def->arch == 1);
                        if (latitude != 0x7FFFFFFF) {
                            has_gps = true;
                        }
                    } else if (field->field_def_num == FIT_FIELD_POSITION_LONG && field->size >= 4) {
                        longitude = read_sint32(record_data + offset, def->arch == 1);
                    } else if (field->field_def_num == FIT_FIELD_HEART_RATE && field->size >= 1) {
                        heart_rate = (uint8_t)read_field_value(record_data + offset, field->size,
                                                               field->base_type, def->arch == 1);
                        if (heart_rate != 0xFF && heart_rate > 0) {
                            has_heart_rate = true;
                        }
                    } else if (field->field_def_num == FIT_FIELD_CADENCE && field->size >= 1) {
                        cadence = (uint8_t)read_field_value(record_data + offset, field->size,
                                                           field->base_type, def->arch == 1);
                        if (cadence != 0xFF && cadence > 0) {
                            has_cadence = true;
                        }
                    }

                    offset += field->size;
                }

                add_sample(data, timestamp, power, has_power, latitude, longitude, has_gps,
                           heart_rate, has_heart_rate, cadence, has_cadence);
                if (has_power) {
                    if (power > data->max_power) data->max_power = power;
                    if (power < data->min_power) data->min_power = power;
                }
            }

            free(record_data);
        }
        else if (record_header & 0x40) {
            // Definition message
            uint8_t local_msg = record_header & 0x0F;
            bool has_dev_data = (record_header & 0x20) != 0;

            uint8_t def_header[5];
            if (fread(def_header, 1, 5, file) != 5) {
                break;
            }
            bytes_read += 5;

            FitDefinition *def = &definitions[local_msg];
            def->defined = true;
            def->reserved = def_header[0];
            def->arch = def_header[1];
            def->global_msg_num = read_uint16(def_header + 2, def->arch == 1);
            def->num_fields = def_header[4];
            def->record_size = 0;

            // Read field definitions
            for (int i = 0; i < def->num_fields && i < FIT_MAX_FIELDS; i++) {
                uint8_t field_def[3];
                if (fread(field_def, 1, 3, file) != 3) {
                    break;
                }
                bytes_read += 3;

                def->fields[i].field_def_num = field_def[0];
                def->fields[i].size = field_def[1];
                def->fields[i].base_type = field_def[2];
                def->record_size += field_def[1];
            }

            // Skip developer field definitions if present
            if (has_dev_data) {
                uint8_t num_dev_fields;
                if (fread(&num_dev_fields, 1, 1, file) != 1) {
                    break;
                }
                bytes_read++;

                for (int i = 0; i < num_dev_fields; i++) {
                    uint8_t dev_field_def[3];
                    if (fread(dev_field_def, 1, 3, file) != 3) {
                        break;
                    }
                    bytes_read += 3;
                    def->record_size += dev_field_def[1];
                }
            }
        }
        else {
            // Data message
            uint8_t local_msg = record_header & 0x0F;
            FitDefinition *def = &definitions[local_msg];

            if (!def->defined) {
                // Unknown message type, try to skip
                fprintf(stderr, "Warning: Undefined local message %d\n", local_msg);
                break;
            }

            uint8_t *record_data = malloc(def->record_size);
            if (!record_data) {
                break;
            }

            if (fread(record_data, 1, def->record_size, file) != def->record_size) {
                free(record_data);
                break;
            }
            bytes_read += def->record_size;

            // Process record message
            if (def->global_msg_num == FIT_MESG_RECORD) {
                uint16_t power = 0;
                bool has_power = false;
                int32_t latitude = 0x7FFFFFFF;  // Invalid value
                int32_t longitude = 0x7FFFFFFF;
                bool has_gps = false;
                uint8_t heart_rate = 0;
                bool has_heart_rate = false;
                uint8_t cadence = 0;
                bool has_cadence = false;
                size_t offset = 0;

                for (int i = 0; i < def->num_fields; i++) {
                    FitFieldDef *field = &def->fields[i];

                    if (field->field_def_num == FIT_FIELD_POWER && field->size >= 2) {
                        power = (uint16_t)read_field_value(record_data + offset, field->size,
                                                          field->base_type, def->arch == 1);
                        if (power != 0xFFFF) {
                            has_power = true;
                        }
                    } else if (field->field_def_num == FIT_FIELD_TIMESTAMP && field->size >= 4) {
                        timestamp = (uint32_t)read_field_value(record_data + offset, field->size,
                                                               field->base_type, def->arch == 1);
                    } else if (field->field_def_num == FIT_FIELD_POSITION_LAT && field->size >= 4) {
                        latitude = read_sint32(record_data + offset, def->arch == 1);
                        if (latitude != 0x7FFFFFFF) {
                            has_gps = true;
                        }
                    } else if (field->field_def_num == FIT_FIELD_POSITION_LONG && field->size >= 4) {
                        longitude = read_sint32(record_data + offset, def->arch == 1);
                    } else if (field->field_def_num == FIT_FIELD_HEART_RATE && field->size >= 1) {
                        heart_rate = (uint8_t)read_field_value(record_data + offset, field->size,
                                                               field->base_type, def->arch == 1);
                        if (heart_rate != 0xFF && heart_rate > 0) {
                            has_heart_rate = true;
                        }
                    } else if (field->field_def_num == FIT_FIELD_CADENCE && field->size >= 1) {
                        cadence = (uint8_t)read_field_value(record_data + offset, field->size,
                                                           field->base_type, def->arch == 1);
                        if (cadence != 0xFF && cadence > 0) {
                            has_cadence = true;
                        }
                    }

                    offset += field->size;
                }

                add_sample(data, timestamp, power, has_power, latitude, longitude, has_gps,
                           heart_rate, has_heart_rate, cadence, has_cadence);
                if (has_power) {
                    if (power > data->max_power) data->max_power = power;
                    if (power < data->min_power) data->min_power = power;
                }
            }

            free(record_data);
        }
    }

    fclose(file);

    // Store source file
    strncpy(data->source_file, filename, sizeof(data->source_file) - 1);

    // Calculate statistics
    if (data->count > 0) {
        uint64_t total_power = 0;
        uint64_t total_hr = 0;
        size_t power_count = 0;
        size_t hr_count = 0;
        data->min_lat = 90.0;
        data->max_lat = -90.0;
        data->min_lon = 180.0;
        data->max_lon = -180.0;
        data->max_heart_rate = 0;

        double prev_lat = 0, prev_lon = 0;
        bool has_prev_gps = false;
        uint64_t total_cadence = 0;
        size_t cadence_count = 0;

        for (size_t i = 0; i < data->count; i++) {
            FitPowerSample *sample = &data->samples[i];
            if (sample->has_power) {
                total_power += sample->power;
                power_count++;
            }
            if (sample->has_gps) {
                double lat = sample->latitude * FIT_SEMICIRCLE_TO_DEGREES;
                double lon = sample->longitude * FIT_SEMICIRCLE_TO_DEGREES;
                if (lat < data->min_lat) data->min_lat = lat;
                if (lat > data->max_lat) data->max_lat = lat;
                if (lon < data->min_lon) data->min_lon = lon;
                if (lon > data->max_lon) data->max_lon = lon;
                data->gps_sample_count++;
                data->has_gps_data = true;

                // Calculate distance from previous GPS point
                if (has_prev_gps) {
                    data->total_distance += (float)haversine_distance(prev_lat, prev_lon, lat, lon);
                }
                prev_lat = lat;
                prev_lon = lon;
                has_prev_gps = true;
            }
            if (sample->has_heart_rate) {
                total_hr += sample->heart_rate;
                hr_count++;
                if (sample->heart_rate > data->max_heart_rate) {
                    data->max_heart_rate = sample->heart_rate;
                }
                data->has_heart_rate_data = true;
            }
            if (sample->has_cadence) {
                if (sample->cadence > data->max_cadence) {
                    data->max_cadence = sample->cadence;
                }
                data->has_cadence_data = true;
                // Only include non-zero cadence in average
                if (sample->cadence > 0) {
                    total_cadence += sample->cadence;
                    cadence_count++;
                }
            }
        }
        if (power_count > 0) {
            data->avg_power = (double)total_power / power_count;
        }
        if (hr_count > 0) {
            data->avg_heart_rate = (uint8_t)(total_hr / hr_count);
        }
        if (cadence_count > 0) {
            data->avg_cadence = (uint8_t)(total_cadence / cadence_count);
        }

        // Calculate elapsed time from first/last timestamp
        // FIT timestamps are seconds since 1989-12-31 00:00:00 UTC
        uint32_t first_ts = data->samples[0].timestamp;
        uint32_t last_ts = data->samples[data->count - 1].timestamp;
        data->elapsed_time = (int)(last_ts - first_ts);

        // Convert FIT timestamp to Unix timestamp (FIT epoch is 631065600 seconds after Unix epoch)
        data->start_time = (time_t)(first_ts + 631065600);

        // Default activity type for FIT files
        strncpy(data->activity_type, "Ride", sizeof(data->activity_type) - 1);

        // Generate default title: "YYYY-MM-DD HH:MM Ride"
        struct tm *tm_info = localtime(&data->start_time);
        if (tm_info) {
            char time_str[32];
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", tm_info);
            snprintf(data->title, sizeof(data->title), "%s %s", time_str, data->activity_type);
        }
    }

    if (data->count == 0) {
        data->min_power = 0;
    }

    printf("Parsed %zu samples (%zu with GPS)\n", data->count, data->gps_sample_count);
    if (data->has_gps_data) {
        printf("GPS bounds: lat [%.5f, %.5f], lon [%.5f, %.5f]\n",
               data->min_lat, data->max_lat, data->min_lon, data->max_lon);
    }
    printf("Power range: %u - %u watts, average: %.1f watts\n",
           data->min_power, data->max_power, data->avg_power);
    if (data->has_heart_rate_data) {
        printf("Heart rate: avg %d bpm, max %d bpm\n", data->avg_heart_rate, data->max_heart_rate);
    }
    fflush(stdout);

    return data->count > 0;
}

void fit_power_data_free(FitPowerData *data) {
    if (data->samples) {
        free(data->samples);
        data->samples = NULL;
    }
    data->count = 0;
    data->capacity = 0;
}

// Simple JSON helpers for parsing activity files
static bool json_get_string(const char *json, const char *key, char *out, size_t out_size) {
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = strstr(json, search);
    if (!pos) return false;

    pos = strchr(pos + strlen(search), ':');
    if (!pos) return false;

    while (*pos && (*pos == ':' || *pos == ' ' || *pos == '\t' || *pos == '\n')) pos++;
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

static bool json_get_number(const char *json, const char *key, double *out) {
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = strstr(json, search);
    if (!pos) return false;

    pos = strchr(pos + strlen(search), ':');
    if (!pos) return false;

    while (*pos && (*pos == ':' || *pos == ' ' || *pos == '\t' || *pos == '\n')) pos++;

    char *end;
    *out = strtod(pos, &end);
    return end != pos;
}

// Parse ISO 8601 date string to Unix timestamp
static time_t parse_iso8601(const char *date_str) {
    struct tm tm = {0};
    int year, month, day, hour, minute, second;

    if (sscanf(date_str, "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) == 6) {
        tm.tm_year = year - 1900;
        tm.tm_mon = month - 1;
        tm.tm_mday = day;
        tm.tm_hour = hour;
        tm.tm_min = minute;
        tm.tm_sec = second;
        return timegm(&tm);
    }
    return 0;
}

// Find an array in JSON by key and return pointer to opening bracket
static const char *json_find_array(const char *json, const char *key) {
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = strstr(json, search);
    if (!pos) return NULL;

    pos = strchr(pos + strlen(search), ':');
    if (!pos) return NULL;

    while (*pos && (*pos == ':' || *pos == ' ' || *pos == '\t' || *pos == '\n')) pos++;
    if (*pos != '[') return NULL;
    return pos;
}

// Count elements in a JSON array
static size_t json_count_array_elements(const char *arr_start) {
    if (*arr_start != '[') return 0;

    const char *p = arr_start + 1;
    size_t count = 0;
    int depth = 1;
    bool in_element = false;

    while (*p && depth > 0) {
        if (*p == '[') {
            depth++;
            in_element = true;
        } else if (*p == ']') {
            depth--;
            if (depth == 0 && in_element) count++;
        } else if (*p == ',' && depth == 1) {
            if (in_element) count++;
            in_element = false;
        } else if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
            in_element = true;
        }
        p++;
    }
    return count;
}

// Parse a number at the current position, advance pointer
static double json_parse_number(const char **p) {
    while (**p && (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r')) (*p)++;
    char *end;
    double val = strtod(*p, &end);
    *p = end;
    return val;
}

// Skip to next element in array
static void json_skip_to_next(const char **p) {
    int depth = 0;
    while (**p) {
        if (**p == '[') depth++;
        else if (**p == ']') {
            if (depth == 0) return;
            depth--;
        }
        else if (**p == ',' && depth == 0) {
            (*p)++;
            return;
        }
        (*p)++;
    }
}

bool json_parse_activity(const char *filename, FitPowerData *data) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "Error: Cannot open file %s\n", filename);
        return false;
    }

    // Read entire file
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *json = malloc(file_size + 1);
    if (!json) {
        fclose(file);
        return false;
    }

    size_t read_size = fread(json, 1, file_size, file);
    json[read_size] = '\0';
    fclose(file);

    // Initialize power data
    memset(data, 0, sizeof(FitPowerData));
    data->min_power = UINT16_MAX;

    // Store source file
    strncpy(data->source_file, filename, sizeof(data->source_file) - 1);

    // Get activity metadata
    json_get_string(json, "name", data->title, sizeof(data->title));
    json_get_string(json, "type", data->activity_type, sizeof(data->activity_type));

    // Get start_date for timestamp calculation
    char start_date[64] = "";
    json_get_string(json, "start_date", start_date, sizeof(start_date));
    time_t base_timestamp = parse_iso8601(start_date);
    data->start_time = base_timestamp;

    // Get duration and distance
    double moving_time = 0, elapsed_time = 0, distance = 0;
    if (json_get_number(json, "moving_time", &moving_time)) {
        data->moving_time = (int)moving_time;
    }
    if (json_get_number(json, "elapsed_time", &elapsed_time)) {
        data->elapsed_time = (int)elapsed_time;
    }
    if (json_get_number(json, "distance", &distance)) {
        data->total_distance = (float)distance;
    }

    // Find streams section
    const char *streams_pos = strstr(json, "\"streams\"");
    if (!streams_pos) {
        fprintf(stderr, "Error: No streams section found in JSON\n");
        free(json);
        return false;
    }

    // Find the time array to determine sample count
    const char *time_arr = json_find_array(streams_pos, "time");
    if (!time_arr) {
        fprintf(stderr, "Error: No time stream found in JSON\n");
        free(json);
        return false;
    }

    size_t sample_count = json_count_array_elements(time_arr);
    if (sample_count == 0) {
        fprintf(stderr, "Error: Empty time stream in JSON\n");
        free(json);
        return false;
    }

    // Allocate samples
    data->capacity = sample_count;
    data->samples = malloc(sample_count * sizeof(FitPowerSample));
    if (!data->samples) {
        free(json);
        return false;
    }
    memset(data->samples, 0, sample_count * sizeof(FitPowerSample));

    // Parse time array
    const char *p = time_arr + 1;  // Skip opening bracket
    for (size_t i = 0; i < sample_count; i++) {
        int time_offset = (int)json_parse_number(&p);
        data->samples[i].timestamp = (uint32_t)(base_timestamp + time_offset);
        json_skip_to_next(&p);
    }

    // Parse watts array if present
    const char *watts_arr = json_find_array(streams_pos, "watts");
    if (watts_arr) {
        p = watts_arr + 1;
        for (size_t i = 0; i < sample_count; i++) {
            int watts = (int)json_parse_number(&p);
            if (watts > 0) {
                data->samples[i].power = (uint16_t)watts;
                data->samples[i].has_power = true;
            }
            json_skip_to_next(&p);
        }
    }

    // Parse latlng array if present (array of [lat, lon] pairs)
    const char *latlng_arr = json_find_array(streams_pos, "latlng");
    if (latlng_arr) {
        p = latlng_arr + 1;
        for (size_t i = 0; i < sample_count; i++) {
            // Skip to opening bracket of pair
            while (*p && *p != '[') p++;
            if (!*p) break;
            p++;  // Skip [

            double lat = json_parse_number(&p);
            // Skip comma
            while (*p && *p != ',') p++;
            if (*p == ',') p++;

            double lon = json_parse_number(&p);

            // Skip closing bracket
            while (*p && *p != ']') p++;
            if (*p == ']') p++;

            // Convert to semicircles (FIT format)
            data->samples[i].latitude = (int32_t)(lat / FIT_SEMICIRCLE_TO_DEGREES);
            data->samples[i].longitude = (int32_t)(lon / FIT_SEMICIRCLE_TO_DEGREES);
            data->samples[i].has_gps = true;

            json_skip_to_next(&p);
        }
    }

    // Parse heartrate array if present
    const char *hr_arr = json_find_array(streams_pos, "heartrate");
    if (hr_arr) {
        p = hr_arr + 1;
        for (size_t i = 0; i < sample_count; i++) {
            int hr = (int)json_parse_number(&p);
            if (hr > 0 && hr < 255) {
                data->samples[i].heart_rate = (uint8_t)hr;
                data->samples[i].has_heart_rate = true;
            }
            json_skip_to_next(&p);
        }
    }

    // Parse cadence array if present
    const char *cadence_arr = json_find_array(streams_pos, "cadence");
    if (cadence_arr) {
        p = cadence_arr + 1;
        for (size_t i = 0; i < sample_count; i++) {
            int cad = (int)json_parse_number(&p);
            if (cad > 0 && cad < 255) {
                data->samples[i].cadence = (uint8_t)cad;
                data->samples[i].has_cadence = true;
            }
            json_skip_to_next(&p);
        }
    }

    // Set count
    data->count = sample_count;

    // Calculate elapsed_time from time stream if not set
    if (data->elapsed_time == 0 && sample_count > 1) {
        // Time stream contains offsets from start, so last value is duration
        const char *p_time = time_arr + 1;
        int last_time = 0;
        for (size_t i = 0; i < sample_count; i++) {
            last_time = (int)json_parse_number(&p_time);
            json_skip_to_next(&p_time);
        }
        data->elapsed_time = last_time;
    }

    // Use moving_time as fallback for elapsed_time
    if (data->elapsed_time == 0 && data->moving_time > 0) {
        data->elapsed_time = data->moving_time;
    }

    // Calculate statistics
    if (data->count > 0) {
        uint64_t total_power = 0;
        uint64_t total_hr = 0;
        uint64_t total_cadence = 0;
        size_t power_count = 0;
        size_t hr_count = 0;
        size_t cadence_count = 0;
        data->min_lat = 90.0;
        data->max_lat = -90.0;
        data->min_lon = 180.0;
        data->max_lon = -180.0;
        data->max_heart_rate = 0;
        data->max_cadence = 0;

        for (size_t i = 0; i < data->count; i++) {
            FitPowerSample *sample = &data->samples[i];
            if (sample->has_power) {
                total_power += sample->power;
                power_count++;
                if (sample->power > data->max_power) data->max_power = sample->power;
                if (sample->power < data->min_power) data->min_power = sample->power;
            }
            if (sample->has_gps) {
                double lat = sample->latitude * FIT_SEMICIRCLE_TO_DEGREES;
                double lon = sample->longitude * FIT_SEMICIRCLE_TO_DEGREES;
                if (lat < data->min_lat) data->min_lat = lat;
                if (lat > data->max_lat) data->max_lat = lat;
                if (lon < data->min_lon) data->min_lon = lon;
                if (lon > data->max_lon) data->max_lon = lon;
                data->gps_sample_count++;
                data->has_gps_data = true;
            }
            if (sample->has_heart_rate) {
                total_hr += sample->heart_rate;
                hr_count++;
                if (sample->heart_rate > data->max_heart_rate) {
                    data->max_heart_rate = sample->heart_rate;
                }
                data->has_heart_rate_data = true;
            }
            if (sample->has_cadence) {
                if (sample->cadence > data->max_cadence) {
                    data->max_cadence = sample->cadence;
                }
                data->has_cadence_data = true;
                // Only include non-zero cadence in average
                if (sample->cadence > 0) {
                    total_cadence += sample->cadence;
                    cadence_count++;
                }
            }
        }
        if (power_count > 0) {
            data->avg_power = (double)total_power / power_count;
        }
        if (hr_count > 0) {
            data->avg_heart_rate = (uint8_t)(total_hr / hr_count);
        }
        if (cadence_count > 0) {
            data->avg_cadence = (uint8_t)(total_cadence / cadence_count);
        }
    }

    if (data->count == 0 || data->min_power == UINT16_MAX) {
        data->min_power = 0;
    }

    free(json);

    printf("Parsed %zu samples from JSON (%zu with GPS)\n", data->count, data->gps_sample_count);
    if (data->has_gps_data) {
        printf("GPS bounds: lat [%.5f, %.5f], lon [%.5f, %.5f]\n",
               data->min_lat, data->max_lat, data->min_lon, data->max_lon);
    }
    printf("Power range: %u - %u watts, average: %.1f watts\n",
           data->min_power, data->max_power, data->avg_power);
    if (data->has_heart_rate_data) {
        printf("Heart rate: avg %d bpm, max %d bpm\n", data->avg_heart_rate, data->max_heart_rate);
    }
    fflush(stdout);

    return data->count > 0;
}
