#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "raylib.h"
#include "fit_parser.h"
#include "strava_api.h"

#define DOWNLOADS_PATH "/Users/jan/Downloads"
#define MAX_FIT_FILES 256
#define WINDOW_WIDTH 1200
#define WINDOW_HEIGHT 700
#define GRAPH_MARGIN_LEFT 80
#define GRAPH_MARGIN_RIGHT 40
#define GRAPH_MARGIN_TOP 80
#define GRAPH_MARGIN_BOTTOM 60

#define FONT_PATH "/Users/jan/Library/Fonts/JetBrainsMono-VariableFont_wght.ttf"

typedef enum {
    TAB_LOCAL,
    TAB_STRAVA
} TabMode;

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

    qsort(files, count, sizeof(FitFileEntry), compare_fit_files);
    return count;
}

static void draw_power_graph(FitPowerData *data, int graph_x, int graph_y, int graph_w, int graph_h) {
    if (data->count < 2) return;

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
        DrawTextF(label, graph_x - 55, y - 8, 14, LIGHTGRAY);
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
        DrawTextF(label, x - 20, graph_y + graph_h + 10, 12, LIGHTGRAY);
    }

    float x_scale = (float)graph_w / (data->count - 1);

    // Filled area
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

        DrawTriangle((Vector2){x1, y1}, (Vector2){x1, y_bottom}, (Vector2){x2, y_bottom}, (Color){30, 100, 180, 80});
        DrawTriangle((Vector2){x1, y1}, (Vector2){x2, y_bottom}, (Vector2){x2, y2}, (Color){30, 100, 180, 80});
    }

    // Power line
    for (size_t i = 0; i < data->count - 1; i++) {
        float x1 = graph_x + i * x_scale;
        float x2 = graph_x + (i + 1) * x_scale;

        float power1 = data->samples[i].power;
        float power2 = data->samples[i + 1].power;

        float y1_ratio = (max_display - power1) / display_range;
        float y2_ratio = (max_display - power2) / display_range;

        float y1 = graph_y + y1_ratio * graph_h;
        float y2 = graph_y + y2_ratio * graph_h;

        Color line_color;
        float avg_power = (power1 + power2) / 2;
        float intensity = (avg_power - min_display) / display_range;

        if (intensity < 0.5) {
            line_color = (Color){50, 150, 255, 255};
        } else if (intensity < 0.75) {
            line_color = (Color){100, 200, 100, 255};
        } else {
            line_color = (Color){255, 100, 100, 255};
        }

        DrawLineEx((Vector2){x1, y1}, (Vector2){x2, y2}, 2.0f, line_color);
    }

    // Average line
    float avg_y_ratio = (max_display - data->avg_power) / display_range;
    int avg_y = graph_y + (int)(avg_y_ratio * graph_h);
    DrawLine(graph_x, avg_y, graph_x + graph_w, avg_y, (Color){255, 200, 50, 200});

    char avg_label[64];
    snprintf(avg_label, sizeof(avg_label), "Avg: %.0fW", data->avg_power);
    DrawTextF(avg_label, graph_x + graph_w - 100, avg_y - 20, 14, (Color){255, 200, 50, 255});
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
    DrawTextF(text, x + (w - text_w) / 2, y + (h - 14) / 2, 14, fg);

    return clicked;
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    // Find local FIT files
    FitFileEntry *fit_files = malloc(MAX_FIT_FILES * sizeof(FitFileEntry));
    int num_files = find_fit_files(fit_files, MAX_FIT_FILES);
    printf("Found %d local FIT files\n", num_files);

    // Load Strava config
    StravaConfig strava_config = {0};
    bool strava_config_loaded = strava_load_config(&strava_config);
    StravaActivityList strava_activities = {0};
    bool strava_activities_loaded = false;
    bool strava_loading = false;

    // Initialize raylib
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "FIT Power Viewer");
    SetTargetFPS(60);

    // Load custom font
    g_font = LoadFontEx(FONT_PATH, 32, NULL, 0);
    SetTextureFilter(g_font.texture, TEXTURE_FILTER_BILINEAR);

    // State
    TabMode current_tab = TAB_LOCAL;
    int selected_file = 0;
    int selected_strava = 0;
    int scroll_offset = 0;
    int strava_scroll_offset = 0;
    int visible_files = 15;
    FitPowerData power_data = {0};
    bool file_loaded = false;
    char status_message[256] = "Select a file to view power data";
    char current_title[256] = "";

    // Load first local file if available
    if (num_files > 0) {
        printf("Loading: %s\n", fit_files[0].path);
        fflush(stdout);
        if (fit_parse_file(fit_files[0].path, &power_data)) {
            file_loaded = true;
            snprintf(status_message, sizeof(status_message), "Loaded: %s (%zu samples)", fit_files[0].name, power_data.count);
            strncpy(current_title, fit_files[0].name, sizeof(current_title) - 1);
        }
    }

    while (!WindowShouldClose()) {
        int key = GetKeyPressed();
        Vector2 mouse = GetMousePosition();

        // Tab switching with number keys
        if (key == KEY_ONE) current_tab = TAB_LOCAL;
        if (key == KEY_TWO) current_tab = TAB_STRAVA;

        int list_count = (current_tab == TAB_LOCAL) ? num_files : (int)strava_activities.count;
        int *selected = (current_tab == TAB_LOCAL) ? &selected_file : &selected_strava;
        int *scroll = (current_tab == TAB_LOCAL) ? &scroll_offset : &strava_scroll_offset;

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

        // Load on Enter (local files only for now)
        if ((key == KEY_ENTER || key == KEY_SPACE) && current_tab == TAB_LOCAL && num_files > 0) {
            fit_power_data_free(&power_data);
            file_loaded = false;

            if (fit_parse_file(fit_files[selected_file].path, &power_data)) {
                file_loaded = true;
                snprintf(status_message, sizeof(status_message), "Loaded: %s (%zu samples)", fit_files[selected_file].name, power_data.count);
                strncpy(current_title, fit_files[selected_file].name, sizeof(current_title) - 1);
            } else {
                snprintf(status_message, sizeof(status_message), "Failed to load: %s", fit_files[selected_file].name);
            }
        }

        // Mouse wheel scrolling
        float wheel = GetMouseWheelMove();
        if (wheel != 0 && mouse.x < 300) {
            *scroll -= (int)wheel * 3;
            if (*scroll < 0) *scroll = 0;
            if (*scroll > list_count - visible_files) {
                *scroll = list_count - visible_files;
                if (*scroll < 0) *scroll = 0;
            }
        }

        // Drawing
        BeginDrawing();
        ClearBackground((Color){20, 20, 25, 255});

        // Title
        DrawTextF("FIT Power Viewer", 10, 10, 22, WHITE);

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
        DrawRectangle(5, list_y, 290, visible_files * 25 + 10, (Color){35, 35, 45, 255});

        if (current_tab == TAB_LOCAL) {
            DrawTextF("Local FIT Files:", 10, list_y + 5, 13, LIGHTGRAY);

            for (int i = 0; i < visible_files && i + scroll_offset < num_files; i++) {
                int file_idx = i + scroll_offset;
                int y = list_y + 25 + i * 25;

                bool hover = mouse.x >= 8 && mouse.x < 292 && mouse.y >= y - 2 && mouse.y < y + 20;

                if (file_idx == selected_file) {
                    DrawRectangle(8, y - 2, 284, 22, (Color){60, 80, 120, 255});
                } else if (hover) {
                    DrawRectangle(8, y - 2, 284, 22, (Color){45, 45, 55, 255});
                }

                // Click to select and load
                if (hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    selected_file = file_idx;
                    fit_power_data_free(&power_data);
                    file_loaded = false;

                    if (fit_parse_file(fit_files[file_idx].path, &power_data)) {
                        file_loaded = true;
                        snprintf(status_message, sizeof(status_message), "Loaded: %s (%zu samples)", fit_files[file_idx].name, power_data.count);
                        strncpy(current_title, fit_files[file_idx].name, sizeof(current_title) - 1);
                    }
                }

                char display_name[40];
                strncpy(display_name, fit_files[file_idx].name, 35);
                display_name[35] = '\0';
                if (strlen(fit_files[file_idx].name) > 35) strcat(display_name, "...");

                DrawTextF(display_name, 12, y, 13, file_idx == selected_file ? WHITE : LIGHTGRAY);
            }

            if (scroll_offset > 0) DrawTextF("^", 145, list_y + 8, 13, GRAY);
            if (scroll_offset + visible_files < num_files) DrawTextF("v", 145, list_y + visible_files * 25 + 5, 13, GRAY);

        } else if (current_tab == TAB_STRAVA) {
            if (!strava_is_authenticated(&strava_config)) {
                DrawTextF("Strava: Not connected", 10, list_y + 5, 13, (Color){252, 82, 0, 255});

                if (draw_button(10, list_y + 30, 280, 30, "Connect to Strava", true)) {
                    snprintf(status_message, sizeof(status_message), "Authenticating with Strava...");
                    if (strava_authenticate(&strava_config)) {
                        snprintf(status_message, sizeof(status_message), "Connected to Strava!");
                    } else {
                        snprintf(status_message, sizeof(status_message), "Strava authentication failed");
                    }
                }
            } else {
                DrawTextF("Strava Activities:", 10, list_y + 5, 13, (Color){252, 82, 0, 255});

                // Fetch activities button
                if (!strava_activities_loaded && !strava_loading) {
                    if (draw_button(10, list_y + 25, 280, 25, "Fetch Activities", true)) {
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
                    for (int i = 0; i < visible_files && i + strava_scroll_offset < (int)strava_activities.count; i++) {
                        int act_idx = i + strava_scroll_offset;
                        int y = list_y + 25 + i * 25;

                        StravaActivity *act = &strava_activities.activities[act_idx];

                        bool hover = mouse.x >= 8 && mouse.x < 292 && mouse.y >= y - 2 && mouse.y < y + 20;

                        if (act_idx == selected_strava) {
                            DrawRectangle(8, y - 2, 284, 22, (Color){120, 60, 40, 255});
                        } else if (hover) {
                            DrawRectangle(8, y - 2, 284, 22, (Color){55, 45, 45, 255});
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

                        DrawTextF(display, 12, y, 13, act_idx == selected_strava ? WHITE : LIGHTGRAY);
                    }

                    if (strava_scroll_offset > 0) DrawTextF("^", 145, list_y + 8, 13, GRAY);
                    if (strava_scroll_offset + visible_files < (int)strava_activities.count) {
                        DrawTextF("v", 145, list_y + visible_files * 25 + 5, 13, GRAY);
                    }
                }
            }
        }

        // Graph area
        int graph_x = 320 + GRAPH_MARGIN_LEFT;
        int graph_y = GRAPH_MARGIN_TOP;
        int graph_w = GetScreenWidth() - 320 - GRAPH_MARGIN_LEFT - GRAPH_MARGIN_RIGHT;
        int graph_h = GetScreenHeight() - GRAPH_MARGIN_TOP - GRAPH_MARGIN_BOTTOM - 40;

        if (file_loaded && power_data.count > 0) {
            char title[300];
            snprintf(title, sizeof(title), "Power Graph - %s", current_title);
            DrawTextF(title, 320, 15, 16, WHITE);

            char stats[256];
            snprintf(stats, sizeof(stats), "Min: %dW | Max: %dW | Avg: %.0fW | Samples: %zu",
                     power_data.min_power, power_data.max_power, power_data.avg_power, power_data.count);
            DrawTextF(stats, 320, 40, 13, LIGHTGRAY);

            draw_power_graph(&power_data, graph_x, graph_y, graph_w, graph_h);
        } else {
            DrawRectangle(graph_x, graph_y, graph_w, graph_h, (Color){30, 30, 40, 255});
            const char *msg = current_tab == TAB_STRAVA ?
                (strava_activities_loaded ? "Select activity (* = has power)" : "Fetch activities to browse") :
                (num_files > 0 ? "Select a file" : "No FIT files in Downloads");
            int text_width = MeasureTextF(msg, 18);
            DrawTextF(msg, graph_x + (graph_w - text_width) / 2, graph_y + graph_h / 2, 18, GRAY);
        }

        // Status bar
        DrawTextF(status_message, 10, GetScreenHeight() - 25, 12, DARKGRAY);
        DrawTextF("1/2: Switch tabs | Up/Down: Select | Enter: Load | ESC: Quit", 320, GetScreenHeight() - 25, 12, GRAY);

        EndDrawing();
    }

    // Cleanup
    UnloadFont(g_font);
    fit_power_data_free(&power_data);
    strava_activity_list_free(&strava_activities);
    free(fit_files);
    CloseWindow();

    return 0;
}
