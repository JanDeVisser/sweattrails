#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "raylib.h"
#include "fit_parser.h"

#define DOWNLOADS_PATH "/Users/jan/Downloads"
#define MAX_FIT_FILES 256
#define WINDOW_WIDTH 1200
#define WINDOW_HEIGHT 700
#define GRAPH_MARGIN_LEFT 80
#define GRAPH_MARGIN_RIGHT 40
#define GRAPH_MARGIN_TOP 80
#define GRAPH_MARGIN_BOTTOM 60

typedef struct {
    char path[512];
    char name[256];
    time_t mtime;
} FitFileEntry;

static int compare_fit_files(const void *a, const void *b) {
    const FitFileEntry *fa = (const FitFileEntry *)a;
    const FitFileEntry *fb = (const FitFileEntry *)b;
    // Sort by modification time, newest first
    return (int)(fb->mtime - fa->mtime);
}

static int find_fit_files(FitFileEntry *files, int max_files) {
    DIR *dir = opendir(DOWNLOADS_PATH);
    if (!dir) {
        fprintf(stderr, "Cannot open Downloads directory\n");
        return 0;
    }

    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && count < max_files) {
        size_t len = strlen(entry->d_name);
        if (len > 4 && strcasecmp(entry->d_name + len - 4, ".fit") == 0) {
            snprintf(files[count].path, sizeof(files[count].path),
                     "%s/%s", DOWNLOADS_PATH, entry->d_name);
            strncpy(files[count].name, entry->d_name, sizeof(files[count].name) - 1);

            struct stat st;
            if (stat(files[count].path, &st) == 0) {
                files[count].mtime = st.st_mtime;
            }
            count++;
        }
    }
    closedir(dir);

    // Sort by modification time
    qsort(files, count, sizeof(FitFileEntry), compare_fit_files);

    return count;
}

static void draw_power_graph(FitPowerData *data, int graph_x, int graph_y, int graph_w, int graph_h) {
    if (data->count < 2) return;

    // Draw background
    DrawRectangle(graph_x, graph_y, graph_w, graph_h, (Color){30, 30, 40, 255});

    // Calculate scaling
    uint16_t power_range = data->max_power - data->min_power;
    if (power_range < 50) power_range = 50;

    // Add padding to power range
    float min_display = (data->min_power > 20) ? data->min_power - 20 : 0;
    float max_display = data->max_power + 20;
    float display_range = max_display - min_display;

    // Draw horizontal grid lines and labels
    int num_grid_lines = 5;
    for (int i = 0; i <= num_grid_lines; i++) {
        float y_ratio = (float)i / num_grid_lines;
        int y = graph_y + (int)(y_ratio * graph_h);
        float power_val = max_display - (y_ratio * display_range);

        DrawLine(graph_x, y, graph_x + graph_w, y, (Color){60, 60, 70, 255});

        char label[32];
        snprintf(label, sizeof(label), "%dW", (int)power_val);
        DrawText(label, graph_x - 50, y - 8, 16, LIGHTGRAY);
    }

    // Draw vertical grid lines (time markers)
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
        DrawText(label, x - 20, graph_y + graph_h + 10, 14, LIGHTGRAY);
    }

    // Draw power curve
    float x_scale = (float)graph_w / (data->count - 1);

    // Draw filled area under curve
    for (size_t i = 0; i < data->count - 1; i++) {
        float x1 = graph_x + i * x_scale;
        float x2 = graph_x + (i + 1) * x_scale;

        float power1 = data->samples[i].power;
        float power2 = data->samples[i + 1].power;

        float y1_ratio = (max_display - power1) / display_range;
        float y2_ratio = (max_display - power2) / display_range;

        float y1 = graph_y + y1_ratio * graph_h;
        float y2 = graph_y + y2_ratio * graph_h;
        float y_bottom = graph_y + graph_h;

        // Draw filled quad
        DrawTriangle(
            (Vector2){x1, y1},
            (Vector2){x1, y_bottom},
            (Vector2){x2, y_bottom},
            (Color){30, 100, 180, 80}
        );
        DrawTriangle(
            (Vector2){x1, y1},
            (Vector2){x2, y_bottom},
            (Vector2){x2, y2},
            (Color){30, 100, 180, 80}
        );
    }

    // Draw the line itself
    for (size_t i = 0; i < data->count - 1; i++) {
        float x1 = graph_x + i * x_scale;
        float x2 = graph_x + (i + 1) * x_scale;

        float power1 = data->samples[i].power;
        float power2 = data->samples[i + 1].power;

        float y1_ratio = (max_display - power1) / display_range;
        float y2_ratio = (max_display - power2) / display_range;

        float y1 = graph_y + y1_ratio * graph_h;
        float y2 = graph_y + y2_ratio * graph_h;

        // Color based on power intensity
        Color line_color;
        float avg_power = (power1 + power2) / 2;
        float intensity = (avg_power - min_display) / display_range;

        if (intensity < 0.5) {
            line_color = (Color){50, 150, 255, 255};  // Blue for low power
        } else if (intensity < 0.75) {
            line_color = (Color){100, 200, 100, 255}; // Green for medium
        } else {
            line_color = (Color){255, 100, 100, 255}; // Red for high power
        }

        DrawLineEx((Vector2){x1, y1}, (Vector2){x2, y2}, 2.0f, line_color);
    }

    // Draw average power line
    float avg_y_ratio = (max_display - data->avg_power) / display_range;
    int avg_y = graph_y + (int)(avg_y_ratio * graph_h);
    DrawLine(graph_x, avg_y, graph_x + graph_w, avg_y, (Color){255, 200, 50, 200});

    char avg_label[64];
    snprintf(avg_label, sizeof(avg_label), "Avg: %.0fW", data->avg_power);
    DrawText(avg_label, graph_x + graph_w - 100, avg_y - 20, 16, (Color){255, 200, 50, 255});
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    // Find FIT files in Downloads
    FitFileEntry *fit_files = malloc(MAX_FIT_FILES * sizeof(FitFileEntry));
    int num_files = find_fit_files(fit_files, MAX_FIT_FILES);

    if (num_files == 0) {
        fprintf(stderr, "No .fit files found in Downloads directory\n");
        free(fit_files);
        return 1;
    }

    printf("Found %d FIT files\n", num_files);

    // Initialize raylib
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "FIT Power Viewer");
    SetTargetFPS(60);

    // State
    int selected_file = 0;
    int scroll_offset = 0;
    int visible_files = 15;
    FitPowerData power_data = {0};
    bool file_loaded = false;
    char status_message[256] = "Select a file to view power data";

    // Load the first file by default
    printf("Loading: %s\n", fit_files[0].path);
    fflush(stdout);
    if (fit_parse_file(fit_files[0].path, &power_data)) {
        file_loaded = true;
        snprintf(status_message, sizeof(status_message),
                 "Loaded: %s (%zu samples)", fit_files[0].name, power_data.count);
    } else {
        snprintf(status_message, sizeof(status_message),
                 "Failed to load or no power data: %s", fit_files[0].name);
    }

    while (!WindowShouldClose()) {
        // Handle input
        int key = GetKeyPressed();

        if (key == KEY_DOWN || key == KEY_J) {
            if (selected_file < num_files - 1) {
                selected_file++;
                if (selected_file >= scroll_offset + visible_files) {
                    scroll_offset = selected_file - visible_files + 1;
                }
            }
        } else if (key == KEY_UP || key == KEY_K) {
            if (selected_file > 0) {
                selected_file--;
                if (selected_file < scroll_offset) {
                    scroll_offset = selected_file;
                }
            }
        } else if (key == KEY_ENTER || key == KEY_SPACE) {
            // Load selected file
            fit_power_data_free(&power_data);
            file_loaded = false;

            printf("Loading: %s\n", fit_files[selected_file].path);
            if (fit_parse_file(fit_files[selected_file].path, &power_data)) {
                file_loaded = true;
                snprintf(status_message, sizeof(status_message),
                         "Loaded: %s (%zu samples)", fit_files[selected_file].name, power_data.count);
            } else {
                snprintf(status_message, sizeof(status_message),
                         "Failed to load or no power data: %s", fit_files[selected_file].name);
            }
        } else if (key == KEY_PAGE_DOWN) {
            selected_file += visible_files;
            if (selected_file >= num_files) selected_file = num_files - 1;
            scroll_offset = selected_file - visible_files + 1;
            if (scroll_offset < 0) scroll_offset = 0;
        } else if (key == KEY_PAGE_UP) {
            selected_file -= visible_files;
            if (selected_file < 0) selected_file = 0;
            scroll_offset = selected_file;
        }

        // Handle mouse wheel for scrolling file list
        float wheel = GetMouseWheelMove();
        if (wheel != 0) {
            scroll_offset -= (int)wheel * 3;
            if (scroll_offset < 0) scroll_offset = 0;
            if (scroll_offset > num_files - visible_files) {
                scroll_offset = num_files - visible_files;
                if (scroll_offset < 0) scroll_offset = 0;
            }
        }

        // Handle mouse click on file list
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            Vector2 mouse = GetMousePosition();
            if (mouse.x < 300 && mouse.y > 50 && mouse.y < 50 + visible_files * 25) {
                int clicked_index = scroll_offset + (int)(mouse.y - 50) / 25;
                if (clicked_index >= 0 && clicked_index < num_files) {
                    selected_file = clicked_index;

                    // Load the file
                    fit_power_data_free(&power_data);
                    file_loaded = false;

                    if (fit_parse_file(fit_files[selected_file].path, &power_data)) {
                        file_loaded = true;
                        snprintf(status_message, sizeof(status_message),
                                 "Loaded: %s (%zu samples)", fit_files[selected_file].name, power_data.count);
                    } else {
                        snprintf(status_message, sizeof(status_message),
                                 "Failed to load or no power data: %s", fit_files[selected_file].name);
                    }
                }
            }
        }

        // Drawing
        BeginDrawing();
        ClearBackground((Color){20, 20, 25, 255});

        // Title
        DrawText("FIT Power Viewer", 10, 10, 24, WHITE);
        DrawText("Up/Down: Select | Enter: Load | ESC: Quit", 10, GetScreenHeight() - 25, 14, GRAY);

        // File list panel
        DrawRectangle(5, 45, 290, visible_files * 25 + 10, (Color){35, 35, 45, 255});
        DrawText("FIT Files:", 10, 50, 16, LIGHTGRAY);

        for (int i = 0; i < visible_files && i + scroll_offset < num_files; i++) {
            int file_idx = i + scroll_offset;
            int y = 70 + i * 25;

            if (file_idx == selected_file) {
                DrawRectangle(8, y - 2, 284, 22, (Color){60, 80, 120, 255});
            }

            // Truncate filename if too long
            char display_name[40];
            strncpy(display_name, fit_files[file_idx].name, 35);
            display_name[35] = '\0';
            if (strlen(fit_files[file_idx].name) > 35) {
                strcat(display_name, "...");
            }

            DrawText(display_name, 12, y, 14,
                     file_idx == selected_file ? WHITE : LIGHTGRAY);
        }

        // Show scroll indicators
        if (scroll_offset > 0) {
            DrawText("^", 145, 55, 14, GRAY);
        }
        if (scroll_offset + visible_files < num_files) {
            DrawText("v", 145, 70 + visible_files * 25 - 15, 14, GRAY);
        }

        // Graph area
        int graph_x = 320 + GRAPH_MARGIN_LEFT;
        int graph_y = GRAPH_MARGIN_TOP;
        int graph_w = GetScreenWidth() - 320 - GRAPH_MARGIN_LEFT - GRAPH_MARGIN_RIGHT;
        int graph_h = GetScreenHeight() - GRAPH_MARGIN_TOP - GRAPH_MARGIN_BOTTOM - 40;

        if (file_loaded && power_data.count > 0) {
            // Draw title for graph
            char title[128];
            snprintf(title, sizeof(title), "Power Graph - %s", fit_files[selected_file].name);
            DrawText(title, 320, 15, 18, WHITE);

            // Stats
            char stats[256];
            snprintf(stats, sizeof(stats), "Min: %dW | Max: %dW | Avg: %.0fW | Samples: %zu",
                     power_data.min_power, power_data.max_power, power_data.avg_power, power_data.count);
            DrawText(stats, 320, 40, 14, LIGHTGRAY);

            draw_power_graph(&power_data, graph_x, graph_y, graph_w, graph_h);
        } else {
            // No data message
            DrawRectangle(graph_x, graph_y, graph_w, graph_h, (Color){30, 30, 40, 255});
            const char *msg = file_loaded ? "No power data in file" : status_message;
            int text_width = MeasureText(msg, 20);
            DrawText(msg, graph_x + (graph_w - text_width) / 2, graph_y + graph_h / 2, 20, GRAY);
        }

        // Status bar
        DrawText(status_message, 10, GetScreenHeight() - 45, 14, DARKGRAY);

        EndDrawing();
    }

    // Cleanup
    fit_power_data_free(&power_data);
    free(fit_files);
    CloseWindow();

    return 0;
}
