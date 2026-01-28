#include "activity_tree.h"
#include "file_organizer.h"
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
        year_node->expanded = (tree->year_count == 0);  // Expand first year by default

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

            // Scan files in this month
            DIR *file_dir = opendir(month_path);
            if (!file_dir) {
                year_node->child_count++;
                continue;
            }

            size_t file_capacity = 32;
            month_node->children = malloc(file_capacity * sizeof(TreeNode));
            if (!month_node->children) {
                closedir(file_dir);
                year_node->child_count++;
                continue;
            }

            struct dirent *file_entry;
            while ((file_entry = readdir(file_dir)) != NULL) {
                size_t len = strlen(file_entry->d_name);
                if (len <= 4 || strcasecmp(file_entry->d_name + len - 4, ".fit") != 0) continue;

                if (month_node->child_count >= file_capacity) {
                    file_capacity *= 2;
                    TreeNode *new_files = realloc(month_node->children, file_capacity * sizeof(TreeNode));
                    if (!new_files) continue;
                    month_node->children = new_files;
                }

                TreeNode *file_node = &month_node->children[month_node->child_count];
                memset(file_node, 0, sizeof(TreeNode));
                file_node->type = NODE_FILE;
                strncpy(file_node->name, file_entry->d_name, sizeof(file_node->name) - 1);
                snprintf(file_node->full_path, sizeof(file_node->full_path), "%s/%s",
                         month_path, file_entry->d_name);

                // Get activity timestamp for sorting
                file_node->activity_time = fit_get_activity_timestamp(file_node->full_path);
                if (file_node->activity_time == 0) {
                    // Fallback to file modification time
                    if (stat(file_node->full_path, &st) == 0) {
                        file_node->activity_time = st.st_mtime;
                    }
                }

                month_node->child_count++;
            }
            closedir(file_dir);

            // Sort files by activity time (newest first)
            if (month_node->child_count > 1) {
                qsort(month_node->children, month_node->child_count, sizeof(TreeNode), compare_files_desc);
            }

            year_node->child_count++;
        }
        closedir(month_dir);

        // Sort months (newest first, i.e., December before January)
        if (year_node->child_count > 1) {
            qsort(year_node->children, year_node->child_count, sizeof(TreeNode), compare_months_desc);
        }

        // Expand first month if year is expanded
        if (year_node->expanded && year_node->child_count > 0) {
            year_node->children[0].expanded = true;
        }

        tree->year_count++;
    }
    closedir(year_dir);

    // Sort years (newest first)
    if (tree->year_count > 1) {
        qsort(tree->years, tree->year_count, sizeof(TreeNode), compare_years_desc);
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
                    count += month->child_count;  // File nodes
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
                    for (size_t f = 0; f < month->child_count; f++) {
                        if (current == visible_index) return &month->children[f];
                        current++;
                    }
                }
            }
        }
    }

    return NULL;
}

TreeNode *activity_tree_toggle(ActivityTree *tree, size_t visible_index) {
    TreeNode *node = activity_tree_get_visible(tree, visible_index);
    if (node && (node->type == NODE_YEAR || node->type == NODE_MONTH)) {
        node->expanded = !node->expanded;
    }
    return node;
}
