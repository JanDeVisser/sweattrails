#ifndef ZWIFT_WORLDS_H
#define ZWIFT_WORLDS_H

#include <stdbool.h>

typedef struct {
    int id;
    const char *name;
    const char *slug;
    // Detection bounds - used to identify if an activity is in this world
    double min_lat, max_lat;
    double min_lon, max_lon;
    // Image bounds - the GPS area that the mini-map image covers
    double img_min_lat, img_max_lat;
    double img_min_lon, img_max_lon;
    const char *map_url;
    bool is_fictional;  // Fictional worlds have Pacific Ocean coords
} ZwiftWorld;

// Returns NULL if coordinates don't match any Zwift world
const ZwiftWorld *zwift_detect_world(double min_lat, double max_lat,
                                      double min_lon, double max_lon);

// Get total number of Zwift worlds
int zwift_world_count(void);

// Get world by index (for iteration)
const ZwiftWorld *zwift_get_world(int index);

#endif // ZWIFT_WORLDS_H
