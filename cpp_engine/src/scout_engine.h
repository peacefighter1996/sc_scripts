#pragma once
int ParsePoiObjects(nlohmann::json_abi_v3_11_2::json &j, std::vector<StarmapPoi> &pois, bool &retFlag);
bool extract_json_from_file(const std::string& file_path, nlohmann::json& j);