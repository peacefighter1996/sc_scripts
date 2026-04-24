#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct DataPoint {
    int id{};
    std::string server;
    double x{};
    double y{};
    double z{};
    std::string planet;
    std::string material;
    bool location{};
    double quality_min{};
    double quality_max{};
    std::string note;

    [[nodiscard]] std::vector<double> to_lat_lon_alt() const;
};

std::string trim(const std::string& value);
std::string to_lower(std::string value);
std::vector<std::string> split_csv_row(const std::string& line);
std::string csv_escape(const std::string& value);
std::vector<DataPoint> load_points(const std::string& csv_path);
bool append_point(const std::string& csv_path, const DataPoint& point);
void print_dump(const std::vector<DataPoint>& points);
bool try_parse_xyz_from_ocr_text(const std::string& ocr_text, double& x, double& y, double& z);

bool predict_labels_onnx(const std::string& model_path, const std::vector<float>& input_values, int64_t sample_count, std::vector<int64_t>& labels, std::string& error_message);
