#pragma once

#include <string>
#include <vector>
#include <functional>

int run_scout_app();

bool write_starmap_json(std::string &starmap_json_path);

void popup_filter(std::string& filtertext, std::string& local_selected, const std::vector<std::string>& items, const std::string& app_selected_item, std::function<void(const std::string&)> on_select);
