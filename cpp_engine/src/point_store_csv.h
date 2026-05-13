#pragma once

#include "point_store.h"
#include <string>
#include <vector>
#include <filesystem>
#include <unordered_map>

std::vector<std::string> load_server_ids_csv(const std::filesystem::path& path, const std::vector<std::string>& defaults);
std::vector<Resource> load_material_catalog(const std::filesystem::path& path, const std::vector<std::string>& default_names, const std::unordered_map<std::string, std::string>& default_shorts);
std::vector<Planet> load_planet_catalog(const std::filesystem::path& path, const std::vector<std::string>& defaults);
bool write_points_csv(const std::filesystem::path& csv_path, const std::vector<DataPoint>& points);
bool write_planets_csv(const std::filesystem::path& csv_path, const std::vector<Planet>& planets);
std::vector<DataPoint> load_points(const std::filesystem::path& csv_path);
bool append_point_csv(const std::filesystem::path& csv_path, const DataPoint& point);

// class CsvPointStore : public IPointStore {
// public:
//     explicit CsvPointStore(const std::string& csv_path);
//     ~CsvPointStore() override = default;

//     std::vector<DataPoint> load_points() override;
//     bool append_point(const DataPoint& p, std::string* out_change_id = nullptr) override;
//     bool overwrite_points(const std::vector<DataPoint>& points) override;
//     bool push_change_event(const ChangeEvent& ev) override;

// private:
//     std::string csv_path_;
// };
