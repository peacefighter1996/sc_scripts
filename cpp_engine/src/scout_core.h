#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>

#ifdef SCOUT_HAS_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif
//
// Define the POI type list once and generate enum + name arrays from it.
#define POI_TYPE_LIST(X) \
    X(Unknown, 0)        \
    X(Location, 1)       \
    X(Cave, 2)           \
    X(Mineral, 3)        \
    X(Other, 4)

enum class PoiType : int {
#define POI_ENUM(name, val) name = val,
    POI_TYPE_LIST(POI_ENUM)
#undef POI_ENUM
};

namespace poi_impl {
    constexpr size_t poi_type_count = 5;
    inline constexpr std::array<const char*, poi_type_count> poi_type_names_arr = {
#define POI_STR(name, val) #name,
        POI_TYPE_LIST(POI_STR)
#undef POI_STR
    };
}

inline std::vector<std::string> poi_type_names() {
    return std::vector<std::string>(std::begin(poi_impl::poi_type_names_arr), std::end(poi_impl::poi_type_names_arr));
}

inline const char* poi_type_name(PoiType t) {
    int idx = static_cast<int>(t);
    if (idx < 0 || static_cast<size_t>(idx) >= poi_impl::poi_type_count) return "Unknown";
    return poi_impl::poi_type_names_arr[static_cast<size_t>(idx)];
}

inline bool poi_type_from_string(const std::string& s, PoiType& out) {
    for (size_t i = 0; i < poi_impl::poi_type_count; ++i) {
        if (s == poi_impl::poi_type_names_arr[i]) {
            out = static_cast<PoiType>(static_cast<int>(i));
            return true;
        }
    }
    return false;
}

struct DataPoint {
    int id{};
    std::string server;
    double x{};
    double y{};
    double z{};
    std::string planet;
    std::string material;
    bool location{};
    int quality_min{};
    int quality_max{};
    std::string note;
    // POI-specific type (e.g., "cave", "location", "mineral")
    PoiType poi_type{};
    // Optional timestamp when the point was added or last updated, for potential future use in filtering/sorting
    // for instance when SC is updated and old mineral points are no longer valid.
    std::string time_info;

    [[nodiscard]] std::vector<double> to_lat_lon_alt() const;
};

struct Material{
    int id{};
    std::string name;
    std::string short_name;
};
//id,system,planet,image_dir,zone_id
struct Planet{
    int id;
    std::string system;
    std::string name;
    std::string image_dir;
    std::string zone_id;
};

std::string trim(const std::string& value);
std::string to_lower(std::string value);
std::vector<std::string> split_csv_row(const std::string& line);
std::string csv_escape(const std::string& value);
std::vector<DataPoint> load_points(const std::string& csv_path);
bool append_point(const std::string& csv_path, const DataPoint& point);
void print_dump(const std::vector<DataPoint>& points);
bool try_parse_xyz_from_ocr_text(const std::string& ocr_text, double& x, double& y, double& z, std::string& locationmarker);

bool predict_labels_onnx(const std::string& model_path, const std::vector<float>& input_values, int64_t sample_count, std::vector<int64_t>& labels, std::string& error_message);
#ifdef SCOUT_HAS_ONNXRUNTIME
bool predict_labels_onnx_session(Ort::Session& session, const std::vector<float>& input_values, int64_t sample_count, std::vector<int64_t>& labels, std::string& error_message);
Ort::Session* create_onnx_session(const std::string& model_path, std::string& error_message);
#endif
