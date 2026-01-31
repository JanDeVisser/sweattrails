#include "activity_tree.h"
#include "file_organizer.h"
#include "activity_meta.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>

static const char *month_names[] = {
    "", "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

const char *get_month_name(int month) {
    if (month < 1 || month > 12) return "";
    return month_names[month];
}

void activity_tree_init(ActivityTree *tree) {
    tree->years = NULL;
    tree->year_count = 0;
}

static void free_node_children(TreeNode *node) {
    if (!node->children) return;

    for (size_t i = 0; i < node->child_count; i++) {
        free_node_children(&node->children[i]);
    }
    free(node->children);
    node->children = NULL;
    node->child_count = 0;
}

void activity_tree_free(ActivityTree *tree) {
    if (!tree->years) return;

    for (size_t i = 0; i < tree->year_count; i++) {
        free_node_children(&tree->years[i]);
    }
    free(tree->years);
    tree->years = NULL;
    tree->year_count = 0;
}

static int compare_years_desc(const void *a, const void *b) {
    const TreeNode *na = (const TreeNode *)a;
    const TreeNode *nb = (const TreeNode *)b;
    return strcmp(nb->name, na->name);  // Descending order
}

static int compare_months_desc(const void *a, const void *b) {
    const TreeNode *na = (const TreeNode *)a;
    const TreeNode *nb = (const TreeNode *)b;
    // Sort by month number (stored in activity_time as month index)
    return (int)(nb->activity_time - na->activity_time);
}

static int compare_files_desc(const void *a, const void *b) {
    const TreeNode *na = (const TreeNode *)a;
    const TreeNode *nb = (const TreeNode *)b;
    // Sort by activity time, newest first
    if (nb->activity_time > na->activity_time) return 1;
    if (nb->activity_time < na->activity_time) return -1;
    return 0;
}

// Quick extract a string field from JSON file buffer
static bool json_extract_field(const char *buf, const char *field, char *out, size_t out_size) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", field);
    const char *pos = strstr(buf, search);
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
            switch (*pos) {
                case 'n': out[i++] = ' '; break;  // Replace newlines with space
                case '"': out[i++] = '"'; break;
                case '\\': out[i++] = '\\'; break;
                default: out[i++] = *pos; break;
            }
        } else {
            out[i++] = *pos;
        }
        pos++;
    }
    out[i] = '\0';
    return i > 0;
}

// Get sport icon based on activity type (disabled for now)
static const char *get_sport_icon(const char *activity_type) {
    (void)activity_type;
    return "";
}

// Load display title for a file node
// Priority: 1) .meta.json sidecar 2) JSON name field 3) filename without extension
static void load_activity_title(TreeNode *node) {
    char title[128] = "";
    char activity_type[32] = "";

    // Default to filename without extension
    strncpy(title, node->name, sizeof(title) - 1);
    char *dot = strrchr(title, '.');
    if (dot && (strcasecmp(dot, ".fit") == 0 || strcasecmp(dot, ".json") == 0)) {
        *dot = '\0';
    }

    size_t len = strlen(node->name);
    bool is_json = (len > 5 && strcasecmp(node->name + len - 5, ".json") == 0);

    // Read JSON file once for both name and type
    char json_buf[4096] = "";
    if (is_json) {
        FILE *f = fopen(node->full_path, "r");
        if (f) {
            size_t n = fread(json_buf, 1, sizeof(json_buf) - 1, f);
            json_buf[n] = '\0';
            fclose(f);
        }
    }

    // 1. Try .meta.json sidecar first (for user-edited title)
    ActivityMeta meta = {0};
    if (activity_meta_load(node->full_path, &meta) && meta.title_edited && meta.title[0]) {
        strncpy(title, meta.title, sizeof(title) - 1);
    }
    // 2. For JSON files, extract the "name" field
    else if (is_json && json_buf[0]) {
        char name[128];
        if (json_extract_field(json_buf, "name", name, sizeof(name)) && name[0]) {
            strncpy(title, name, sizeof(title) - 1);
        }
    }

    // Get activity type for icon
    if (is_json && json_buf[0]) {
        json_extract_field(json_buf, "type", activity_type, sizeof(activity_type));
    } else {
        // FIT files default to cycling
        strncpy(activity_type, "Ride", sizeof(activity_type) - 1);
    }

    // Build final display title with icon prefix
    const char *icon = get_sport_icon(activity_type);
    snprintf(node->display_title, sizeof(node->display_title), "%s%s", icon, title);
}

bool activity_tree_scan(ActivityTree *tree, const char *data_dir) {
    activity_tree_free(tree);

    char activity_dir[512];
    snprintf(activity_dir, sizeof(activity_dir), "%s/activity", data_dir);

    // Create activity directory if it doesn't exist
    create_directory_path(activity_dir);

    DIR *year_dir = opendir(activity_dir);
    if (!year_dir) {
        return false;
    }

    // First pass: count years
    size_t year_capacity = 16;
    tree->years = malloc(year_capacity * sizeof(TreeNode));
    if (!tree->years) {
        closedir(year_dir);
        return false;
    }

    struct dirent *year_entry;
    while ((year_entry = readdir(year_dir)) != NULL) {
        if (year_entry->d_name[0] == '.') continue;

        char year_path[512];
        snprintf(year_path, sizeof(year_path), "%s/%s", activity_dir, year_entry->d_name);

        struct stat st;
        if (stat(year_path, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        // Validate year format (4 digits)
        if (strlen(year_entry->d_name) != 4) continue;

        if (tree->year_count >= year_capacity) {
            year_capacity *= 2;
            TreeNode *new_years = realloc(tree->years, year_capacity * sizeof(TreeNode));
            if (!new_years) {
                closedir(year_dir);
                return false;
            }
            tree->years = new_years;
        }

        TreeNode *year_node = &tree->years[tree->year_count];
        memset(year_node, 0, sizeof(TreeNode));
        year_node->type = NODE_YEAR;
        strncpy(year_node->name, year_entry->d_name, sizeof(year_node->name) - 1);
        year_node->expanded = false;

        // Scan months in this year
        DIR *month_dir = opendir(year_path);
        if (!month_dir) {
            tree->year_count++;
            continue;
        }

        size_t month_capacity = 12;
        year_node->children = malloc(month_capacity * sizeof(TreeNode));
        if (!year_node->children) {
            closedir(month_dir);
            tree->year_count++;
            continue;
        }

        struct dirent *month_entry;
        while ((month_entry = readdir(month_dir)) != NULL) {
            if (month_entry->d_name[0] == '.') continue;

            char month_path[512];
            snprintf(month_path, sizeof(month_path), "%s/%s", year_path, month_entry->d_name);

            if (stat(month_path, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

            // Validate month format (2 digits, 01-12)
            if (strlen(month_entry->d_name) != 2) continue;
            int month_num = atoi(month_entry->d_name);
            if (month_num < 1 || month_num > 12) continue;

            if (year_node->child_count >= month_capacity) {
                month_capacity *= 2;
                TreeNode *new_months = realloc(year_node->children, month_capacity * sizeof(TreeNode));
                if (!new_months) continue;
                year_node->children = new_months;
            }

            TreeNode *month_node = &year_node->children[year_node->child_count];
            memset(month_node, 0, sizeof(TreeNode));
            month_node->type = NODE_MONTH;
            snprintf(month_node->name, sizeof(month_node->name), "%s", get_month_name(month_num));
            month_node->activity_time = month_num;  // Store month number for sorting
            month_node->expanded = false;

            // Scan files in this month - first collect all files
            DIR *file_dir = opendir(month_path);
            if (!file_dir) {
                year_node->child_count++;
                continue;
            }

            // Temporary array to collect all files before grouping
            size_t temp_capacity = 32;
            size_t temp_count = 0;
            TreeNode *temp_files = malloc(temp_capacity * sizeof(TreeNode));
            if (!temp_files) {
                closedir(file_dir);
                year_node->child_count++;
                continue;
            }

            struct dirent *file_entry;
            while ((file_entry = readdir(file_dir)) != NULL) {
                size_t len = strlen(file_entry->d_name);
                bool is_fit = (len > 4 && strcasecmp(file_entry->d_name + len - 4, ".fit") == 0);
                bool is_json = (len > 5 && strcasecmp(file_entry->d_name + len - 5, ".json") == 0);
                bool is_meta = (len > 10 && strcasecmp(file_entry->d_name + len - 10, ".meta.json") == 0);
                if (is_meta) continue;  // Skip metadata sidecar files
                if (!is_fit && !is_json) continue;

                if (temp_count >= temp_capacity) {
                    temp_capacity *= 2;
                    TreeNode *new_files = realloc(temp_files, temp_capacity * sizeof(TreeNode));
                    if (!new_files) continue;
                    temp_files = new_files;
                }

                TreeNode *file_node = &temp_files[temp_count];
                memset(file_node, 0, sizeof(TreeNode));
                file_node->type = NODE_FILE;
                strncpy(file_node->name, file_entry->d_name, sizeof(file_node->name) - 1);
                snprintf(file_node->full_path, sizeof(file_node->full_path), "%s/%s",
                         month_path, file_entry->d_name);

                // Load display title for treeview
                load_activity_title(file_node);

                // Get activity timestamp for sorting
                file_node->activity_time = fit_get_activity_timestamp(file_node->full_path);
                if (file_node->activity_time == 0) {
                    // Fallback to file modification time
                    if (stat(file_node->full_path, &st) == 0) {
                        file_node->activity_time = st.st_mtime;
                    }
                }

                temp_count++;
            }
            closedir(file_dir);

            // Sort files by activity time (newest first)
            if (temp_count > 1) {
                qsort(temp_files, temp_count, sizeof(TreeNode), compare_files_desc);
            }

            // Group overlapping activities (within 10 minutes = 600 seconds)
            #define OVERLAP_THRESHOLD 600
            bool *grouped = calloc(temp_count, sizeof(bool));
            size_t child_capacity = temp_count;
            month_node->children = malloc(child_capacity * sizeof(TreeNode));
            if (!month_node->children || !grouped) {
                free(temp_files);
                free(grouped);
                if (month_node->children) free(month_node->children);
                month_node->children = NULL;
                year_node->child_count++;
                continue;
            }

            for (size_t i = 0; i < temp_count; i++) {
                if (grouped[i]) continue;

                // Find all files that overlap with this one
                size_t group_indices[32];
                size_t group_size = 0;
                group_indices[group_size++] = i;
                grouped[i] = true;

                for (size_t j = i + 1; j < temp_count && group_size < 32; j++) {
                    if (grouped[j]) continue;

                    // Check if j overlaps with any file in the current group
                    for (size_t k = 0; k < group_size; k++) {
                        time_t t1 = temp_files[group_indices[k]].activity_time;
                        time_t t2 = temp_files[j].activity_time;
                        time_t diff = (t1 > t2) ? (t1 - t2) : (t2 - t1);
                        if (diff <= OVERLAP_THRESHOLD) {
                            group_indices[group_size++] = j;
                            grouped[j] = true;
                            break;
                        }
                    }
                }

                if (group_size == 1) {
                    // Single file, add directly to month
                    month_node->children[month_node->child_count++] = temp_files[i];
                } else {
                    // Create a group node
                    TreeNode *group_node = &month_node->children[month_node->child_count++];
                    memset(group_node, 0, sizeof(TreeNode));
                    group_node->type = NODE_GROUP;
                    group_node->expanded = false;
                    group_node->activity_time = temp_files[group_indices[0]].activity_time;

                    // Generate meta path for the group sidecar
                    group_meta_path(month_path, group_node->activity_time,
                                    group_node->meta_path, sizeof(group_node->meta_path));

                    // Try to load group metadata
                    GroupMeta gmeta = {0};
                    bool has_meta = group_meta_load(group_node->meta_path, &gmeta);

                    // Use metadata title if available, otherwise first file's title
                    const char *title = (has_meta && gmeta.title_edited && gmeta.title[0])
                        ? gmeta.title : temp_files[group_indices[0]].display_title;

                    snprintf(group_node->name, sizeof(group_node->name), "%s (%zu)",
                             title, group_size);
                    strncpy(group_node->display_title, group_node->name, sizeof(group_node->display_title) - 1);

                    // Add files as children of the group
                    group_node->children = malloc(group_size * sizeof(TreeNode));
                    if (group_node->children) {
                        for (size_t g = 0; g < group_size; g++) {
                            group_node->children[g] = temp_files[group_indices[g]];
                        }
                        group_node->child_count = group_size;
                    }
                }
            }
            #undef OVERLAP_THRESHOLD

            free(grouped);
            free(temp_files);

            year_node->child_count++;
        }
        closedir(month_dir);

        // Sort months (newest first, i.e., December before January)
        if (year_node->child_count > 1) {
            qsort(year_node->children, year_node->child_count, sizeof(TreeNode), compare_months_desc);
        }

        tree->year_count++;
    }
    closedir(year_dir);

    // Sort years (newest first)
    if (tree->year_count > 1) {
        qsort(tree->years, tree->year_count, sizeof(TreeNode), compare_years_desc);
    }

    // Expand newest year and its newest month
    if (tree->year_count > 0) {
        tree->years[0].expanded = true;
        if (tree->years[0].child_count > 0) {
            tree->years[0].children[0].expanded = true;
        }
    }

    return true;
}

size_t activity_tree_visible_count(const ActivityTree *tree) {
    size_t count = 0;

    for (size_t y = 0; y < tree->year_count; y++) {
        count++;  // Year node
        TreeNode *year = &tree->years[y];

        if (year->expanded) {
            for (size_t m = 0; m < year->child_count; m++) {
                count++;  // Month node
                TreeNode *month = &year->children[m];

                if (month->expanded) {
                    for (size_t c = 0; c < month->child_count; c++) {
                        count++;  // File or Group node
                        TreeNode *child = &month->children[c];
                        if (child->type == NODE_GROUP && child->expanded) {
                            count += child->child_count;  // Files in group
                        }
                    }
                }
            }
        }
    }

    return count;
}

TreeNode *activity_tree_get_visible(ActivityTree *tree, size_t visible_index) {
    size_t current = 0;

    for (size_t y = 0; y < tree->year_count; y++) {
        TreeNode *year = &tree->years[y];

        if (current == visible_index) return year;
        current++;

        if (year->expanded) {
            for (size_t m = 0; m < year->child_count; m++) {
                TreeNode *month = &year->children[m];

                if (current == visible_index) return month;
                current++;

                if (month->expanded) {
                    for (size_t c = 0; c < month->child_count; c++) {
                        TreeNode *child = &month->children[c];
                        if (current == visible_index) return child;
                        current++;

                        if (child->type == NODE_GROUP && child->expanded) {
                            for (size_t f = 0; f < child->child_count; f++) {
                                if (current == visible_index) return &child->children[f];
                                current++;
                            }
                        }
                    }
                }
            }
        }
    }

    return NULL;
}

TreeNode *activity_tree_toggle(ActivityTree *tree, size_t visible_index) {
    TreeNode *node = activity_tree_get_visible(tree, visible_index);
    if (node && (node->type == NODE_YEAR || node->type == NODE_MONTH || node->type == NODE_GROUP)) {
        node->expanded = !node->expanded;
    }
    return node;
}
