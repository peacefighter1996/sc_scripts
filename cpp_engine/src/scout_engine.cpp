#include "scout_app.h"
#include "scout_core.h"
#include "point_store_csv.h"
#include "starmap_poi.h"

#include <algorithm>
#include <filesystem>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <iterator>
#include <cmath>
#include <nlohmann/json.hpp>
#include <regex>
#include <string>
#include <vector>
#include "scout_engine.h"

static std::string sanitize_json_content(const std::string& input) {
	std::string out;
	out.reserve(input.size());
	bool in_string = false;
	int backslash_count = 0;
	for (size_t i = 0; i < input.size(); ++i) {
		char c = input[i];
		if (c == '\\') {
			++backslash_count;
			out.push_back(c);
			continue;
		}
		if (c == '"') {
			if ((backslash_count % 2) == 0) {
				in_string = !in_string;
			}
			out.push_back(c);
			backslash_count = 0;
			continue;
		}
		if (c == '\n') {
			if (in_string && (backslash_count % 2) == 0) {
				out.append("\\n");
			} else {
				out.push_back(c);
			}
			backslash_count = 0;
			continue;
		}
		if (c == '\r') {
			if (in_string && (backslash_count % 2) == 0) {
				out.append("\\r");
			} else {
				// drop CR outside of strings to normalize CRLF to LF
			}
			backslash_count = 0;
			continue;
		}
		out.push_back(c);
		backslash_count = 0;
	}
	return out;
}

int main(int argc, char** argv) {
	if (argc < 2) {
		return run_scout_app();
	}

	const std::string command = argv[1];
	if (command == "app") {
		return run_scout_app();
	}

	if (command == "dump") {
		if (argc != 3) {
			std::cerr << "usage: scout_engine dump <csv_path>\n";
			return 2;
		}
		print_dump(load_points(std::filesystem::path(argv[2])));
		return 0;
	}

	if (command == "next-id") {
		if (argc != 3) {
			std::cerr << "usage: scout_engine next-id <csv_path>\n";
			return 2;
		}

		int max_id = 0;
		for (const auto& point : load_points(std::filesystem::path(argv[2]))) {
			max_id = std::max(max_id, point.id);
		}
		std::cout << (max_id + 1) << '\n';
		return 0;
	}

	if (command == "append") {
		if (argc != 13) {
			std::cerr << "usage: scout_engine append <csv_path> <id> <server> <x> <y> <z> <planet> <material> <quality_min> <quality_max> <note>\n";
			return 2;
		}

		try {
			DataPoint point;
			point.id = std::stoi(argv[3]);
			point.server = argv[4];
			point.x = std::stod(argv[5]);
			point.y = std::stod(argv[6]);
			point.z = std::stod(argv[7]);
			point.planet = argv[8];
			point.material = argv[9];
			point.quality_min = std::stod(argv[10]);
			point.quality_max = std::stod(argv[11]);
			point.note = argv[12];
			const auto material = to_lower(point.material);
			point.location = material == "location" || material == "cave";
			return append_point_csv(argv[2], point) ? 0 : 1;
		}
		catch (...) {
			std::cerr << "invalid append arguments\n";
			return 2;
		}
	}

	if (command == "parse-xyz") {
		if (argc != 3) {
			std::cerr << "usage: scout_engine parse-xyz <ocr_text>\n";
			return 2;
		}

		double x = 0.0;
		double y = 0.0;
		double z = 0.0;
		std::string locationmarker;
		if (!try_parse_xyz_from_ocr_text(argv[2], x, y, z, locationmarker)) {
			return 1;
		}

		std::cout << x << '\t' << y << '\t' << z << '\n';
		return 0;
	}

	if (command == "ocr-task") {
		if (argc != 3 && argc != 4) {
			std::cerr << "usage: scout_engine ocr-task [rock_or_empty] <ocr_text>\n";
			return 2;
		}

		const std::string rock = (argc == 4) ? argv[2] : std::string{};
		const std::string text = (argc == 4) ? argv[3] : argv[2];
		double x = 0.0;
		double y = 0.0;
		double z = 0.0;
		std::string locationmarker;
		const bool ok = try_parse_xyz_from_ocr_text(text, x, y, z, locationmarker);
		std::cout << rock << '\t';
		if (!ok) {
			std::cout << "nan\tnan\tnan\n";
			return 1;
		}
		std::cout << x << '\t' << y << '\t' << z << '\n';
		return 0;
	}

	if (command == "predict-labels-onnx") {
		if (argc != 4) {
			std::cerr << "usage: scout_engine predict-labels-onnx <model_path> <sample_count>\n";
			return 2;
		}

		int64_t sample_count = 0;
		try {
			sample_count = std::stoll(argv[3]);
		}
		catch (...) {
			std::cerr << "invalid sample_count\n";
			return 2;
		}

		std::vector<float> input_values;
		input_values.reserve(static_cast<size_t>(sample_count) * 14 * 9);
		float value = 0.0f;
		while (std::cin >> value) {
			input_values.push_back(value);
		}

		std::vector<int64_t> labels;
		std::string error_message;
		if (!predict_labels_onnx(argv[2], input_values, sample_count, labels, error_message)) {
			std::cerr << error_message << '\n';
			return 1;
		}

		for (size_t i = 0; i < labels.size(); ++i) {
			if (i > 0) {
				std::cout << ' ';
			}
			std::cout << labels[i];
		}
		std::cout << '\n';
		return 0;
	}

	if (command == "print-starmap-json" || command == "print-json" || command == "parse-starmap") {
		if (argc != 3) {
			std::cerr << "usage: scout_engine print-starmap-json <json_path>\n";
			return 2;
		}

		

		nlohmann::json j;

		if (!extract_json_from_file(argv[2], j)) {
			std::cerr << "failed to extract JSON from file\n";
			return 1;
		}

		std::vector<StarmapPoi> pois;
		std::vector<DataPoint> points;
		bool retFlag;
        int retVal = ParsePoiObjects(j, pois, retFlag);
        if (retFlag)
            return retVal;
		for (const auto& p : pois) {
			if (!std::isnan(p.Latitude) && !std::isnan(p.Longitude) && !std::isnan(p.XCoord) && !std::isnan(p.YCoord) && !std::isnan(p.ZCoord)) {
				DataPoint dp;
				if (starmap_poi_to_datapoint(p, dp)) {
					points.push_back(dp);
				}
			}
		}

        // Print parsed POIs: Latitude, Longitude, Type/Subtype
		double lat = 0.0;
		double lon = 0.0;
		double alt = 0.0;
		for (const auto& p : points) {
			if (p.subtype != PoiSubType::None) {
				continue;
			}
			std::cout << "[x,y,z]: [" << p.x << ", " << p.y << ", " << p.z << "]" <<", Name: " << p.note
				<< ", Type: " << poi_type_name(p.poi_type) << ", Subtype: " << poi_subtype_name(p.subtype) << '\n';
		}

		std::vector<std::string> unique_types;
		std::map<std::string, std::vector<std::string>> unique_subtypes_by_type;
		for (const auto& p : pois) {
			if (!p.Type.empty() && std::find(unique_types.begin(), unique_types.end(), p.Type) == unique_types.end()) {
				unique_types.push_back(p.Type);
				if (p.POI_Subtype.empty())
					unique_subtypes_by_type.insert({ p.Type, {} });
				else {
					unique_subtypes_by_type.insert({ p.Type, {p.POI_Subtype} });
				}
				continue;
			}
			auto& subtypes = unique_subtypes_by_type[p.Type];
			if (!p.POI_Subtype.empty() && std::find(subtypes.begin(), subtypes.end(), p.POI_Subtype) == subtypes.end()) {
				subtypes.push_back(p.POI_Subtype);
			}
		}

		for (const auto& type : unique_types) {
			std::cout << "Type: " << type << '\n';
			const auto& subtypes = unique_subtypes_by_type[type];
			for (const auto& subtype : subtypes) {
				std::cout << "  Subtype: " << subtype << '\n';
			}
		}

		return 0;
	}
	// convert starmap POIs from JSON to CSV format for import into point store; expects same JSON structure as print-starmap-json command, but outputs CSV lines with x,y,z,planet,note,type,subtype fields for each POI with valid coordinates
	if (command == "import-starmap-json") {
		if (argc != 3) {
			std::cerr << "usage: scout_engine import-starmap-json <json_path>\n";
			return 2;
		}
		nlohmann::json j;
		if (!extract_json_from_file(argv[2], j)) {
			std::cerr << "failed to extract JSON from file\n";
			return 1;
		}

		std::vector<StarmapPoi> pois;
		bool retFlag;
        int retVal = ParsePoiObjects(j, pois, retFlag);
        if (retFlag)
            return retVal;

		//convert POI to DataPoint and append to CSV
		for (const auto& p : pois) {
			if (!std::isnan(p.Latitude) && !std::isnan(p.Longitude) && !std::isnan(p.XCoord) && !std::isnan(p.YCoord) && !std::isnan(p.ZCoord)) {
				DataPoint dp;
				if (starmap_poi_to_datapoint(p, dp)) {
					append_point_csv("starmap_pois.csv", dp);
				}
			}
		}

		return 0;
	}


	if (command == "dbimport-starmap-json") {
		if (argc != 4) {
			std::cerr << "usage: scout_engine dbimport-starmap-json <json_path> <db>\n";
			return 2;
		}
		auto db_path = std::string(argv[3]);
		auto jsonpath = std::string(argv[2]);

        bool retFlag;
        int retVal = dbimport_starmap(db_path, jsonpath, retFlag);
        if (retFlag)
            return retVal;

        return 0;
	}



	std::cerr << "unknown command: " << command << '\n';
	return 2;
}

int dbimport_starmap(std::string &db_path, std::string &jsonpath, bool &retFlag)
{
    retFlag = true;
    SqliteStore store(db_path, "");

    if (!store.init())
    {
        std::cerr << "failed to initialize connection";
        return 3;
    }

    nlohmann::json j;
    if (!extract_json_from_file(jsonpath, j))
    {
        std::cerr << "failed to extract JSON from file\n";
        return 1;
    }

    std::vector<StarmapPoi> pois;
    int retVal = ParsePoiObjects(j, pois, retFlag);
    if (retFlag)
        return retVal;

    // convert POI to DataPoint and append to CSV
    for (const auto &p : pois)
    {
        if (!std::isnan(p.XCoord) && !std::isnan(p.YCoord) && !std::isnan(p.ZCoord))
        {
            DataPoint dp;
            if (!starmap_poi_to_datapoint(p, dp))
            {
                std::cerr << "failed to convert POI to datapoint: " << p.PoiName << '\n';
                continue;
            }
            if (store.uuid_insert_or_update(dp))
            {
                std::cout << "imported POI: " << p.PoiName << " at [" << p.XCoord << ", " << p.YCoord << ", " << p.ZCoord << "]\n";
            }
            else
            {
                std::cerr << "failed to import POI: " << p.PoiName << '\n';
            }
        }
    }
    retFlag = false;
    return {};
}

int ParsePoiObjects(nlohmann::json_abi_v3_11_2::json &j, std::vector<StarmapPoi> &pois, bool &retFlag)
{
	retFlag = true;
	if (j.is_array()) {
		for (const auto& elem : j) {

			if (!elem.is_object()) continue;
			try {
				StarmapPoi p = elem.get<StarmapPoi>();
				pois.push_back(std::move(p));
			}
			catch (const std::exception& e) {
				std::cout << elem.dump() << '\n' << e.what() << '\n';
				// ignore malformed entries
			}
		}
	} else if (j.is_object()) {
		if (j.contains("Latitude") || j.contains("Longitude") || j.contains("item_id")) {
			try {
				StarmapPoi p = j.get<StarmapPoi>();
				pois.push_back(std::move(p));
			}
			catch (const std::exception&) {}
		} else {
			for (auto it = j.begin(); it != j.end(); ++it) {
				if (!it.value().is_array()) continue;
				for (const auto& elem : it.value()) {
					if (!elem.is_object()) continue;
					try {
						StarmapPoi p = elem.get<StarmapPoi>();
						pois.push_back(std::move(p));
					}
					catch (const std::exception&) {}
				}
			}
		}
	} else {
		std::cerr << "JSON is not an object or array\n";
		return 1;
	}
	retFlag = false;
	return 0;
}


bool extract_json_from_file(const std::string& file_path, nlohmann::json& j) {
	std::ifstream file(file_path, std::ios::in | std::ios::binary);
	if (!file) {
		auto full_path = std::filesystem::absolute(file_path);
		std::cerr << "failed to open file: " << full_path << '\n';
		return false;
	}

	std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	if (content.empty()) {
		return false;
	}

	// Attempt to parse with nlohmann::json; if that fails, fall back to regex extraction

	// Sanitize content: escape CR/LF that appear inside JSON string literals
	content = sanitize_json_content(content);

	try {
		j = nlohmann::json::parse(content, nullptr, /*allow_exceptions=*/true, /*ignore_comments=*/true);
		return true;
	}
	catch (const std::exception&) {
		try {
			j = nlohmann::json::parse(content);
			return true;
		}
		catch (const std::exception& e) {
			std::cerr << "failed to parse JSON: " << e.what() << '\n';
			return false;
		}
	}
}
