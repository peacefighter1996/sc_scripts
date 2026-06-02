// Travel log subsystem (separate from main data storage)
#pragma once

#include <vector>
#include <array>
#include <mutex>
#include <memory>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "scout_core.h"
#include "point_store.h"

struct TravelLog {
    struct Config {
        double distance_threshold_km = 5.0;
        double max_speed_mps = 1500.0;
        double max_accel_mps2 = 30.0 * 9.80665;
        double kalman_max_life_s = 10.0;
        double qt_threshold = 100000.0;
        double qt_disable_duration_s = 3.0;
        double min_core_distance_km = 100.0;
    };

    TravelLog();
    ~TravelLog();

    void configure(const Config& cfg);
    // Optionally associate a repo root and store so logs can be persisted
    void set_repo_root(const std::filesystem::path& repo_root);
    // Start/stop the log for a specific zone name 
    // Provide zone type and center coordinates (in km) when starting
    // Optionally resume from last saved log for the same zone when available
    void start(const std::string& zone_name, ZoneType zone_type, std::string server);
	// Restart: restart the tracker and contine form current position.
    void restart();
    void stop();
    
    bool is_active() const;
    bool is_locked_due_to_qt() const;

    // Feed a raw measurement (x,y,z in kilometers, timestamp in seconds)
    // Returns true if a new tracked point was created.
    bool feed_measurement(double x, double y, double z, double timestamp_s);

    const std::vector<DataPoint>& get_tracked_points() const;
    // Return a thread-safe copy of tracked points for rendering/UI
    std::vector<DataPoint> get_tracked_points_copy() const;

private:
    std::string iso_time_from_epoch_s(double epoch_s) const;

    // Internal helpers that assume mutex_ is already held
    void persist_locked(const std::filesystem::path& full_path);
    bool load_from_file_locked(const std::filesystem::path& full_path);

    // Path to last saved travel log (if any)
    std::filesystem::path last_saved_path_;

    Config cfg_;
    bool active_ = false;
    bool locked_ = false;
    bool last_exceed_ = false;
	std::string travel_log_file_name = "travel_log.json";
    std::vector<DataPoint> tracked_points_;
	DataPoint* last_point_ = nullptr;
    mutable std::mutex mutex_;
    int next_id_ = 1;
    double start_time_s_ = 0.0;
    double last_time_s_ = 0.0;
    double last_x_ = 0.0, last_y_ = 0.0, last_z_ = 0.0;
    double last_speed_mps_ = 0.0;
    std::chrono::steady_clock::time_point qt_exceed_start_;
    std::filesystem::path repo_root_;
    IStore* store_ = nullptr;
    std::string server_;
    std::string zone_name_;
    ZoneType zone_type_ = ZoneType::CelestialBody;
    double zone_center_x_km_ = 0.0, zone_center_y_km_ = 0.0, zone_center_z_km_ = 0.0;

    // Simple Kalman filter for position+velocity (x,y,z,vx,vy,vz) in km and km/s
    bool kf_initialized_ = false;
    double last_kf_time_s_ = 0.0;
    std::array<double, 6> kf_x_{}; // state vector
    std::array<double, 36> kf_P_{}; // 6x6 covariance
    std::array<double, 36> kf_Q_{}; // 6x6 process noise
    std::array<double, 9> kf_R_{};  // 3x3 measurement noise (pos)
    void kf_reset(double x, double y, double z, double ts);
    void kf_predict(double ts);
    bool kf_update(double x, double y, double z);
    bool invert_3x3(const double m[9], double out[9]) const;
};
