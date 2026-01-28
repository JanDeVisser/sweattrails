#ifndef FILE_ORGANIZER_H
#define FILE_ORGANIZER_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

// FIT timestamp epoch offset (1989-12-31 00:00:00 UTC)
#define FIT_TIMESTAMP_OFFSET 631065600

// Create directory path recursively (like mkdir -p)
bool create_directory_path(const char *path);

// Parse FIT file and return Unix timestamp from first record
// Returns 0 on failure
time_t fit_get_activity_timestamp(const char *filepath);

// Move a FIT file from inbox to activity/YYYY/MM/
// data_dir should be ~/.local/share/fitpower
// Returns true on success
bool organize_fit_file(const char *data_dir, const char *filepath);

// Process all .fit files in inbox directory
// Returns number of files processed
int process_inbox(const char *data_dir);

#endif // FILE_ORGANIZER_H
