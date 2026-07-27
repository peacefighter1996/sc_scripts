#include "scout_core.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <regex>

const LatLonAlt DataPoint::get_lat_lon_alt() const {
    if (lat_lon_alt_cache_.altitude != 0.0) {
        return lat_lon_alt_cache_;
    }
	return to_lat_lon_alt();
}

const LatLonAlt DataPoint::to_lat_lon_alt() const {
    const double r = std::sqrt((coord.x * coord.x) + (coord.y * coord.y) + (coord.z * coord.z));
    if (r == 0.0) {
        return origin_latlonalt;
    }

    const double lat = std::asin(coord.z / r) * rad_to_deg;
    const double lon = std::atan2(coord.y, coord.x) * rad_to_deg;

    lat_lon_alt_cache_ = {lat, lon, r};

    return lat_lon_alt_cache_;
}

std::string trim(const std::string& value) {
    const auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) { return std::isspace(ch) != 0; });
    const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) { return std::isspace(ch) != 0; }).base();
    if (begin >= end) {
        return {};
    }
    return std::string(begin, end);
}

std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}
std::vector<std::string> vector_to_lower(const std::vector<std::string>& values) {
    std::vector<std::string> lower;
    lower.reserve(values.size());
    for (const auto& v : values) {
        lower.push_back(to_lower(v));
    }
    return lower;
}
std::vector<std::string> split_csv_row(const std::string& line) {
    std::vector<std::string> values;
    std::string current;
    bool in_quotes = false;

    for (size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (ch == '"') {
            if (in_quotes && i + 1 < line.size() && line[i + 1] == '"') {
                current.push_back('"');
                ++i;
            } else {
                in_quotes = !in_quotes;
            }
        } else if (ch == ',' && !in_quotes) {
            values.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }

    values.push_back(current);
    return values;
}

std::string csv_escape(const std::string& value) {
    const bool needs_quotes = value.find(',') != std::string::npos || value.find('"') != std::string::npos || value.find('\n') != std::string::npos || value.find('\r') != std::string::npos;
    if (!needs_quotes) {
        return value;
    }

    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (const char ch : value) {
        if (ch == '"') {
            escaped.push_back('"');
        }
        escaped.push_back(ch);
    }
    escaped.push_back('"');
    return escaped;
}
// (point_store_csv.cpp) to centralize CSV access. Definitions removed here.

void print_dump(const std::vector<DataPoint>& points) {
    for (const auto& point : points) {
        std::cout << point.id << '\t'
                  << point.server << '\t'
                  << std::setprecision(15) << point.coord.x << '\t'
                  << std::setprecision(15) << point.coord.y << '\t'
                  << std::setprecision(15) << point.coord.z << '\t'
                  << point.planet << '\t'
                  << point.material << '\t'
                  << poi_type_name(point.poi_type) << '\t'
                  << point.time_info << '\t'
                  << std::setprecision(15) << point.quality_min << '\t'
                  << std::setprecision(15) << point.quality_max << '\t'
                  << point.note
                  << '\n';
    }
}

static std::vector<std::string> split_whitespace(const std::string& text) {
    std::vector<std::string> parts;
    std::istringstream stream(text);
    std::string token;
    while (stream >> token) {
        parts.push_back(token);
    }
    return parts;
}

bool try_parse_xyz_from_ocr_text(const std::string& ocr_text, double& x, double& y, double& z, std::string& locationmarker) {
    const auto parts = split_whitespace(ocr_text);
    if (parts.size() < 3) {
        return false;
    }

    std::vector<std::string> coordinates = {
        parts[parts.size() - 3],
        parts[parts.size() - 2],
        parts[parts.size() - 1],
    };

    std::vector<double> parsed(3, 0.0);
    for (size_t i = 0; i < coordinates.size(); ++i) {
        auto value = coordinates[i];
		// regex expression check if value matches something like 123.45km or 123.45m (case insensitive)
		if (!std::regex_match(value, std::regex(R"(-?\d+(\.\d+)[kmKM]{1,2})"))) {
            return false;
        }


        value.erase(std::remove(value.begin(), value.end(), 'm'), value.end());
        if (value.empty()) {
            return false;
        }

        try {
            const bool is_km = value.find('k') != std::string::npos;
            value.erase(std::remove(value.begin(), value.end(), 'k'), value.end());
            const double parsed_value = std::stod(value);
            parsed[i] = is_km ? parsed_value : (parsed_value / 1000.0);
        } catch (...) {
            return false;
        }
    }

    x = parsed[0];
    y = parsed[1];
    z = parsed[2];

    if (parts.size() >= 5) {
        locationmarker = parts[parts.size() - 5];
    } else {
        locationmarker.clear();
    }
    return true;
}

bool poi_subtype_from_string(const std::string& s, PoiSubType& out){
    for (size_t i = 0; i < poi_impl::poi_subtype_count; ++i) {
        if (s == poi_impl::poi_subtype_names_arr[i]) {
            out = static_cast<PoiSubType>(static_cast<int>(i));
            return true;
        }
    }

    auto normalized = to_lower(s);
    if (normalized == "none") {
        out = PoiSubType::None;
        return true;
    }

    if (string_in_vector_case_insensitive({ "Sand Cave", "Sand_Cave", "Cave (Sand)", "Cave_Sand" }, normalized)) {  out = PoiSubType::Sand_Cave; return true; }
    if (string_in_vector_case_insensitive({ "Rock Cave", "Rock_Cave", "Cave (Rock)", "Cave_Rock" }, normalized)) {  out = PoiSubType::Rock_Cave; return true; }
    if (string_in_vector_case_insensitive({ "Sink Hole", "Sink_Hole", "Cave (Sink)", "Cave_Sink" }, normalized)) {  out = PoiSubType::Sink_Hole; return true; }
    if (string_in_vector_case_insensitive({ "Underground Facility", "Underground_Facility" }, normalized)) { out = PoiSubType::Underground_Facility; return true; }
    if (string_in_vector_case_insensitive({ "Security Bunker", "Security_Bunker" }, normalized)) { out = PoiSubType::Security_Bunker; return true; }
    if (string_in_vector_case_insensitive({ "Hurston Dynamics Operations Facility", "Hurston_Dynamics_Operations_Facility" }, normalized)) { out = PoiSubType::Hurston_Dynamics_Operations_Facility; return true; }
    if (string_in_vector_case_insensitive({ "Security Outpost", "Security_Outpost" }, normalized)) { out = PoiSubType::Security_Outpost; return true; }
    if (string_in_vector_case_insensitive({ "Derelict Outpost", "Derelict_Outpost" }, normalized)) { out = PoiSubType::Derelict_Outpost; return true; }
    if (string_in_vector_case_insensitive({ "Outpost" }, normalized)) { out = PoiSubType::Outpost; return true; }
    if (string_in_vector_case_insensitive({ "Mining Outpost", "Mining_Outpost" }, normalized)) { out = PoiSubType::Mining_Outpost; return true; }
    if (string_in_vector_case_insensitive({ "Ship Wreck", "Ship_Wreck" }, normalized)) { out = PoiSubType::Ship_Wreck; return true; }
    if (string_in_vector_case_insensitive({ "Caterpillar Puzzle Wreck", "Caterpillar_Puzzle_Wreck" }, normalized)) { out = PoiSubType::Caterpillar_Puzzle_Wreck; return true; }
    if (normalized == "druglab") { out = PoiSubType::Druglab; return true; }
    if (normalized == "easteregg") { out = PoiSubType::Easteregg; return true; }
    if (string_in_vector_case_insensitive({ "Animal Area", "Animal_Area" }, normalized)) { out = PoiSubType::Animal_Area; return true; }
    if (normalized == "event") { out = PoiSubType::Event; return true; }
    if (string_in_vector_case_insensitive({ "Object Container", "Object_Container" }, normalized)) { out = PoiSubType::Object_Container; return true; }
    if (string_in_vector_case_insensitive({ "Orbital Station", "Orbital_Station" }, normalized)) { out = PoiSubType::Orbital_Station; return true; }
    if (string_in_vector_case_insensitive({ "Landing Zone", "Landing_Zone" }, normalized)) { out = PoiSubType::Landing_Zone; return true; }
    if (string_in_vector_case_insensitive({ "Racetrack(Community)", "Racetrack_Community" }, normalized)) { out = PoiSubType::Racetrack_Community; return true; }
    if (normalized == "racetrack") { out = PoiSubType::Racetrack; return true; }
    if (normalized == "river") { out = PoiSubType::River; return true; }
    if (string_in_vector_case_insensitive({ "Onyx Facility", "Onyx_Facility" }, normalized)) { out = PoiSubType::Onyx_Facility; return true; }
    if (string_in_vector_case_insensitive({ "Comm Array", "Comm_Array" }, normalized)) { out = PoiSubType::Comm_Array; return true; }
    if (string_in_vector_case_insensitive({ "Abandoned Outpost", "Abandoned_Outpost" }, normalized)) { out = PoiSubType::Abandoned_Outpost; return true; }
    if (normalized == "spaceport") { out = PoiSubType::Spaceport; return true; }
    if (string_in_vector_case_insensitive({ "Forward Operating Base", "Forward_Operating_Base" }, normalized)) { out = PoiSubType::Forward_Operating_Base; return true; }
    if (string_in_vector_case_insensitive({ "Scrapyard" }, normalized)) { out = PoiSubType::Scrapyard; return true; }
    if (string_in_vector_case_insensitive({ "Jump Point", "Jump_Point" }, normalized)) { out = PoiSubType::Jump_Point; return true; }
    if (string_in_vector_case_insensitive({ "Derelict Settlement", "Derelict_Settlement" }, normalized)) { out = PoiSubType::Derelict_Settlement; return true; }
    if (string_in_vector_case_insensitive({ "Planetary Alignment Facility", "Planetary_Alignment_Facility" }, normalized)) { out = PoiSubType::Planetary_Alignment_Facility; return true; }
    if (string_in_vector_case_insensitive({ "Prison" }, normalized)) { out = PoiSubType::Prison; return true; }
    if (string_in_vector_case_insensitive({ "RestStop", "Rest_Stop" }, normalized)) { out = PoiSubType::RestStop; return true; }
    if (string_in_vector_case_insensitive({ "Colonial Outpost", "Colonial_Outpost" }, normalized)) { out = PoiSubType::Colonial_Outpost; return true; }
    if (string_in_vector_case_insensitive({ "Missing Derelict Outpost", "Missing_Derelict_Outpost" }, normalized)) { out = PoiSubType::Missing_Derelict_Outpost; return true; }
    if (string_in_vector_case_insensitive({ "Distribution Center", "Distribution_Center" }, normalized)) { out = PoiSubType::Distribution_Center; return true; }
    if (string_in_vector_case_insensitive({ "Mission Area", "Mission_Area" }, normalized)) { out = PoiSubType::Mission_Area; return true; }
    if (string_in_vector_case_insensitive({ "Colonial Bunker", "Colonial_Bunker" }, normalized)) { out = PoiSubType::Colonial_Bunker; return true; }
    if (string_in_vector_case_insensitive({ "Asteroid Base", "Asteroid_Base" }, normalized)) { out = PoiSubType::Asteroid_Base; return true; }
    if (string_in_vector_case_insensitive({ "Orbital Laser Platform", "Orbital_Laser_Platform" }, normalized)) { out = PoiSubType::Orbital_Laser_Platform; return true; }
    if (string_in_vector_case_insensitive({ "Ground Activation Platform", "Ground_Activation_Platform" }, normalized)) { out = PoiSubType::Ground_Activation_Platform; return true; }
    if (string_in_vector_case_insensitive({ "Asteroid Belt", "Asteroid_Belt" }, normalized)) { out = PoiSubType::Asteroid_Belt; return true; }
    if (s == to_lower("Spaceport")) { out = PoiSubType::Station; return true; }
    if (string_in_vector_case_insensitive({ "LandingZone", "Landing_Zone" }, normalized)) { out = PoiSubType::LandingZone; return true; }
    if (string_in_vector_case_insensitive({ "Lazarus Transport Hub", "Lazarus_Transport_Hub" }, normalized)) { out = PoiSubType::Lazarus_Transport_Hub; return true; }
    if (string_in_vector_case_insensitive({ "Satellite Wreck", "Satellite_Wreck" }, normalized)) { out = PoiSubType::Satellite_Wreck; return true; }
    if (string_in_vector_case_insensitive({ "Mining Tower", "Mining_Tower" }, normalized)) { out = PoiSubType::Mining_Tower; return true; }

    return false;
}


bool operator==(const DataPoint& a, const DataPoint& b) {
    return 
       //  a.id == b.id && // database id and trackking but is not a determaning factor if its equivilent
           a.server == b.server &&
           a.coord.x == b.coord.x &&
           a.coord.y == b.coord.y &&
           a.coord.z == b.coord.z &&
           a.planet == b.planet &&
           a.material == b.material &&
           a.poi_type == b.poi_type &&
           a.time_info == b.time_info &&
           a.quality_min == b.quality_min &&
           a.quality_max == b.quality_max &&
           a.note == b.note &&
           a.subtype == b.subtype &&
           a.qt_persistent == b.qt_persistent;
}

bool operator!=(const DataPoint& a, const DataPoint& b) {
    return !(a == b);
}


bool predict_labels_onnx(const std::string& model_path, const std::vector<float>& input_values, int64_t sample_count, std::vector<int64_t>& labels, std::string& error_message) {
#ifndef SCOUT_HAS_ONNXRUNTIME
    (void)model_path;
    (void)input_values;
    (void)sample_count;
    (void)labels;
    error_message = "ONNX Runtime support is not enabled";
    return false;
#else
    if (sample_count <= 0) {
        error_message = "sample_count must be > 0";
        return false;
    }

    constexpr size_t values_per_sample = 14 * 9;
    const size_t expected_values = static_cast<size_t>(sample_count) * values_per_sample;
    if (input_values.size() != expected_values) {
        error_message = "input size does not match expected tensor shape";
        return false;
    }

    try {
        static std::mutex session_mutex;
        static std::unordered_map<std::string, std::unique_ptr<Ort::Session>> sessions;
        static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "scout_engine");

        std::lock_guard<std::mutex> lock(session_mutex);
        auto it = sessions.find(model_path);
        if (it == sessions.end()) {
            Ort::SessionOptions session_options;
            session_options.SetIntraOpNumThreads(1);
            session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
#if defined(_WIN32)
            const std::wstring wide_model_path(model_path.begin(), model_path.end());
            auto session = std::make_unique<Ort::Session>(env, wide_model_path.c_str(), session_options);
#else
            auto session = std::make_unique<Ort::Session>(env, model_path.c_str(), session_options);
#endif
            it = sessions.emplace(model_path, std::move(session)).first;
        }

        // Delegate to session-based variant to allow reuse of loaded sessions
        return predict_labels_onnx_session(*it->second, input_values, sample_count, labels, error_message);
    } catch (const std::exception& ex) {
        error_message = ex.what();
        return false;
    }
#endif
}

#ifdef SCOUT_HAS_ONNXRUNTIME
bool predict_labels_onnx_session(Ort::Session& session, const std::vector<float>& input_values, int64_t sample_count, std::vector<int64_t>& labels, std::string& error_message) {
    if (sample_count <= 0) {
        error_message = "sample_count must be > 0";
        return false;
    }

    constexpr size_t values_per_sample = 14 * 9;
    const size_t expected_values = static_cast<size_t>(sample_count) * values_per_sample;
    if (input_values.size() != expected_values) {
        error_message = "input size does not match expected tensor shape";
        return false;
    }

    try {
        Ort::AllocatorWithDefaultOptions allocator;
        auto input_name = session.GetInputNameAllocated(0, allocator);
        auto output_name = session.GetOutputNameAllocated(0, allocator);

        std::vector<int64_t> input_shape = {sample_count, 14, 9, 1};
        Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::vector<float> local_input = input_values;
        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory_info,
            local_input.data(),
            local_input.size(),
            input_shape.data(),
            input_shape.size());

        const char* input_names[] = {input_name.get()};
        const char* output_names[] = {output_name.get()};
        auto outputs = session.Run(Ort::RunOptions{nullptr}, input_names, &input_tensor, 1, output_names, 1);
        if (outputs.empty() || !outputs[0].IsTensor()) {
            error_message = "model output is not a tensor";
            return false;
        }

        const auto shape_info = outputs[0].GetTensorTypeAndShapeInfo();
        const auto output_shape = shape_info.GetShape();
        if (output_shape.size() < 2 || output_shape[0] != sample_count || output_shape[1] <= 0) {
            error_message = "unexpected output tensor shape";
            return false;
        }

        const int64_t class_count = output_shape[1];
        const float* output_data = outputs[0].GetTensorData<float>();
        labels.assign(static_cast<size_t>(sample_count), 0);
        for (int64_t i = 0; i < sample_count; ++i) {
            const float* row = output_data + (i * class_count);
            int64_t best_index = 0;
            float best_value = row[0];
            for (int64_t c = 1; c < class_count; ++c) {
                if (row[c] > best_value) {
                    best_value = row[c];
                    best_index = c;
                }
            }
            labels[static_cast<size_t>(i)] = best_index;
        }
        return true;
    } catch (const std::exception& ex) {
        error_message = ex.what();
        return false;
    }
}

Ort::Session* create_onnx_session(const std::string& model_path, std::string& error_message) {
    try {
        static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "scout_engine");
        Ort::SessionOptions session_options;
        session_options.SetIntraOpNumThreads(1);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
#if defined(_WIN32)
        const std::wstring wide_model_path(model_path.begin(), model_path.end());
        return new Ort::Session(env, wide_model_path.c_str(), session_options);
#else
        return new Ort::Session(env, model_path.c_str(), session_options);
#endif
    } catch (const std::exception& ex) {
        error_message = ex.what();
        std::cerr << "Failed to create ONNX session: " << error_message <<
            "\nMake sure the model path is correct and ONNX Runtime is properly set up.\n";
        return nullptr;
    }
}
#endif
