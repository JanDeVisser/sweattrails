#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include "raylib.h"
#include "fit_parser.h"
#include "strava_api.h"
#include "file_organizer.h"
#include "activity_tree.h"
#include "tile_map.h"
#include "activity_meta.h"

#define MAX_FIT_FILES 256
#define WINDOW_WIDTH 1200
#define WINDOW_HEIGHT 700
#define GRAPH_MARGIN_LEFT 80
#define GRAPH_MARGIN_RIGHT 40
#define GRAPH_MARGIN_TOP 80
#define GRAPH_MARGIN_BOTTOM 60

static char g_downloads_path[512];
static char g_font_path[512];
static char g_data_dir[512];

typedef enum {
    TAB_LOCAL,
    TAB_STRAVA
} TabMode;

typedef enum {
    GRAPH_VIEW_SUMMARY,
    GRAPH_VIEW_POWER,
    GRAPH_VIEW_MAP
} GraphViewMode;

// Text editing state
typedef enum {
    EDIT_NONE,
    EDIT_TITLE,
    EDIT_DESCRIPTION
} EditField;

typedef struct {
    char path[512];
    char name[256];
    time_t mtime;
} FitFileEntry;

// Global font
static Font g_font;

// Helper to draw text with our font
static void DrawTextF(const char *text, int x, int y, int size, Color color) {
    DrawTextEx(g_font, text, (Vector2){(float)x, (float)y}, (float)size, 1.0f, color);
}

static int MeasureTextF(const char *text, int size) {
    return (int)MeasureTextEx(g_font, text, (float)size, 1.0f).x;
}

static int compare_fit_files(const void *a, const void *b) {
    const FitFileEntry *fa = (const FitFileEntry *)a;
    const FitFileEntry *fb = (const FitFileEntry *)b;
    return (int)(fb->mtime - fa->mtime);
}

// Load activity file - detects .json vs .fit and calls appropriate parser
static bool load_activity_file(const char *path, FitPowerData *data) {
    size_t len = strlen(path);
    if (len > 5 && strcasecmp(path + len - 5, ".json") == 0) {
        return json_parse_activity(path, data);
    }
    return fit_parse_file(path, data);
}

static int find_fit_files(FitFileEntry *files, int max_files) {
    DIR *dir = opendir(g_downloads_path);
    if (!dir) {
        fprintf(stderr, "Cannot open Downloads directory: %s\n", g_downloads_path);
        return 0;
    }

    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && count < max_files) {
        size_t len = strlen(entry->d_name);
        if (len > 4 && strcasecmp(entry->d_name + len - 4, ".fit") == 0) {
            snprintf(files[count].path, sizeof(files[count].path),
                     "%s/%s", g_downloads_path, entry->d_name);
            strncpy(files[count].name, entry->d_name, sizeof(files[count].name) - 1);

            struct stat st;
            if (stat(files[count].path, &st) == 0) {
                files[count].mtime = st.st_mtime;
            }
            count++;
        }
    }
    closedir(dir);

    qsort(files, count, sizeof(FitFileEntry), compare_fit_files);
    return count;
}

static void draw_power_graph(FitPowerData *data, int graph_x, int graph_y, int graph_w, int graph_h, int smoothing_seconds) {
    if (data->count < 2) return;

    // Apply smoothing if requested
    float *smoothed = NULL;
    if (smoothing_seconds > 0) {
        smoothed = malloc(data->count * sizeof(float));
        if (smoothed) {
            int half_window = smoothing_seconds / 2;
            for (size_t i = 0; i < data->count; i++) {
                size_t start = (i > (size_t)half_window) ? i - half_window : 0;
                size_t end = (i + half_window < data->count) ? i + half_window : data->count - 1;
                float sum = 0;
                for (size_t j = start; j <= end; j++) sum += data->samples[j].power;
                smoothed[i] = sum / (end - start + 1);
            }
        }
    }

    DrawRectangle(graph_x, graph_y, graph_w, graph_h, (Color){30, 30, 40, 255});

    float min_display = (data->min_power > 20) ? data->min_power - 20 : 0;
    float max_display = data->max_power + 20;
    float display_range = max_display - min_display;

    // Horizontal grid lines
    int num_grid_lines = 5;
    for (int i = 0; i <= num_grid_lines; i++) {
        float y_ratio = (float)i / num_grid_lines;
        int y = graph_y + (int)(y_ratio * graph_h);
        float power_val = max_display - (y_ratio * display_range);

        DrawLine(graph_x, y, graph_x + graph_w, y, (Color){60, 60, 70, 255});

        char label[32];
        snprintf(label, sizeof(label), "%dW", (int)power_val);
        DrawTextF(label, graph_x - 55, y - 8, 16, LIGHTGRAY);
    }

    // Vertical grid lines (time)
    int num_time_markers = 10;
    uint32_t start_time = data->samples[0].timestamp;
    uint32_t end_time = data->samples[data->count - 1].timestamp;
    uint32_t duration = end_time - start_time;

    for (int i = 0; i <= num_time_markers; i++) {
        float x_ratio = (float)i / num_time_markers;
        int x = graph_x + (int)(x_ratio * graph_w);

        DrawLine(x, graph_y, x, graph_y + graph_h, (Color){60, 60, 70, 255});

        uint32_t time_offset = (uint32_t)(x_ratio * duration);
        int minutes = time_offset / 60;
        int seconds = time_offset % 60;

        char label[32];
        snprintf(label, sizeof(label), "%d:%02d", minutes, seconds);
        DrawTextF(label, x - 20, graph_y + graph_h + 10, 14, LIGHTGRAY);
    }

    float x_scale = (float)graph_w / (data->count - 1);

    // Filled area
    for (size_t i = 0; i < data->count - 1; i++) {
        float x1 = graph_x + i * x_scale;
        float x2 = graph_x + (i + 1) * x_scale;

        float power1 = smoothed ? smoothed[i] : data->samples[i].power;
        float power2 = smoothed ? smoothed[i + 1] : data->samples[i + 1].power;

        float y1_ratio = (max_display - power1) / display_range;
        float y2_ratio = (max_display - power2) / display_range;

        float y1 = graph_y + y1_ratio * graph_h;
        float y2 = graph_y + y2_ratio * graph_h;
        float y_bottom = graph_y + graph_h;

        DrawTriangle((Vector2){x1, y1}, (Vector2){x1, y_bottom}, (Vector2){x2, y_bottom}, (Color){30, 100, 180, 80});
        DrawTriangle((Vector2){x1, y1}, (Vector2){x2, y_bottom}, (Vector2){x2, y2}, (Color){30, 100, 180, 80});
    }

    // Power line
    for (size_t i = 0; i < data->count - 1; i++) {
        float x1 = graph_x + i * x_scale;
        float x2 = graph_x + (i + 1) * x_scale;

        float power1 = smoothed ? smoothed[i] : data->samples[i].power;
        float power2 = smoothed ? smoothed[i + 1] : data->samples[i + 1].power;

        float y1_ratio = (max_display - power1) / display_range;
        float y2_ratio = (max_display - power2) / display_range;

        float y1 = graph_y + y1_ratio * graph_h;
        float y2 = graph_y + y2_ratio * graph_h;

        Color line_color = (Color){50, 150, 255, 255};  // Consistent blue

        DrawLineEx((Vector2){x1, y1}, (Vector2){x2, y2}, 2.0f, line_color);
    }

    // Average line
    float avg_y_ratio = (max_display - data->avg_power) / display_range;
    int avg_y = graph_y + (int)(avg_y_ratio * graph_h);
    DrawLine(graph_x, avg_y, graph_x + graph_w, avg_y, (Color){255, 200, 50, 200});

    char avg_label[64];
    snprintf(avg_label, sizeof(avg_label), "Avg: %.0fW", data->avg_power);
    DrawTextF(avg_label, graph_x + graph_w - 100, avg_y - 20, 16, (Color){255, 200, 50, 255});

    // Free smoothed buffer
    if (smoothed) free(smoothed);
}

static bool draw_button(int x, int y, int w, int h, const char *text, bool enabled) {
    Vector2 mouse = GetMousePosition();
    bool hover = enabled && mouse.x >= x && mouse.x < x + w && mouse.y >= y && mouse.y < y + h;
    bool clicked = hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);

    Color bg = enabled ? (hover ? (Color){80, 100, 140, 255} : (Color){60, 80, 120, 255}) : (Color){40, 40, 50, 255};
    Color fg = enabled ? WHITE : GRAY;

    DrawRectangle(x, y, w, h, bg);
    DrawRectangleLines(x, y, w, h, (Color){100, 120, 160, 255});

    int text_w = MeasureTextF(text, 14);
    DrawTextF(text, x + (w - text_w) / 2, y + (h - 16) / 2, 16, fg);

    return clicked;
}

// Format seconds as HH:MM:SS or MM:SS
static void format_duration(int seconds, char *out, size_t out_size) {
    int hours = seconds / 3600;
    int mins = (seconds % 3600) / 60;
    int secs = seconds % 60;
    if (hours > 0) {
        snprintf(out, out_size, "%d:%02d:%02d", hours, mins, secs);
    } else {
        snprintf(out, out_size, "%d:%02d", mins, secs);
    }
}

// Draw an editable text field, returns true if clicked to start editing
static bool draw_text_field(int x, int y, int w, int h, const char *text,
                            bool is_editing, int cursor_pos, double blink_time) {
    Vector2 mouse = GetMousePosition();
    bool hover = mouse.x >= x && mouse.x < x + w && mouse.y >= y && mouse.y < y + h;
    bool clicked = hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);

    // Background
    Color bg = is_editing ? (Color){50, 50, 60, 255} : (hover ? (Color){40, 40, 50, 255} : (Color){35, 35, 45, 255});
    DrawRectangle(x, y, w, h, bg);
    DrawRectangleLines(x, y, w, h, is_editing ? (Color){100, 150, 255, 255} : (Color){60, 60, 70, 255});

    // Text (clip to field width)
    int text_x = x + 8;
    int text_y = y + (h - 16) / 2;

    // Simple clipping by truncating display
    char display[512];
    strncpy(display, text, sizeof(display) - 1);
    display[sizeof(display) - 1] = '\0';

    // Measure and truncate if needed
    int max_width = w - 16;
    while (MeasureTextF(display, 15) > max_width && strlen(display) > 0) {
        display[strlen(display) - 1] = '\0';
    }

    DrawTextF(display, text_x, text_y, 15, WHITE);

    // Cursor when editing
    if (is_editing) {
        // Calculate cursor x position
        char temp[512];
        strncpy(temp, text, sizeof(temp) - 1);
        temp[sizeof(temp) - 1] = '\0';
        if (cursor_pos < (int)strlen(temp)) {
            temp[cursor_pos] = '\0';
        }
        int cursor_x = text_x + MeasureTextF(temp, 15);

        // Blinking cursor
        if ((int)(blink_time * 2) % 2 == 0) {
            DrawRectangle(cursor_x, text_y, 2, 16, WHITE);
        }
    }

    return clicked;
}

// Draw multiline text area, returns true if clicked
static bool draw_text_area(int x, int y, int w, int h, const char *text,
                           bool is_editing, int cursor_pos, double blink_time) {
    Vector2 mouse = GetMousePosition();
    bool hover = mouse.x >= x && mouse.x < x + w && mouse.y >= y && mouse.y < y + h;
    bool clicked = hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);

    // Background
    Color bg = is_editing ? (Color){50, 50, 60, 255} : (hover ? (Color){40, 40, 50, 255} : (Color){35, 35, 45, 255});
    DrawRectangle(x, y, w, h, bg);
    DrawRectangleLines(x, y, w, h, is_editing ? (Color){100, 150, 255, 255} : (Color){60, 60, 70, 255});

    // Draw text with simple word wrapping
    int text_x = x + 8;
    int text_y = y + 8;
    int line_height = 18;
    int max_width = w - 16;
    int max_lines = (h - 16) / line_height;

    const char *p = text;
    int line = 0;
    int char_index = 0;
    int cursor_draw_x = text_x;
    int cursor_draw_y = text_y;

    while (*p && line < max_lines) {
        // Find end of line (newline or wrap point)
        const char *line_start = p;
        const char *line_end = p;
        const char *word_end = p;

        while (*line_end && *line_end != '\n') {
            // Find next word boundary
            while (*word_end && *word_end != ' ' && *word_end != '\n') word_end++;

            // Check if this word fits
            char temp[512];
            size_t len = word_end - line_start;
            if (len >= sizeof(temp)) len = sizeof(temp) - 1;
            strncpy(temp, line_start, len);
            temp[len] = '\0';

            if (MeasureTextF(temp, 15) > max_width && line_end > line_start) {
                break;  // Word doesn't fit, wrap here
            }

            line_end = word_end;
            if (*word_end == ' ') word_end++;
        }

        if (line_end == line_start && *line_end && *line_end != '\n') {
            // Single word too long, force break
            while (*line_end && *line_end != '\n') {
                char temp[512];
                size_t len = line_end - line_start + 1;
                if (len >= sizeof(temp)) len = sizeof(temp) - 1;
                strncpy(temp, line_start, len);
                temp[len] = '\0';
                if (MeasureTextF(temp, 15) > max_width) break;
                line_end++;
            }
        }

        // Draw this line
        char line_text[512];
        size_t line_len = line_end - line_start;
        if (line_len >= sizeof(line_text)) line_len = sizeof(line_text) - 1;
        strncpy(line_text, line_start, line_len);
        line_text[line_len] = '\0';

        DrawTextF(line_text, text_x, text_y + line * line_height, 15, WHITE);

        // Track cursor position
        int line_start_idx = char_index;
        int line_end_idx = char_index + (int)line_len;

        if (is_editing && cursor_pos >= line_start_idx && cursor_pos <= line_end_idx) {
            char temp[512];
            int pos_in_line = cursor_pos - line_start_idx;
            strncpy(temp, line_start, pos_in_line);
            temp[pos_in_line] = '\0';
            cursor_draw_x = text_x + MeasureTextF(temp, 15);
            cursor_draw_y = text_y + line * line_height;
        }

        char_index += (int)line_len;
        p = line_end;
        if (*p == '\n') {
            p++;
            char_index++;
        }
        line++;
    }

    // Draw cursor
    if (is_editing && (int)(blink_time * 2) % 2 == 0) {
        DrawRectangle(cursor_draw_x, cursor_draw_y, 2, 16, WHITE);
    }

    return clicked;
}

// Draw the Summary tab content
static void draw_summary_tab(FitPowerData *data, ActivityMeta *meta, EditField *edit_field,
                             int *cursor_pos, double blink_time, int x, int y, int w, int h) {
    int label_x = x + 20;
    int value_x = x + 150;
    int row_height = 28;
    int current_y = y + 15;

    // Title (editable)
    DrawTextF("Title:", label_x, current_y + 4, 15, LIGHTGRAY);
    bool title_clicked = draw_text_field(value_x, current_y, w - 170, 24,
                                         data->title, *edit_field == EDIT_TITLE, *cursor_pos, blink_time);
    if (title_clicked && *edit_field != EDIT_TITLE) {
        *edit_field = EDIT_TITLE;
        *cursor_pos = (int)strlen(data->title);
    }
    current_y += row_height;

    // Activity Type (read-only)
    DrawTextF("Type:", label_x, current_y + 4, 15, LIGHTGRAY);
    DrawTextF(data->activity_type[0] ? data->activity_type : "-", value_x, current_y + 4, 15, WHITE);
    current_y += row_height;

    // Date
    DrawTextF("Date:", label_x, current_y + 4, 15, LIGHTGRAY);
    if (data->start_time > 0) {
        char date_str[64];
        struct tm *tm_info = localtime(&data->start_time);
        if (tm_info) {
            strftime(date_str, sizeof(date_str), "%Y-%m-%d %H:%M", tm_info);
            DrawTextF(date_str, value_x, current_y + 4, 15, WHITE);
        }
    } else {
        DrawTextF("-", value_x, current_y + 4, 15, WHITE);
    }
    current_y += row_height;

    // Duration
    DrawTextF("Duration:", label_x, current_y + 4, 15, LIGHTGRAY);
    if (data->elapsed_time > 0) {
        char duration_str[32];
        format_duration(data->elapsed_time, duration_str, sizeof(duration_str));
        if (data->moving_time > 0 && data->moving_time != data->elapsed_time) {
            char moving_str[32];
            format_duration(data->moving_time, moving_str, sizeof(moving_str));
            char combined[80];
            snprintf(combined, sizeof(combined), "%s (moving: %s)", duration_str, moving_str);
            DrawTextF(combined, value_x, current_y + 4, 15, WHITE);
        } else {
            DrawTextF(duration_str, value_x, current_y + 4, 15, WHITE);
        }
    } else {
        DrawTextF("-", value_x, current_y + 4, 15, WHITE);
    }
    current_y += row_height;

    // Distance
    DrawTextF("Distance:", label_x, current_y + 4, 15, LIGHTGRAY);
    if (data->total_distance > 0) {
        char dist_str[32];
        snprintf(dist_str, sizeof(dist_str), "%.2f km", data->total_distance / 1000.0f);
        DrawTextF(dist_str, value_x, current_y + 4, 15, WHITE);
    } else {
        DrawTextF("-", value_x, current_y + 4, 15, WHITE);
    }
    current_y += row_height;

    // Average Speed (if distance and time available)
    DrawTextF("Avg Speed:", label_x, current_y + 4, 15, LIGHTGRAY);
    int time_for_speed = data->moving_time > 0 ? data->moving_time : data->elapsed_time;
    if (data->total_distance > 0 && time_for_speed > 0) {
        float speed_kmh = (data->total_distance / 1000.0f) / (time_for_speed / 3600.0f);
        char speed_str[32];
        snprintf(speed_str, sizeof(speed_str), "%.1f km/h", speed_kmh);
        DrawTextF(speed_str, value_x, current_y + 4, 15, WHITE);
    } else {
        DrawTextF("-", value_x, current_y + 4, 15, WHITE);
    }
    current_y += row_height;

    // Power stats
    DrawTextF("Power:", label_x, current_y + 4, 15, LIGHTGRAY);
    if (data->avg_power > 0) {
        char power_str[64];
        snprintf(power_str, sizeof(power_str), "%.0f W avg / %d W max", data->avg_power, data->max_power);
        DrawTextF(power_str, value_x, current_y + 4, 15, WHITE);
    } else {
        DrawTextF("-", value_x, current_y + 4, 15, WHITE);
    }
    current_y += row_height;

    // Heart rate stats
    DrawTextF("Heart Rate:", label_x, current_y + 4, 15, LIGHTGRAY);
    if (data->has_heart_rate_data) {
        char hr_str[64];
        snprintf(hr_str, sizeof(hr_str), "%d bpm avg / %d bpm max", data->avg_heart_rate, data->max_heart_rate);
        DrawTextF(hr_str, value_x, current_y + 4, 15, WHITE);
    } else {
        DrawTextF("-", value_x, current_y + 4, 15, WHITE);
    }
    current_y += row_height;

    // Cadence stats
    DrawTextF("Cadence:", label_x, current_y + 4, 15, LIGHTGRAY);
    if (data->has_cadence_data) {
        char cad_str[64];
        snprintf(cad_str, sizeof(cad_str), "%d rpm avg / %d rpm max", data->avg_cadence, data->max_cadence);
        DrawTextF(cad_str, value_x, current_y + 4, 15, WHITE);
    } else {
        DrawTextF("-", value_x, current_y + 4, 15, WHITE);
    }
    current_y += row_height + 10;

    // Description/Notes (editable multiline)
    DrawTextF("Notes:", label_x, current_y + 4, 15, LIGHTGRAY);
    current_y += 22;

    int desc_height = h - (current_y - y) - 20;
    if (desc_height < 60) desc_height = 60;

    bool desc_clicked = draw_text_area(label_x, current_y, w - 40, desc_height,
                                       data->description, *edit_field == EDIT_DESCRIPTION, *cursor_pos, blink_time);
    if (desc_clicked && *edit_field != EDIT_DESCRIPTION) {
        *edit_field = EDIT_DESCRIPTION;
        *cursor_pos = (int)strlen(data->description);
    }

    // Click outside to stop editing
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !title_clicked && !desc_clicked) {
        *edit_field = EDIT_NONE;
    }

    // Store meta state for saving
    (void)meta;  // Meta is updated in main loop when editing stops
}

static void init_paths(void) {
    const char *home = getenv("HOME");
    if (!home) home = ".";

    // Set downloads path
    snprintf(g_downloads_path, sizeof(g_downloads_path), "%s/Downloads", home);

    // Set data directory (platform-specific)
#ifdef __APPLE__
    snprintf(g_data_dir, sizeof(g_data_dir), "%s/Library/Application Support/fitpower", home);
#else
    snprintf(g_data_dir, sizeof(g_data_dir), "%s/.local/share/fitpower", home);
#endif

    // Try multiple font locations
    const char *font_paths[] = {
        "%s/.local/share/fonts/JetBrainsMono-Regular.ttf",
        "%s/.local/share/fonts/JetBrainsMonoNerdFont-Regular.ttf",
        "%s/Library/Fonts/JetBrainsMono-VariableFont_wght.ttf",
        NULL
    };

    g_font_path[0] = '\0';
    for (int i = 0; font_paths[i]; i++) {
        char path[512];
        snprintf(path, sizeof(path), font_paths[i], home);
        if (FileExists(path)) {
            strncpy(g_font_path, path, sizeof(g_font_path) - 1);
            break;
        }
    }
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    init_paths();

    // Create data directories and process inbox
    char inbox_path[512];
    snprintf(inbox_path, sizeof(inbox_path), "%s/inbox", g_data_dir);
    create_directory_path(inbox_path);

    int inbox_processed = process_inbox(g_data_dir);
    if (inbox_processed > 0) {
        printf("Processed %d files from inbox\n", inbox_processed);
    }

    // Build activity tree
    ActivityTree activity_tree;
    activity_tree_init(&activity_tree);
    activity_tree_scan(&activity_tree, g_data_dir);
    printf("Scanned activity tree: %zu years\n", activity_tree.year_count);

    // Keep legacy file list for backwards compatibility (may be removed later)
    FitFileEntry *fit_files = malloc(MAX_FIT_FILES * sizeof(FitFileEntry));
    int num_files = find_fit_files(fit_files, MAX_FIT_FILES);
    printf("Found %d local FIT files in Downloads\n", num_files);

    // Load Strava config
    StravaConfig strava_config = {0};
    bool strava_config_loaded = strava_load_config(&strava_config);
    StravaActivityList strava_activities = {0};
    bool strava_activities_loaded = false;
    bool strava_loading = false;
    bool strava_downloading = false;

    // Initialize raylib
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "FIT Power Viewer");
    MaximizeWindow();
    SetTargetFPS(60);

    // Load custom font
    if (g_font_path[0]) {
        g_font = LoadFontEx(g_font_path, 32, NULL, 0);
        SetTextureFilter(g_font.texture, TEXTURE_FILTER_BILINEAR);
    } else {
        g_font = GetFontDefault();
    }

    // State
    TabMode current_tab = TAB_LOCAL;
    GraphViewMode graph_view = GRAPH_VIEW_SUMMARY;
    int selected_tree = 0;
    int selected_strava = 0;
    int tree_scroll_offset = 0;
    int strava_scroll_offset = 0;
    int visible_files = 15;
    FitPowerData power_data = {0};
    bool file_loaded = false;
    char status_message[256] = "Select a file to view power data";
    char current_title[256] = "";

    // Graph smoothing state
    int smoothing_index = 0;  // 0=none, 1=5s, 2=15s, 3=30s, 4=1m, 5=2m, 6=5m
    const int smoothing_seconds[] = {0, 5, 15, 30, 60, 120, 300};
    const char *smoothing_labels[] = {"Off", "5s", "15s", "30s", "1m", "2m", "5m"};
    const int smoothing_count = 7;

    // Map state
    TileCache tile_cache;
    tile_cache_init(&tile_cache);
    MapView map_view = {0};

    // Summary tab editing state
    ActivityMeta activity_meta = {0};
    EditField edit_field = EDIT_NONE;
    int cursor_pos = 0;
    double blink_time = 0;
    char original_title[256] = "";
    char original_description[2048] = "";

    // Load first file from activity tree if available
    size_t tree_visible = activity_tree_visible_count(&activity_tree);
    if (tree_visible > 0) {
        // Find first file node
        for (size_t i = 0; i < tree_visible; i++) {
            TreeNode *node = activity_tree_get_visible(&activity_tree, i);
            if (node && node->type == NODE_FILE) {
                selected_tree = (int)i;
                printf("Loading: %s\n", node->full_path);
                fflush(stdout);
                if (load_activity_file(node->full_path, &power_data)) {
                    file_loaded = true;
                    graph_view = GRAPH_VIEW_SUMMARY;
                    map_view.zoom = 0;  // Reset to recalculate on next map view
                    snprintf(status_message, sizeof(status_message), "Loaded: %s (%zu samples)", node->name, power_data.count);
                    strncpy(current_title, power_data.title, sizeof(current_title) - 1);

                    // Load metadata sidecar if exists
                    if (activity_meta_load(node->full_path, &activity_meta)) {
                        if (activity_meta.title_edited && activity_meta.title[0]) {
                            strncpy(power_data.title, activity_meta.title, sizeof(power_data.title) - 1);
                            strncpy(current_title, power_data.title, sizeof(current_title) - 1);
                        }
                        if (activity_meta.description_edited && activity_meta.description[0]) {
                            strncpy(power_data.description, activity_meta.description, sizeof(power_data.description) - 1);
                        }
                    }
                    strncpy(original_title, power_data.title, sizeof(original_title) - 1);
                    strncpy(original_description, power_data.description, sizeof(original_description) - 1);
                }
                break;
            }
        }
    }

    while (!WindowShouldClose()) {
        int key = GetKeyPressed();
        Vector2 mouse = GetMousePosition();

        // Tab switching with number keys
        if (key == KEY_ONE) current_tab = TAB_LOCAL;
        if (key == KEY_TWO) current_tab = TAB_STRAVA;

        // Graph view switching with S/P/M keys (only when not editing text)
        if (edit_field == EDIT_NONE) {
            if (key == KEY_S) graph_view = GRAPH_VIEW_SUMMARY;
            if (key == KEY_G) graph_view = GRAPH_VIEW_POWER;
            if (key == KEY_M && power_data.has_gps_data) graph_view = GRAPH_VIEW_MAP;
        }

        // Update blink timer for cursor
        blink_time += GetFrameTime();

        // Update tree visible count (may change with expand/collapse)
        tree_visible = activity_tree_visible_count(&activity_tree);

        int list_count = (current_tab == TAB_LOCAL) ? (int)tree_visible : (int)strava_activities.count;
        int *selected = (current_tab == TAB_LOCAL) ? &selected_tree : &selected_strava;
        int *scroll = (current_tab == TAB_LOCAL) ? &tree_scroll_offset : &strava_scroll_offset;

        // Navigation
        if (key == KEY_DOWN || key == KEY_J) {
            if (*selected < list_count - 1) {
                (*selected)++;
                if (*selected >= *scroll + visible_files) {
                    *scroll = *selected - visible_files + 1;
                }
            }
        } else if (key == KEY_UP || key == KEY_K) {
            if (*selected > 0) {
                (*selected)--;
                if (*selected < *scroll) {
                    *scroll = *selected;
                }
            }
        } else if (key == KEY_PAGE_DOWN) {
            *selected += visible_files;
            if (*selected >= list_count) *selected = list_count - 1;
            if (*selected < 0) *selected = 0;
            *scroll = *selected - visible_files + 1;
            if (*scroll < 0) *scroll = 0;
        } else if (key == KEY_PAGE_UP) {
            *selected -= visible_files;
            if (*selected < 0) *selected = 0;
            *scroll = *selected;
        }

        // Handle tree expansion/collapse with LEFT/RIGHT keys
        if (current_tab == TAB_LOCAL && tree_visible > 0) {
            TreeNode *selected_node = activity_tree_get_visible(&activity_tree, (size_t)selected_tree);
            if (selected_node) {
                if (key == KEY_LEFT && (selected_node->type == NODE_YEAR || selected_node->type == NODE_MONTH || selected_node->type == NODE_GROUP)) {
                    if (selected_node->expanded) {
                        selected_node->expanded = false;
                    }
                } else if (key == KEY_RIGHT && (selected_node->type == NODE_YEAR || selected_node->type == NODE_MONTH || selected_node->type == NODE_GROUP)) {
                    if (!selected_node->expanded) {
                        selected_node->expanded = true;
                    }
                }
            }
        }

        // Load on Enter/Space for local tree files (only when not editing text)
        if ((key == KEY_ENTER || key == KEY_SPACE) && current_tab == TAB_LOCAL && tree_visible > 0 && edit_field == EDIT_NONE) {
            TreeNode *node = activity_tree_get_visible(&activity_tree, (size_t)selected_tree);
            if (node) {
                if (node->type == NODE_FILE) {
                    // Load the file
                    fit_power_data_free(&power_data);
                    file_loaded = false;
                    graph_view = GRAPH_VIEW_SUMMARY;
                    edit_field = EDIT_NONE;
                    zwift_map_free(&map_view);  // Free any loaded Zwift map
                    map_view.zoom = 0;  // Reset to recalculate on next map view

                    if (load_activity_file(node->full_path, &power_data)) {
                        file_loaded = true;
                        snprintf(status_message, sizeof(status_message), "Loaded: %s (%zu samples)", node->name, power_data.count);
                        strncpy(current_title, power_data.title, sizeof(current_title) - 1);

                        // Load metadata sidecar if exists
                        memset(&activity_meta, 0, sizeof(activity_meta));
                        if (activity_meta_load(node->full_path, &activity_meta)) {
                            if (activity_meta.title_edited && activity_meta.title[0]) {
                                strncpy(power_data.title, activity_meta.title, sizeof(power_data.title) - 1);
                                strncpy(current_title, power_data.title, sizeof(current_title) - 1);
                            }
                            if (activity_meta.description_edited && activity_meta.description[0]) {
                                strncpy(power_data.description, activity_meta.description, sizeof(power_data.description) - 1);
                            }
                        }
                        strncpy(original_title, power_data.title, sizeof(original_title) - 1);
                        strncpy(original_description, power_data.description, sizeof(original_description) - 1);
                    } else {
                        snprintf(status_message, sizeof(status_message), "Failed to load: %s", node->name);
                    }
                } else {
                    // Toggle expand/collapse for year/month nodes
                    node->expanded = !node->expanded;
                }
            }
        }

        // Mouse wheel scrolling
        float wheel = GetMouseWheelMove();
        if (wheel != 0 && mouse.x < 375) {
            *scroll -= (int)wheel * 3;
            if (*scroll < 0) *scroll = 0;
            if (*scroll > list_count - visible_files) {
                *scroll = list_count - visible_files;
                if (*scroll < 0) *scroll = 0;
            }
        }

        // Text editing input handling
        if (edit_field != EDIT_NONE) {
            char *text_buffer = (edit_field == EDIT_TITLE) ? power_data.title : power_data.description;
            size_t max_len = (edit_field == EDIT_TITLE) ? sizeof(power_data.title) - 1 : sizeof(power_data.description) - 1;
            size_t text_len = strlen(text_buffer);

            // Handle character input
            int ch;
            while ((ch = GetCharPressed()) != 0) {
                if (ch >= 32 && ch < 127 && text_len < max_len) {
                    // Insert character at cursor position
                    memmove(text_buffer + cursor_pos + 1, text_buffer + cursor_pos, text_len - cursor_pos + 1);
                    text_buffer[cursor_pos] = (char)ch;
                    cursor_pos++;
                    text_len++;
                }
            }

            // Handle special keys
            if (IsKeyPressed(KEY_BACKSPACE) && cursor_pos > 0) {
                memmove(text_buffer + cursor_pos - 1, text_buffer + cursor_pos, text_len - cursor_pos + 1);
                cursor_pos--;
            }
            if (IsKeyPressed(KEY_DELETE) && cursor_pos < (int)text_len) {
                memmove(text_buffer + cursor_pos, text_buffer + cursor_pos + 1, text_len - cursor_pos);
            }
            if (IsKeyPressed(KEY_LEFT) && cursor_pos > 0) cursor_pos--;
            if (IsKeyPressed(KEY_RIGHT) && cursor_pos < (int)text_len) cursor_pos++;
            if (IsKeyPressed(KEY_HOME)) cursor_pos = 0;
            if (IsKeyPressed(KEY_END)) cursor_pos = (int)text_len;

            // Enter key handling (single line ends editing, multiline adds newline)
            if (IsKeyPressed(KEY_ENTER)) {
                if (edit_field == EDIT_TITLE) {
                    // End title editing
                    edit_field = EDIT_NONE;
                } else if (edit_field == EDIT_DESCRIPTION && text_len < max_len) {
                    // Add newline for description
                    memmove(text_buffer + cursor_pos + 1, text_buffer + cursor_pos, text_len - cursor_pos + 1);
                    text_buffer[cursor_pos] = '\n';
                    cursor_pos++;
                }
            }

            // Escape cancels editing and reverts changes
            if (IsKeyPressed(KEY_ESCAPE)) {
                if (edit_field == EDIT_TITLE) {
                    strncpy(power_data.title, original_title, sizeof(power_data.title) - 1);
                } else {
                    strncpy(power_data.description, original_description, sizeof(power_data.description) - 1);
                }
                edit_field = EDIT_NONE;
            }

            // Save when editing stops (will be triggered by clicking outside)
            // Actual save happens when edit_field changes from non-NONE to NONE
        }

        // Check if editing just stopped - save if content changed
        static EditField prev_edit_field = EDIT_NONE;
        if (prev_edit_field != EDIT_NONE && edit_field == EDIT_NONE && file_loaded) {
            bool title_changed = strcmp(power_data.title, original_title) != 0;
            bool desc_changed = strcmp(power_data.description, original_description) != 0;

            if (title_changed || desc_changed) {
                // Update activity_meta and save
                if (title_changed) {
                    strncpy(activity_meta.title, power_data.title, sizeof(activity_meta.title) - 1);
                    activity_meta.title_edited = true;
                    strncpy(current_title, power_data.title, sizeof(current_title) - 1);
                }
                if (desc_changed) {
                    strncpy(activity_meta.description, power_data.description, sizeof(activity_meta.description) - 1);
                    activity_meta.description_edited = true;
                }

                // Save to sidecar file
                if (activity_meta_save(power_data.source_file, &activity_meta)) {
                    snprintf(status_message, sizeof(status_message), "Saved metadata");
                }

                // Update originals
                strncpy(original_title, power_data.title, sizeof(original_title) - 1);
                strncpy(original_description, power_data.description, sizeof(original_description) - 1);
            }
        }
        prev_edit_field = edit_field;

        // Drawing
        BeginDrawing();
        ClearBackground((Color){20, 20, 25, 255});

        // Calculate visible files based on window height (status bar takes 30px)
        visible_files = (GetScreenHeight() - 110) / 25;
        if (visible_files < 5) visible_files = 5;

        // Title
        DrawTextF("FIT Power Viewer", 10, 10, 26, WHITE);

        // Tabs
        int tab_y = 45;
        if (draw_button(10, tab_y, 90, 25, "1: Local", true)) {
            current_tab = TAB_LOCAL;
        }
        if (current_tab == TAB_LOCAL) {
            DrawRectangle(10, tab_y + 23, 90, 2, (Color){100, 150, 255, 255});
        }

        if (draw_button(105, tab_y, 90, 25, "2: Strava", strava_config_loaded)) {
            current_tab = TAB_STRAVA;
        }
        if (current_tab == TAB_STRAVA) {
            DrawRectangle(105, tab_y + 23, 90, 2, (Color){252, 82, 0, 255});
        }

        // List panel
        int list_y = tab_y + 35;
        DrawRectangle(5, list_y, 365, visible_files * 25 + 10, (Color){35, 35, 45, 255});

        if (current_tab == TAB_LOCAL) {
            DrawTextF("Activities:", 10, list_y + 5, 15, LIGHTGRAY);

            for (int i = 0; i < visible_files && i + tree_scroll_offset < (int)tree_visible; i++) {
                int node_idx = i + tree_scroll_offset;
                int y = list_y + 25 + i * 25;

                TreeNode *node = activity_tree_get_visible(&activity_tree, (size_t)node_idx);
                if (!node) continue;

                bool hover = mouse.x >= 8 && mouse.x < 367 && mouse.y >= y - 2 && mouse.y < y + 20;

                if (node_idx == selected_tree) {
                    DrawRectangle(8, y - 2, 359, 22, (Color){60, 80, 120, 255});
                } else if (hover) {
                    DrawRectangle(8, y - 2, 359, 22, (Color){45, 45, 55, 255});
                }

                // Click to select and possibly load/toggle
                if (hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    selected_tree = node_idx;
                    if (node->type == NODE_FILE) {
                        fit_power_data_free(&power_data);
                        file_loaded = false;
                        graph_view = GRAPH_VIEW_SUMMARY;
                        edit_field = EDIT_NONE;
                        zwift_map_free(&map_view);  // Free any loaded Zwift map
                        map_view.zoom = 0;  // Reset to recalculate on next map view

                        if (load_activity_file(node->full_path, &power_data)) {
                            file_loaded = true;
                            snprintf(status_message, sizeof(status_message), "Loaded: %s (%zu samples)", node->name, power_data.count);
                            strncpy(current_title, power_data.title, sizeof(current_title) - 1);

                            // Load metadata sidecar if exists
                            memset(&activity_meta, 0, sizeof(activity_meta));
                            if (activity_meta_load(node->full_path, &activity_meta)) {
                                if (activity_meta.title_edited && activity_meta.title[0]) {
                                    strncpy(power_data.title, activity_meta.title, sizeof(power_data.title) - 1);
                                    strncpy(current_title, power_data.title, sizeof(current_title) - 1);
                                }
                                if (activity_meta.description_edited && activity_meta.description[0]) {
                                    strncpy(power_data.description, activity_meta.description, sizeof(power_data.description) - 1);
                                }
                            }
                            strncpy(original_title, power_data.title, sizeof(original_title) - 1);
                            strncpy(original_description, power_data.description, sizeof(original_description) - 1);
                        }
                    } else {
                        // Toggle expand/collapse
                        node->expanded = !node->expanded;
                    }
                }

                // Determine indentation and prefix based on node type
                int indent = 0;
                char prefix[8] = "";
                Color text_color = (node_idx == selected_tree) ? WHITE : LIGHTGRAY;

                if (node->type == NODE_YEAR) {
                    snprintf(prefix, sizeof(prefix), "%s ", node->expanded ? "[-]" : "[+]");
                    text_color = (node_idx == selected_tree) ? WHITE : (Color){150, 180, 255, 255};
                } else if (node->type == NODE_MONTH) {
                    indent = 16;
                    snprintf(prefix, sizeof(prefix), "%s ", node->expanded ? "[-]" : "[+]");
                    text_color = (node_idx == selected_tree) ? WHITE : (Color){180, 200, 150, 255};
                } else if (node->type == NODE_GROUP) {
                    indent = 32;
                    snprintf(prefix, sizeof(prefix), "%s ", node->expanded ? "[-]" : "[+]");
                    text_color = (node_idx == selected_tree) ? WHITE : (Color){255, 200, 150, 255};
                } else if (node->type == NODE_FILE) {
                    // Check if this file is inside a group (has siblings with same parent time)
                    // Files directly under month get indent 32, files under group get indent 48
                    indent = (node->full_path[0] != '\0') ? 32 : 32;  // Will be 48 if under group
                }

                // Detect if file is under a group by checking previous visible nodes
                if (node->type == NODE_FILE) {
                    // Look back to find parent - if it's a group, increase indent
                    for (int look = node_idx - 1; look >= 0; look--) {
                        TreeNode *parent = activity_tree_get_visible(&activity_tree, (size_t)look);
                        if (parent && parent->type == NODE_GROUP && parent->expanded) {
                            indent = 48;
                            break;
                        }
                        if (parent && (parent->type == NODE_MONTH || parent->type == NODE_YEAR)) {
                            break;
                        }
                    }
                }

                char display_name[50];
                int max_chars = 40 - (indent / 8);
                const char *text = (node->type == NODE_FILE || node->type == NODE_GROUP) ? node->display_title : node->name;
                snprintf(display_name, sizeof(display_name), "%s%.*s%s",
                         prefix, max_chars, text,
                         (int)strlen(text) > max_chars ? "..." : "");

                DrawTextF(display_name, 12 + indent, y, 15, text_color);
            }

            if (tree_scroll_offset > 0) DrawTextF("^", 145, list_y + 8, 15, GRAY);
            if (tree_scroll_offset + visible_files < (int)tree_visible) DrawTextF("v", 145, list_y + visible_files * 25 + 5, 15, GRAY);

            // Show hint if tree is empty
            if (tree_visible == 0) {
                DrawTextF("No activities found.", 12, list_y + 30, 14, GRAY);
                DrawTextF("Drop .fit files in:", 12, list_y + 50, 14, GRAY);
#ifdef __APPLE__
                DrawTextF("~/Library/Application Support/", 12, list_y + 70, 13, (Color){100, 150, 200, 255});
                DrawTextF("fitpower/inbox/", 12, list_y + 88, 13, (Color){100, 150, 200, 255});
#else
                DrawTextF("~/.local/share/fitpower/inbox/", 12, list_y + 70, 13, (Color){100, 150, 200, 255});
#endif
            }

        } else if (current_tab == TAB_STRAVA) {
            if (!strava_is_authenticated(&strava_config)) {
                DrawTextF("Strava: Not connected", 10, list_y + 5, 15, (Color){252, 82, 0, 255});

                if (draw_button(10, list_y + 30, 355, 30, "Connect to Strava", true)) {
                    snprintf(status_message, sizeof(status_message), "Authenticating with Strava...");
                    if (strava_authenticate(&strava_config)) {
                        snprintf(status_message, sizeof(status_message), "Connected to Strava!");
                    } else {
                        snprintf(status_message, sizeof(status_message), "Strava authentication failed");
                    }
                }
            } else {
                DrawTextF("Strava Activities:", 10, list_y + 5, 15, (Color){252, 82, 0, 255});

                // Fetch activities button
                if (!strava_activities_loaded && !strava_loading) {
                    if (draw_button(10, list_y + 25, 355, 25, "Fetch Activities", true)) {
                        strava_loading = true;
                        snprintf(status_message, sizeof(status_message), "Fetching activities from Strava...");
                    }
                }

                // Actually fetch (done here to not block button drawing)
                if (strava_loading) {
                    EndDrawing();  // Need to end frame before blocking call
                    if (strava_fetch_activities(&strava_config, &strava_activities, 1, 50)) {
                        strava_activities_loaded = true;
                        snprintf(status_message, sizeof(status_message), "Loaded %zu activities from Strava", strava_activities.count);
                    } else {
                        snprintf(status_message, sizeof(status_message), "Failed to fetch Strava activities");
                    }
                    strava_loading = false;
                    continue;  // Restart frame
                }

                if (strava_activities_loaded) {
                    // Download button next to header
                    bool can_download = selected_strava >= 0 && selected_strava < (int)strava_activities.count && !strava_downloading;
                    if (draw_button(200, list_y + 2, 90, 20, strava_downloading ? "..." : "Download", can_download)) {
                        strava_downloading = true;
                    }

                    // Handle download in separate frame to avoid blocking
                    if (strava_downloading) {
                        EndDrawing();
                        StravaActivity *act = &strava_activities.activities[selected_strava];

                        // Parse year and month from start_date (format: YYYY-MM-DDTHH:MM:SSZ)
                        char year[5] = "";
                        char month[3] = "";
                        if (strlen(act->start_date) >= 10) {
                            strncpy(year, act->start_date, 4);
                            year[4] = '\0';
                            strncpy(month, act->start_date + 5, 2);
                            month[2] = '\0';
                        }

                        // Create output directory
                        char output_dir[512];
                        snprintf(output_dir, sizeof(output_dir), "%s/activity/%s/%s", g_data_dir, year, month);
                        create_directory_path(output_dir);

                        // Create output path
                        char output_path[512];
                        snprintf(output_path, sizeof(output_path), "%s/%lld.json", output_dir, (long long)act->id);

                        if (strava_download_activity(&strava_config, act->id, output_path)) {
                            snprintf(status_message, sizeof(status_message), "Downloaded: %s", act->name);
                            // Refresh activity tree
                            activity_tree_scan(&activity_tree, g_data_dir);
                        } else {
                            snprintf(status_message, sizeof(status_message), "Download failed: %s", act->name);
                        }
                        strava_downloading = false;
                        continue;  // Restart frame
                    }

                    for (int i = 0; i < visible_files && i + strava_scroll_offset < (int)strava_activities.count; i++) {
                        int act_idx = i + strava_scroll_offset;
                        int y = list_y + 25 + i * 25;

                        StravaActivity *act = &strava_activities.activities[act_idx];

                        bool hover = mouse.x >= 8 && mouse.x < 367 && mouse.y >= y - 2 && mouse.y < y + 20;

                        if (act_idx == selected_strava) {
                            DrawRectangle(8, y - 2, 359, 22, (Color){120, 60, 40, 255});
                        } else if (hover) {
                            DrawRectangle(8, y - 2, 359, 22, (Color){55, 45, 45, 255});
                        }

                        // Format: date + type + power indicator
                        char display[50];
                        char date_short[12] = "";
                        if (strlen(act->start_date) >= 10) {
                            strncpy(date_short, act->start_date, 10);
                            date_short[10] = '\0';
                        }

                        const char *power_ind = act->has_power ? "*" : "";
                        snprintf(display, sizeof(display), "%s %s%s", date_short, act->type, power_ind);

                        if (hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                            selected_strava = act_idx;
                            snprintf(status_message, sizeof(status_message), "%s - %.1fkm, %dmin, %.0fW avg",
                                     act->name, act->distance / 1000.0, act->moving_time / 60,
                                     act->average_watts);
                            strncpy(current_title, act->name, sizeof(current_title) - 1);
                        }

                        DrawTextF(display, 12, y, 15, act_idx == selected_strava ? WHITE : LIGHTGRAY);
                    }

                    if (strava_scroll_offset > 0) DrawTextF("^", 145, list_y + 8, 15, GRAY);
                    if (strava_scroll_offset + visible_files < (int)strava_activities.count) {
                        DrawTextF("v", 145, list_y + visible_files * 25 + 5, 15, GRAY);
                    }
                }
            }
        }

        // Graph area
        int graph_x = 400 + GRAPH_MARGIN_LEFT;
        int graph_y = GRAPH_MARGIN_TOP;
        int graph_w = GetScreenWidth() - 400 - GRAPH_MARGIN_LEFT - GRAPH_MARGIN_RIGHT;
        int graph_h = GetScreenHeight() - GRAPH_MARGIN_TOP - GRAPH_MARGIN_BOTTOM - 40;

        if (file_loaded && power_data.count > 0) {
            const char *view_name = "Summary";
            if (graph_view == GRAPH_VIEW_POWER) view_name = "Power Graph";
            else if (graph_view == GRAPH_VIEW_MAP) view_name = "Map";

            char title[300];
            snprintf(title, sizeof(title), "%s - %s", view_name, current_title);
            DrawTextF(title, 400, 15, 18, WHITE);

            char stats[256];
            snprintf(stats, sizeof(stats), "Min: %dW | Max: %dW | Avg: %.0fW | Samples: %zu",
                     power_data.min_power, power_data.max_power, power_data.avg_power, power_data.count);
            DrawTextF(stats, 400, 40, 15, LIGHTGRAY);

            // Summary/Power/Map tab buttons
            int tab_btn_y = 58;
            int btn_x = 400;

            if (draw_button(btn_x, tab_btn_y, 85, 20, "S: Summary", true)) {
                graph_view = GRAPH_VIEW_SUMMARY;
            }
            if (graph_view == GRAPH_VIEW_SUMMARY) {
                DrawRectangle(btn_x, tab_btn_y + 18, 85, 2, (Color){200, 150, 100, 255});
            }
            btn_x += 90;

            if (draw_button(btn_x, tab_btn_y, 70, 20, "G: Graph", true)) {
                graph_view = GRAPH_VIEW_POWER;
            }
            if (graph_view == GRAPH_VIEW_POWER) {
                DrawRectangle(btn_x, tab_btn_y + 18, 70, 2, (Color){100, 150, 255, 255});
            }
            btn_x += 75;

            bool has_gps = power_data.has_gps_data;
            if (draw_button(btn_x, tab_btn_y, 60, 20, "M: Map", has_gps)) {
                if (has_gps) graph_view = GRAPH_VIEW_MAP;
            }
            if (graph_view == GRAPH_VIEW_MAP) {
                DrawRectangle(btn_x, tab_btn_y + 18, 60, 2, (Color){100, 200, 100, 255});
            }

            // Adjust graph area to be below the tab buttons
            int content_y = tab_btn_y + 25;
            int content_h = graph_h - (content_y - graph_y);

            if (graph_view == GRAPH_VIEW_SUMMARY) {
                draw_summary_tab(&power_data, &activity_meta, &edit_field, &cursor_pos, blink_time,
                                 400, content_y, graph_w + GRAPH_MARGIN_LEFT, content_h);
            } else if (graph_view == GRAPH_VIEW_MAP && has_gps) {
                // Initialize map view if needed
                if (map_view.zoom == 0) {
                    map_view_fit_bounds(&map_view, power_data.min_lat, power_data.max_lat,
                                        power_data.min_lon, power_data.max_lon, graph_w, content_h);
                    // Load Zwift map if detected
                    if (map_view.source == MAP_SOURCE_ZWIFT && map_view.zwift_world) {
                        zwift_map_load(&map_view, tile_cache.cache_dir);
                    }
                }
                map_view.view_width = graph_w;
                map_view.view_height = content_h;

                // Draw map (Zwift or OSM)
                if (map_view.source == MAP_SOURCE_ZWIFT && map_view.zwift_map_loaded) {
                    zwift_map_draw(&map_view, graph_x, content_y);
                    zwift_map_draw_path(&map_view, graph_x, content_y, power_data.samples, power_data.count);
                } else {
                    tile_map_draw(&tile_cache, &map_view, graph_x, content_y);
                    tile_map_draw_path(&map_view, graph_x, content_y, power_data.samples, power_data.count);
                }

                // Draw attribution (shows "Map: Zwift" or OSM depending on source)
                tile_map_draw_attribution(&map_view, graph_x + graph_w - 200, content_y + content_h - 18, 12);
            } else {
                // Graph view - draw smoothing slider
                int slider_y = content_y;
                int slider_x = graph_x;
                int slider_w = graph_w;
                int slider_h = 25;

                DrawTextF("Smoothing:", slider_x - 75, slider_y + 5, 14, LIGHTGRAY);

                // Draw slider track
                int track_y = slider_y + 10;
                DrawRectangle(slider_x, track_y, slider_w, 4, (Color){60, 60, 70, 255});

                // Draw discrete stops and labels
                for (int i = 0; i < smoothing_count; i++) {
                    float stop_ratio = (float)i / (smoothing_count - 1);
                    int stop_x = slider_x + (int)(stop_ratio * slider_w);

                    // Draw stop marker
                    DrawRectangle(stop_x - 2, track_y - 2, 4, 8, (Color){80, 80, 90, 255});

                    // Draw label
                    int label_w = MeasureTextF(smoothing_labels[i], 12);
                    DrawTextF(smoothing_labels[i], stop_x - label_w / 2, slider_y + 18, 12,
                              i == smoothing_index ? WHITE : GRAY);
                }

                // Draw handle at current position
                float handle_ratio = (float)smoothing_index / (smoothing_count - 1);
                int handle_x = slider_x + (int)(handle_ratio * slider_w);
                DrawCircle(handle_x, track_y + 2, 8, (Color){100, 150, 255, 255});
                DrawCircle(handle_x, track_y + 2, 5, WHITE);

                // Handle slider interaction
                if (IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
                    if (mouse.y >= slider_y && mouse.y <= slider_y + slider_h + 10 &&
                        mouse.x >= slider_x - 10 && mouse.x <= slider_x + slider_w + 10) {
                        float click_ratio = (mouse.x - slider_x) / (float)slider_w;
                        if (click_ratio < 0) click_ratio = 0;
                        if (click_ratio > 1) click_ratio = 1;
                        // Snap to nearest stop
                        smoothing_index = (int)(click_ratio * (smoothing_count - 1) + 0.5f);
                    }
                }

                // Adjust content area for graph (below slider)
                int graph_content_y = content_y + 35;
                int graph_content_h = content_h - 35;

                draw_power_graph(&power_data, graph_x, graph_content_y, graph_w, graph_content_h, smoothing_seconds[smoothing_index]);
            }
        } else {
            DrawRectangle(graph_x, graph_y, graph_w, graph_h, (Color){30, 30, 40, 255});
            const char *msg;
            if (current_tab == TAB_STRAVA) {
                msg = strava_activities_loaded ? "Select activity (* = has power)" : "Fetch activities to browse";
            } else if (tree_visible > 0) {
                msg = "Select an activity";
            } else {
#ifdef __APPLE__
                msg = "Drop .fit files in ~/Library/Application Support/fitpower/inbox/";
#else
                msg = "Drop .fit files in ~/.local/share/fitpower/inbox/";
#endif
            }
            int text_width = MeasureTextF(msg, 18);
            DrawTextF(msg, graph_x + (graph_w - text_width) / 2, graph_y + graph_h / 2, 20, GRAY);
        }

        // Status bar
        DrawTextF("Up/Down: Navigate | Left/Right: Collapse/Expand | S/G/M: Summary/Graph/Map | ESC: Quit", 10, GetScreenHeight() - 25, 14, GRAY);

        EndDrawing();
    }

    // Cleanup
    zwift_map_free(&map_view);
    tile_cache_free(&tile_cache);
    UnloadFont(g_font);
    fit_power_data_free(&power_data);
    strava_activity_list_free(&strava_activities);
    activity_tree_free(&activity_tree);
    free(fit_files);
    CloseWindow();

    return 0;
}
