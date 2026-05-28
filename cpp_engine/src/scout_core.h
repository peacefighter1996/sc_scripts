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

#define ZONE_TYPE_LIST(X) \
    X(CelestialBody, 0)    \
    X(AsteroidField, 1)    \
    X(Solar, 2)

enum class ZoneType : int {
    #define ZONE_ENUM(name, val) name = val,
        ZONE_TYPE_LIST(ZONE_ENUM)
    #undef ZONE_ENUM
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
    
    [[nodiscard]] std::vector<double> to_lat_lon_alt() const; // force recalculation, bypassing cache (useful if point coordinates have been modified after initial calculation)
    [[nodiscard]] std::vector<double> get_lat_lon_alt() const;
    
    private:
    mutable std::vector<double> lat_lon_alt_cache_;
};

enum class ResourceType {
    None = 0,
    Mineral = 1,
    Plant = 2
};

// flag enum for harvest types, allowing bitwise combination of multiple types if needed in the future (e.g., a resource that can be harvested both by FPS and Vehicle methods)
enum class HarvestType {
    None = 0,
    FPS = 1 << 0,
    Vehicle = 1 << 1,
    Ship = 1 << 2
};

struct Resource{
    int id{};
    std::string name;
    std::string short_name;
    ResourceType type{};
    HarvestType harvest_type{};
};
// Historical planets table is being generalized to "zones".
// Zones can represent planets, asteroid fields, solar/system locations, etc.
struct Zone {
    int id{};
    std::string system;
    std::string name;       // human-friendly name of the zone (planet name or field name)
    std::string image_dir;  // optional image directory or key
    std::string zone_id;    // external id string

    // Zone metadata
    // Zone type: use X-macro to define canonical types


    

    ZoneType zone_type{ZoneType::CelestialBody};  // e.g. CelestialBody (planets/moons), AsteroidField, Solar
    bool quantumable{true}; // whether this zone can be targeted by a quantum beacon

    // Optional center coordinate for zones that have a stable location (e.g., solar system)
    double center_x{0.0};
    double center_y{0.0};
    double center_z{0.0};

    // Projection/grid settings are global (stored in AppSettings) rather than per-zone.
    // Bounding box for planar zones (asteroid fields) in kilometers, aligned to grid spacing.
    // Stored as integer multiples of kilometers for simplicity.
    int min_x_km{ -300 };
    int max_x_km{ 300 };
    int min_y_km{ -300 };
    int max_y_km{ 300 };
};

// Keep `Planet` as an alias for backwards compatibility with existing code.
using Planet = Zone;

std::string trim(const std::string& value);
std::string to_lower(std::string value);
std::vector<std::string> split_csv_row(const std::string& line);
std::string csv_escape(const std::string& value);
void print_dump(const std::vector<DataPoint>& points);
bool try_parse_xyz_from_ocr_text(const std::string& ocr_text, double& x, double& y, double& z, std::string& locationmarker);

// Zone type helpers
inline const char* zone_type_name(ZoneType t) {
    switch (t) {
    case ZoneType::CelestialBody: return "celestial";
    case ZoneType::AsteroidField: return "asteroid";
    case ZoneType::Solar: return "solar";
    default: return "celestial";
    }
}

inline bool zone_type_from_string(const std::string& s, ZoneType& out) {
    const std::string v = to_lower(s);
    if (v == "celestial" || v == "planet" || v == "planetary" || v == "celestialbody" || v == "celestial_body") { out = ZoneType::CelestialBody; return true; }
    if (v == "asteroid" || v == "asteroidfield" || v == "asteroid_field") { out = ZoneType::AsteroidField; return true; }
    if (v == "solar" || v == "system" || v == "solar_system") { out = ZoneType::Solar; return true; }
    return false;
}

inline int zone_type_to_int(ZoneType t) {
    return static_cast<int>(t);
}

inline bool zone_type_from_int(int v, ZoneType& out) {
    switch (v) {
    case static_cast<int>(ZoneType::CelestialBody): out = ZoneType::CelestialBody; return true;
    case static_cast<int>(ZoneType::AsteroidField): out = ZoneType::AsteroidField; return true;
    case static_cast<int>(ZoneType::Solar): out = ZoneType::Solar; return true;
    default: return false;
    }
}

bool predict_labels_onnx(const std::string& model_path, const std::vector<float>& input_values, int64_t sample_count, std::vector<int64_t>& labels, std::string& error_message);
#ifdef SCOUT_HAS_ONNXRUNTIME
bool predict_labels_onnx_session(Ort::Session& session, const std::vector<float>& input_values, int64_t sample_count, std::vector<int64_t>& labels, std::string& error_message);
Ort::Session* create_onnx_session(const std::string& model_path, std::string& error_message);
#endif
