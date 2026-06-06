#include "scout_app.h"

#include "scout_ocr.h"
#include "scout_core.h"
#include "scout_render.h"
#include "scout_engine.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <thread>
#include <chrono>
#include <mutex>
#include <optional>
#include <filesystem>
#include <memory>
#include <cstring>
#include <cctype>
#include <cpr/cpr.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <windows.h>
#include <gdiplus.h>
#include <commdlg.h>
#endif

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

// Simple RAII for GDI+ initialization on Windows
#ifdef _WIN32
struct GdiplusSession {
	ULONG_PTR token{ 0 };
	GdiplusSession() {
		Gdiplus::GdiplusStartupInput input;
		Gdiplus::GdiplusStartup(&token, &input, nullptr);
	}
	~GdiplusSession() {
		if (token) Gdiplus::GdiplusShutdown(token);
	}
};
#else
struct GdiplusSession {};
#endif

std::filesystem::path detect_repo_root() {
	std::vector<std::filesystem::path> candidates;
#ifdef _WIN32
	wchar_t exe_path[MAX_PATH];
	const auto len = GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
	if (len > 0) {
		candidates.push_back(std::filesystem::path(exe_path).parent_path());
	}
#endif
	candidates.push_back(std::filesystem::current_path());

	for (auto candidate : candidates) {
		for (int depth = 0; depth < 6; ++depth) {
			if (std::filesystem::exists(candidate / "data" / "label_map.json")) {
				return candidate;
			}
			if (!candidate.has_parent_path()) {
				break;
			}
			candidate = candidate.parent_path();
		}
	}

	return std::filesystem::current_path();
}

std::wstring to_wstring(const std::filesystem::path& path) {
	return path.wstring();
}

#ifdef _WIN32
static bool win32_save_file_dialog(std::wstring& out_path) {
	wchar_t szFile[MAX_PATH] = L"";
	OPENFILENAMEW ofn{};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = nullptr;
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrFilter = L"JSON Files\0*.json\0All Files\0*.*\0";
	ofn.nFilterIndex = 1;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
	if (GetSaveFileNameW(&ofn) == TRUE) {
		out_path = szFile;
		return true;
	}
	return false;
}
#endif

#include "point_store.h"
#include "point_store_sqlite.h"
#include "point_store_csv.h"
#include "sync_service.h"
#include "timer_footer_renderer.h"

#include "timer_display.h"

#include "travel_log.h"

#include "camera.h"

#include <unordered_map>
#include <unordered_set>

// Fallback defaults (original defaults may live elsewhere; provide minimal fallbacks to compile)
static const std::vector<std::string> kDefaultPlanets = { "Default" };
static const std::vector<std::string> kDefaultMaterials = { "All" };
static const std::vector<std::string> kDefaultServerIds = { "All" };
static const std::unordered_map<std::string, std::string> kMaterialIds = {};
static const int kMinQuality = 0;
static const int kMaxQuality = 100;

// Simple time helpers used for point timestamps
static std::chrono::system_clock::time_point now_ymdhm() {
	return std::chrono::system_clock::now();
}

static std::string format_iso_datetime(const std::chrono::system_clock::time_point& tp) {
	std::time_t t = std::chrono::system_clock::to_time_t(tp);
	std::tm tm;
#ifdef _WIN32
	gmtime_s(&tm, &t);
#else
	gmtime_r(&t, &tm);
#endif
	char buf[64];
	std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
	return std::string(buf);
}

static std::string format_iso_date(const std::chrono::system_clock::time_point& tp) {
	std::time_t t = std::chrono::system_clock::to_time_t(tp);
	std::tm tm;
#ifdef _WIN32
	gmtime_s(&tm, &t);
#else
	gmtime_r(&t, &tm);
#endif
	char buf[64];
	std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
	return std::string(buf);
}

static std::string format_iso_filesystem_date(const std::chrono::system_clock::time_point& tp) {
	std::time_t t = std::chrono::system_clock::to_time_t(tp);
	std::tm tm;
#ifdef _WIN32
	gmtime_s(&tm, &t);
#else
	gmtime_r(&t, &tm);
#endif
	char buf[64];
	std::strftime(buf, sizeof(buf), "%Y%m%d%H%M%SZ", &tm);
	return std::string(buf);
}


// Frame timing (seconds per frame)
static constexpr double kFrameTime = 1.0 / 60.0;



GLuint load_texture_file(const std::filesystem::path& path) {
	if (!std::filesystem::exists(path)) {
		return 0;
	}

	Gdiplus::Bitmap bitmap(to_wstring(path).c_str());
	if (bitmap.GetLastStatus() != Gdiplus::Ok) {
		return 0;
	}

	const int width = static_cast<int>(bitmap.GetWidth());
	const int height = static_cast<int>(bitmap.GetHeight());
	if (width <= 0 || height <= 0) {
		return 0;
	}

	Gdiplus::Rect rect(0, 0, width, height);
	Gdiplus::BitmapData data{};
	if (bitmap.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok) {
		return 0;
	}

	std::vector<std::uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
	for (int y = 0; y < height; ++y) {
		const auto* src = reinterpret_cast<const std::uint8_t*>(static_cast<const std::uint8_t*>(data.Scan0) + (static_cast<size_t>(y) * static_cast<size_t>(data.Stride)));
		std::memcpy(pixels.data() + (static_cast<size_t>(y) * static_cast<size_t>(width) * 4), src, static_cast<size_t>(width) * 4);
	}
	bitmap.UnlockBits(&data);

	GLuint texture = 0;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, 0x80E1, GL_UNSIGNED_BYTE, pixels.data());
	return texture;
}

std::pair<float, float> latlon_to_uv(double lat, double lon) {
	const auto u = static_cast<float>((lon + 180.0) / 360.0);
	const auto v = static_cast<float>((lat + 90.0) / 180.0);
	return { u, v };
}

struct AppSettings {
	bool show_timings{ false };
	bool auto_update_ocr_newpoint_enabled{ true };
	bool ocr_feed_planet_update_enabled{ true };
	bool auto_export_session_minerals{ true };

	// Last used export file path (persisted to settings.ini)
	std::string last_export_path;
	std::string session_export_dir = "./data/exports/"; // directory to export session minerals (relative to repo root or absolute path)

	// Sync/storage settings
	bool sync_enabled{ false };
	std::string sync_server_url;
	std::string sync_node_id;
	std::string storage_db_path; // relative to repo_root or absolute path to geoscout.db
	int sync_max_outbox_size{ 10000 };
	double grid_spacing_km{ 100.0 };
	double planet_grid_spacing_degrees{ 22.5 }; // for celestial bodies, grid spacing in degrees (e.g., 22.5° = 16x16 grid); for asteroid fields, grid spacing is in kilometers.
	// Tracking / Travel Log settings
	// Default: do not auto-start the travel log - user must enable it manually
	bool tracking_enabled_on_start{ false };
	double tracking_distance_threshold_km{ 5.0 };
	double tracking_max_speed_mps{ 1500.0 };
	double tracking_max_accel_mps2{ 30.0 * 9.80665 };
	double kalman_max_life_s{ 10.0 };
	double qt_threshold{ 100000.0 };
	double qt_disable_duration_s{ 3.0 };
	double tracking_min_core_distance_km{ 100.0 };
};



struct AppState {
	AppSettings settings;

	std::chrono::system_clock::time_point app_start_time;

	std::filesystem::path repo_root;
	std::filesystem::path onnx_model_path;
	std::filesystem::path label_map_path;
	std::filesystem::path planets_dir;

	DisplayMode display_mode;

	std::vector<DataPoint> points;
	// Points used specifically for the datatable view (after column filters and sorting)
	std::vector<DataPoint> datatablepoints;
	std::vector<DataPoint> filtered_points;
	std::vector<std::string> server_ids;
	std::vector<std::string> planets;
	std::vector<std::string> systems;
	std::vector<std::string> system_planets;
	std::vector<std::string> materials;
	// Per-column filter strings for datatable header row (index corresponds to table column index)
	std::vector<std::string> datatable_filters;
	// Sort state for datatable: column key and order (0=none,1=asc,2=desc)
	std::string datatable_sort_column;
	int datatable_sort_order{0};
	std::vector<std::string> resource_filter_materials;
	std::vector<Planet> planet_catalog;
	std::vector<Resource> material_catalog;
	std::unique_ptr<IStore> store;
	std::unique_ptr<ISyncService> sync_service;
	ScoutOcr::SubscriptionId ocr_subscription_id;

	double x;
	double y;
	double z;
	double grid_spacing{ 100.0 };

	std::string selected_system;
	std::string selected_planet;
	Planet* selected_planet_obj;
	std::string last_detected_region;
	std::string selected_material;
	std::string popup_selected_item;
	// Text used to filter resources in the UI (typed by the user)
	std::string resource_filter_text;
	// Text used to filter resources when selecting for a new record
	std::string resource_record_filter_text;

	// Text used to filter servers in the Server selector popup
	std::string server_filter_text;

	// Text used to filter planets in the Zone selector popup
	std::string planet_filter_text;

	// Most-recently-used resources for quick selection when adding a new point
	std::vector<std::string> recent_resources;
	std::string selected_server;
	std::optional<std::string> last_detected_rock;
	int quality_min{ kMinQuality };
	int quality_max{ kMaxQuality };
	DataPoint new_data{};
	std::unordered_map<std::string, GLuint> texture_cache;
	std::optional<std::string> hovered_text;

	// Map interaction/cameras
	Camera2D camera2d;
	Camera3D camera3d;
	// Highlighted materials (names)
	std::unordered_set<std::string> highlighted_materials;

	bool data_form_active{ false };
	bool planets_form_active{ false };

	// Travel Log (separate from the main data system)
	std::unique_ptr<TravelLog> travel_log;
	bool travel_log_active{ false };
	bool travel_log_disabled_due_to_qt{ false };

	// OCR results pushed from worker thread are stored here for main-thread processing
	std::mutex ocr_mutex;
	std::vector<OcrResult> ocr_results;

	AppState()
		: repo_root(detect_repo_root()),
		onnx_model_path(repo_root / "data" / "best_pareto_model.onnx"),
		label_map_path(repo_root / "data" / "label_map.json"),
		planets_dir(repo_root / "images" / "planets") {

		settings = load_settings(repo_root / "config" / "settings.ini");
		app_start_time = std::chrono::system_clock::now();

		// Initialize TravelLog subsystem (kept separate from main data store)
		travel_log = std::make_unique<TravelLog>();
		TravelLog::Config tlcfg;
		tlcfg.distance_threshold_km = settings.tracking_distance_threshold_km;
		tlcfg.max_speed_mps = settings.tracking_max_speed_mps;
		tlcfg.max_accel_mps2 = settings.tracking_max_accel_mps2;
		tlcfg.kalman_max_life_s = settings.kalman_max_life_s;
		tlcfg.qt_threshold = settings.qt_threshold;
		tlcfg.qt_disable_duration_s = settings.qt_disable_duration_s;
		tlcfg.min_core_distance_km = settings.tracking_min_core_distance_km;
		travel_log->configure(tlcfg);
		// Do not auto-start here; we'll auto-start later after planet/selection initialization if explicitly configured

		// Defer loading of catalogs until after the point store is initialized so we can prefer sqlite-backed tables.

		// Initialize point store: prefer SQLite if configured and available, otherwise CSV adapter
		std::string db_path_str = settings.storage_db_path;
		if (db_path_str.empty()) {
			db_path_str = (repo_root / "data" / "geoscout.db").string();
			settings.storage_db_path = db_path_str;
		} else {
			std::filesystem::path p(db_path_str);
			if (!p.is_absolute()) {
				db_path_str = (repo_root / p).string();
			}
		}

		auto sqlite_store = std::make_unique<SqliteStore>(db_path_str, settings.sync_node_id);
		if (sqlite_store->init()) {
			store = std::move(sqlite_store);
		} else {
			// store = std::make_unique<CsvPointStore>(csv_path.string());
		}

		// Load catalogs from chosen backend (prefer sqlite if available)
		auto sqlite_backend = store.get();
		if (sqlite_backend) {
			server_ids = sqlite_backend->load_server_ids();
			if (server_ids.empty()) server_ids = load_server_ids_csv(std::filesystem::path(repo_root / "data" / "server_ids.csv"), kDefaultServerIds);

			planet_catalog = sqlite_backend->load_planets();
			if (planet_catalog.empty()) planet_catalog = load_planet_catalog(std::filesystem::path(repo_root / "data" / "planets.csv"), kDefaultPlanets);

			material_catalog = sqlite_backend->load_resources();
			if (material_catalog.empty()) material_catalog = load_material_catalog(std::filesystem::path(repo_root / "data" / "resources.csv"), kDefaultMaterials, kMaterialIds);
		} else {
			server_ids = load_server_ids_csv(std::filesystem::path(repo_root / "data" / "server_ids.csv"), kDefaultServerIds);
			planet_catalog = load_planet_catalog(std::filesystem::path(repo_root / "data" / "planets.csv"), kDefaultPlanets);
			material_catalog = load_material_catalog(std::filesystem::path(repo_root / "data" / "resources.csv"), kDefaultMaterials, kMaterialIds);
		}

		// Populate derived lists
		planets.clear();
		for (const auto& planet : planet_catalog) planets.push_back(planet.name);
		systems = get_unique_systems(planet_catalog);
		if (systems.empty()) systems.push_back("All");
		selected_system = systems.front();

		if (selected_system == "All") {
			system_planets = planets;
		} else {
			system_planets.clear();
			for (const auto& p : planet_catalog) if (p.system == selected_system) system_planets.push_back(p.name);
		}

		materials.clear();
		resource_filter_materials.clear();
		for (const auto& material : material_catalog) {
			materials.push_back(material.name);
			if (material.type == ResourceType::Mineral || material.type == ResourceType::Plant || material.name == "All") {
				resource_filter_materials.push_back(material.name);
			}
		}

		std::sort(materials.begin(), materials.end(), [](const std::string& a, const std::string& b) {
			if (a == "All") return true;
			if (b == "All") return false;
			return a < b;
			});
		std::sort(resource_filter_materials.begin(), resource_filter_materials.end(), [](const std::string& a, const std::string& b) {
			if (a == "All") return true;
			if (b == "All") return false;
			return a < b;
			});

		if (planets.empty()) {
			for (const auto& key : kDefaultPlanets) planets.push_back(key);
		}
		if (materials.empty()) {
			for (const auto& name : kDefaultMaterials) materials.push_back(name);
		}

		update_selected_planet(planets.empty() ? kDefaultPlanets.front() : planets.front());
		update_grid_spacing();

		// Initialize datatable helper structures
		datatable_filters.resize(15);
		datatable_sort_column = "id";
		datatable_sort_order = 1; // default sort by id ascending

		// Provide TravelLog with repository path and optional store for metadata recording
		if (travel_log) {
			travel_log->set_repo_root(repo_root);
		}


		selected_material = materials.empty() ? kDefaultMaterials.front() : materials.front();
		selected_server = server_ids.empty() ? kDefaultServerIds.front() : server_ids.front();
		new_data.server = selected_server;
		new_data.planet = selected_planet;
		new_data.material = selected_material;
		new_data.quality_min = 0;
		new_data.quality_max = kMaxQuality;

		// Start sync service if enabled
		if (settings.sync_enabled && !settings.sync_server_url.empty()) {
			const std::string node_id = settings.sync_node_id.empty() ? "local-node" : settings.sync_node_id;
			sync_service = std::make_unique<SyncService>(settings.sync_server_url, node_id);
			// When sync pushes full updates, update in-memory points and re-filter
			sync_service->set_on_points_updated([this](const std::vector<DataPoint>& pts) {
				this->points = pts;
				this->filter_points();
				});
			sync_service->start();
		}
	}

	~AppState() {
		for (const auto& [_, texture] : texture_cache) {
			if (texture != 0) {
				glDeleteTextures(1, &texture);
			}
		}
	}

	void update_selected_planet(const std::string& new_planet) {
		selected_planet = new_planet;
		auto it = std::find_if(planet_catalog.begin(), planet_catalog.end(), [this](const Planet& p) {
			return p.name == selected_planet;
			});
		if (it != planet_catalog.end()) {
			selected_planet_obj = &(*it);
		} else {
			selected_planet_obj = nullptr;
		}
		update_grid_spacing();
		filter_points();

		// Update display mode based on zone preference or default for the zone type
		if (selected_planet_obj) {
			if (selected_planet_obj->last_display_mode > 0) {
				set_display_mode(static_cast<DisplayMode>(selected_planet_obj->last_display_mode));
			} else {
				set_display_mode(get_zone_default_display_mode(selected_planet_obj->zone_type));
			}
		}
	}

	void set_display_mode(DisplayMode new_mode) {
		// update in-memory state
		display_mode = new_mode;
		// persist preference if backend supports it and zone has asteroid belt
		if (store && selected_planet_obj) {
			auto sqlite_backend = dynamic_cast<SqliteStore*>(store.get());
			if (sqlite_backend && selected_planet_obj->has_asteroid_belt) {
				sqlite_backend->set_zone_last_display_mode(selected_planet_obj->id, static_cast<int>(new_mode));
			}
		}
	}

	std::vector<std::string> get_unique_systems(const std::vector<Planet>& planet_catalog) {
		std::vector<std::string> systems = {};
		std::vector<std::string> nsystems = {};
		systems.push_back("All");

		for (const auto& planet : planet_catalog) {
			if (std::find(nsystems.begin(), nsystems.end(), planet.system) == nsystems.end()) {
				nsystems.push_back(planet.system);
			}
		}
		std::sort(nsystems.begin(), nsystems.end());
		systems.insert(systems.end(), nsystems.begin(), nsystems.end());

		return systems;
	}

	void update_grid_spacing() {
		auto it = std::find_if(planet_catalog.begin(), planet_catalog.end(), [this](const Planet& p) {
			return p.name == selected_planet;
			});
		if (it != planet_catalog.end()) {
			if (it->zone_type == ZoneType::AsteroidField) {
				grid_spacing = settings.grid_spacing_km;
			} else {
				grid_spacing = settings.planet_grid_spacing_degrees; // default for non-asteroid zones
			}
		}
	}

	void reload_planet_data() {
		if (store) {
			points = store->load_points();
		} else {
			points = load_points(std::filesystem::path(repo_root / "data" / "geoscout.csv"));
		}
		int max_id = 0;
		for (const auto& point : points) {
			max_id = max(max_id, point.id);
		}
		new_data.id = max_id + 1;
	}

	void reload_planet_catalog() {
		if (store) {
			auto sqlite_backend = dynamic_cast<SqliteStore*>(store.get());
			if (sqlite_backend) {
				planet_catalog = sqlite_backend->load_planets();
			} else {
				planet_catalog = load_planet_catalog(std::filesystem::path(repo_root / "data" / "planets.csv"), kDefaultPlanets);
			}
		} else {
			planet_catalog = load_planet_catalog(std::filesystem::path(repo_root / "data" / "planets.csv"), kDefaultPlanets);
		}
		planets.clear();
		for (const auto& p : planet_catalog) {
			planets.push_back(p.name);
		}
		systems = get_unique_systems(planet_catalog);

		// Rebuild system_planets for the current selection
		if (selected_system == "All") {
			system_planets = planets;
		} else {
			system_planets.clear();
			for (const auto& planet : planet_catalog) {
				if (planet.system == selected_system) {
					system_planets.push_back(planet.name);
				}
			}
		}

		// Ensure selected values remain valid
		if (systems.empty()) systems.push_back("All");
		if (std::find(systems.begin(), systems.end(), selected_system) == systems.end()) {
			selected_system = systems.front();
		}
		if (planets.empty()) {
			for (const auto& key : kDefaultPlanets) planets.push_back(key);
		}
		if (std::find(planets.begin(), planets.end(), selected_planet) == planets.end()) {
			update_selected_planet( planets.front());
		}
	}

	void filter_points() {
		filtered_points.clear();
		for (const auto& point : points) {
			const bool material_match = point.material == selected_material || selected_material == "All";
			const bool planet_match = point.planet == selected_planet;
			const bool server_match = point.server == selected_server || selected_server == "All";
			if (planet_match && (material_match && server_match || point.poi_type == PoiType::Location)) {
				filtered_points.push_back(point);
			}
		}
	}

	GLuint get_texture_for_selected_planet() {
		auto it = texture_cache.find(selected_planet);
		if (it != texture_cache.end()) {
			return it->second;
		}

		// selected to image dir for planet in planets.csv, if not found fallback to looking for image named after planet key directly in planets dir

		std::string image_dir_name = selected_planet;
		for (const auto& planet : planet_catalog) {
			if (planet.name == selected_planet && !planet.image_dir.empty()) {
				image_dir_name = planet.image_dir;
				break;
			}
		}


		std::filesystem::path texture_path = planets_dir / image_dir_name / "planet.jpg";
		const auto dir_it = std::find_if(planet_catalog.begin(), planet_catalog.end(), [this](const Planet& p) {
			return p.name == selected_planet;
			});
		if (dir_it != planet_catalog.end() && !dir_it->image_dir.empty()) {
			texture_path = planets_dir / dir_it->image_dir / "planet.jpg";
		}

		GLuint texture = load_texture_file(texture_path);
		if (texture == 0) {
			texture = load_texture_file(planets_dir / (selected_planet + ".jpg"));
		}
		if (texture == 0) {
			texture = load_texture_file(repo_root / "images" / "planets" / "skybox" / "default.jpg");
		}
		texture_cache[selected_planet] = texture;
		return texture;
	}

	void save_settings() {
		std::ofstream out(repo_root / "config" / "settings.ini");
		if (!out.is_open()) {
			return;
		}
		out << "show_timings=" << (settings.show_timings ? "1" : "0") << '\n';
		out << "ocr_feed_newpoint_enabled=" << (settings.auto_update_ocr_newpoint_enabled ? "1" : "0") << '\n';
		out << "ocr_feed_planet_update_enabled=" << (settings.ocr_feed_planet_update_enabled ? "1" : "0") << '\n';
		out << "sync_enabled=" << (settings.sync_enabled ? "1" : "0") << '\n';
		out << "sync_server_url=" << settings.sync_server_url << '\n';
		out << "sync_node_id=" << settings.sync_node_id << '\n';
		out << "storage_db_path=" << settings.storage_db_path << '\n';
		out << "sync_max_outbox_size=" << settings.sync_max_outbox_size << '\n';
		out << "grid_spacing_km=" << settings.grid_spacing_km << '\n';
		out << "planet_grid_spacing_degrees=" << settings.planet_grid_spacing_degrees << '\n'; // legacy key
		// Travel Log settings
		out << "tracking_enabled_on_start=" << (settings.tracking_enabled_on_start ? "1" : "0") << '\n';
		out << "tracking_distance_threshold_km=" << settings.tracking_distance_threshold_km << '\n';
		out << "tracking_max_speed_mps=" << settings.tracking_max_speed_mps << '\n';
		out << "tracking_max_accel_mps2=" << settings.tracking_max_accel_mps2 << '\n';
		out << "kalman_max_life_s=" << settings.kalman_max_life_s << '\n';
		out << "qt_threshold=" << settings.qt_threshold << '\n';
		out << "qt_disable_duration_s=" << settings.qt_disable_duration_s << '\n';
		out << "tracking_min_core_distance_km=" << settings.tracking_min_core_distance_km << '\n';
		out << "last_export_path=" << settings.last_export_path << '\n';
		out << "session_export_dir=" << settings.session_export_dir << '\n';
		out << "auto_export_session_minerals=" << (settings.auto_export_session_minerals ? "1" : "0") << '\n';
	}

	AppSettings load_settings(const std::filesystem::path& path) {
		AppSettings settings;
		std::ifstream in(path);
		if (!in.is_open()) {
			return settings;
		}

		std::string line;
		while (std::getline(in, line)) {
			const auto trimmed = trim(line);
			if (trimmed.empty() || trimmed[0] == '#') {
				continue;
			}

			const auto delimiter_pos = trimmed.find('=');
			if (delimiter_pos == std::string::npos) {
				continue;
			}

			const auto key = trim(trimmed.substr(0, delimiter_pos));
			const auto value = trim(trimmed.substr(delimiter_pos + 1));

			if (key == "show_timings") {
				settings.show_timings = (value == "1");
			} else if (key == "ocr_feed_newpoint_enabled") {
				settings.auto_update_ocr_newpoint_enabled = (value == "1");
			} else if (key == "ocr_feed_planet_update_enabled") {
				settings.ocr_feed_planet_update_enabled = (value == "1");
			} else if (key == "sync.enabled" || key == "sync_enabled") {
				settings.sync_enabled = (value == "1" || value == "true");
			} else if (key == "sync.server_url" || key == "sync_server_url") {
				settings.sync_server_url = value;
			} else if (key == "sync.node_id" || key == "sync_node_id") {
				settings.sync_node_id = value;
			} else if (key == "storage.db_path" || key == "storage_db_path") {
				settings.storage_db_path = value;
			} else if (key == "sync.max_outbox_size" || key == "sync_max_outbox_size") {
				try { settings.sync_max_outbox_size = std::stoi(value); }
				catch (...) {}
			} else if (key == "grid_spacing_km" || key == "grid_spacing") {
				try { settings.grid_spacing_km = std::stod(value); }
				catch (...) {}
			} else if (key == "planet_grid_spacing_degrees") {
				try { settings.planet_grid_spacing_degrees = std::stod(value); }
				catch (...) {
				}
			} else if (key == "tracking_enabled_on_start") {
				settings.tracking_enabled_on_start = (value == "1" || value == "true");
			} else if (key == "tracking_distance_threshold_km") {
				try { settings.tracking_distance_threshold_km = std::stod(value); }
				catch (...) {}
			} else if (key == "tracking_max_speed_mps") {
				try { settings.tracking_max_speed_mps = std::stod(value); }
				catch (...) {}
			} else if (key == "tracking_max_accel_mps2") {
				try { settings.tracking_max_accel_mps2 = std::stod(value); }
				catch (...) {}
			} else if (key == "kalman_max_life_s") {
				try { settings.kalman_max_life_s = std::stod(value); }
				catch (...) {}
			} else if (key == "qt_threshold") {
				try { settings.qt_threshold = std::stod(value); }
				catch (...) {}
			} else if (key == "qt_disable_duration_s") {
				try { settings.qt_disable_duration_s = std::stod(value); }
				catch (...) {}
			} else if (key == "tracking_min_core_distance_km") {
				try { settings.tracking_min_core_distance_km = std::stod(value); }
				catch (...) {}
			} else if (key == "last_export_path") {
				settings.last_export_path = value;
			} else if (key == "session_export_dir") {
				settings.session_export_dir = value;
			} else if (key == "auto_export_session_minerals") {
				settings.auto_export_session_minerals = (value == "1" || value == "true");
			}
		}

		return settings;

	}
	void export_session_minerals() {
		// get start of day timestamp for filename
		uint64_t start_of_day_ts = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(app_start_time.time_since_epoch()).count());
		std::vector<PoiType> mineral_types = { PoiType::Mineral };
		std::vector<DataPoint> minerals = store->load_points("", "", mineral_types, start_of_day_ts, 0);

		if (minerals.empty()) {
			std::cout << "No minerals detected during this session, skipping export.\n";
			return;
		}
		// make absolute path to export file in session folder with unique name
		if (!std::filesystem::exists(settings.session_export_dir)) {
			std::filesystem::create_directories(settings.session_export_dir);
		}
		std::filesystem::path export_path_dir = settings.session_export_dir;
		std::string timestamp_str = ("session_minerals_" + format_iso_date(app_start_time) + "_" + uuid::generate_uuid_v4().to_string() + ".json");
		std::filesystem::path export_path = (export_path_dir / timestamp_str);

		// write minerals to export file as JSON
		JsonExchangeDatapoint json_exporter;
		if (json_exporter.export_json_datapoints(export_path, minerals)) {
			std::cout << "Exported session minerals to " << export_path << "\n";
		} else {
			std::cerr << "Failed to export session minerals to " << export_path << "\n";
		}
	}

	void finalize() {
		if (settings.auto_export_session_minerals) {
			export_session_minerals();
		}
		save_settings();
	}

	std::vector<double> process_ocr_results() {
		std::vector<OcrResult> results_to_process;
		{
			std::lock_guard<std::mutex> lk(ocr_mutex);
			results_to_process.swap(ocr_results);
		}

		if (results_to_process.empty()) {
			return {};
		}
		std::vector<double> ocr_values;

		for (const auto& result : results_to_process) {
			if (result.x.has_value()
				&& result.y.has_value()
				&& result.z.has_value()) {

				x = result.x.value();
				y = result.y.value();
				z = result.z.value();

				if (settings.auto_update_ocr_newpoint_enabled) {
					new_data.x = result.x.value();
					new_data.y = result.y.value();
					new_data.z = result.z.value();
				}

				if (travel_log && travel_log_active) {
					const double ts_s = static_cast<double>(std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::system_clock::now().time_since_epoch()).count());
					bool added = travel_log->feed_measurement(x, y, z, ts_s);
					if (travel_log->is_locked_due_to_qt()) {
						travel_log_active = false;
						travel_log_disabled_due_to_qt = true;
					}
				}

			}

			//if (result.rock.has_value()) {
			//    if 
			//    new_data.material = result.rock.value();
			//}

			if (settings.ocr_feed_planet_update_enabled) {
				if (result.locationmarker.has_value()) {
					for (const auto& planet : planet_catalog) {
						if (planet.zone_id.empty()) {
							continue;
						}
						if (result.locationmarker.value().find(planet.zone_id) != std::string::npos) {
							// only update if different
							if (planet.name != selected_planet) {
								update_selected_planet(planet.name);
								filter_points();
							}
							break;
						}
					}
				}
			}
			ocr_values.push_back(result.task_time_ms);
		}
		return ocr_values;
	}

	void update_new_data_from_ocr(const OcrResult& result) {
		std::lock_guard<std::mutex> lk(ocr_mutex);
		ocr_results.push_back(result);
	}
};

static std::string to_lower(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

static bool contains_case_insensitive(const std::string& text, const std::string& query) {
	if (query.empty()) return true;
	const auto lt = to_lower(text);
	const auto lq = to_lower(query);
	return lt.find(lq) != std::string::npos;
}

static void push_recent_resource(AppState& state, const std::string& resource) {
	if (resource.empty()) return;
	auto it = std::find(state.recent_resources.begin(), state.recent_resources.end(), resource);
	if (it != state.recent_resources.end()) {
		state.recent_resources.erase(it);
	}
	state.recent_resources.insert(state.recent_resources.begin(), resource);
	if (state.recent_resources.size() > 8) state.recent_resources.resize(8);
}

bool combo_string(const char* label, const std::vector<std::string>& items, std::string& current_value) {
	auto current_it = std::find(items.begin(), items.end(), current_value);
	int current_index = current_it != items.end() ? static_cast<int>(std::distance(items.begin(), current_it)) : 0;
	std::vector<const char*> c_strs;
	c_strs.reserve(items.size());
	for (const auto& item : items) {
		c_strs.push_back(item.c_str());
	}

	if (ImGui::Combo(label, &current_index, c_strs.data(), static_cast<int>(c_strs.size()))) {
		current_value = items[static_cast<size_t>(current_index)];
		return true;
	}
	return false;
}


int run_scout_app() {
	GdiplusSession gdiplus_session;
    auto app_start_time = std::chrono::system_clock::now();


	if (!glfwInit()) {
		std::cerr << "Failed to initialize GLFW\n";
		return 1;
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4); // Specify OpenGL version
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); // Use core profile
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

	GLFWwindow* window = glfwCreateWindow(1280, 720, "Scout Engine", nullptr, nullptr);
	if (!window) {
		glfwTerminate();
		return 1;
	}

	glfwMakeContextCurrent(window);
	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
		std::cerr << "Failed to initialize GLAD\n";
		glfwDestroyWindow(window);
		glfwTerminate();
		return 1;
	}
	glfwSwapInterval(1);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init();

	ScoutRenderer renderer;
	if (!renderer.init()) {
		std::cerr << "Failed to initialize renderer\n";
		glfwDestroyWindow(window);
		glfwTerminate();
		return 1;
	}

	AppState state;

	auto root_location = detect_repo_root();
	auto onnx_model_path = root_location / "data" / "best_pareto_model.onnx";
	auto label_map_path = root_location / "data" / "label_map.json";
	ScoutOcr ocr_instance(onnx_model_path, label_map_path);
	// Register OCR callback to push results into AppState queue
	state.ocr_subscription_id = ocr_instance.subscribe([&state](const OcrResult& result) {
		state.update_new_data_from_ocr(result);
		});

	// Timer to periodically call OCR on the current window content
	auto ocr_timer = std::thread([&ocr_instance]() {
		auto start = std::chrono::steady_clock::now();
		auto end = start;
		while (ocr_instance.active) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100) - (end - start));
			start = std::chrono::steady_clock::now();
			ocr_instance.request_async();
			end = std::chrono::steady_clock::now();
		}
		});
	// keep thread joinable so we can stop it cleanly on shutdown

	state.reload_planet_data();
	state.filter_points();
	TimerDisplay timer_display;
	TimerFooterRenderer timer_footer_renderer;

	bool location_on = false;
	bool f3_was_down = false;
	auto last_toggle = std::chrono::steady_clock::now();
	auto next_tick = std::chrono::steady_clock::now();

	while (!glfwWindowShouldClose(window)) {
		auto now = std::chrono::steady_clock::now();
		timer_display.loop_time_ms().stamp();

		// Simple fixed timestep loop with sleep to limit CPU usage. Not super precise but good enough for this use case.
		double actual_sleep = 0.0;
		if (next_tick > now) {
			const auto sleep_start = std::chrono::steady_clock::now();
			std::this_thread::sleep_for(next_tick - now);
			const auto sleep_end = std::chrono::steady_clock::now();
			actual_sleep = std::chrono::duration<double>(sleep_end - sleep_start).count();
		}


		const auto sleep_pct = kFrameTime > 0.0 ? min(100.0, (actual_sleep / kFrameTime) * 100.0) : 0.0;
		timer_display.sleep_percent().add_sample(sleep_pct);
		timer_display.work_time_ms().stamp();

		timer_display.ocr_poll_time_ms().stamp();
		// Process any OCR results pushed by the worker thread
		auto ocr_values = state.process_ocr_results();
		for (const auto& value : ocr_values) {
			timer_display.ocr_task_time_ms().add_sample(value);
		}
		timer_display.ocr_poll_time_ms().record_time_since_stamp();

		glfwPollEvents();

		const bool f3_is_down = glfwGetKey(window, GLFW_KEY_F3) == GLFW_PRESS;
		if (f3_is_down && !f3_was_down) {
			state.settings.show_timings = !state.settings.show_timings;
		}
		f3_was_down = f3_is_down;

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		ImGui::Begin("MAIN CONTROLS");
		if (ImGui::BeginTabBar("ControlsTabs")) {
			if (ImGui::BeginTabItem("Controls")) {
				if (ImGui::Button("Open Data Form")) {
					state.data_form_active = true;
				}
				ImGui::SameLine();
				if (ImGui::Button("Open Planets Editor")) {
					state.planets_form_active = true;
				}

				if (state.selected_planet_obj->has_asteroid_belt && state.selected_planet_obj->zone_type == ZoneType::CelestialBody) {
					ImGui::Separator();
					if (state.display_mode == DisplayMode::Surface) {
						if (ImGui::Button("Switch to Asteroid Belt Display Mode")) {
							state.set_display_mode(DisplayMode::Celestial_Belt);
						}
					} else {
						if (ImGui::Button("Switch to Surface Display Mode")) {
							state.set_display_mode(DisplayMode::Surface);
						}
					}

				}

				ImGui::Separator();

				// Travel Log controls (separate tracking system)
				ImGui::Text("Travel Log:");
				if (state.travel_log_disabled_due_to_qt) {
					ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Travel Log disabled due to QT. Manual reactivation required.");
				}
				// Disable the Start button when the user has selected the "All" server
				const bool start_disabled_due_to_server = (!state.travel_log_active && state.selected_server == "All");
				if (start_disabled_due_to_server) {
					ImGui::BeginDisabled();
				}

				if (state.travel_log_active) {
					if (ImGui::Button("Stop Travel Log")) {
						if (state.travel_log) state.travel_log->stop();
						state.travel_log_active = false;
					}
					ImGui::SameLine();
					if (ImGui::Button("Restart Travel Log")) {
						if (state.travel_log) {
							// Persist current log then restart
							state.travel_log->stop();
						} else {
							state.travel_log = std::make_unique<TravelLog>();
						}
						// (re)configure to current settings
						{
							TravelLog::Config tlcfg;
							tlcfg.distance_threshold_km = state.settings.tracking_distance_threshold_km;
							tlcfg.max_speed_mps = state.settings.tracking_max_speed_mps;
							tlcfg.max_accel_mps2 = state.settings.tracking_max_accel_mps2;
							tlcfg.kalman_max_life_s = state.settings.kalman_max_life_s;
							tlcfg.qt_threshold = state.settings.qt_threshold;
							tlcfg.qt_disable_duration_s = state.settings.qt_disable_duration_s;
							tlcfg.min_core_distance_km = state.settings.tracking_min_core_distance_km;
							state.travel_log->configure(tlcfg);
						}
						// Ensure repo/store are set and start with current selected zone
						state.travel_log->set_repo_root(state.repo_root);
						ZoneType zt = ZoneType::CelestialBody;
						double cx = 0.0, cy = 0.0, cz = 0.0;
						for (const auto& p : state.planet_catalog) {
							if (p.name == state.selected_planet) {
								zt = p.zone_type;
								cx = p.center_x; cy = p.center_y; cz = p.center_z;
								break;
							}
						}
						state.travel_log->start(state.selected_planet, zt, state.selected_server);
						state.travel_log_active = true;
						state.travel_log_disabled_due_to_qt = false;
					}
				} else {
					if (ImGui::Button("Start Travel Log")) {
						if (!state.travel_log) {
							state.travel_log = std::make_unique<TravelLog>();
							TravelLog::Config tlcfg;
							tlcfg.distance_threshold_km = state.settings.tracking_distance_threshold_km;
							tlcfg.max_speed_mps = state.settings.tracking_max_speed_mps;
							tlcfg.max_accel_mps2 = state.settings.tracking_max_accel_mps2;
							tlcfg.kalman_max_life_s = state.settings.kalman_max_life_s;
							tlcfg.qt_threshold = state.settings.qt_threshold;
							tlcfg.qt_disable_duration_s = state.settings.qt_disable_duration_s;
							tlcfg.min_core_distance_km = state.settings.tracking_min_core_distance_km;
							state.travel_log->configure(tlcfg);
						}
						// Ensure repo/store are set and start with current selected zone
						state.travel_log->set_repo_root(state.repo_root);
						// find zone info for selected planet
						ZoneType zt = ZoneType::CelestialBody;
						for (const auto& p : state.planet_catalog) {
							if (p.name == state.selected_planet) {
								zt = p.zone_type;
								break;
							}
						}
						state.travel_log->start(state.selected_planet, zt, state.selected_server);
						state.travel_log_active = true;
						state.travel_log_disabled_due_to_qt = false;
					}
					if (start_disabled_due_to_server && ImGui::IsItemHovered()) {
						ImGui::SetTooltip("Select a specific server (not 'All') to enable Travel Log.");
					}

					ImGui::SameLine();
					if (ImGui::Button("Restart Travel Log")) {
						state.travel_log->restart();
						state.travel_log_active = true;
						state.travel_log_disabled_due_to_qt = false;
					}
				}



				if (start_disabled_due_to_server) {
					ImGui::EndDisabled();
					ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Disabled for 'All' server");
				}
				ImGui::Separator();

				{
					std::string server_btn = std::string("Server: ") + state.selected_server;
					if (ImGui::Button(server_btn.c_str())) {
						ImGui::OpenPopup("ServerSelectPopup");
					}
					if (ImGui::BeginPopupModal("ServerSelectPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
						popup_filter(state.server_filter_text, state.popup_selected_item, state.server_ids, state.selected_server, [&](const std::string& selected) {
							state.selected_server = selected;
							state.new_data.server = selected;
							state.filter_points();
						});
					}
				}

				if (!state.settings.ocr_feed_planet_update_enabled) {
					if (state.last_detected_region.empty()) {
						ImGui::Text("Detected Region: None");
					} else {
						ImGui::Text("Detected Region: %s", state.last_detected_region.c_str());
						if (state.last_detected_region != state.selected_planet) {
							if (ImGui::Button("Use Detected Region")) {
								state.update_selected_planet(state.last_detected_region);
								state.new_data.planet = state.last_detected_region;
								state.filter_points();
							}
						}
					}
				}
				if (combo_string("System", state.systems, state.selected_system)) {
					if (state.selected_system == "All") {
						state.system_planets = state.planets;
					} else {
						state.system_planets.clear();
						for (const auto& planet : state.planet_catalog) {
							if (planet.system == state.selected_system) {
								state.system_planets.push_back(planet.name);
							}
						}
						if (std::find(state.system_planets.begin(), state.system_planets.end(), state.selected_planet) == state.system_planets.end()) {
							state.update_selected_planet(state.system_planets.front());
							state.new_data.planet = state.selected_planet;
							state.update_grid_spacing();
						}
					}

					state.filter_points();
				}

				{
					std::string zone_btn = std::string("Zone: ") + state.selected_planet;
					if (ImGui::Button(zone_btn.c_str())) {
						ImGui::OpenPopup("ZoneSelectPopup");
					}
					if (ImGui::BeginPopupModal("ZoneSelectPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
						const std::vector<std::string>* planet_items = (state.selected_system != "All") ? &state.system_planets : &state.planets;
						popup_filter(state.planet_filter_text, state.popup_selected_item, *planet_items, state.selected_planet, [&](const std::string& selected) {
							state.update_selected_planet(selected);
							state.new_data.planet = selected;
							state.filter_points();
							state.update_grid_spacing();
						});
					}
				}

				{
					// Button opens a modal popup for selecting resource with a filter
					std::string resource_button = std::string("Resource: ") + state.selected_material;
					if (ImGui::Button(resource_button.c_str())) {
						ImGui::OpenPopup("ResourceSelectPopup");
					}

					if (ImGui::BeginPopupModal("ResourceSelectPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
						popup_filter(state.resource_filter_text, state.popup_selected_item, state.resource_filter_materials, state.selected_material, [&](const std::string& selected) {
							state.selected_material = selected;
							state.filter_points();
						});
					}
				}

				ImGui::Separator();

				ImGui::Text("New Point:");
				// if (state.last_detected_rock) {
				//     ImGui::Text("Last detected rock type: %s", state.last_detected_rock->c_str());
				//     if (ImGui::Button("Use Detected Rock Type")) {
				//         state.selected_material = *state.last_detected_rock;
				//         state.new_data.material = *state.last_detected_rock;
				//     }
				// }

				ImGui::Checkbox("Auto updates new point coordinates", &state.settings.auto_update_ocr_newpoint_enabled);

				float input_x = static_cast<float>(state.new_data.x);
				float input_y = static_cast<float>(state.new_data.y);
				float input_z = static_cast<float>(state.new_data.z);

				if (!state.settings.auto_update_ocr_newpoint_enabled) {
					ImGui::Text("Last OCR values: X=%.2f Y=%.2f Z=%.2f", state.x, state.y, state.z);
					if (ImGui::Button("Use Last OCR Values")) {
						input_x = static_cast<float>(state.x);
						input_y = static_cast<float>(state.y);
						input_z = static_cast<float>(state.z);
					};
				}

				{
					// Button opens a modal popup for selecting resource for the new record
					std::string rec_button = std::string("Resource Record: ") + state.new_data.material;
					if (ImGui::Button(rec_button.c_str())) {
						ImGui::OpenPopup("ResourceRecordSelectPopup");
					}

					if (ImGui::BeginPopupModal("ResourceRecordSelectPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
						popup_filter(state.resource_record_filter_text, state.popup_selected_item, state.resource_filter_materials, state.new_data.material, [&](const std::string& selected) {
							state.new_data.material = selected;
							push_recent_resource(state, selected);
						});
					}
				}

				// Recent resources for quick selection when adding a new point
				if (!state.recent_resources.empty()) {
					ImGui::Text("Recent:");
					ImGui::SameLine();
					for (size_t ri = 0; ri < state.recent_resources.size(); ++ri) {
						std::string btn = state.recent_resources[ri] + std::string("##recent") + std::to_string(ri);
						if (ImGui::SmallButton(btn.c_str())) {
							state.new_data.material = state.recent_resources[ri];
							push_recent_resource(state, state.new_data.material);
						}
						if (ri + 1 < state.recent_resources.size() && ri != 4) ImGui::SameLine();
					}
				}

				ImGui::InputFloat("X", &input_x);
				ImGui::InputFloat("Y", &input_y);
				ImGui::InputFloat("Z", &input_z);
				state.new_data.x = input_x;
				state.new_data.y = input_y;
				state.new_data.z = input_z;
				ImGui::InputInt("Quality Min", &state.new_data.quality_min);
				ImGui::InputInt("Quality Max", &state.new_data.quality_max);

				if (ImGui::Button("Add Point")) {
					DataPoint new_point;
					new_point.server = state.selected_server;
					new_point.uuid = uuid::generate_uuid_v4();
					new_point.x = state.new_data.x;
					new_point.y = state.new_data.y;
					new_point.z = state.new_data.z;
					new_point.planet = state.selected_planet;
					new_point.material = state.new_data.material;
					new_point.location = false;
					new_point.quality_min = state.new_data.quality_min;
					new_point.quality_max = state.new_data.quality_max;
					new_point.subtype = PoiSubType::None;
					new_point.qt_persistent = false;
					new_point.poi_type = PoiType::Mineral;
					new_point.time_info = format_iso_datetime(now_ymdhm());
					if (state.store) {
						uuid change_id;
						if (state.store->append_point(new_point, &change_id)) {
							state.points.push_back(new_point);
							state.new_data.id += 1;
							push_recent_resource(state, new_point.material);
							// Persist an outbox event and notify sync service if present
							if (state.sync_service) {
								ChangeEvent ev;
								// generate a simple change_id if none provided
								if (change_id == nil_uuid) {
									ev.change_id = uuid::generate_uuid_v4();
								} else {
									ev.change_id = change_id;
								}
								ev.node_id = state.settings.sync_node_id.empty() ? "local-node" : state.settings.sync_node_id;
								ev.created_ts = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
								ev.op = "upsert";
								ev.recordid = new_point.id;
								// The store already persisted the change event; just notify the sync service.
								ev.payload_json = "";
								state.sync_service->notify_new_local_event(ev);
							}
							// Ensure asteroid zone bounding boxes expand to include the new point when needed
							if (state.store) {
								if (auto sqlite_backend = dynamic_cast<SqliteStore*>(state.store.get())) {
									if (sqlite_backend->ensure_zone_contains_point(new_point.planet, new_point.x, new_point.y, state.settings.grid_spacing_km)) {
										state.reload_planet_catalog();

									}
								}
							}
						}
					} else {
						if (append_point_csv(detect_repo_root() / "data" / "geoscout.csv", new_point)) {
							state.points.push_back(new_point);
							state.new_data.id += 1;
							push_recent_resource(state, new_point.material);
						}
					}
					state.filter_points();
				}
				ImGui::SameLine();
				if (ImGui::Button("Add Qualityless")) {
					DataPoint new_point;
					new_point.id = state.new_data.id;
					new_point.server = state.selected_server;
					new_point.uuid = uuid::generate_uuid_v4();
					new_point.x = state.new_data.x;
					new_point.y = state.new_data.y;
					new_point.z = state.new_data.z;
					new_point.planet = state.selected_planet;
					new_point.material = state.new_data.material;
					new_point.location = false;
					new_point.quality_min = 1;
					new_point.quality_max = 1;
					new_point.subtype = state.new_data.subtype;
					new_point.qt_persistent = state.new_data.qt_persistent;
					new_point.poi_type = PoiType::Mineral;
					new_point.time_info = format_iso_datetime(now_ymdhm());
					if (state.store) {
						uuid change_id;
						if (state.store->append_point(new_point, &change_id)) {
							state.points.push_back(new_point);
							state.new_data.id += 1;
							push_recent_resource(state, new_point.material);
							if (state.sync_service) {
								ChangeEvent ev;
								if (change_id == nil_uuid) {
									ev.change_id = uuid::generate_uuid_v4();
								} else {
									ev.change_id = change_id;
								}
								ev.node_id = state.settings.sync_node_id.empty() ? "local-node" : state.settings.sync_node_id;
								ev.created_ts = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
								ev.op = "upsert";
								ev.recordid = new_point.id;
								ev.payload_json = "";
								state.sync_service->notify_new_local_event(ev);
							}
							if (auto sqlite_backend = dynamic_cast<SqliteStore*>(state.store.get())) {
								if (sqlite_backend->ensure_zone_contains_point(new_point.planet, new_point.x, new_point.y, state.settings.grid_spacing_km)) {
									state.reload_planet_catalog();
								}
							}
						}
					} else {
						if (append_point_csv(detect_repo_root() / "data" / "geoscout.csv", new_point)) {
							state.points.push_back(new_point);
							state.new_data.id += 1;
							push_recent_resource(state, new_point.material);
						}
					}
					state.filter_points();
				}
				ImGui::EndTabItem();
			}
			// ImGui::EndTabItem();

			if (ImGui::BeginTabItem("Transfer")) {
				static bool export_init = false;
				static char export_buf[1024] = {0};
				static std::string export_status;

if (ImGui::Button("Browse")) {
#ifdef _WIN32
					std::wstring outp;
					if (win32_save_file_dialog(outp)) {
						std::string sp = std::filesystem::path(outp).string();
						strncpy(export_buf, sp.c_str(), sizeof(export_buf) - 1);
						export_buf[sizeof(export_buf) - 1] = '\0';
					}
#else
					// No native dialog on this platform
#endif
				}

				if (!export_init) {
					std::string def;
					if (!state.settings.last_export_path.empty()) def = state.settings.last_export_path;
					else def = (state.repo_root / "data" / "point.json").string();
					strncpy(export_buf, def.c_str(), sizeof(export_buf) - 1);
					export_init = true;
				}

				ImGui::InputText("Export File", export_buf, sizeof(export_buf));
				
				if (ImGui::Button("Export All Points")) {
					if (!state.store) {
						export_status = "No store backend available.";
					} else {
						std::filesystem::path p(export_buf);
						JsonExchangeDatapoint je(state.store.get());
						auto pts = state.store->load_points();
						int rc = je.export_json_datapoints(p, pts);
						if (rc == 0) {
							state.settings.last_export_path = p.string();
							export_status = std::string("Exported ") + std::to_string(pts.size()) + " points to: " + p.string();
						} else {
							export_status = std::string("Failed to export to: ") + p.string();
						}
					}
				}

				ImGui::SameLine();
				if (ImGui::Button("Export Filtered")) {
					if (!state.store) {
						export_status = "No store backend available.";
					} else {
						std::filesystem::path p(export_buf);
						JsonExchangeDatapoint je(state.store.get());
						auto pts = state.filtered_points;
						int rc = je.export_json_datapoints(p, pts);
						if (rc == 0) {
							state.settings.last_export_path = p.string();
							export_status = std::string("Exported ") + std::to_string(pts.size()) + " filtered points to: " + p.string();
						} else {
							export_status = std::string("Failed to export to: ") + p.string();
						}
					}
				}
				
				if (ImGui::Button("Export All Mineral Points")) {
					if (!state.store) {
						export_status = "No store backend available.";
					} else {
						std::filesystem::path p(export_buf);
						JsonExchangeDatapoint je(state.store.get());
						auto pts = state.store->load_points("ALL","ALL",std::vector<PoiType>{PoiType::Mineral});
						int rc = je.export_json_datapoints(p, pts);
						if (rc == 0) {
							state.settings.last_export_path = p.string();
							export_status = std::string("Exported ") + std::to_string(pts.size()) + " mineral points to: " + p.string();
						} else {
							export_status = std::string("Failed to export to: ") + p.string();
						}
					}
				}

				if (ImGui::Button("Export Local Mineral Points")) {
					if (!state.store) {
						export_status = "No store backend available.";
					} else {
						std::filesystem::path p(export_buf);
						JsonExchangeDatapoint je(state.store.get());
						auto pts = state.store->load_points(state.selected_planet,state.selected_server,std::vector<PoiType>{PoiType::Mineral});
						int rc = je.export_json_datapoints(p, pts);
						if (rc == 0) {
							state.settings.last_export_path = p.string();
							export_status = std::string("Exported ") + std::to_string(pts.size()) + " mineral points to: " + p.string();
						} else {
							export_status = std::string("Failed to export to: ") + p.string();
						}
					}
				}

				if (ImGui::Button("Import from file")) {
					std::filesystem::path p(export_buf);
					JsonExchangeDatapoint je(state.store.get());
					auto result = je.import_json_datapoints(p);
					if (result.imported_count > 0) {
						state.reload_planet_data();
						state.filter_points();
						export_status = std::string("Import successful from: ") + 
						p.string() + '|' + std::to_string(result.imported_count) + " points imported. " + std::to_string(result.skipped_count) + " duplicates skipped.";
						state.settings.last_export_path = p.string();
					} else {
						export_status = std::string("Failed to import from: ") +
							p.string() + '|' + std::to_string(result.imported_count) + " points imported. " + std::to_string(result.skipped_count) + " duplicates skipped.";
						state.settings.last_export_path = p.string();
					}
				}

				if (!export_status.empty()) {
					ImGui::TextWrapped("%s", export_status.c_str());
				}

				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Settings")) {
				ImGui::Checkbox("Show Timings (F3)", &state.settings.show_timings);
				ImGui::Separator();
				ImGui::Text("OCR Settings:");
				ImGui::Checkbox("Auto updates new point coordinates", &state.settings.auto_update_ocr_newpoint_enabled);
				ImGui::Checkbox("Auto updates active planet", &state.settings.ocr_feed_planet_update_enabled);
				ImGui::Separator();
				if (ImGui::Button("Import starmap data")) {
				  // executing proceduer 
				  // curl -o ./data/starmap_locations.json https://starmap.space/api/v3/pois/index.php
				  // dbimport-starmap-json ./data/starmap_locations.json ./data/geoscout.db
					std::string starmap_json_path = (state.repo_root / "data" / "starmap_locations.json").string();
					std::string starmap_db_path = (state.repo_root / "data" / "geoscout.db").string();
					bool retFlag = false;

					if(write_starmap_json(starmap_json_path)) {
						dbimport_starmap(starmap_db_path, starmap_json_path, retFlag);
					}

					state.reload_planet_data();
					state.filter_points();
				}
				ImGui::EndTabItem();
			}
			// ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
		ImGui::End();

		if (state.data_form_active) {

			ImGui::Begin("Datatable", &state.data_form_active);
			// Editable table for DataPoint entries
			static bool data_dirty = false;
			static std::string save_message;

			ImGui::Text("Edit existing points (changes are in-memory until you Save)");
			ImGui::Separator();

			// Paging controls for large point sets
			static int page_size = 100;
			static int page_index = 0;
			// Track per-record changes: pair<op, recordid> where op is "modify" or "delete"
			static std::vector<std::pair<std::string, int>> change_list;
			// Prepare datatablepoints by applying header filters and sorting
			if (state.datatable_filters.size() < 15) state.datatable_filters.resize(15);
			// Cache and only rebuild datatable index map when filters, sort, or data change
			static std::vector<size_t> datatable_index_map;
			static std::vector<std::string> prev_filters;
			static std::string prev_sort_col;
			static int prev_sort_order = -1;
			static size_t prev_points_size = 0;
			static bool prev_data_dirty = true;
			auto matches = [&](const DataPoint &p) {
				// Column 0: id
				if (!state.datatable_filters[0].empty()) {
					if (std::to_string(p.id).find(state.datatable_filters[0]) == std::string::npos) return false;
				}
				// 1: time_info
				if (!state.datatable_filters[1].empty()) {
					if (p.time_info.find(state.datatable_filters[1]) == std::string::npos) return false;
				}
				// 2: server
				if (!state.datatable_filters[2].empty()) {
					if (p.server.find(state.datatable_filters[2]) == std::string::npos) return false;
				}
				// 3,4,5: x,y,z
				if (!state.datatable_filters[3].empty()) { if (std::to_string(p.x).find(state.datatable_filters[3]) == std::string::npos) return false; }
				if (!state.datatable_filters[4].empty()) { if (std::to_string(p.y).find(state.datatable_filters[4]) == std::string::npos) return false; }
				if (!state.datatable_filters[5].empty()) { if (std::to_string(p.z).find(state.datatable_filters[5]) == std::string::npos) return false; }
				// 6: planet
				if (!state.datatable_filters[6].empty()) { if (p.planet.find(state.datatable_filters[6]) == std::string::npos) return false; }
				// 7: poi_type
				if (!state.datatable_filters[7].empty()) { if (std::string(poi_type_name(p.poi_type)).find(state.datatable_filters[7]) == std::string::npos) return false; }
				// 8: subtype
				if (!state.datatable_filters[8].empty()) { if (std::string(poi_subtype_name(p.subtype)).find(state.datatable_filters[8]) == std::string::npos) return false; }
				// 9: material
				if (!state.datatable_filters[9].empty()) { if (p.material.find(state.datatable_filters[9]) == std::string::npos) return false; }
				// 10,11: qmin/qmax
				if (!state.datatable_filters[10].empty()) { if (std::to_string(p.quality_min).find(state.datatable_filters[10]) == std::string::npos) return false; }
				if (!state.datatable_filters[11].empty()) { if (std::to_string(p.quality_max).find(state.datatable_filters[11]) == std::string::npos) return false; }
				// 12: note
				if (!state.datatable_filters[12].empty()) { if (p.note.find(state.datatable_filters[12]) == std::string::npos) return false; }
				// 13: qt_persistent
				if (!state.datatable_filters[13].empty()) { if (std::string(p.qt_persistent ? "1" : "0").find(state.datatable_filters[13]) == std::string::npos) return false; }
				return true;
			};
			// detect changes
			if (prev_filters.size() != state.datatable_filters.size()) prev_filters.resize(state.datatable_filters.size());
			bool filters_changed = false;
			for (size_t i = 0; i < state.datatable_filters.size(); ++i) {
				if (prev_filters[i] != state.datatable_filters[i]) { filters_changed = true; break; }
			}
			bool sort_changed = (prev_sort_col != state.datatable_sort_column) || (prev_sort_order != state.datatable_sort_order);
			bool points_changed = (prev_points_size != state.points.size()) || (data_dirty != prev_data_dirty);

			if (filters_changed || sort_changed || points_changed) {
				datatable_index_map.clear();
				datatable_index_map.reserve(state.points.size());
				for (size_t i = 0; i < state.points.size(); ++i) {
					if (matches(state.points[i])) datatable_index_map.push_back(i);
				}

				// Sorting index map based on selected column
				if (!state.datatable_sort_column.empty() && state.datatable_sort_order != 0) {
					bool asc = (state.datatable_sort_order == 1);
					const std::string &c = state.datatable_sort_column;
					std::stable_sort(datatable_index_map.begin(), datatable_index_map.end(), [&](size_t ai, size_t bi) {
						const DataPoint &a = state.points[ai];
						const DataPoint &b = state.points[bi];
						if (c == "id") return asc ? a.id < b.id : a.id > b.id;
						if (c == "time") return asc ? a.time_info < b.time_info : a.time_info > b.time_info;
						if (c == "server") return asc ? a.server < b.server : a.server > b.server;
						if (c == "x") return asc ? a.x < b.x : a.x > b.x;
						if (c == "y") return asc ? a.y < b.y : a.y > b.y;
						if (c == "z") return asc ? a.z < b.z : a.z > b.z;
						if (c == "planet") return asc ? a.planet < b.planet : a.planet > b.planet;
						if (c == "material") return asc ? a.material < b.material : a.material > b.material;
						if (c == "qmin") return asc ? a.quality_min < b.quality_min : a.quality_min > b.quality_min;
						if (c == "qmax") return asc ? a.quality_max < b.quality_max : a.quality_max > b.quality_max;
						return asc ? a.id < b.id : a.id > b.id;
					});
				} else {
					// default sort by id ascending
					std::stable_sort(datatable_index_map.begin(), datatable_index_map.end(), [&](size_t ai, size_t bi){ return state.points[ai].id < state.points[bi].id; });
				}

				// update cached state
				prev_filters = state.datatable_filters;
				prev_sort_col = state.datatable_sort_column;
				prev_sort_order = state.datatable_sort_order;
				prev_points_size = state.points.size();
				prev_data_dirty = data_dirty;
			}

			size_t total_items = datatable_index_map.size();
			size_t total_pages = page_size > 0 ? (total_items + static_cast<size_t>(page_size) - 1) / static_cast<size_t>(page_size) : 1;
			if (page_size <= 0) page_size = 1;

			ImGui::PushID("datatable_paging");
			ImGui::Text("Page size:"); ImGui::SameLine();
			if (ImGui::InputInt("##page_size", &page_size)) {
				if (page_size <= 0) page_size = 1;
				total_pages = (total_items + static_cast<size_t>(page_size) - 1) / static_cast<size_t>(page_size);
				if (page_index >= static_cast<int>(total_pages) && total_pages > 0) page_index = static_cast<int>(total_pages) - 1;
			}
			ImGui::SameLine();
			if (ImGui::Button("Prev")) {
				if (page_index > 0) --page_index;
			}
			ImGui::SameLine();
			if (ImGui::Button("Next")) {
				if (page_index < static_cast<int>(std::max<size_t>(1, total_pages)) - 1) ++page_index;
			}
			ImGui::SameLine();
			// Page jump input (1-based)
			int page_input = page_index + 1;
			ImGui::PushItemWidth(80);
			if (ImGui::InputInt("##page_input", &page_input)) {
				if (page_input < 1) page_input = 1;
				if (total_pages > 0 && static_cast<size_t>(page_input) > total_pages) page_input = static_cast<int>(total_pages);
				page_index = page_input - 1;
			}
			ImGui::PopItemWidth();
			ImGui::SameLine();
			ImGui::Text("/ %zu", total_pages == 0 ? 1 : total_pages);
			ImGui::PopID();

			// helper lambdas for marking changes
			auto mark_modified = [&](int id) {
				if (id <= 0) return;
				for (const auto &c : change_list) {
					if (c.second == id && c.first == "delete") return; // deleted takes precedence
				}
				for (const auto &c : change_list) {
					if (c.second == id && c.first == "modify") return; // already marked
				}
				change_list.emplace_back("modify", id);
			};
			auto mark_deleted = [&](int id) {
				if (id <= 0) return;
				// remove any pending modify entries for this id
				change_list.erase(std::remove_if(change_list.begin(), change_list.end(), [&](const std::pair<std::string,int>& p) { return p.second == id && p.first == "modify"; }), change_list.end());
				for (const auto &c : change_list) {
					if (c.second == id && c.first == "delete") return; // already marked
				}
				change_list.emplace_back("delete", id);
			};

			ImGuiTableFlags table_flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY;
			// Use a distinct table ID to avoid legacy table-layout mismatches across builds
			if (ImGui::BeginTable("DataPointsTable_v3", 15, table_flags, ImVec2(0, ImGui::GetContentRegionAvail().y - 30))) {
				ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 30.0f); // 0
				ImGui::TableSetupColumn("Record Time", ImGuiTableColumnFlags_WidthFixed, 100.0f); // 1
				ImGui::TableSetupColumn("Server"); // 2
				ImGui::TableSetupColumn("X"); // 3
				ImGui::TableSetupColumn("Y"); // 4
				ImGui::TableSetupColumn("Z"); // 5
				ImGui::TableSetupColumn("Zone"); // 6
				ImGui::TableSetupColumn("POI Type", ImGuiTableColumnFlags_WidthFixed, 100.0f); // 7
				ImGui::TableSetupColumn("Subtype", ImGuiTableColumnFlags_WidthFixed, 160.0f); // 8
				ImGui::TableSetupColumn("Resource"); // 9
				ImGui::TableSetupColumn("QMin"); // 10
				ImGui::TableSetupColumn("QMax"); // 11
				ImGui::TableSetupColumn("Note"); // 12
				ImGui::TableSetupColumn("QT Persist", ImGuiTableColumnFlags_WidthFixed, 80.0f); // 13
				ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthFixed, 80.0f); //14
				ImGui::TableSetupScrollFreeze(0, 2);
				ImGui::TableHeadersRow();

				// Second header row: filter inputs and clickable sort buttons
				// Style the filter row to visually separate it from data rows and make inputs compact
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.10f, 0.10f, 0.10f, 1.0f));
				ImGui::TableNextRow();
				for (int col = 0; col < 15; ++col) {
					ImGui::TableSetColumnIndex(col);
					// Determine display name and key
					if (col == 14) continue;
					const char* disp = "";
					std::string key;
					switch (col) {
						case 0: disp = "ID"; key = "id"; break;
						case 1: disp = "Record Time"; key = "time"; break;
						case 2: disp = "Server"; key = "server"; break;
						case 3: disp = "X"; key = "x"; break;
						case 4: disp = "Y"; key = "y"; break;
						case 5: disp = "Z"; key = "z"; break;
						case 6: disp = "Zone"; key = "zone"; break;
						case 7: disp = "POI Type"; key = "poi_type"; break;
						case 8: disp = "Subtype"; key = "subtype"; break;
						case 9: disp = "Resource"; key = "material"; break;
						case 10: disp = "QMin"; key = "qmin"; break;
						case 11: disp = "QMax"; key = "qmax"; break;
						case 12: disp = "Note"; key = "note"; break;
						case 13: disp = "QT"; key = "qt"; break;
						case 14: disp = "Control"; key = "control"; break;
					}
					// show sort symbol if active (use ASCII arrows for compatibility)
					std::string btn_label = disp;
					if (state.datatable_sort_column == key) {
						if (state.datatable_sort_order == 1) btn_label += " ^";
						else if (state.datatable_sort_order == 2) btn_label += " v";
					}
					std::string btn_id = btn_label + std::string("##hdr") + std::to_string(col);
					// color active sort button green
					if (state.datatable_sort_column == key) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.6f, 0.0f, 1.0f));
					if (ImGui::Button(btn_id.c_str())) {
						if (state.datatable_sort_column == key) {
							// cycle none -> asc -> desc -> none
							state.datatable_sort_order = (state.datatable_sort_order + 1) % 3;
							if (state.datatable_sort_order == 0) state.datatable_sort_column.clear();
						} else {
							state.datatable_sort_column = key;
							state.datatable_sort_order = 1;
						}
					}
					if (state.datatable_sort_column == key) ImGui::PopStyleColor();
					// filter input below button
					// Compact per-column widths for filter inputs
					int iw = 100;
					switch (col) {
						case 0: iw = 60; break; // ID
						case 1: iw = 160; break; // Record Time
						case 2: iw = 100; break; // Server
						case 3: case 4: case 5: iw = 80; break; // X,Y,Z
						case 6: iw = 120; break; // Planet
						case 7: case 8: iw = 100; break; // POI Type, Subtype
						case 9: iw = 120; break; // Resource
						case 10: case 11: iw = 80; break; // QMin/QMax
						case 12: iw = 160; break; // Note
						case 13: iw = 80; break; // QT
						case 14: iw = 80; break; // Control
						default: iw = 100; break;
					}
					ImGui::PushItemWidth(static_cast<float>(iw));
					std::string f_lbl = std::string("##filter") + std::to_string(col);
					// For POI Type, Subtype and Resource use dropdowns
					if (col == 1) {
					}
					else if (col == 7) {
						auto names = poi_type_names();
						std::vector<const char*> cstrs;
						cstrs.reserve(names.size() + 1);
						cstrs.push_back("Any");
						for (const auto &s : names) cstrs.push_back(s.c_str());
						int sel = 0; // 0 == Any
						if (state.datatable_filters.size() > 7 && !state.datatable_filters[7].empty()) {
							for (size_t si = 0; si < names.size(); ++si) if (names[si] == state.datatable_filters[7]) { sel = static_cast<int>(si + 1); break; }
						}
						if (ImGui::Combo(f_lbl.c_str(), &sel, cstrs.data(), static_cast<int>(cstrs.size()))) {
							if (sel == 0) state.datatable_filters[7].clear(); else state.datatable_filters[7] = names[sel - 1];
						}
					} else if (col == 8) {
						std::vector<const char*> cstrs;
						cstrs.reserve(poi_impl::poi_subtype_count + 1);
						cstrs.push_back("Any");
						for (size_t si = 0; si < poi_impl::poi_subtype_count; ++si) cstrs.push_back(poi_impl::poi_subtype_names_arr[si]);
						int sel = 0;
						if (state.datatable_filters.size() > 8 && !state.datatable_filters[8].empty()) {
							for (size_t si = 0; si < poi_impl::poi_subtype_count; ++si) if (state.datatable_filters[8] == poi_impl::poi_subtype_names_arr[si]) { sel = static_cast<int>(si + 1); break; }
						}
						if (ImGui::Combo(f_lbl.c_str(), &sel, cstrs.data(), static_cast<int>(cstrs.size()))) {
							if (sel == 0) state.datatable_filters[8].clear(); else state.datatable_filters[8] = poi_impl::poi_subtype_names_arr[sel - 1];
						}
					} else if (col == 9) {
						std::vector<const char*> cstrs;
						cstrs.reserve(state.materials.size() + 1);
						cstrs.push_back("Any");
						for (const auto &s : state.materials) cstrs.push_back(s.c_str());
						int sel = 0;
						if (state.datatable_filters.size() > 9 && !state.datatable_filters[9].empty()) {
							for (size_t si = 0; si < state.materials.size(); ++si) if (state.datatable_filters[9] == state.materials[si]) { sel = static_cast<int>(si + 1); break; }
						}
						if (ImGui::Combo(f_lbl.c_str(), &sel, cstrs.data(), static_cast<int>(cstrs.size()))) {
							if (sel == 0) state.datatable_filters[9].clear(); else state.datatable_filters[9] = state.materials[sel - 1];
						}
					} else {
						char fbuf[128] = {0};
						if (state.datatable_filters.size() > static_cast<size_t>(col)) strncpy(fbuf, state.datatable_filters[col].c_str(), sizeof(fbuf)-1);
						if (ImGui::InputText(f_lbl.c_str(), fbuf, sizeof(fbuf))) {
							state.datatable_filters[col] = fbuf;
						}
					}
					ImGui::PopItemWidth();
				}
				ImGui::PopStyleColor(2);

				std::vector<size_t> to_erase;

				// Rebuild index map after potential filter changes in header inputs
				datatable_index_map.clear();
				for (size_t i = 0; i < state.points.size(); ++i) {
					if (matches(state.points[i])) datatable_index_map.push_back(i);
				}
				if (!state.datatable_sort_column.empty() && state.datatable_sort_order != 0) {
					bool asc = (state.datatable_sort_order == 1);
					const std::string &c = state.datatable_sort_column;
					std::stable_sort(datatable_index_map.begin(), datatable_index_map.end(), [&](size_t ai, size_t bi) {
						const DataPoint &a = state.points[ai];
						const DataPoint &b = state.points[bi];
						if (c == "id") return asc ? a.id < b.id : a.id > b.id;
						if (c == "time") return asc ? a.time_info < b.time_info : a.time_info > b.time_info;
						if (c == "server") return asc ? a.server < b.server : a.server > b.server;
						if (c == "x") return asc ? a.x < b.x : a.x > b.x;
						if (c == "y") return asc ? a.y < b.y : a.y > b.y;
						if (c == "z") return asc ? a.z < b.z : a.z > b.z;
						if (c == "planet") return asc ? a.planet < b.planet : a.planet > b.planet;
						if (c == "material") return asc ? a.material < b.material : a.material > b.material;
						if (c == "qmin") return asc ? a.quality_min < b.quality_min : a.quality_min > b.quality_min;
						if (c == "qmax") return asc ? a.quality_max < b.quality_max : a.quality_max > b.quality_max;
						return asc ? a.id < b.id : a.id > b.id;
					});
				} else {
					std::stable_sort(datatable_index_map.begin(), datatable_index_map.end(), [&](size_t ai, size_t bi){ return state.points[ai].id < state.points[bi].id; });
				}
				// Compute visible range based on current page (use datatable index map)
				size_t start = static_cast<size_t>(page_index) * static_cast<size_t>(page_size);
				if (start >= datatable_index_map.size()) start = 0;
				size_t end_idx = ((start + static_cast<size_t>(page_size)) < datatable_index_map.size()) ? (start + static_cast<size_t>(page_size)) : datatable_index_map.size();
				for (size_t local = start; local < end_idx; ++local) {
					size_t idx = datatable_index_map[local];
					DataPoint& dp = state.points[idx];
					ImGui::TableNextRow();

					// ID (read-only)
					ImGui::TableSetColumnIndex(0);
					ImGui::TextDisabled("%d", dp.id);

					// Record Time (read-only, formatted from time_info)
					ImGui::TableSetColumnIndex(1);
					if (dp.time_info.empty()) {
						ImGui::TextDisabled("N/A");
					} else {
						ImGui::Text("%s", dp.time_info.c_str());
					}

					// Server
					ImGui::TableSetColumnIndex(2);
					{
						char buf[16] = { 0 };
						strncpy(buf, dp.server.c_str(), sizeof(buf) - 1);
						std::string lbl = std::string("##server") + std::to_string(idx);
						ImGui::PushItemWidth(-FLT_MIN);
						if (ImGui::InputText(lbl.c_str(), buf, IM_ARRAYSIZE(buf))) {
							dp.server = buf;
							data_dirty = true;
							mark_modified(dp.id);
						}
						ImGui::PopItemWidth();
					}
					// X
					ImGui::TableSetColumnIndex(3);
					{
						double val = dp.x;
						std::string lbl = std::string("##x") + std::to_string(idx);
						ImGui::PushItemWidth(-FLT_MIN);
						if (ImGui::InputDouble(lbl.c_str(), &val, 0.0, 0.0, "%.6f")) {
							dp.x = val;
							data_dirty = true;
							mark_modified(dp.id);
						}
						ImGui::PopItemWidth();
					}

					// Y
					ImGui::TableSetColumnIndex(4);
					{
						double val = dp.y;
						std::string lbl = std::string("##y") + std::to_string(idx);
						ImGui::PushItemWidth(-FLT_MIN);
						if (ImGui::InputDouble(lbl.c_str(), &val, 0.0, 0.0, "%.6f")) {
							dp.y = val;
							data_dirty = true;
							mark_modified(dp.id);
						}
						ImGui::PopItemWidth();
					}

					// Z
					ImGui::TableSetColumnIndex(5);
					{
						double val = dp.z;
						std::string lbl = std::string("##z") + std::to_string(idx);
						ImGui::PushItemWidth(-FLT_MIN);
						if (ImGui::InputDouble(lbl.c_str(), &val, 0.0, 0.0, "%.6f")) {
							dp.z = val;
							data_dirty = true;
							mark_modified(dp.id);
						}
						ImGui::PopItemWidth();
					}

					// Planet (combo)
					ImGui::TableSetColumnIndex(6);
					{
						std::string lbl = std::string("##planet") + std::to_string(idx);
						auto it = std::find(state.planets.begin(), state.planets.end(), dp.planet);
						int cur = it != state.planets.end() ? static_cast<int>(std::distance(state.planets.begin(), it)) : 0;
						std::vector<const char*> cstrs;
						cstrs.reserve(state.planets.size());
						for (const auto& s : state.planets) cstrs.push_back(s.c_str());
						ImGui::PushItemWidth(-FLT_MIN);
						if (ImGui::Combo(lbl.c_str(), &cur, cstrs.data(), static_cast<int>(cstrs.size()))) {
							dp.planet = state.planets[static_cast<size_t>(cur)];
							data_dirty = true;
							mark_modified(dp.id);
						}
						ImGui::PopItemWidth();
					}
					// POI Type (enum-backed combo)
					ImGui::TableSetColumnIndex(7);
					{
						std::string lbl = std::string("##poi_type") + std::to_string(idx);
						int cur = static_cast<int>(dp.poi_type);
						auto names = poi_type_names();
						std::vector<const char*> cstrs;
						cstrs.reserve(names.size());
						for (const auto& s : names) cstrs.push_back(s.c_str());
						ImGui::PushItemWidth(-FLT_MIN);
						if (ImGui::Combo(lbl.c_str(), &cur, cstrs.data(), static_cast<int>(cstrs.size()))) {
							dp.poi_type = static_cast<PoiType>(cur);
							data_dirty = true;
							mark_modified(dp.id);
						}
						ImGui::PopItemWidth();
					}

					// Subtype (enum-backed combo)
					ImGui::TableSetColumnIndex(8);
					{
						std::string lbl = std::string("##subtype") + std::to_string(idx);
						int cur = static_cast<int>(dp.subtype);
						std::vector<const char*> cstrs;
						cstrs.reserve(poi_impl::poi_subtype_count);
						for (size_t si = 0; si < poi_impl::poi_subtype_count; ++si) cstrs.push_back(poi_impl::poi_subtype_names_arr[si]);
						ImGui::PushItemWidth(-FLT_MIN);
						if (ImGui::Combo(lbl.c_str(), &cur, cstrs.data(), static_cast<int>(cstrs.size()))) {
							dp.subtype = static_cast<PoiSubType>(cur);
							data_dirty = true;
							mark_modified(dp.id);
						}
						ImGui::PopItemWidth();
					}

					// Resource (combo)
					ImGui::TableSetColumnIndex(9);
					{
						std::string lbl = std::string("##material") + std::to_string(idx);
						auto it = std::find(state.materials.begin(), state.materials.end(), dp.material);
						int cur = it != state.materials.end() ? static_cast<int>(std::distance(state.materials.begin(), it)) : 0;
						std::vector<const char*> cstrs;
						cstrs.reserve(state.materials.size());
						for (const auto& s : state.materials) cstrs.push_back(s.c_str());
						ImGui::PushItemWidth(-FLT_MIN);
						if (ImGui::Combo(lbl.c_str(), &cur, cstrs.data(), static_cast<int>(cstrs.size()))) {
							dp.material = state.materials[static_cast<size_t>(cur)];
							data_dirty = true;
							mark_modified(dp.id);
						}
						ImGui::PopItemWidth();
					}


					// QMin
					ImGui::TableSetColumnIndex(10);
					{
						int val = int(dp.quality_min);
						std::string lbl = std::string("##qmin") + std::to_string(idx);
						ImGui::PushItemWidth(-FLT_MIN);
						if (ImGui::InputInt(lbl.c_str(), &val, 0, 0)) {
							dp.quality_min = val;
							data_dirty = true;
							mark_modified(dp.id);
						}
						ImGui::PopItemWidth();
					}

					// QMax
					ImGui::TableSetColumnIndex(11);
					{
						int val = int(dp.quality_max);
						std::string lbl = std::string("##qmax") + std::to_string(idx);
						ImGui::PushItemWidth(-FLT_MIN);
						if (ImGui::InputInt(lbl.c_str(), &val, 0, 0)) {
							dp.quality_max = val;
							data_dirty = true;
							mark_modified(dp.id);
						}
						ImGui::PopItemWidth();
					}

					// Note
					ImGui::TableSetColumnIndex(12);
					{
						char buf[256] = { 0 };
						strncpy(buf, dp.note.c_str(), sizeof(buf) - 1);
						std::string lbl = std::string("##note") + std::to_string(idx);
						ImGui::PushItemWidth(-FLT_MIN);
						if (ImGui::InputText(lbl.c_str(), buf, IM_ARRAYSIZE(buf))) {
							dp.note = buf;
							data_dirty = true;
							mark_modified(dp.id);
						}
						ImGui::PopItemWidth();
					}

					// QT persistent checkbox
					ImGui::TableSetColumnIndex(13);
					{
						std::string lbl = std::string("##qt_persistent") + std::to_string(idx);
						bool val = dp.qt_persistent;
						if (ImGui::Checkbox(lbl.c_str(), &val)) {
							dp.qt_persistent = val;
							data_dirty = true;
							mark_modified(dp.id);
						}
					}

					// Controls (Delete)
					ImGui::TableSetColumnIndex(14);
					{
						std::string del_lbl = std::string("Delete##del") + std::to_string(idx);
						if (ImGui::SmallButton(del_lbl.c_str())) {
							mark_deleted(dp.id);
							to_erase.push_back(idx);
						}
					}
				}

				// erase rows in reverse order
				if (!to_erase.empty()) {
					std::sort(to_erase.rbegin(), to_erase.rend());
					for (size_t idx : to_erase) {
						if (idx < state.points.size()) {
							state.points.erase(state.points.begin() + static_cast<std::ptrdiff_t>(idx));
							data_dirty = true;
						}
					}

					// Rebuild datatable index map after removals
					datatable_index_map.clear();
					for (size_t i = 0; i < state.points.size(); ++i) {
						if (matches(state.points[i])) datatable_index_map.push_back(i);
					}
				}

				// After potential removals, ensure the current page index is valid
				total_items = datatable_index_map.size();
				total_pages = page_size > 0 ? (total_items + static_cast<size_t>(page_size) - 1) / static_cast<size_t>(page_size) : 1;
				if (total_pages == 0) {
					page_index = 0;
				} else if (page_index >= static_cast<int>(total_pages)) {
					page_index = static_cast<int>(total_pages) - 1;
				}

				ImGui::EndTable();
			}

			ImGui::Separator();
			bool save_enabled = !change_list.empty();
			if (!save_enabled) ImGui::BeginDisabled();
			if (ImGui::Button("Save Changes")) {			
				bool ok = true;
				if (state.store) {
					// If we have a store and a change list, apply incremental changes where possible
					if (!change_list.empty()) {
						// Try to apply per-change operations against SqliteStore when available
						for (const auto &c : change_list) {
							const std::string &op = c.first;
							int rid = c.second;
							if (op == "delete") {
								if (!state.store->delete_point_by_id(rid)) {
									ok = false;
								}
							} else if (op == "modify") {
								// locate datapoint in memory
								auto it = std::find_if(state.points.begin(), state.points.end(), [&](const DataPoint &p) { return p.id == rid; });
								if (it != state.points.end()) {
									int res = state.store->uuid_insert_or_update(*it, nullptr);
									if (res == 0) ok = false;
								} else {
									// record not present locally anymore; nothing to update
								}
							}
						} 
					}
					// } else {
					// 	// no per-record changes tracked -> fallback to full overwrite
					// 	ok = state.store->overwrite_points(state.points);
					// }
				} else {
					ok = write_points_csv((state.repo_root / "data" / "geoscout.csv"), state.points);
				}

				if (ok) {
					save_message = "Saved successfully";
					// Clear tracked changes and reload
					change_list.clear();
					state.reload_planet_data();
					state.filter_points();
					data_dirty = false;
				} else {
					save_message = "Save failed";
				}
			}
			if (!save_enabled) ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Reload Data")) {
				state.reload_planet_data();
				state.filter_points();
				save_message = "Reloaded";
				data_dirty = false;
			}
			ImGui::SameLine();
			ImGui::Text("%s", save_message.c_str());
			ImGui::End();
		}


		if (state.planets_form_active) {
			ImGui::Begin("Planets Editor", &state.planets_form_active);
			static bool planets_dirty = false;
			static std::string planets_save_message;

			static int new_planet_id = 0;
			static char new_sys[64] = "";
			static char new_name[128] = "";
			static char new_img[128] = "";
			static char new_zone[64] = "";
			static bool new_has_asteroid_belt = false;
			static double new_planet_radius = 0.0;
			static double new_karman_line = 100.0;
			static double new_belt_inner = 0.0;
			static double new_belt_outer = 0.0;
			static double new_belt_thickness = 0.0;
			static int new_zone_type = zone_type_to_int(ZoneType::CelestialBody);
			const char* zone_items_new[] = { zone_type_name(ZoneType::CelestialBody), zone_type_name(ZoneType::AsteroidField), zone_type_name(ZoneType::Solar) };

			int spacers = new_has_asteroid_belt?8:7;
			ImGui::Text("Edit planet catalog (changes are in-memory until you Save)");
			ImGui::Separator();

			ImGuiTableFlags table_flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY;
			// added one more column for Zone Type selection and Belt editing
			if (ImGui::BeginTable("PlanetsTable_v1", 8, table_flags, ImVec2(0, ImGui::GetContentRegionAvail().y - 165))) {
				ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 50.0f);
				ImGui::TableSetupColumn("System");
				ImGui::TableSetupColumn("Planet");
				ImGui::TableSetupColumn("Image Dir");
				ImGui::TableSetupColumn("Zone ID");
				ImGui::TableSetupColumn("Zone Type");
				ImGui::TableSetupColumn("Belt");
				ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableHeadersRow();

				std::vector<size_t> to_erase;
				static std::unordered_map<size_t, std::array<int, 4>> bbox_edits;
				static std::unordered_map<size_t, std::array<double, 5>> belt_edits; // planet_radius, karman_line, inner, outer, thickness
				for (size_t i = 0; i < state.planet_catalog.size(); ++i) {
					Planet& pl = state.planet_catalog[i];
					ImGui::TableNextRow();

					// ID (read-only)
					ImGui::TableSetColumnIndex(0);
					ImGui::TextDisabled("%d", pl.id);

					// System
					ImGui::TableSetColumnIndex(1);
					char sysbuf[64] = { 0 };
					strncpy(sysbuf, pl.system.c_str(), sizeof(sysbuf) - 1);
					std::string lbl_sys = std::string("##system") + std::to_string(i);
					ImGui::PushItemWidth(-FLT_MIN);
					if (ImGui::InputText(lbl_sys.c_str(), sysbuf, IM_ARRAYSIZE(sysbuf))) {
						pl.system = sysbuf;
						planets_dirty = true;
					}
					ImGui::PopItemWidth();

					// Planet name
					ImGui::TableSetColumnIndex(2);
					char namebuf[128] = { 0 };
					strncpy(namebuf, pl.name.c_str(), sizeof(namebuf) - 1);
					std::string lbl_name = std::string("##planetname") + std::to_string(i);
					ImGui::PushItemWidth(-FLT_MIN);
					if (ImGui::InputText(lbl_name.c_str(), namebuf, IM_ARRAYSIZE(namebuf))) {
						pl.name = namebuf;
						planets_dirty = true;
					}
					ImGui::PopItemWidth();

					// Image Dir
					ImGui::TableSetColumnIndex(3);
					char imgbuf[128] = { 0 };
					strncpy(imgbuf, pl.image_dir.c_str(), sizeof(imgbuf) - 1);
					std::string lbl_img = std::string("##imgdir") + std::to_string(i);
					ImGui::PushItemWidth(-FLT_MIN);
					if (ImGui::InputText(lbl_img.c_str(), imgbuf, IM_ARRAYSIZE(imgbuf))) {
						pl.image_dir = imgbuf;
						planets_dirty = true;
					}
					ImGui::PopItemWidth();

					// Zone ID
					ImGui::TableSetColumnIndex(4);
					char zonebuf[64] = { 0 };
					strncpy(zonebuf, pl.zone_id.c_str(), sizeof(zonebuf) - 1);
					std::string lbl_zone = std::string("##zone") + std::to_string(i);
					ImGui::PushItemWidth(-FLT_MIN);
					if (ImGui::InputText(lbl_zone.c_str(), zonebuf, IM_ARRAYSIZE(zonebuf))) {
						pl.zone_id = zonebuf;
						planets_dirty = true;
					}
					ImGui::PopItemWidth();

					// Zone Type
					ImGui::TableSetColumnIndex(5);
					int zt_idx = zone_type_to_int(pl.zone_type);
					const char* zone_items[] = { zone_type_name(ZoneType::CelestialBody), zone_type_name(ZoneType::AsteroidField), zone_type_name(ZoneType::Solar) };
					std::string lbl_zt = std::string("##zonetype") + std::to_string(i);
					ImGui::PushItemWidth(-FLT_MIN);
					if (ImGui::Combo(lbl_zt.c_str(), &zt_idx, zone_items, IM_ARRAYSIZE(zone_items))) {
						ZoneType newzt;
						if (zone_type_from_int(zt_idx, newzt)) {
							pl.zone_type = newzt;
							planets_dirty = true;
						}
					}
					ImGui::PopItemWidth();

					// Belt controls (checkbox + edit popup)
					ImGui::TableSetColumnIndex(6);
					// Checkbox for whether this zone has an asteroid belt
					if (pl.zone_type == ZoneType::AsteroidField) {
						ImGui::TextDisabled("Asteroid Field");
					} else {
						if (ImGui::Checkbox((std::string("##hasbelt") + std::to_string(i)).c_str(), &pl.has_asteroid_belt)) {
							planets_dirty = true;
						}
						
						ImGui::SameLine();
						// Edit Planet button
						std::string edit_belt_btn = std::string("Edit Planet##belt") + std::to_string(i);
						if (ImGui::SmallButton(edit_belt_btn.c_str())) {
							belt_edits[i] = { pl.planet_radius_km, pl.karman_line_km, pl.belt_inner_radius_km, pl.belt_outer_radius_km, pl.belt_thickness_km };
							std::string popup_name = std::string("Edit Planet##beltpopup") + std::to_string(i);
							ImGui::OpenPopup(popup_name.c_str());
					}	
					}
					// Delete button moved to next column
					ImGui::TableSetColumnIndex(7);
					if (pl.zone_type == ZoneType::AsteroidField || pl.has_asteroid_belt) {
						std::string bbox_btn = std::string("BBox##bbox") + std::to_string(i);
						if (ImGui::SmallButton(bbox_btn.c_str())) {
							bbox_edits[i] = { static_cast<int>(pl.bounding_box_km.min_x), static_cast<int>(pl.bounding_box_km.max_x), static_cast<int>(pl.bounding_box_km.min_y), static_cast<int>(pl.bounding_box_km.max_y) };
							std::string popup_name = std::string("Edit BBox##popup") + std::to_string(i);
							ImGui::OpenPopup(popup_name.c_str());
						}
						ImGui::SameLine();
					}
					std::string del_lbl = std::string("Delete##pdel") + std::to_string(i);
					if (ImGui::SmallButton(del_lbl.c_str())) {
						to_erase.push_back(i);
					}

					// Popup modal for editing bbox
					std::string popup_name = std::string("Edit BBox##popup") + std::to_string(i);
					if (ImGui::BeginPopupModal(popup_name.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
						auto it = bbox_edits.find(i);
						if (it != bbox_edits.end()) {
							auto& vals = it->second;
							ImGui::InputInt((std::string("Min X (km)##minx") + std::to_string(i)).c_str(), &vals[0]);
							ImGui::InputInt((std::string("Max X (km)##maxx") + std::to_string(i)).c_str(), &vals[1]);
							ImGui::InputInt((std::string("Min Y (km)##miny") + std::to_string(i)).c_str(), &vals[2]);
							ImGui::InputInt((std::string("Max Y (km)##maxy") + std::to_string(i)).c_str(), &vals[3]);
							if (ImGui::Button((std::string("Save##bboxsave") + std::to_string(i)).c_str())) {
								pl.bounding_box_km = { static_cast<double>(vals[0]), static_cast<double>(vals[1]), static_cast<double>(vals[2]), static_cast<double>(vals[3]) };
								planets_dirty = true;
								bbox_edits.erase(it);
								ImGui::CloseCurrentPopup();
							}
							ImGui::SameLine();
							if (ImGui::Button((std::string("Cancel##bboxcancel") + std::to_string(i)).c_str())) {
								bbox_edits.erase(it);
								ImGui::CloseCurrentPopup();
							}
						} else {
							ImGui::Text("No bbox data available");
							if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
						}
						ImGui::EndPopup();
					}

					// Popup modal for editing belt params
					std::string belt_popup = std::string("Edit Planet##beltpopup") + std::to_string(i);
					if (ImGui::BeginPopupModal(belt_popup.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
						auto itb = belt_edits.find(i);
						if (itb != belt_edits.end()) {
							auto& bvals = itb->second;
							ImGui::InputDouble((std::string("Planet Radius (km)##pr") + std::to_string(i)).c_str(), &bvals[0]);
							ImGui::InputDouble((std::string("Karman Line (km)##kl") + std::to_string(i)).c_str(), &bvals[1]);
							ImGui::InputDouble((std::string("Belt Inner (km)##bi") + std::to_string(i)).c_str(), &bvals[2]);
							ImGui::InputDouble((std::string("Belt Outer (km)##bo") + std::to_string(i)).c_str(), &bvals[3]);
							ImGui::InputDouble((std::string("Belt Thickness (km)##bt") + std::to_string(i)).c_str(), &bvals[4]);
							if (ImGui::Button((std::string("Save##beltsave") + std::to_string(i)).c_str())) {
								pl.planet_radius_km = bvals[0];
								pl.karman_line_km = bvals[1];
								pl.belt_inner_radius_km = bvals[2];
								pl.belt_outer_radius_km = bvals[3];
								pl.belt_thickness_km = bvals[4];
								planets_dirty = true;
								belt_edits.erase(itb);
								ImGui::CloseCurrentPopup();
							}
							ImGui::SameLine();
							if (ImGui::Button((std::string("Cancel##beltcancel") + std::to_string(i)).c_str())) {
								belt_edits.erase(itb);
								ImGui::CloseCurrentPopup();
							}
						} else {
							ImGui::Text("No belt data available");
							if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
						}
						ImGui::EndPopup();
					}
				}

				if (!to_erase.empty()) {
					std::sort(to_erase.rbegin(), to_erase.rend());
					for (size_t idx : to_erase) {
						if (idx < state.planet_catalog.size()) {
							state.planet_catalog.erase(state.planet_catalog.begin() + static_cast<std::ptrdiff_t>(idx));
							planets_dirty = true;
						}
					}
				}

				ImGui::EndTable();
			}

			ImGui::Separator();
			ImGui::Text("Add new planet:");
			
			
			if (new_planet_id == 0) {
				int maxid = -1;
				for (const auto& p : state.planet_catalog) maxid = max(maxid, p.id);
				new_planet_id = maxid + 1;
			}
			// ImGui::InputInt("ID##newplanet", &new_planet_id);
			ImGui::PushItemWidth(100);
			ImGui::InputText("System##newplanet", new_sys, IM_ARRAYSIZE(new_sys));ImGui::SameLine();
			ImGui::PushItemWidth(100);
			ImGui::InputText("Planet##newplanet", new_name, IM_ARRAYSIZE(new_name));ImGui::SameLine();
			ImGui::PushItemWidth(100);
			ImGui::Combo("Zone Type##newplanet", &new_zone_type, zone_items_new, IM_ARRAYSIZE(zone_items_new));ImGui::SameLine();
			ImGui::PushItemWidth(100);
			ImGui::InputText("Zone ID##newplanet", new_zone, IM_ARRAYSIZE(new_zone));
			ImGui::PushItemWidth(300);
			ImGui::InputText("Image Dir##newplanet", new_img, IM_ARRAYSIZE(new_img)); 
			
			static int new_min_x = -300;
			static int new_max_x = 300;
			static int new_min_y = -300;
			static int new_max_y = 300;
			if (static_cast<ZoneType>(new_zone_type) == ZoneType::CelestialBody) {
				ImGui::Checkbox("Has Asteroid Belt##newbelt", &new_has_asteroid_belt);
			}
			if (new_has_asteroid_belt || static_cast<ZoneType>(new_zone_type) == ZoneType::AsteroidField) {
				ImGui::PushItemWidth(100);
				ImGui::SameLine();ImGui::InputDouble("Planet Radius (km)##new_pr", &new_planet_radius);ImGui::SameLine();
				ImGui::PushItemWidth(100);
				ImGui::InputDouble("Karman Line (km)##new_kl", &new_karman_line);ImGui::SameLine();
				ImGui::PushItemWidth(100);
				ImGui::InputDouble("Belt Inner (km)##new_bi", &new_belt_inner);ImGui::SameLine();
				ImGui::PushItemWidth(100);
				ImGui::InputDouble("Belt Outer (km)##new_bo", &new_belt_outer);ImGui::SameLine();
				ImGui::PushItemWidth(100);
				ImGui::InputDouble("Belt Thickness (km)##new_bt", &new_belt_thickness);
			}
			if (static_cast<ZoneType>(new_zone_type) == ZoneType::AsteroidField || new_has_asteroid_belt) {
				ImGui::Text("Bounding Box (km):"); ImGui::SameLine();
				ImGui::PushItemWidth(100);
				ImGui::InputInt("Min X (km)##new_minx", &new_min_x); ImGui::SameLine();
				ImGui::PushItemWidth(100);
				ImGui::InputInt("Max X (km)##new_maxx", &new_max_x); ImGui::SameLine();
				ImGui::PushItemWidth(100);
				ImGui::InputInt("Min Y (km)##new_miny", &new_min_y); ImGui::SameLine();
				ImGui::PushItemWidth(100);
				ImGui::InputInt("Max Y (km)##new_maxy", &new_max_y);
			}

			if (ImGui::Button("Add Planet")) {
				Planet p{ new_planet_id, std::string(new_sys), std::string(new_name), std::string(new_img), std::string(new_zone) };
				p.zone_type = static_cast<ZoneType>(new_zone_type);
				p.has_asteroid_belt = new_has_asteroid_belt;
				p.planet_radius_km = new_planet_radius;
				p.karman_line_km = new_karman_line;
				p.belt_inner_radius_km = new_belt_inner;
				p.belt_outer_radius_km = new_belt_outer;
				p.belt_thickness_km = new_belt_thickness;
				if (p.zone_type == ZoneType::AsteroidField) {
					p.bounding_box_km = { static_cast<double>(new_min_x), static_cast<double>(new_max_x), static_cast<double>(new_min_y), static_cast<double>(new_max_y) };
				}
				state.planet_catalog.push_back(p);
				planets_dirty = true;
				// reset new fields
				new_planet_id = 0;
				new_sys[0] = '\0'; new_name[0] = '\0'; new_img[0] = '\0'; new_zone[0] = '\0';
				new_zone_type = zone_type_to_int(ZoneType::CelestialBody);
				new_min_x = -300; new_max_x = 300; new_min_y = -300; new_max_y = 300;
				new_has_asteroid_belt = false;
				new_planet_radius = 0.0; new_karman_line = 100.0; new_belt_inner = 0.0; new_belt_outer = 0.0; new_belt_thickness = 0.0;
			}

			ImGui::Separator();
			if (ImGui::Button("Save Changes")) {
				bool ok = false;
				if (state.store) {
					auto sqlite_backend = dynamic_cast<SqliteStore*>(state.store.get());
					if (sqlite_backend) {
						ok = sqlite_backend->overwrite_planets(state.planet_catalog);
					} else {
						ok = write_planets_csv(std::filesystem::path(state.repo_root / "data" / "planets.csv"), state.planet_catalog);
					}
				} else {
					ok = write_planets_csv(std::filesystem::path(state.repo_root / "data" / "planets.csv"), state.planet_catalog);
				}

				if (ok) {
					planets_save_message = "Saved successfully";
					state.reload_planet_catalog();
					planets_dirty = false;
				} else {
					planets_save_message = "Save failed";
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Reload From CSV")) {
				state.reload_planet_catalog();
				planets_save_message = "Reloaded";
				planets_dirty = false;
			}
			ImGui::SameLine();
			ImGui::Text("%s", planets_save_message.c_str());
			ImGui::End();
		}



		timer_display.render_time_ms().stamp();

		int width = 0;
		int height = 0;
		glfwGetFramebufferSize(window, &width, &height);
		glViewport(0, 0, width, height);
		glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		const GLuint texture = state.get_texture_for_selected_planet();
		double cursor_x = 0.0;
		double cursor_y = 0.0;
		glfwGetCursorPos(window, &cursor_x, &cursor_y);
		std::optional<std::pair<float, float>> mouse_pos;
		if (width > 0 && height > 0) {
			mouse_pos = {
				static_cast<float>((cursor_x / static_cast<double>(width)) * 2.0 - 1.0),
				static_cast<float>(-(((cursor_y / static_cast<double>(height)) * 2.0) - 1.0))
			};
		}

		state.hovered_text.reset();
		if (texture != 0) {
			const Planet* selected_zone = nullptr;
			for (const auto& z : state.planet_catalog) {
				if (z.name == state.selected_planet) {
					selected_zone = &z; break;
				}
			}


			state.hovered_text = renderer.render_map(texture, state.filtered_points, mouse_pos, state.material_catalog, selected_zone, state.display_mode, state.grid_spacing, state.camera2d, &state.highlighted_materials);
			// Render travel log overlay (if available) so users can see their tracked path
			if (state.travel_log) {
				const auto track = state.travel_log->get_tracked_points_copy();
				if (!track.empty()) {
					renderer.render_track(state.display_mode, track, selected_zone, state.grid_spacing, state.camera2d);
				}
			}
			const auto toggle_now = std::chrono::steady_clock::now();
			if (std::chrono::duration<double>(toggle_now - last_toggle).count() > 0.25) {
				last_toggle = toggle_now;
				location_on = !location_on;
			}
			if (location_on) {
				if (state.selected_planet_obj != nullptr) {
					if (state.selected_planet_obj->zone_type == ZoneType::AsteroidField) {
						// For asteroid fields, use the center of the field as the location marker
						const double x = state.new_data.x;
						const double y = state.new_data.y;
						const bbox2d box = state.selected_planet_obj->bounding_box_km;
						const double x_min = box.min_x;
						const double x_max = box.max_x;
						const double y_min = box.min_y;
						const double y_max = box.max_y;
						double u = 0.0;
						double v = 0.0;
						if (x_max > x_min && y_max > y_min) {
							u = (x - x_min) / (x_max - x_min);
							v = (y - y_min) / (y_max - y_min);
							const float px = (u * 2.0f) - 1.0f;
							const float py = (v * 2.0f) - 1.0f;
							// apply global map pan/zoom
							const auto t = state.camera2d.applyToNdc(px, py);
							renderer.render_marker(t.first, t.second, 1.0f, 1.0f, 0.0f, 0.9f, 6.0f);
						} 
						
					} else {
						const auto lat_lon_alt = state.new_data.to_lat_lon_alt();
						const auto [u, v] = latlon_to_uv(lat_lon_alt[0], lat_lon_alt[1]);
						const float px = (u * 2.0f) - 1.0f;
						const float py = (v * 2.0f) - 1.0f;
						const auto t = state.camera2d.applyToNdc(px, py);
						renderer.render_marker(t.first, t.second, 1.0f, 1.0f, 0.0f, 0.9f, 6.0f);
					}
				}
			}
		}

		// Small legend / controls overlay (top-left)
		{
			ImGui::SetNextWindowBgAlpha(0.55f);
			ImGui::Begin("Map Legend", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar);
			ImGui::Text("Zoom: %.2fx", state.camera2d.getZoom());
			if (ImGui::Button("+")) state.camera2d.zoomBy(1.25);
			ImGui::SameLine();
			if (ImGui::Button("-")) state.camera2d.zoomBy(1.0/1.25);
			ImGui::SameLine();
			if (ImGui::Button("Reset")) { state.camera2d.setZoom(1.0); state.camera2d.setPan({0.0f,0.0f}); }

			ImGui::Separator();
			ImGui::Text("Pan:");
			if (ImGui::Button("Up")) state.camera2d.panBy(0.0f, 0.05f);
			ImGui::SameLine(); if (ImGui::Button("Down")) state.camera2d.panBy(0.0f, -0.05f);
			ImGui::SameLine(); if (ImGui::Button("Left")) state.camera2d.panBy(-0.05f, 0.0f);
			ImGui::SameLine(); if (ImGui::Button("Right")) state.camera2d.panBy(0.05f, 0.0f);

			ImGui::Separator();
			ImGui::Text("Legend (materials)");
			// Collect materials visible on map
			std::unordered_map<std::string,int> counts;
			for (const auto &p : state.filtered_points) {
				counts[p.material]++;
			}
			for (const auto &m : state.material_catalog) {
				const auto it = counts.find(m.name);
				if (it == counts.end()) continue;
				bool highlighted = state.highlighted_materials.count(m.name) > 0;
				if (ImGui::Checkbox((m.name + " (" + std::to_string(it->second) + ")").c_str(), &highlighted)) {
					if (highlighted) state.highlighted_materials.insert(m.name);
					else state.highlighted_materials.erase(m.name);
				}
			}
			ImGui::End();
		}

		if (state.hovered_text) {
			ImVec2 mouse = ImGui::GetMousePos();
			if (mouse.x + 200.0f > width) {
				mouse.x -= 130.0f;
			}
			if (mouse.y + 50.0f > height) {
				mouse.y -= 60.0f;
			}

			ImGui::SetNextWindowPos(ImVec2(mouse.x + 10.0f, mouse.y + 10.0f));
			ImGui::SetNextWindowBgAlpha(0.7f);
			ImGui::Begin("##hover_tooltip", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
			ImGui::TextUnformatted(state.hovered_text->c_str());
			ImGui::End();
		}

		// Coordinate display (bottom-right). Shows Lat/Lon for planetary zones,
		// or X/Y (km) for asteroid fields. If the mouse is in the bottom-right
		// 25% x 25% sector, move the widget to the top-right to avoid overlap.
		if (mouse_pos) {
			const auto [mx, my] = *mouse_pos; // NDC coords in [-1,1]
			std::string coords_str;
			const Planet* sel_zone = nullptr;
			for (const auto& z : state.planet_catalog) {
				if (z.name == state.selected_planet) { sel_zone = &z; break; }
			}
			if (sel_zone && (state.display_mode == DisplayMode::Asteroid_Field || state.display_mode == DisplayMode::Celestial_Belt)) {
				const bbox2d box = sel_zone->bounding_box_km;
				double minx = box.min_x;
				double maxx = box.max_x;
				double miny = box.min_y;
				double maxy = box.max_y;
				double cx = 0.0, cy = 0.0;
				double half_w = 0.0, half_h = 0.0;
				if (maxx > minx) {
					cx = (minx + maxx) * 0.5;
					half_w = (maxx - minx) * 0.5;
				}
				if (maxy > miny) {
					cy = (miny + maxy) * 0.5;
					half_h = (maxy - miny) * 0.5;
				}
				if (half_w <= 0.0) {
					half_w = max(1.0, static_cast<double>(state.settings.grid_spacing_km) * 3.0);
					cx = 0.0;
				}
				if (half_h <= 0.0) {
					half_h = max(1.0, static_cast<double>(state.settings.grid_spacing_km) * 3.0);
					cy = 0.0;
				}
				double world_x = mx * half_w + cx;
				double world_y = my * half_h + cy;
				std::ostringstream ss;
				ss << std::fixed << std::setprecision(2) << "X: " << world_x << " km  Y: " << world_y << " km";
				coords_str = ss.str();
			} else {
				// Planetary / Solar: interpret NDC as UV and invert to lat/lon
				double u = (mx + 1.0) * 0.5;
				double v = (my + 1.0) * 0.5;
				double lon = u * 360.0 - 180.0;
				double lat = v * 180.0 - 90.0;
				std::ostringstream ss;
				ss << std::fixed << std::setprecision(4) << "Lat: " << lat << "  Lon: " << lon;
				coords_str = ss.str();
			}

			if (!coords_str.empty()) {
				// Determine widget position
				const float widget_w = 220.0f;
				const float widget_h = 0.0f; // autosize
				float pos_x = static_cast<float>(width) - widget_w - 10.0f;
				float pos_y = static_cast<float>(height) - 40.0f - 10.0f;
				// If cursor is in the bottom-right 25% x 25% sector, move to top-right
				if (cursor_x > static_cast<double>(width) * 0.75 && cursor_y > static_cast<double>(height) * 0.75) {
					pos_y = 10.0f;
				}

				ImGui::SetNextWindowPos(ImVec2(pos_x, pos_y));
				ImGui::SetNextWindowBgAlpha(0.5f);
				ImGui::Begin("##coords_display", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
				ImGui::TextUnformatted(coords_str.c_str());
				ImGui::End();
			}
		}

		timer_display.render_time_ms().record_time_since_stamp();

		timer_display.loop_time_ms().record_time_since_stamp();
		timer_display.work_time_ms().record_time_since_stamp();

		if (state.settings.show_timings) {
			timer_footer_renderer.render(timer_display, height);
		}

		glDisable(GL_BLEND);
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		glfwSwapBuffers(window);

		auto last_now = std::chrono::steady_clock::now();

		next_tick += std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(kFrameTime));
		if (last_now > next_tick + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(kFrameTime))) {
			auto missed_frames = std::chrono::duration<double>(last_now - next_tick).count() / kFrameTime;
			// std::cout << "Warning: Missed " << missed_frames << " frames\n";
			next_tick = missed_frames > 5.0 ? last_now : next_tick + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(kFrameTime * std::ceil(missed_frames)));
		}

	}
	// Signal the OCR thread to stop and wait for it to finish before exiting
	ocr_instance.stop_and_wait();


	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	glfwDestroyWindow(window);
	glfwTerminate();


	state.finalize();
	

	ocr_timer.join();

	return 0;
}

bool write_starmap_json(std::string& starmap_json_path)
{
	bool result = true;
	auto r = cpr::Get(cpr::Url{ "https://starmap.space/api/v3/pois/index.php" });
	if (r.status_code == 200) {
		// write to starmap_json
		std::ofstream ofs(starmap_json_path);
		ofs << r.text;
		ofs.close();
	} else {
		std::cerr << "Failed to fetch starmap data: HTTP " << r.status_code << "\n";
		result = false;
	}
	return result;
}

void popup_filter(std::string& filtertext, std::string& local_selected, const std::vector<std::string>& items, const std::string& app_selected_item, std::function<void(const std::string&)> on_select)
{
	char buf[128] = { 0 };
	strncpy(buf, filtertext.c_str(), sizeof(buf) - 1);
	if (ImGui::IsWindowAppearing()) {
		filtertext = "";
		local_selected = app_selected_item;
		ImGui::SetKeyboardFocusHere();
	}
	if (ImGui::InputText("Filter", buf, IM_ARRAYSIZE(buf), ImGuiInputTextFlags_AutoSelectAll)) {
		filtertext = buf;
	}

	std::vector<std::string> filtered_items;
	filtered_items.reserve(items.size());
	for (const auto& m : items) {
		if (contains_case_insensitive(m, filtertext))
			filtered_items.push_back(m);
	}

	// Local selection state (do not mutate the app's selected item until commit)
	int sel_index = -1;
	for (size_t i = 0; i < filtered_items.size(); ++i) {
		if (filtered_items[i] == local_selected) {
			sel_index = static_cast<int>(i);
			break;
		}
	}
	if (sel_index == -1 && !filtered_items.empty()) {
		sel_index = 0;
		local_selected = filtered_items[0];
	}

	// Keyboard navigation: Up/Down to move selection, Enter to accept
	const bool nav_up = ImGui::IsKeyPressed(ImGuiKey_UpArrow, true);
	const bool nav_down = ImGui::IsKeyPressed(ImGuiKey_DownArrow, true);
	const bool nav_enter = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
	if (!filtered_items.empty()) {
		if (nav_up) {
			if (sel_index > 0) {
				--sel_index;
				local_selected = filtered_items[sel_index];
			}
		} else if (nav_down) {
			if (sel_index < static_cast<int>(filtered_items.size()) - 1) {
				++sel_index;
				local_selected = filtered_items[sel_index];
			}
		}
		if (nav_enter && sel_index >= 0) {
			on_select(filtered_items[sel_index]);
			ImGui::CloseCurrentPopup();
		}
	}

	ImGui::Separator();
	if (filtered_items.empty()) {
		ImGui::TextDisabled("No matches");
	} else {
		const int child_h = 8 * static_cast<int>(ImGui::GetTextLineHeightWithSpacing());
		ImGui::BeginChild("ResourceListChild", ImVec2(300, (float)child_h), true);
		bool any_selected = false;
		for (size_t i = 0; i < filtered_items.size(); ++i) {
			const bool sel = (filtered_items[i] == local_selected);
			if (ImGui::Selectable(filtered_items[i].c_str(), sel)) {
				on_select(filtered_items[i]);
				ImGui::CloseCurrentPopup();
				break;
			}
			if (sel) {
				any_selected = true;
			}
		}
		// If nothing is selected in the filtered list, default to first item so Enter has a target
		if (!any_selected && !filtered_items.empty()) {
			local_selected = filtered_items[0];
		}
		ImGui::EndChild();
	}

	ImGui::Separator();
	if (ImGui::Button("Close"))
		ImGui::CloseCurrentPopup();
	ImGui::EndPopup();
}
