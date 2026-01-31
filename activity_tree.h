#ifndef ACTIVITY_TREE_H
#define ACTIVITY_TREE_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

typedef enum {
    NODE_YEAR,
    NODE_MONTH,
    NODE_FILE
} TreeNodeType;

typedef struct TreeNode {
    TreeNodeType type;
    char name[64];           // "2024", "January", "ride.fit"
    char display_title[128]; // Title for display in treeview
    char full_path[512];     // Full path for files
    time_t activity_time;    // For sorting
    bool expanded;
    struct TreeNode *children;
    size_t child_count;
} TreeNode;

typedef struct {
    TreeNode *years;
    size_t year_count;
} ActivityTree;

// Initialize activity tree
void activity_tree_init(ActivityTree *tree);

// Free activity tree resources
void activity_tree_free(ActivityTree *tree);

// Scan data_dir/activity and build tree structure
// Returns true on success
bool activity_tree_scan(ActivityTree *tree, const char *data_dir);

// Count total visible rows (for scrolling calculations)
size_t activity_tree_visible_count(const ActivityTree *tree);

// Toggle expand/collapse on a node by visible index
// Returns the node that was toggled (or NULL if index out of bounds)
TreeNode *activity_tree_toggle(ActivityTree *tree, size_t visible_index);

// Get node at visible index
TreeNode *activity_tree_get_visible(ActivityTree *tree, size_t visible_index);

// Get month name from month number (1-12)
const char *get_month_name(int month);

#endif // ACTIVITY_TREE_H
