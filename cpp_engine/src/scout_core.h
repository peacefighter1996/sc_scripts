#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>
#include <uuid.h>
#ifdef SCOUT_HAS_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

std::string trim(const std::string& value);
std::string to_lower(std::string value);
std::vector<std::string> vector_to_lower(const std::vector<std::string>& values);
std::vector<std::string> split_csv_row(const std::string& line);
std::string csv_escape(const std::string& value);
bool try_parse_xyz_from_ocr_text(const std::string& ocr_text, double& x, double& y, double& z, std::string& locationmarker);
inline bool string_in_vector_case_insensitive(const std::vector<std::string>& vec, const std::string& query) {
	std::string lower_query = query;
	std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
	for (const auto& v : vec) {
		std::string lower_v = v;
		std::transform(lower_v.begin(), lower_v.end(), lower_v.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
		if (lower_v == lower_query) return true;
	}
	return false;
}


//
// Define the POI type list once and generate enum + name arrays from it.
#define POI_TYPE_LIST(X) \
    X(Unknown, 0)        \
    X(Location, 1)       \
    X(Cave, 2)           \
    X(Mineral, 3)        \
    X(Other, 4)          \
    X(Wreck, 5)          \
    X(Animal_Area, 6) 

enum class PoiType : int {
#define POI_ENUM(name, val) name = val,
	POI_TYPE_LIST(POI_ENUM)
#undef POI_ENUM
};

#define POI_SUBTYPE_LIST(X) \
    X(None, 0)           \
    X(Sand_Cave, 1)      \
    X(Rock_Cave, 2)      \
    X(Sink_Hole, 3)      \
    X(Acidic_Cave, 4)    \
    X(Underground_Facility, 5) \
    X(Security_Bunker, 6) \
    X(Hurston_Dynamics_Operations_Facility, 7) \
    X(Security_Outpost, 8)     \
    X(Derelict_Outpost, 9)    \
    X(Outpost, 10)           \
    X(Mining_Outpost, 11)    \
    X(Ship_Wreck, 12)         \
    X(Caterpillar_Puzzle_Wreck, 13) \
    X(Druglab, 14)          \
    X(Easteregg, 15)       \
    X(Animal_Area, 16)    \
    X(Event, 17)          \
    X(Object_Container, 18) \
    X(Orbital_Station, 19)  \
    X(Landing_Zone, 20)     \
    X(Racetrack_Community, 21) \
    X(Racetrack, 22)       \
    X(River, 23)          \
    X(Onyx_Facility, 24)   \
    X(Comm_Array, 25)     \
    X(Abandoned_Outpost, 26) \
    X(Spaceport, 27)      \
    X(Forward_Operating_Base, 28) \
    X(Scrapyard, 29)      \
    X(Jump_Point, 30)     \
    X(Derelict_Settlement, 31) \
    X(Planetary_Alignment_Facility, 32) \
    X(Prison, 33)         \
    X(RestStop, 34)       \
    X(Colonial_Outpost, 35) \
    X(Missing_Derelict_Outpost, 36) \
    X(Distribution_Center, 37) \
    X(Mission_Area, 38)   \
    X(Colonial_Bunker, 39) \
    X(Asteroid_Base, 40)  \
    X(Orbital_Laser_Platform, 41) \
    X(Ground_Activation_Platform, 42) \
    X(Asteroid_Belt, 43)  \
    X(Station, 44)        \
    X(LandingZone, 45)    \
    X(Lazarus_Transport_Hub, 46) \
    X(Satellite_Wreck, 47) \
    X(Mining_Tower, 48)


enum class PoiSubType : int {
#define POI_SUBTYPE_ENUM(name, val) name = val,
	POI_SUBTYPE_LIST(POI_SUBTYPE_ENUM)
#undef POI_SUBTYPE_ENUM
};

// alias PoiSubType to PoiSubtype
using PoiSubtype = PoiSubType;

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

constexpr size_t poi_type_count = 7;
inline constexpr std::array<const char*, poi_type_count> poi_type_names_arr = {
#define POI_STR(name, val) #name,
		POI_TYPE_LIST(POI_STR)
#undef POI_STR
};

constexpr size_t poi_subtype_count = 49;
inline constexpr std::array<const char*, poi_subtype_count> poi_subtype_names_arr = {
#define POI_SUBTYPE_STR(name, val) #name,
		POI_SUBTYPE_LIST(POI_SUBTYPE_STR)
#undef POI_SUBTYPE_STR
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

inline const char* poi_subtype_name(PoiSubType t) {
	int idx = static_cast<int>(t);
	if (idx < 0 || static_cast<size_t>(idx) >= poi_impl::poi_subtype_count) return "None";
	return poi_impl::poi_subtype_names_arr[static_cast<size_t>(idx)];
}

bool poi_subtype_from_string(const std::string& s, PoiSubType& out);

struct DataPoint {
	uuid uuid;
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
	// POI-specific subtype (enum derived from starmap POI type strings)
	PoiSubType subtype{};
	// POI-specific type (e.g., "cave", "location", "mineral")
	PoiType poi_type{};
	// Optional timestamp when the point was added or last updated, for potential future use in filtering/sorting
	// for instance when SC is updated and old mineral points are no longer valid.
	std::string time_info;
	// Whether this POI has a persistent quantum target (can QT to it from starmap)
	bool qt_persistent{};

	[[nodiscard]] std::vector<double> to_lat_lon_alt() const; // force recalculation, bypassing cache (useful if point coordinates have been modified after initial calculation)
	[[nodiscard]] std::vector<double> get_lat_lon_alt() const;

private:
	mutable std::vector<double> lat_lon_alt_cache_;
};

bool operator==(const DataPoint& a, const DataPoint& b);
bool operator!=(const DataPoint& a, const DataPoint& b);

void print_dump(const std::vector<DataPoint>& points);

inline void from_json(const nlohmann::json& j, DataPoint& p) {
	p.uuid = uuid::from_string(j.value("uuid", ""));
	p.id = j.value("id", 0);
	p.server = j.value("server", "All");
	p.x = j.value("x", 0.0);
	p.y = j.value("y", 0.0);
	p.z = j.value("z", 0.0);
	p.planet = j.value("planet", "");
	p.material = j.value("material", "");
	p.location = j.value("location", false);
	p.quality_min = j.value("quality_min", 0);
	p.quality_max = j.value("quality_max", 0);
	p.note = j.value("note", "");
	poi_subtype_from_string(j.value("subtype", ""), p.subtype);
	poi_type_from_string(j.value("poi_type", ""), p.poi_type);
	p.qt_persistent = j.value("qt_persistent", false);
	p.time_info = j.value("time_info", "");
}

inline void to_json(nlohmann::json& j, const DataPoint& p) {
	j = nlohmann::json{
		{"id", p.id},
		{"uuid", p.uuid.to_string()},
		{"server", p.server},
		{"x", p.x},
		{"y", p.y},
		{"z", p.z},
		{"planet", p.planet},
		{"material", p.material},
		{"location", p.location},
		{"quality_min", p.quality_min},
		{"quality_max", p.quality_max},
		{"note", p.note},
		{"subtype", poi_subtype_name(p.subtype)},
		{"poi_type", poi_type_name(p.poi_type)},
		{"qt_persistent", p.qt_persistent},
		{"time_info", p.time_info}
	};
}

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

struct Resource {
	int id{};
	std::string name;
	std::string short_name;
	ResourceType type{};
	HarvestType harvest_type{};
};

struct bbox2d {
	double min_x;
	double max_x;
	double min_y;
	double max_y;
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

	ZoneType zone_type{ ZoneType::CelestialBody };  // e.g. CelestialBody (planets/moons), AsteroidField, Solar
	bool quantumable{ true }; // whether this zone can be targeted by a quantum beacon

	// Optional center coordinate for zones that have a stable location (e.g., solar system)
	double center_x{ 0.0 };
	double center_y{ 0.0 };
	double center_z{ 0.0 };

	// Projection/grid settings are global (stored in AppSettings) rather than per-zone.
	// Bounding box for planar zones (asteroid fields) in kilometers, aligned to grid spacing.
	// Stored as integer multiples of kilometers for simplicity.
	//int min_x_km{ -300 };
	//int max_x_km{ 300 };
	//int min_y_km{ -300 };
	//int max_y_km{ 300 };
	bbox2d bounding_box_km{ -300.0,  300.0, -300.0, 300.0 };


	// Whether this zone has an associated asteroid belt (useful to enable asteroid-field displays)
	bool has_asteroid_belt{ false };
	// Planet physical radius in kilometers (optional, used for rendering disks/occlusion in asteroid-field view)
	double planet_radius_km{ 0.0 };
	// Last display mode selected for this zone (store DisplayMode as int). -1 = unset
	int last_display_mode{ -1 };
};

// Keep `Planet` as an alias for backwards compatibility with existing code.
using Planet = Zone;


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
