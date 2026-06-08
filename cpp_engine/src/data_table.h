#pragma once
#include <vector>
#include <string>
#include <utility>
#include <cstddef>
#include <memory>
#include "scout_core.h"

struct AppState; // forward

class DataTable {
public:
    DataTable();
    // Render the datatable UI and manage in-memory edits
    void render(AppState& state);

    // Initialize defaults
    void init();

private:
    std::vector<std::string> filters;
    std::string sort_column;
    int sort_order{0};

    // Paging and change tracking
    int page_size{100};
    int page_index{0};
    std::vector<std::pair<std::string,int>> change_list;

    // Internal caches for efficient rebuilds
    std::vector<size_t> index_map;
    std::vector<std::string> prev_filters;
    std::string prev_sort_col;
    std::vector<size_t> datatable_index_map;
    int prev_sort_order{-1};
    size_t prev_points_size{0};

    // UI state
    bool data_dirty{false};
    bool prev_data_dirty {true};
    std::string save_message;

    // Cached lists for combo box options
    std::vector<const char*> poi_types;
    std::vector<const char*> poi_subtypes;
    std::vector<const char*> materials;
    std::vector<const char*> servers;
    std::vector<const char*> planets;
    // Cache of label pointers (owned via cell_control_name_owned)
    std::vector<char*> cell_control_name_cache;
    std::vector<std::unique_ptr<char[]>> cell_control_name_owned;

    // Allocate and own a C-string label; returns pointer stored in owned vector
    char* make_label(const std::string& s);
};
