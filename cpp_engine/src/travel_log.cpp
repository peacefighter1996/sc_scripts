#include "travel_log.h"

#include <cmath>
#include <chrono>
#include <array>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>
#include <regex>

// Helper: small matrix utilities
static inline double sqr(double x) { return x * x; }

TravelLog::TravelLog() = default;
TravelLog::~TravelLog() = default;

void TravelLog::configure(const Config& cfg) {
    std::lock_guard<std::mutex> lk(mutex_);
    cfg_ = cfg;
}


void TravelLog::set_repo_root(const std::filesystem::path& repo_root) {
    std::lock_guard<std::mutex> lk(mutex_);
    repo_root_ = repo_root;
}

void TravelLog::kf_reset(double x, double y, double z, double ts) {
    // state: [x,y,z,vx,vy,vz]
    kf_x_.fill(0.0);
    kf_x_[0] = x; kf_x_[1] = y; kf_x_[2] = z;
    // P: large initial uncertainty
    kf_P_.fill(0.0);
    for (int i = 0; i < 3; ++i) kf_P_[i * 6 + i] = 1.0; // pos var
    for (int i = 3; i < 6; ++i) kf_P_[i * 6 + i] = 1.0; // vel var
    // Q: process noise (small)
    kf_Q_.fill(0.0);
    const double pos_q = 1e-4; // km^2
    const double vel_q = 1e-6; // (km/s)^2
    for (int i = 0; i < 3; ++i) kf_Q_[i * 6 + i] = pos_q;
    for (int i = 3; i < 6; ++i) kf_Q_[i * 6 + i] = vel_q;
    // R: measurement noise (pos)
    kf_R_.fill(0.0);
    const double meas_var = 0.25; // km^2 (sigma ~0.5 km)
    kf_R_[0] = meas_var; kf_R_[4] = meas_var; kf_R_[8] = meas_var;

    last_kf_time_s_ = ts;
    kf_initialized_ = true;
}

void TravelLog::kf_predict(double ts) {
    if (!kf_initialized_) return;
    const double dt = ts - last_kf_time_s_;
    if (dt <= 0.0) return;

    // F*x
    std::array<double,6> xpred{};
    for (int i = 0; i < 3; ++i) xpred[i] = kf_x_[i] + kf_x_[i+3] * dt;
    for (int i = 3; i < 6; ++i) xpred[i] = kf_x_[i];

    // P = F P F^T + Q
    std::array<double,36> Pnew{};
    // Build F explicitly when multiplying
    // Pnew = F * P * F^T
    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < 6; ++j) {
            double sum = 0.0;
            for (int k = 0; k < 6; ++k) {
                double Fik = 0.0;
                if (k < 3) {
                    if (i == k) Fik = 1.0;
                    if (i == k && k < 3) {
                    }
                }
                // compute Fik
                if (i < 3 && k == i) Fik = 1.0;
                if (i < 3 && k == i + 3) Fik = dt;
                if (i >= 3 && k == i) Fik = 1.0;

                for (int l = 0; l < 6; ++l) {
                    double Fjl = 0.0;
                    if (j < 3 && l == j) Fjl = 1.0;
                    if (j < 3 && l == j + 3) Fjl = dt;
                    if (j >= 3 && l == j) Fjl = 1.0;
                    sum += Fik * kf_P_[k * 6 + l] * Fjl;
                }
            }
            Pnew[i * 6 + j] = sum;
        }
    }

    // Add Q
    for (int i = 0; i < 36; ++i) Pnew[i] += kf_Q_[i];

    kf_x_ = xpred;
    kf_P_ = Pnew;
    last_kf_time_s_ = ts;
}

bool TravelLog::invert_3x3(const double m[9], double out[9]) const {
    const double a00 = m[0], a01 = m[1], a02 = m[2];
    const double a10 = m[3], a11 = m[4], a12 = m[5];
    const double a20 = m[6], a21 = m[7], a22 = m[8];
    const double det = a00*(a11*a22 - a12*a21) - a01*(a10*a22 - a12*a20) + a02*(a10*a21 - a11*a20);
    if (std::abs(det) < 1e-12) return false;
    const double invdet = 1.0 / det;
    out[0] =  (a11*a22 - a12*a21) * invdet;
    out[1] = -(a01*a22 - a02*a21) * invdet;
    out[2] =  (a01*a12 - a02*a11) * invdet;
    out[3] = -(a10*a22 - a12*a20) * invdet;
    out[4] =  (a00*a22 - a02*a20) * invdet;
    out[5] = -(a00*a12 - a02*a10) * invdet;
    out[6] =  (a10*a21 - a11*a20) * invdet;
    out[7] = -(a00*a21 - a01*a20) * invdet;
    out[8] =  (a00*a11 - a01*a10) * invdet;
    return true;
}

bool TravelLog::kf_update(double x, double y, double z) {
    if (!kf_initialized_) return false;
    // Measurement residual y = z - Hx (H = [I3 0])
    double resid[3];
    for (int i = 0; i < 3; ++i) resid[i] = (i==0?x:(i==1?y:z)) - kf_x_[i];
    const double resid_norm = std::sqrt(resid[0]*resid[0] + resid[1]*resid[1] + resid[2]*resid[2]);
    // Gate: if residual too large (convert cfg_.qt_threshold meters -> km)
    const double gate_km = cfg_.qt_threshold / 1000.0;
    if (resid_norm > gate_km) {
        // immediate lock on large jump
        if (last_exceed_ == false){
            qt_exceed_start_ = std::chrono::steady_clock::now();
            last_exceed_ = true;
        }

        if (qt_exceed_start_.time_since_epoch().count() > 0) {
            const auto now = std::chrono::steady_clock::now();
            const double exceed_duration_s = std::chrono::duration_cast<std::chrono::duration<double>>(now - qt_exceed_start_).count();
            if (exceed_duration_s >= cfg_.qt_disable_duration_s) {
                locked_ = true;
                active_ = false;
            }
        }     
        
        return false;
    }
    else {
        last_exceed_ = false;
    }

    // S = H P H^T + R -> S is top-left 3x3 of P plus R
    double S[9] = {0};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            S[i*3 + j] = kf_P_[i*6 + j] + kf_R_[i*3 + j];
        }
    }
    double Sinv[9];
    if (!invert_3x3(S, Sinv)) return false;

    // K = P * H^T * Sinv -> P[:,0..2] * Sinv (6x3)
    double K[18] = {0};
    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < 3; ++j) {
            double sum = 0.0;
            for (int k = 0; k < 3; ++k) {
                sum += kf_P_[i*6 + k] * Sinv[k*3 + j];
            }
            K[i*3 + j] = sum;
        }
    }

    // x = x + K * resid
    for (int i = 0; i < 6; ++i) {
        double sum = 0.0;
        for (int j = 0; j < 3; ++j) sum += K[i*3 + j] * resid[j];
        kf_x_[i] += sum;
    }

    // P = (I - K H) P. Compute (I - K H)
    double IminusKH[36];
    for (int i = 0; i < 6; ++i) for (int j = 0; j < 6; ++j) IminusKH[i*6 + j] = (i==j)?1.0:0.0;
    // KH is 6x6 but KH = K * H where H is [I3 0], so KH[i,j] = K[i, j] for j<3, else 0
    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < 3; ++j) {
            IminusKH[i*6 + j] -= K[i*3 + j];
        }
    }

    // newP = (I-KH) * P
    std::array<double,36> newP{};
    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < 6; ++j) {
            double sum = 0.0;
            for (int k = 0; k < 6; ++k) sum += IminusKH[i*6 + k] * kf_P_[k*6 + j];
            newP[i*6 + j] = sum;
        }
    }
    kf_P_ = newP;
    return true;
}

void TravelLog::start(const std::string& zone_name, ZoneType zone_type, std::string server) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (server.empty()) return; // server should be set via set_server before start, to ensure thread safety. This is a safeguard against misuse.
    zone_name_ = zone_name;
    zone_type_ = zone_type;
    server_ = server;
	zone_center_km_ = { 0.0, 0.0, 0.0 }; // Reset zone center; can be set externally if needed  
	travel_log_file_name.clear();
    
    // Default fresh start
    active_ = true;
    locked_ = false;
    tracked_points_.clear();
    next_id_ = 1;
    start_time_s_ = 0.0;
    last_time_s_ = 0.0;
    last_speed_mps_ = 0.0;
    last_pos_ = { 0.0, 0.0, 0.0 };
    qt_exceed_start_ = std::chrono::steady_clock::time_point();
    kf_initialized_ = false;
    last_kf_time_s_ = 0.0;
}

void TravelLog::restart() {
    std::lock_guard<std::mutex> lk(mutex_);
	// continue from current position but reset tracking state (e.g., after QT lock or zone transition)
    
    next_id_ = 1;
    start_time_s_ = 0.0;
    last_time_s_ = 0.0;
    last_speed_mps_ = 0.0;
	last_pos_ = { 0.0, 0.0, 0.0 };
    qt_exceed_start_ = std::chrono::steady_clock::time_point();
    kf_initialized_ = false;
    last_kf_time_s_ = 0.0;
	locked_ = false;
    active_ = true;
}

// Persist current log to JSON and push small metadata record to store (no blob).
void TravelLog::stop() {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!active_) return;

    if (!tracked_points_.empty()) {
        // Save to a fixed filename so restart can resume/override instead of creating duplicates
        std::filesystem::path out_dir = repo_root_ / "data" / "travel_logs" / zone_name_;
        std::error_code ec;
        std::filesystem::create_directories(out_dir, ec);
        const std::filesystem::path full_path = out_dir / travel_log_file_name;
        persist_locked(full_path);
    }

    active_ = false;
}

bool TravelLog::is_active() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return active_;
}

bool TravelLog::is_locked_due_to_qt() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return locked_;
}
bool TravelLog::feed_measurement(const Vector3& pos, const double timestamp_s) {
    return feed_measurement(pos.x, pos.y, pos.z, timestamp_s);
}
bool TravelLog::feed_measurement(const double x, const double y, const double z, const double timestamp_s) {
    const Vector3 pos{ x, y, z };
    std::lock_guard<std::mutex> lk(mutex_);
    if (!active_ || locked_) return false;

    const double now_s = timestamp_s;
    // Initialize Kalman if needed
    if (!kf_initialized_ || (now_s - last_kf_time_s_) > cfg_.kalman_max_life_s) {
        kf_reset(x, y, z, now_s);
        // For first sample, optionally add if passes core-distance gating
        double core_dist = 0.0;
        if (zone_type_ == ZoneType::CelestialBody) {
			core_dist = lenght(pos - zone_center_km_);
        }

        if (travel_log_file_name.empty()) {
            travel_log_file_name = ("travel_log_" + iso_time_from_epoch_s(now_s) + ".json");
			// replace : with _ for file system safety
			travel_log_file_name = std::regex_replace(travel_log_file_name, std::regex(":"), "_");
		}

        if (tracked_points_.empty()) {
            if (zone_type_ != ZoneType::CelestialBody || core_dist >= cfg_.min_core_distance_km) {
                DataPoint p{};
                p.id = next_id_++;
                p.coord.x = kf_x_[0]; p.coord.y = kf_x_[1]; p.coord.z = kf_x_[2];
                p.time_info = iso_time_from_epoch_s(now_s);
                tracked_points_.push_back(p);
				last_point_ = &tracked_points_.back();
                start_time_s_ = now_s;
                last_time_s_ = now_s;
                last_pos_ = p.coord;
                last_speed_mps_ = 0.0;
                return true;
            }
            // if inside core zone, do not add but keep filter initialized
            last_time_s_ = now_s;
            //last_x_ = x; last_y_ = y; last_z_ = z;
			last_pos_ = pos;
            return false;
        }
    }

    // Predict and update Kalman
    kf_predict(now_s);
    // before update, check residual gating inside kf_update (locks if necessary)
    if (!kf_update(x, y, z)) {
        // either locked or update failed
        // If we were locked due to QT gating, auto-persist the current track so restart can resume
        if (locked_) {
            if (!repo_root_.empty()) {
                std::filesystem::path out_dir = repo_root_ / "data" / "travel_logs" / zone_name_;
                std::error_code ec;
                std::filesystem::create_directories(out_dir, ec);
                const std::filesystem::path full_path = out_dir / travel_log_file_name;
                // persist while we still hold the mutex
                persist_locked(full_path);
            }
        }
        return false;
    }

    // Use filtered position for progression
	const Vector3 filtered_pos{ kf_x_[0], kf_x_[1], kf_x_[2] };

    const Vector3 filtered_distance = filtered_pos - last_pos_;
	const double dist_km = lenght(filtered_distance);

	const DataPoint& last_point = tracked_points_.empty() ? DataPoint{} : *last_point_;

	const Vector3 last_delta = filtered_pos - last_point.coord;
    const double dist_km_lastnode = lenght(last_delta);

    // Compute implied speed from filter velocity
    //const double vx = kf_x_[3], vy = kf_x_[4], vz = kf_x_[5];
    const Vector3 velocity_kmps{ kf_x_[3], kf_x_[4], kf_x_[5] };
    const double speed_mps = lenght(velocity_kmps) * 1000.0; // km/s -> m/s


	bool reject = false;
    // Enforce core-distance gating for celestial bodies
    if (zone_type_ == ZoneType::CelestialBody) {
		const Vector3 c = filtered_pos - zone_center_km_;
		const double core_dist = lenght(c);
        if (core_dist < cfg_.min_core_distance_km) {
            // do not add points inside core exclusion
            reject = true;
        }
    }

    // Motion gating: reject if speed too high
    if (speed_mps > cfg_.max_speed_mps) {
        reject = true;
    }

	// minimum distance for new point from last node
    if (dist_km_lastnode <= cfg_.distance_threshold_km) {
		reject = true;
    }

    if (reject) {
        last_speed_mps_ = speed_mps;
        last_time_s_ = now_s;
        last_pos_ = filtered_pos;
        last_kf_time_s_ = now_s;
        return false;
    }

    DataPoint p{};
    p.id = next_id_++;
	p.coord = filtered_pos;
    p.time_info = iso_time_from_epoch_s(now_s);
    tracked_points_.push_back(p);
    last_point_ = &tracked_points_.back();
    last_speed_mps_ = speed_mps;
    last_time_s_ = now_s;
    last_pos_ = filtered_pos;
    last_kf_time_s_ = now_s;
    return true;
    // Update last state for future calculations
    
}

const std::vector<DataPoint>& TravelLog::get_tracked_points() const {
    return tracked_points_;
}

std::vector<DataPoint> TravelLog::get_tracked_points_copy() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return tracked_points_;
}

std::string TravelLog::iso_time_from_epoch_s(double epoch_s) const {
    std::time_t t = static_cast<std::time_t>(std::floor(epoch_s));
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

// Minimal ISO time parser (expects UTC 'Z' suffix) returning epoch seconds
static double parse_iso_time_to_epoch_s(const std::string& iso) {
    if (iso.empty()) return 0.0;
    std::tm tm{};
    std::istringstream ss(iso);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    if (ss.fail()) return 0.0;
#ifdef _WIN32
    time_t t = _mkgmtime(&tm);
#else
    time_t t = timegm(&tm);
#endif
    if (t == -1) return 0.0;
    return static_cast<double>(t);
}

static inline std::string trim_str(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && std::isspace((unsigned char)s[a])) ++a;
    size_t b = s.size();
    while (b > a && std::isspace((unsigned char)s[b-1])) --b;
    return s.substr(a, b - a);
}

void TravelLog::persist_locked(const std::filesystem::path& full_path) {
    // assumes mutex_ is held by caller
    std::error_code ec;
    std::filesystem::create_directories(full_path.parent_path(), ec);
    if (ec) return; // fail early if directory couldn't be created
    
    std::ofstream out(full_path, std::ios::trunc);
    if (!out.is_open()) return;

    out << "{\n";
    out << "  \"zone\": \"" << zone_name_ << "\",\n";
    std::string start_iso = tracked_points_.empty() ? std::string() : tracked_points_.front().time_info;
    std::string end_iso = tracked_points_.empty() ? std::string() : tracked_points_.back().time_info;
	out << "  \"server\": \"" << server_ << "\",\n";
    out << "  \"start\": \"" << start_iso << "\",\n";
    out << "  \"end\": \"" << end_iso << "\",\n";
    out << "  \"point_count\": " << tracked_points_.size() << ",\n";
    out << "  \"points\": [\n";
    for (size_t i = 0; i < tracked_points_.size(); ++i) {
        const auto& p = tracked_points_[i];
        out << "    { \"x\": " << p.coord.x << ", \"y\": " << p.coord.y << ", \"z\": " << p.coord.z << ", \"time\": \"" << p.time_info << "\" }";
        if (i + 1 < tracked_points_.size()) out << ",\n"; else out << "\n";
    }
    out << "  ]\n";
    out << "}\n";
    out.close();

    // Store last saved path for resume
    last_saved_path_ = full_path;

    // Push small metadata record into store (without blob)
    if (store_) {
        ChangeEvent ev;
		ev.change_id = uuid::generate_uuid_v4();
        ev.node_id = "local";
        ev.created_ts = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        ev.op = "travel_log";
        ev.payload_json = "{\"zone\": \"" + zone_name_ + "\", \"file\": \"" + std::string(std::filesystem::relative(full_path, repo_root_).generic_string()) + "\", \"start\": \"" + start_iso + "\", \"end\": \"" + end_iso + "\", \"count\": " + std::to_string(tracked_points_.size()) + "}";
        ev.recordid.reset();
        ev.applied_ts.reset();
        store_->push_change_event(ev);
    }
}

bool TravelLog::load_from_file_locked(const std::filesystem::path& full_path) {
    // assumes mutex_ is held by caller
    std::ifstream in(full_path);
    if (!in.is_open()) return false;

    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    size_t pos = 0;
    size_t pstart = content.find("\"points\"");
    if (pstart == std::string::npos) return false;
    size_t arr = content.find('[', pstart);
    if (arr == std::string::npos) return false;
    pos = arr + 1;

    tracked_points_.clear();
    next_id_ = 1;

    while (true) {
        size_t obj_start = content.find('{', pos);
        if (obj_start == std::string::npos) break;
        size_t obj_end = content.find('}', obj_start);
        if (obj_end == std::string::npos) break;
        std::string obj = content.substr(obj_start, obj_end - obj_start + 1);

        auto parse_num = [&](const std::string& key, double& out)->bool {
            size_t kpos = obj.find('"' + key + '"');
            if (kpos == std::string::npos) return false;
            size_t colon = obj.find(':', kpos);
            if (colon == std::string::npos) return false;
            size_t vstart = colon + 1;
            while (vstart < obj.size() && std::isspace((unsigned char)obj[vstart])) ++vstart;
            size_t vend = vstart;
            while (vend < obj.size() && obj[vend] != ',' && obj[vend] != '}') ++vend;
            std::string token = trim_str(obj.substr(vstart, vend - vstart));
            try {
                out = std::stod(token);
                return true;
            }
            catch (...) {
                return false;
            }
        };

        double x = 0.0, y = 0.0, z = 0.0;
        std::string timestr;
        // parse time string if present
        size_t tpos = obj.find("\"time\"");
        if (tpos != std::string::npos) {
            size_t colon = obj.find(':', tpos);
            if (colon != std::string::npos) {
                size_t q1 = obj.find('"', colon + 1);
                if (q1 != std::string::npos) {
                    size_t q2 = obj.find('"', q1 + 1);
                    if (q2 != std::string::npos) {
                        timestr = obj.substr(q1 + 1, q2 - q1 - 1);
                    }
                }
            }
        }

        if (!parse_num("x", x)) { pos = obj_end + 1; continue; }
        if (!parse_num("y", y)) { pos = obj_end + 1; continue; }
        if (!parse_num("z", z)) { pos = obj_end + 1; continue; }

        DataPoint p{};
        p.id = next_id_++;
        p.coord.x = x; p.coord.y = y; p.coord.z = z;
        p.time_info = timestr;
        p.server = server_;
        p.planet = zone_name_;

        tracked_points_.push_back(p);

        pos = obj_end + 1;
    }

    if (!tracked_points_.empty()) {
        // set timing and last state from loaded data
        start_time_s_ = parse_iso_time_to_epoch_s(tracked_points_.front().time_info);
        last_time_s_ = parse_iso_time_to_epoch_s(tracked_points_.back().time_info);
        last_pos_ = tracked_points_.back().coord;
        last_speed_mps_ = 0.0;
    }

    last_saved_path_ = full_path;
    return true;
}
