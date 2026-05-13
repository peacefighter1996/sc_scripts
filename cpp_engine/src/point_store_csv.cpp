#include "point_store_csv.h"
#include "scout_core.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>

// Local helper: find column index from header row
static int find_column_index(const std::vector<std::string>& headers, const std::vector<std::string>& candidates) {
    for (size_t i = 0; i < headers.size(); ++i) {
        const auto header = to_lower(trim(headers[i]));
        for (const auto& candidate : candidates) {
            if (header == candidate) {
                return static_cast<int>(i);
            }
        }
    }
    return -1;
}

std::vector<std::string> load_server_ids_csv(const std::filesystem::path& path, const std::vector<std::string>& defaults) {
    std::ifstream in(path);
    if (!in.is_open()) {
        return defaults;
    }

    bool header_parsed = false;
    int value_index = 0;
    std::vector<std::string> values;
    std::string line;
    while (std::getline(in, line)) {
        const auto trimmed = trim(line);
        if (trimmed.empty()) {
            continue;
        }

        const auto cells = split_csv_row(trimmed);
        if (cells.empty()) {
            continue;
        }

        if (!header_parsed) {
            value_index = find_column_index(cells, { "value", "server", "server_id", "id" });
            if (value_index < 0) {
                value_index = 0;
            }
            header_parsed = true;
            continue;
        }

        if (value_index >= static_cast<int>(cells.size())) {
            continue;
        }

        const auto value = trim(cells[static_cast<size_t>(value_index)]);
        if (value.empty()) {
            continue;
        }

        if (std::find(values.begin(), values.end(), value) == values.end()) {
            values.push_back(value);
        }
    }

    return values.empty() ? defaults : values;
}

std::vector<Resource> load_material_catalog(const std::filesystem::path& path, const std::vector<std::string>& default_names, const std::unordered_map<std::string, std::string>& default_shorts) {
    std::ifstream in(path);
    if (!in.is_open()) {
        return {};
    }

    std::vector<Resource> catalog;
    bool header_parsed = false;
    int name_index = -1;
    int shortname_index = -1;
    int id_index = -1;
    int type_index = -1;
    int harvest_type_index = -1;

    std::string line;
    while (std::getline(in, line)) {
        const auto trimmed = trim(line);
        if (trimmed.empty()) {
            continue;
        }

        const auto cells = split_csv_row(trimmed);
        if (cells.empty()) {
            continue;
        }

        if (!header_parsed) {
            id_index = find_column_index(cells, { "id" });
            name_index = find_column_index(cells, { "name", "material", "value" });
            shortname_index = find_column_index(cells, { "shortname", "short_name", "short", "code" });
            if (name_index < 0) {
                name_index = std::min(1, static_cast<int>(cells.size()) - 1);
            }
            type_index = find_column_index(cells, { "type", "resourcetype", "resource_type" });
            harvest_type_index = find_column_index(cells, { "harvest_type", "harvesttype" });

            header_parsed = true;
            continue;
        }

        if (name_index < 0 || name_index >= static_cast<int>(cells.size())) {
            continue;
        }

        const auto name = trim(cells[static_cast<size_t>(name_index)]);
        if (name.empty()) {
            continue;
        }

        std::string shortname;
        if (shortname_index >= 0 && shortname_index < static_cast<int>(cells.size())) {
            shortname = trim(cells[static_cast<size_t>(shortname_index)]);
        }

        int id = -1;
        if (id_index >= 0 && id_index < static_cast<int>(cells.size())) {
            try {
                id = std::stoi(trim(cells[static_cast<size_t>(id_index)]));
            }
            catch (const std::exception&) {
                // ignore parse errors and just use default ids
            }
        }

        ResourceType type = ResourceType::None;
        if (type_index >= 0 && type_index < static_cast<int>(cells.size()))
        {
            const auto type_str = to_lower(trim(cells[static_cast<size_t>(type_index)]));
            if (type_str == "mineral") {
                type = ResourceType::Mineral;
            } else if (type_str == "plant") {
                type = ResourceType::Plant;
            }
        }

        HarvestType harvest_type = HarvestType::None;
        if (harvest_type_index >= 0 && harvest_type_index < static_cast<int>(cells.size()))
        {
            const auto harvest_str = to_lower(trim(cells[static_cast<size_t>(harvest_type_index)]));
            if (harvest_str.find("fps") != std::string::npos) {
                harvest_type = static_cast<HarvestType>(static_cast<int>(harvest_type) | static_cast<int>(HarvestType::FPS));
            }
            if (harvest_str.find("vehicle") != std::string::npos) {
                harvest_type = static_cast<HarvestType>(static_cast<int>(harvest_type) | static_cast<int>(HarvestType::Vehicle));
            }
            if (harvest_str.find("ship") != std::string::npos) {
                harvest_type = static_cast<HarvestType>(static_cast<int>(harvest_type) | static_cast<int>(HarvestType::Ship));
            }
        }


        Resource material{ id, name, shortname, type, harvest_type };
        catalog.push_back(material);
    }

    if (catalog.empty()) {
        for (const auto& name : default_names) {
            const auto it = default_shorts.find(name);
            const auto short_name = it != default_shorts.end() ? it->second : name.substr(0, std::min<size_t>(4, name.size()));
            catalog.push_back(Resource{ -1, name, short_name });
        }
    }

    //split catolog where resource type is none / and defined. order defined by name. and combine them
    std::vector<Resource> none_type_resources;
    std::vector<Resource> defined_type_resources;

    for (const auto& resource : catalog) {
        if (resource.type == ResourceType::None) {
            none_type_resources.push_back(resource);
        }
        else {
            defined_type_resources.push_back(resource);
        }
    }

    std::sort(defined_type_resources.begin(), defined_type_resources.end(), [](const Resource& a, const Resource& b) {
        return a.name < b.name;
    });

    catalog.clear();
    catalog.insert(catalog.end(), none_type_resources.begin(), none_type_resources.end());
    catalog.insert(catalog.end(), defined_type_resources.begin(), defined_type_resources.end());

    return catalog;
}

std::vector<Planet> load_planet_catalog(const std::filesystem::path& path, const std::vector<std::string>& defaults) {
    std::ifstream in(path);
    if (!in.is_open()) {
        std::vector<Planet> catalog;
        for (const auto& key : defaults) {
            catalog.push_back(Planet{ -1, "", key, key, "" });
        }
        return catalog;
    }

    std::vector<Planet> catalog;
    bool header_parsed = false;
    int image_dir_index = -1;
    int planet_name_index = -1;
    int id_index = -1;
    int system_index = -1;
    int zone_id_index = -1;

    std::string line;
    while (std::getline(in, line)) {
        const auto trimmed = trim(line);
        if (trimmed.empty()) {
            continue;
        }

        const auto cells = split_csv_row(trimmed);
        if (cells.empty()) {
            continue;
        }

        if (!header_parsed) {
            //id,system,planet,image_dir,zone_id
            id_index = find_column_index(cells, { "id", "planet_id", "key" });
            image_dir_index = find_column_index(cells, { "image_dir", "imagedir", "image" });
            planet_name_index = find_column_index(cells, { "planet", "name", "value" });
            system_index = find_column_index(cells, { "system" });
            zone_id_index = find_column_index(cells, { "zone_id", "zoneid", "zone" });
            header_parsed = true;
            continue;
        }

        int id = -1;
        std::string system;
        std::string zone_id;
        std::string planet_name;
        std::string img_dir;

        if (id_index >= 0 && id_index < static_cast<int>(cells.size())) {
            try {
                id = std::stoi(trim(cells[static_cast<size_t>(id_index)]));
            }
            catch (const std::exception&) {
                // ignore parse errors and just use default ids
            }
        }

        if (system_index >= 0 && system_index < static_cast<int>(cells.size())) {
            system = trim(cells[static_cast<size_t>(system_index)]);
        }

        if (zone_id_index >= 0 && zone_id_index < static_cast<int>(cells.size())) {
            zone_id = trim(cells[static_cast<size_t>(zone_id_index)]);
        }

        if (planet_name_index >= 0 && planet_name_index < static_cast<int>(cells.size())) {
            planet_name = trim(cells[static_cast<size_t>(planet_name_index)]);
        }

        if (image_dir_index >= 0 && image_dir_index < static_cast<int>(cells.size())) {
            img_dir = trim(cells[static_cast<size_t>(image_dir_index)]);
        }

        if (planet_name.empty()) {
            continue;
        }

        Planet planet{ id, system, planet_name, img_dir, zone_id };
        catalog.push_back(planet);
    }

    if (catalog.empty()) {
        for (const auto& key : defaults) {
            catalog.push_back(Planet{ -1, "", key, key, "" });
        }
    }

    // Ensure catalog is sorted by name for consistent ordering in UI
    std::sort(catalog.begin(), catalog.end(), [](const Planet& a, const Planet& b) {
        return a.name < b.name;
    });

    return catalog;
}

std::vector<DataPoint> load_points(const std::filesystem::path& csv_path) {
    std::vector<DataPoint> points;
    std::ifstream in(csv_path);
    if (!in.is_open()) {
        return points;
    }

    std::string line;
    bool first_row = true;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }

        if (first_row) {
            first_row = false;
            const auto lower = to_lower(line);
            if (lower.find("recordid") != std::string::npos || lower.find("id") != std::string::npos) {
                continue;
            }
        }

        const auto row = split_csv_row(line);
        if (row.size() < 9) {
            continue;
        }

        try {
            DataPoint point;
            point.id = std::stoi(trim(row[0]));
            point.server = trim(row[1]);
            point.x = std::stod(trim(row[2]));
            point.y = std::stod(trim(row[3]));
            point.z = std::stod(trim(row[4]));
            point.planet = trim(row[5]);
            point.material = trim(row[6]);
            // backward-compatible: quality fields expected at indices 7 and 8
            point.quality_min = row.size() > 7 ? std::stod(trim(row[7])) : 0.0;
            point.quality_max = row.size() > 8 ? std::stod(trim(row[8])) : 0.0;
            point.note = row.size() >= 10 ? row[9] : std::string{};
            // POI type may be stored as an integer index or a name. Handle both for compatibility.
            point.poi_type = PoiType::Unknown;
            if (row.size() >= 11) {
                const std::string v = trim(row[10]);
                if (!v.empty()) {
                    try {
                        int iv = std::stoi(v);
                        if (iv >= 0 && static_cast<size_t>(iv) < poi_impl::poi_type_count) {
                            point.poi_type = static_cast<PoiType>(iv);
                        } else {
                            PoiType tmp;
                            if (poi_type_from_string(v, tmp)) {
                                point.poi_type = tmp;
                            }
                        }
                    } catch (...) {
                        PoiType tmp;
                        if (poi_type_from_string(v, tmp)) {
                            point.poi_type = tmp;
                        }
                    }
                }
            }
            point.time_info = row.size() >= 12 ? trim(row[11]) : std::string{};
            const auto material = to_lower(point.material);
            point.location = material == "location" || material == "cave";
            points.push_back(point);
        } catch (...) {
        }
    }

    return points;
}

bool append_point_csv(const std::filesystem::path& csv_path, const DataPoint& point) {
    const auto file_exists = std::filesystem::exists(csv_path);
    const auto parent = csv_path.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
    }

    std::ofstream out(csv_path, std::ios::app);
    if (!out.is_open()) {
        return false;
    }

    if (!file_exists) {
        out << "recordid,server,x,y,z,planet,material,quality_min,quality_max,note,poi_type,poi_time\n";
    }

    out << point.id << ','
        << csv_escape(point.server) << ','
        << std::setprecision(15) << point.x << ','
        << std::setprecision(15) << point.y << ','
        << std::setprecision(15) << point.z << ','
        << csv_escape(point.planet) << ','
        << csv_escape(point.material) << ','
        << std::setprecision(15) << point.quality_min << ','
        << std::setprecision(15) << point.quality_max << ','
        << csv_escape(point.note) << ','
        << csv_escape(poi_type_name(point.poi_type)) << ','
        << csv_escape(point.time_info)
        << '\n';

    return true;
}


// Overwrite the CSV with the provided points vector. Matches append_point header/order.
bool write_points_csv(const std::filesystem::path& csv_path, const std::vector<DataPoint>& points) {
    const auto parent = csv_path.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
    }

    std::ofstream out(csv_path, std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    out << "recordid,server,x,y,z,planet,material,quality_min,quality_max,note,poi_type,poi_time\n";
    for (const auto& point : points) {
        out << point.id << ','
            << csv_escape(point.server) << ','
            << std::setprecision(15) << point.x << ','
            << std::setprecision(15) << point.y << ','
            << std::setprecision(15) << point.z << ','
            << csv_escape(point.planet) << ','
            << csv_escape(point.material) << ','
            << std::setprecision(15) << point.quality_min << ','
            << std::setprecision(15) << point.quality_max << ','
            << csv_escape(point.note) << ','
            << csv_escape(poi_type_name(point.poi_type)) << ','
            << csv_escape(point.time_info)
            << '\n';
    }

    return true;
}

// Overwrite the CSV with the provided planets vector.
bool write_planets_csv(const std::filesystem::path& csv_path, const std::vector<Planet>& planets) {
    const auto parent = csv_path.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
    }

    std::ofstream out(csv_path, std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    out << "id,system,planet,image_dir,zone_id\n";
    for (const auto& p : planets) {
        out << p.id << ','
            << csv_escape(p.system) << ','
            << csv_escape(p.name) << ','
            << csv_escape(p.image_dir) << ','
            << csv_escape(p.zone_id) << '\n';
    }

    return true;
}

// CsvPointStore::CsvPointStore(const std::string& csv_path)
//     : csv_path_(csv_path) {}

// std::vector<DataPoint> CsvPointStore::load_points() {
//     return ::load_points(std::filesystem::path(csv_path_));
// }

// bool CsvPointStore::append_point(const DataPoint& p, std::string* out_change_id) {
//     // CSV backend doesn't produce change_ids; optional out_change_id left untouched
//     return ::append_point(std::filesystem::path(csv_path_), p);
// }

// bool CsvPointStore::overwrite_points(const std::vector<DataPoint>& points) {
//     return write_points_csv(std::filesystem::path(csv_path_), points);
// }

// bool CsvPointStore::push_change_event(const ChangeEvent& /*ev*/) {
//     // No-op for CSV backend; events are not tracked here
//     return true;
// }
