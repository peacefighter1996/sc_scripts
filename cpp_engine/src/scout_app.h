#pragma once
#include "scout_ocr.h"
#include "scout_core.h"
#include "scout_render.h"
#include "scout_engine.h"
#include "point_store.h"
#include "sync_service.h"
#include "travel_log.h"
#include <string>
#include <vector>
#include <functional>
#include <data_table.h>
#include <glad/glad.h>
#include <filesystem>

static const int kMinQuality = 0;
static const int kMaxQuality = 1000;

int run_scout_app();

void GetDistanceAndText(const AppState &state, const DataPoint &wp, double &dist_km, std::string &wp_direction_text, bool include_velocity = true);

bool write_starmap_json(std::string &starmap_json_path);

void popup_filter(std::string& filtertext, std::string& local_selected, const std::vector<std::string>& items, const std::string& app_selected_item, std::function<void(const std::string&)> on_select);

struct NavInfoSettings {
	double default_nav_auto_advance_distance_km{ 10.0 };
	double mineral_nav_auto_advance_distance_km{ 2.0 };
	double qt_persistent_nav_auto_advance_distance_km{ 30.0 };
	
};


struct AppSettings {
	bool show_timings{ false };
	bool auto_update_ocr_newpoint_enabled{ true };
	bool ocr_feed_planet_update_enabled{ true };
	bool auto_export_session_minerals{ true };

	// Last used export file path (persisted to settings.ini)
	std::string last_export_path;
	// Last selected planet to restore on startup
	std::string last_selected_planet;
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
	double tracking_max_speed_mps{ 1500.0 }; //tracking max speed meters per second (mps) - default: 1500 m/s = 5400 km/h = 3354 mph
	double tracking_max_accel_mps2{ 30.0 * 9.80665 }; // tracking max acceleration meters per second squared (m/s^2) - default: 30g = 30 * 9.80665 m/s^2
	double kalman_max_life_s{ 10.0 }; // tracking Kalman filter max life in seconds (default: 10s)
	double qt_threshold{ 100000.0 };
	double qt_disable_duration_s{ 3.0 };
	double tracking_min_core_distance_km{ 100.0 };

	// Navigation UI settings
	NavInfoSettings route_nav_info_settings;
	
};


struct AppState {
	AppSettings settings;

	std::chrono::system_clock::time_point app_start_time;

	std::filesystem::path repo_root;
	std::filesystem::path onnx_model_path;
	std::filesystem::path label_map_path;
	std::filesystem::path planets_dir;

	DisplayMode display_mode;
	DataTable data_table;

	std::vector<DataPoint> points;
	// Points used specifically for the datatable view (after column filters and sorting)
	std::vector<DataPoint> filtered_points;
	std::vector<std::string> server_ids;
	std::vector<std::string> planets;
	std::vector<std::string> systems;
	std::vector<std::string> system_planets;
	std::vector<std::string> materials;

	std::vector<std::string> resource_filter_materials;
	std::vector<Planet> planet_catalog;
	std::vector<Resource> material_catalog;
	std::unique_ptr<IStore> store;
	std::unique_ptr<ISyncService> sync_service;
	ScoutOcr::SubscriptionId ocr_subscription_id;

	Vector3 position{ 0.0, 0.0, 0.0 };
	Vector3 velocity{ 0.0, 0.0, 0.0 };
	Vector3 VelocityHistory[5];
	double VelocityHistoryTime[5];
 	double grid_spacing{ 100.0 };
	double PositionUpdateTime{ 0.0 };
	double LastPostionUpdateTime{ 0.0 };

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
	// Focused point requested by UI "Show On Map" button
	DataPoint focus_point{};
	bool focus_on_map{ false };
	std::string loaded_texture;
	GLuint loaded_texture_id{ 0 };
	//std::unordered_map<std::string, GLuint> texture_cache;
	std::optional<std::string> hovered_text;

	// Map interaction/cameras
	// Camera2D camera2d;
	// Camera3D camera3d;
	// Highlighted materials (names)
	//std::unordered_set<std::string> highlighted_materials;

	bool data_form_active{ false };
	bool planets_form_active{ false };
	std::chrono::steady_clock::time_point last_location_toggle{ std::chrono::steady_clock::now() };

	// Travel Log (separate from the main data system)
	TravelLog travel_log;
	bool travel_log_active{ false };
	bool travel_log_disabled_due_to_qt{ false };

	// Navigation route: an ordered list of waypoints (DataPoint) for route following
	std::vector<DataPoint> nav_route;
	// Index of the current active waypoint within nav_route
	int nav_route_index{ 0 };
	// Whether the nav route is active and should be shown on the UI
	bool nav_route_active{ false };

	// OCR results pushed from worker thread are stored here for main-thread processing
	std::mutex ocr_mutex;
	std::vector<OcrResult> ocr_results;

	AppState();

	~AppState();

	void update_selected_planet(const std::string& new_planet);

	void set_display_mode(DisplayMode new_mode);

	std::vector<std::string> get_unique_systems(const std::vector<Planet>& planet_catalog);

	void update_grid_spacing();

	void reload_planet_data();

	void reload_planet_catalog();

	void filter_points() ;

	GLuint get_texture_for_selected_planet();

    AppSettings load_settings(const std::filesystem::path& path) ;
	void save_settings() ;
	void export_session_minerals();

	void finalize() ;

	std::vector<double> process_ocr_results();
	void update_new_data_from_ocr(const OcrResult& result);
};