#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "starmap_poi.h"

int ParsePoiObjects(nlohmann::json_abi_v3_11_2::json &j, std::vector<StarmapPoi> &pois, bool &retFlag);
int dbimport_starmap(std::string &db_path, std::string &jsonpath, bool &retFlag);
bool extract_json_from_file(const std::string& file_path, nlohmann::json& j);