#include "point_store.h"
#include <filesystem>
#include <vector>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <iomanip>

import_json_datapoints_result JsonExchangeDatapoint::import_json_datapoints(const std::filesystem::path& json_path) {
    import_json_datapoints_result result;
    std::ifstream in(json_path);
    if (!in.is_open()) {
        std::cerr << "failed to open JSON file: " << json_path << '\n';
        result.errors.push_back("failed to open JSON file: " + json_path.string());
        return result;
    }

    nlohmann::json j;
    try {
        in >> j;
    }
    catch (const std::exception& e) {
        std::cerr << "failed to parse JSON: " << e.what() << '\n';
        result.errors.push_back("failed to parse JSON: " + std::string(e.what()));
        return result;
    }

    std::vector<DataPoint> points_to_import;
    if (j.is_array()) {
        // try parse as array of datapoints
        for (const auto& elem : j) {
            if (!elem.is_object()) continue;
            try {
                DataPoint p = elem.get<DataPoint>();
                points_to_import.push_back(p);
            }
            catch (const std::exception& e) {
                std::cerr << elem.dump() << '\n' << e.what() << '\n';
                // ignore malformed entries
            }
        }
    }
    else {
        // try parse as object with array properties (e.g., "minerals", "locations", etc.)
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (!it.value().is_array()) continue;
            for (const auto& elem : it.value()) {
                if (!elem.is_object()) continue;
                try {
                    DataPoint p = elem.get<DataPoint>();
                    points_to_import.push_back(p);
                }
                catch (const std::exception& e) {
                    std::cerr << elem.dump() << '\n' << e.what() << '\n';
                    // ignore malformed entries
                }
            }
        }
    }

    int failures = 0;
    for (const auto& p : points_to_import) {
        int rc = store->uuid_insert_or_update(p);
        if (rc == 0) {
            std::cerr << "failed to import datapoint with id: " << p.id << '\n';
            result.errors.push_back("failed to import datapoint with id: " + p.id);
            ++failures;
        }
            else if (rc == 2) {
                std::cerr << "skipped duplicate datapoint with id: " << p.id << '\n';
                result.errors.push_back("skipped duplicate datapoint with id: " + p.id);
                ++result.skipped_count;
            }
    }

    result.imported_count = static_cast<int>(points_to_import.size()) - failures - result.skipped_count;
    result.failed_count = failures;
    return result;
}

int JsonExchangeDatapoint::export_json_datapoints(const std::filesystem::path& json_path, const std::vector<DataPoint>& points) {
    nlohmann::json j = nlohmann::json::array();
    for (const auto& p : points) {
        j.push_back(p);
    }

    std::ofstream out(json_path);
    if (!out.is_open()) {
        std::cerr << "failed to open output file: " << json_path << '\n';
        return 1;
    }

    // set for minimal 
	out << std::setw(0) << j << std::endl;
    return 0;
}

int JsonExchangeDatapoint::export_json_datapoints(const std::filesystem::path& json_path, const std::vector<PoiType>& poi_types) {
    auto points = store->load_points(std::string(), std::string(), poi_types);
    return export_json_datapoints(json_path, points);
}