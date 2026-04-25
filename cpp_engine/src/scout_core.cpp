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

#ifdef SCOUT_HAS_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

std::vector<double> DataPoint::to_lat_lon_alt() const {
    const double r = std::sqrt((x * x) + (y * y) + (z * z));
    if (r == 0.0) {
        return {0.0, 0.0, 0.0};
    }

    const double lat = std::asin(z / r) * 180.0 / 3.14159265358979323846;
    const double lon = std::atan2(y, x) * 180.0 / 3.14159265358979323846;
    return {lat, lon, r};
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

std::vector<DataPoint> load_points(const std::string& csv_path) {
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

bool append_point(const std::string& csv_path, const DataPoint& point) {
    const auto file_exists = std::filesystem::exists(csv_path);
    const auto parent = std::filesystem::path(csv_path).parent_path();
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

void print_dump(const std::vector<DataPoint>& points) {
    for (const auto& point : points) {
        std::cout << point.id << '\t'
                  << point.server << '\t'
                  << std::setprecision(15) << point.x << '\t'
                  << std::setprecision(15) << point.y << '\t'
                  << std::setprecision(15) << point.z << '\t'
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

        Ort::AllocatorWithDefaultOptions allocator;
        auto input_name = it->second->GetInputNameAllocated(0, allocator);
        auto output_name = it->second->GetOutputNameAllocated(0, allocator);

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
        auto outputs = it->second->Run(Ort::RunOptions{nullptr}, input_names, &input_tensor, 1, output_names, 1);
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
#endif
}
