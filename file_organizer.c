#include "file_organizer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <libgen.h>

// FIT parsing constants (minimal, just for timestamp extraction)
#define FIT_MESG_RECORD 20
#define FIT_MESG_FILE_ID 0
#define FIT_FIELD_TIMESTAMP 253
#define FIT_FIELD_TIME_CREATED 4

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

bool create_directory_path(const char *path) {
    char tmp[512];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);

    // Remove trailing slash
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    // Create directories one by one
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return false;
            }
            *p = '/';
        }
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return false;
    }

    return true;
}

time_t fit_get_activity_timestamp(const char *filepath) {
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        return 0;
    }

    // Read FIT header
    uint8_t header[14];
    if (fread(header, 1, 1, file) != 1) {
        fclose(file);
        return 0;
    }

    uint8_t header_size = header[0];
    if (header_size != 12 && header_size != 14) {
        fclose(file);
        return 0;
    }

    if (fread(header + 1, 1, header_size - 1, file) != (size_t)(header_size - 1)) {
        fclose(file);
        return 0;
    }

    // Verify FIT signature
    if (header[8] != '.' || header[9] != 'F' || header[10] != 'I' || header[11] != 'T') {
        fclose(file);
        return 0;
    }

    uint32_t data_size = read_uint32(header + 4, false);

    // Local message definitions
    typedef struct {
        bool defined;
        uint8_t arch;
        uint16_t global_msg_num;
        uint8_t num_fields;
        uint8_t field_def_nums[256];
        uint8_t field_sizes[256];
        size_t record_size;
    } LocalDef;

    LocalDef definitions[16] = {0};
    uint32_t timestamp = 0;
    size_t bytes_read = 0;

    while (bytes_read < data_size) {
        uint8_t record_header;
        if (fread(&record_header, 1, 1, file) != 1) {
            break;
        }
        bytes_read++;

        if (record_header & 0x80) {
            // Compressed timestamp header
            uint8_t local_msg = (record_header >> 5) & 0x03;
            LocalDef *def = &definitions[local_msg];

            if (!def->defined) {
                continue;
            }

            uint8_t *record_data = malloc(def->record_size);
            if (!record_data || fread(record_data, 1, def->record_size, file) != def->record_size) {
                free(record_data);
                break;
            }
            bytes_read += def->record_size;

            // Check for timestamp in record
            if (def->global_msg_num == FIT_MESG_RECORD) {
                size_t offset = 0;
                for (int i = 0; i < def->num_fields; i++) {
                    if (def->field_def_nums[i] == FIT_FIELD_TIMESTAMP && def->field_sizes[i] >= 4) {
                        timestamp = read_uint32(record_data + offset, def->arch == 1);
                        free(record_data);
                        fclose(file);
                        return (time_t)timestamp + FIT_TIMESTAMP_OFFSET;
                    }
                    offset += def->field_sizes[i];
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

            LocalDef *def = &definitions[local_msg];
            def->defined = true;
            def->arch = def_header[1];
            def->global_msg_num = read_uint16(def_header + 2, def->arch == 1);
            def->num_fields = def_header[4];
            def->record_size = 0;

            for (int i = 0; i < def->num_fields && i < 256; i++) {
                uint8_t field_def[3];
                if (fread(field_def, 1, 3, file) != 3) {
                    break;
                }
                bytes_read += 3;
                def->field_def_nums[i] = field_def[0];
                def->field_sizes[i] = field_def[1];
                def->record_size += field_def[1];
            }

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
            LocalDef *def = &definitions[local_msg];

            if (!def->defined) {
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

            // Check for timestamp in file_id or record message
            if (def->global_msg_num == FIT_MESG_FILE_ID || def->global_msg_num == FIT_MESG_RECORD) {
                size_t offset = 0;
                for (int i = 0; i < def->num_fields; i++) {
                    uint8_t field_num = def->field_def_nums[i];
                    // Look for timestamp (253) or time_created (4 in file_id)
                    if ((field_num == FIT_FIELD_TIMESTAMP ||
                         (def->global_msg_num == FIT_MESG_FILE_ID && field_num == FIT_FIELD_TIME_CREATED))
                        && def->field_sizes[i] >= 4) {
                        timestamp = read_uint32(record_data + offset, def->arch == 1);
                        if (timestamp != 0 && timestamp != 0xFFFFFFFF) {
                            free(record_data);
                            fclose(file);
                            return (time_t)timestamp + FIT_TIMESTAMP_OFFSET;
                        }
                    }
                    offset += def->field_sizes[i];
                }
            }
            free(record_data);
        }
    }

    fclose(file);
    return timestamp ? (time_t)timestamp + FIT_TIMESTAMP_OFFSET : 0;
}

bool organize_fit_file(const char *data_dir, const char *filepath) {
    time_t timestamp = fit_get_activity_timestamp(filepath);
    if (timestamp == 0) {
        fprintf(stderr, "Warning: Could not get timestamp from %s, using current time\n", filepath);
        timestamp = time(NULL);
    }

    struct tm *tm_info = localtime(&timestamp);
    if (!tm_info) {
        return false;
    }

    // Create destination directory: data_dir/activity/YYYY/MM
    char dest_dir[512];
    snprintf(dest_dir, sizeof(dest_dir), "%s/activity/%04d/%02d",
             data_dir, tm_info->tm_year + 1900, tm_info->tm_mon + 1);

    if (!create_directory_path(dest_dir)) {
        fprintf(stderr, "Error: Could not create directory %s\n", dest_dir);
        return false;
    }

    // Get just the filename
    char filepath_copy[512];
    strncpy(filepath_copy, filepath, sizeof(filepath_copy) - 1);
    filepath_copy[sizeof(filepath_copy) - 1] = '\0';
    const char *filename = basename(filepath_copy);

    // Build destination path
    char dest_path[512];
    snprintf(dest_path, sizeof(dest_path), "%s/%s", dest_dir, filename);

    // Check if destination already exists
    struct stat st;
    if (stat(dest_path, &st) == 0) {
        fprintf(stderr, "Warning: File already exists at %s, skipping\n", dest_path);
        return true;  // Not an error, just skip
    }

    // Move the file
    if (rename(filepath, dest_path) != 0) {
        fprintf(stderr, "Error: Could not move %s to %s: %s\n",
                filepath, dest_path, strerror(errno));
        return false;
    }

    printf("Organized: %s -> %s\n", filename, dest_path);
    return true;
}

int process_inbox(const char *data_dir) {
    char inbox_path[512];
    snprintf(inbox_path, sizeof(inbox_path), "%s/inbox", data_dir);

    // Create inbox if it doesn't exist
    if (!create_directory_path(inbox_path)) {
        fprintf(stderr, "Error: Could not create inbox directory\n");
        return 0;
    }

    DIR *dir = opendir(inbox_path);
    if (!dir) {
        return 0;
    }

    int processed = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        size_t len = strlen(entry->d_name);
        if (len > 4 && strcasecmp(entry->d_name + len - 4, ".fit") == 0) {
            char filepath[512];
            snprintf(filepath, sizeof(filepath), "%s/%s", inbox_path, entry->d_name);

            if (organize_fit_file(data_dir, filepath)) {
                processed++;
            }
        }
    }

    closedir(dir);
    return processed;
}
