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
    GRAPH_VIEW_POWER,
    GRAPH_VIEW_MAP
} GraphViewMode;

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
    DrawTextF(avg_label, graph_x + graph_w - 100, avg_y - 20, 16, (Color){255, 200, 50, 255});
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
    GraphViewMode graph_view = GRAPH_VIEW_POWER;
    int selected_tree = 0;
    int selected_strava = 0;
    int tree_scroll_offset = 0;
    int strava_scroll_offset = 0;
    int visible_files = 15;
    FitPowerData power_data = {0};
    bool file_loaded = false;
    char status_message[256] = "Select a file to view power data";
    char current_title[256] = "";

    // Map state
    TileCache tile_cache;
    tile_cache_init(&tile_cache);
    MapView map_view = {0};

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
                if (fit_parse_file(node->full_path, &power_data)) {
                    file_loaded = true;
                    graph_view = GRAPH_VIEW_POWER;
                    map_view.zoom = 0;  // Reset to recalculate on next map view
                    snprintf(status_message, sizeof(status_message), "Loaded: %s (%zu samples)", node->name, power_data.count);
                    strncpy(current_title, node->name, sizeof(current_title) - 1);
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

        // Graph view switching with P/M keys
        if (key == KEY_P) graph_view = GRAPH_VIEW_POWER;
        if (key == KEY_M && power_data.has_gps_data) graph_view = GRAPH_VIEW_MAP;

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
                if (key == KEY_LEFT && (selected_node->type == NODE_YEAR || selected_node->type == NODE_MONTH)) {
                    if (selected_node->expanded) {
                        selected_node->expanded = false;
                    }
                } else if (key == KEY_RIGHT && (selected_node->type == NODE_YEAR || selected_node->type == NODE_MONTH)) {
                    if (!selected_node->expanded) {
                        selected_node->expanded = true;
                    }
                }
            }
        }

        // Load on Enter/Space for local tree files
        if ((key == KEY_ENTER || key == KEY_SPACE) && current_tab == TAB_LOCAL && tree_visible > 0) {
            TreeNode *node = activity_tree_get_visible(&activity_tree, (size_t)selected_tree);
            if (node) {
                if (node->type == NODE_FILE) {
                    // Load the file
                    fit_power_data_free(&power_data);
                    file_loaded = false;
                    graph_view = GRAPH_VIEW_POWER;
                    zwift_map_free(&map_view);  // Free any loaded Zwift map
                    map_view.zoom = 0;  // Reset to recalculate on next map view

                    if (fit_parse_file(node->full_path, &power_data)) {
                        file_loaded = true;
                        snprintf(status_message, sizeof(status_message), "Loaded: %s (%zu samples)", node->name, power_data.count);
                        strncpy(current_title, node->name, sizeof(current_title) - 1);
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
        DrawRectangle(5, list_y, 290, visible_files * 25 + 10, (Color){35, 35, 45, 255});

        if (current_tab == TAB_LOCAL) {
            DrawTextF("Activities:", 10, list_y + 5, 15, LIGHTGRAY);

            for (int i = 0; i < visible_files && i + tree_scroll_offset < (int)tree_visible; i++) {
                int node_idx = i + tree_scroll_offset;
                int y = list_y + 25 + i * 25;

                TreeNode *node = activity_tree_get_visible(&activity_tree, (size_t)node_idx);
                if (!node) continue;

                bool hover = mouse.x >= 8 && mouse.x < 292 && mouse.y >= y - 2 && mouse.y < y + 20;

                if (node_idx == selected_tree) {
                    DrawRectangle(8, y - 2, 284, 22, (Color){60, 80, 120, 255});
                } else if (hover) {
                    DrawRectangle(8, y - 2, 284, 22, (Color){45, 45, 55, 255});
                }

                // Click to select and possibly load/toggle
                if (hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    selected_tree = node_idx;
                    if (node->type == NODE_FILE) {
                        fit_power_data_free(&power_data);
                        file_loaded = false;
                        graph_view = GRAPH_VIEW_POWER;
                        zwift_map_free(&map_view);  // Free any loaded Zwift map
                        map_view.zoom = 0;  // Reset to recalculate on next map view

                        if (fit_parse_file(node->full_path, &power_data)) {
                            file_loaded = true;
                            snprintf(status_message, sizeof(status_message), "Loaded: %s (%zu samples)", node->name, power_data.count);
                            strncpy(current_title, node->name, sizeof(current_title) - 1);
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
                } else if (node->type == NODE_FILE) {
                    indent = 32;
                }

                char display_name[50];
                int max_chars = 32 - (indent / 8);
                snprintf(display_name, sizeof(display_name), "%s%.*s%s",
                         prefix, max_chars, node->name,
                         (int)strlen(node->name) > max_chars ? "..." : "");

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

                if (draw_button(10, list_y + 30, 280, 30, "Connect to Strava", true)) {
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
        int graph_x = 320 + GRAPH_MARGIN_LEFT;
        int graph_y = GRAPH_MARGIN_TOP;
        int graph_w = GetScreenWidth() - 320 - GRAPH_MARGIN_LEFT - GRAPH_MARGIN_RIGHT;
        int graph_h = GetScreenHeight() - GRAPH_MARGIN_TOP - GRAPH_MARGIN_BOTTOM - 40;

        if (file_loaded && power_data.count > 0) {
            char title[300];
            snprintf(title, sizeof(title), "%s - %s",
                     graph_view == GRAPH_VIEW_MAP ? "Map" : "Power Graph", current_title);
            DrawTextF(title, 320, 15, 18, WHITE);

            char stats[256];
            snprintf(stats, sizeof(stats), "Min: %dW | Max: %dW | Avg: %.0fW | Samples: %zu",
                     power_data.min_power, power_data.max_power, power_data.avg_power, power_data.count);
            DrawTextF(stats, 320, 40, 15, LIGHTGRAY);

            // Power/Map tab buttons
            int tab_btn_y = 58;
            if (draw_button(320, tab_btn_y, 70, 20, "P: Power", true)) {
                graph_view = GRAPH_VIEW_POWER;
            }
            if (graph_view == GRAPH_VIEW_POWER) {
                DrawRectangle(320, tab_btn_y + 18, 70, 2, (Color){100, 150, 255, 255});
            }

            bool has_gps = power_data.has_gps_data;
            if (draw_button(395, tab_btn_y, 60, 20, "M: Map", has_gps)) {
                if (has_gps) graph_view = GRAPH_VIEW_MAP;
            }
            if (graph_view == GRAPH_VIEW_MAP) {
                DrawRectangle(395, tab_btn_y + 18, 60, 2, (Color){100, 200, 100, 255});
            }

            // Adjust graph area to be below the tab buttons
            int content_y = tab_btn_y + 25;
            int content_h = graph_h - (content_y - graph_y);

            if (graph_view == GRAPH_VIEW_MAP && has_gps) {
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
                draw_power_graph(&power_data, graph_x, content_y, graph_w, content_h);
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
        DrawTextF("Up/Down: Navigate | Left/Right: Collapse/Expand | P/M: Power/Map | ESC: Quit", 10, GetScreenHeight() - 25, 14, GRAY);

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
