#include "point_store_sqlite.h"

#include <sqlite3.h>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <random>
#include <chrono>
#include <iomanip>
#include <vector>
#include <cstring>

static bool exec_sql(sqlite3* db, const std::string& sql) {
    char* err = nullptr;
    const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        if (err) {
            std::cerr << "SQL error: " << err << "\n";
            sqlite3_free(err);
        }
        return false;
    }
    return true;
}

static int get_user_version(sqlite3* db) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }
    int v = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        v = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return v;
}

static bool set_user_version(sqlite3* db, int v) {
    std::stringstream ss;
    ss << "PRAGMA user_version = " << v << ";";
    return exec_sql(db, ss.str());
}

SqlitePointStore::SqlitePointStore(const std::string& db_path, const std::string& node_id)
    : db_path_(db_path), node_id_(node_id), db_handle_(nullptr) {}

SqlitePointStore::~SqlitePointStore() {
    if (db_handle_) {
        sqlite3_close(db_handle_);
        db_handle_ = nullptr;
    }
}

std::string SqlitePointStore::generate_uuid_v4() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist;
    uint8_t bytes[16];
    uint64_t a = dist(gen);
    uint64_t b = dist(gen);
    for (int i = 0; i < 8; ++i) bytes[i] = static_cast<uint8_t>((a >> (i * 8)) & 0xFF);
    for (int i = 0; i < 8; ++i) bytes[8 + i] = static_cast<uint8_t>((b >> (i * 8)) & 0xFF);
    // set version to 4
    bytes[6] = (bytes[6] & 0x0F) | 0x40;
    // set variant to 10xx
    bytes[8] = (bytes[8] & 0x3F) | 0x80;
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < 16; ++i) {
        ss << std::setw(2) << static_cast<int>(bytes[i]);
        if (i == 3 || i == 5 || i == 7 || i == 9) ss << '-';
    }
    return ss.str();
}

std::string SqlitePointStore::data_point_to_json(const DataPoint& p) {
    auto esc = [](const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
            }
        }
        return out;
    };
    std::ostringstream ss;
    ss << "{";
    ss << "\"id\":" << p.id << ",";
    ss << "\"server\":\"" << esc(p.server) << "\",";
    ss << "\"x\":" << std::setprecision(15) << p.x << ",";
    ss << "\"y\":" << std::setprecision(15) << p.y << ",";
    ss << "\"z\":" << std::setprecision(15) << p.z << ",";
    ss << "\"planet\":\"" << esc(p.planet) << "\",";
    ss << "\"material\":\"" << esc(p.material) << "\",";
    ss << "\"location\":" << (p.location ? 1 : 0) << ",";
    ss << "\"quality_min\":" << p.quality_min << ",";
    ss << "\"quality_max\":" << p.quality_max << ",";
    ss << "\"note\":\"" << esc(p.note) << "\",";
    ss << "\"poi_type\":" << static_cast<int>(p.poi_type) << ",";
    ss << "\"time_info\":\"" << esc(p.time_info) << "\"";
    ss << "}";
    return ss.str();
}

bool SqlitePointStore::ensure_migrations() {
    if (!db_handle_) return false;
        // Use PRAGMA user_version to manage schema version.
        const int current = get_user_version(db_handle_);
        const int target = 1;
        if (current >= target) return true;

        for (int v = current + 1; v <= target; ++v) {
                bool ok = false;
                switch (v) {
                case 1:
                        ok = migrate_to_v1();
                        break;
                default:
                        ok = false;
                        break;
                }
                if (!ok) return false;
                if (!set_user_version(db_handle_, v)) return false;
        }
        return true;
}

bool SqlitePointStore::migrate_to_v1() {
        if (!db_handle_) return false;
        std::string sql = R"SQL(
PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;
CREATE TABLE IF NOT EXISTS points (
    recordid INTEGER PRIMARY KEY,
    server TEXT,
    x REAL, y REAL, z REAL,
    planet TEXT, material TEXT,
    location INTEGER,
    quality_min INTEGER, quality_max INTEGER,
    note TEXT,
    poi_type INTEGER,
    poi_time TEXT,
    last_modified_ts INTEGER,
    last_modified_node TEXT
);

CREATE TABLE IF NOT EXISTS change_events (
    change_id TEXT PRIMARY KEY,
    node_id TEXT,
    seq INTEGER,
    created_ts INTEGER,
    op TEXT,
    recordid INTEGER,
    payload TEXT,
    applied_ts INTEGER
);
CREATE INDEX IF NOT EXISTS idx_change_events_created ON change_events(created_ts);

CREATE TABLE IF NOT EXISTS sync_checkpoint (
    node_id TEXT PRIMARY KEY,
    last_remote_ts INTEGER,
    last_remote_change_id TEXT
);

CREATE TABLE IF NOT EXISTS server_ids (
    value TEXT PRIMARY KEY
);

CREATE TABLE IF NOT EXISTS planets (
    id INTEGER PRIMARY KEY,
    system TEXT,
    planet TEXT,
    image_dir TEXT,
    zone_id TEXT
);

CREATE TABLE IF NOT EXISTS resources (
    id INTEGER PRIMARY KEY,
    shortname TEXT,
    name TEXT,
    resource_type TEXT,
    harvest_type TEXT
);
)SQL";

        if (!exec_sql(db_handle_, sql)) return false;

        // populate server_ids/planets/resources from CSV if present and table empty (non-fatal)
        if (!populate_reference_tables_if_empty()) {
                // log but continue
        }
        return true;
}

bool SqlitePointStore::populate_reference_tables_if_empty() {
    if (!db_handle_) return false;
    std::filesystem::path dbp(db_path_);
    auto base_dir = dbp.parent_path();

    // helper to check count
    auto table_count = [this](const char* table) -> int64_t {
        sqlite3_stmt* stmt = nullptr;
        std::string q = std::string("SELECT COUNT(*) FROM ") + table + ";";
        if (sqlite3_prepare_v2(db_handle_, q.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            return -1;
        }
        int64_t cnt = 0;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            cnt = sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
        return cnt;
    };

    // server_ids
    int64_t scnt = table_count("server_ids");
    auto server_csv = base_dir / "server_ids.csv";
    if (scnt == 0 && std::filesystem::exists(server_csv)) {
        std::ifstream in(server_csv);
        std::string line;
        bool first = true;
        sqlite3_stmt* ins = nullptr;
        const char* insert_sql = "INSERT OR IGNORE INTO server_ids(value) VALUES(?);";
        if (sqlite3_prepare_v2(db_handle_, insert_sql, -1, &ins, nullptr) != SQLITE_OK) {
            return false;
        }
        while (std::getline(in, line)) {
            const auto trimmed = trim(line);
            if (trimmed.empty()) continue;
            auto cells = split_csv_row(trimmed);
            if (cells.empty()) continue;
            if (first) {
                first = false;
                const auto h = to_lower(trim(cells[0]));
                if (h == "value" || h == "server") continue;
            }
            const auto val = trim(cells[0]);
            sqlite3_bind_text(ins, 1, val.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(ins);
            sqlite3_reset(ins);
        }
        sqlite3_finalize(ins);
    }

    // planets
    int64_t pcnt = table_count("planets");
    auto planets_csv = base_dir / "planets.csv";
    if (pcnt == 0 && std::filesystem::exists(planets_csv)) {
        std::ifstream in(planets_csv);
        std::string line;
        bool first = true;
        sqlite3_stmt* ins = nullptr;
        const char* insert_sql = "INSERT OR REPLACE INTO planets(id,system,planet,image_dir,zone_id) VALUES(?,?,?,?,?);";
        if (sqlite3_prepare_v2(db_handle_, insert_sql, -1, &ins, nullptr) != SQLITE_OK) {
            return false;
        }
        while (std::getline(in, line)) {
            const auto trimmed = trim(line);
            if (trimmed.empty()) continue;
            auto cells = split_csv_row(trimmed);
            if (cells.empty()) continue;
            if (first) {
                first = false;
                const auto h = to_lower(trim(cells[0]));
                if (h == "id" || h == "recordid") continue;
            }
            int id = -1;
            try { id = std::stoi(trim(cells[0])); } catch(...) { id = -1; }
            const std::string system = cells.size() > 1 ? trim(cells[1]) : std::string();
            const std::string planet = cells.size() > 2 ? trim(cells[2]) : std::string();
            const std::string image_dir = cells.size() > 3 ? trim(cells[3]) : std::string();
            const std::string zone_id = cells.size() > 4 ? trim(cells[4]) : std::string();

            if (id >= 0) sqlite3_bind_int(ins, 1, id); else sqlite3_bind_null(ins, 1);
            sqlite3_bind_text(ins, 2, system.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(ins, 3, planet.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(ins, 4, image_dir.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(ins, 5, zone_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(ins);
            sqlite3_reset(ins);
        }
        sqlite3_finalize(ins);
    }

    // resources
    int64_t rcnt = table_count("resources");
    auto resources_csv = base_dir / "resources.csv";
    if (rcnt == 0 && std::filesystem::exists(resources_csv)) {
        std::ifstream in(resources_csv);
        std::string line;
        bool first = true;
        sqlite3_stmt* ins = nullptr;
        const char* insert_sql = "INSERT OR REPLACE INTO resources(id,shortname,name,resource_type,harvest_type) VALUES(?,?,?,?,?);";
        if (sqlite3_prepare_v2(db_handle_, insert_sql, -1, &ins, nullptr) != SQLITE_OK) {
            return false;
        }
        while (std::getline(in, line)) {
            const auto trimmed = trim(line);
            if (trimmed.empty()) continue;
            auto cells = split_csv_row(trimmed);
            if (cells.empty()) continue;
            if (first) {
                first = false;
                const auto h = to_lower(trim(cells[0]));
                if (h == "id") continue;
            }
            int id = -1;
            try { id = std::stoi(trim(cells[0])); } catch(...) { id = -1; }
            const std::string shortname = cells.size() > 1 ? trim(cells[1]) : std::string();
            const std::string name = cells.size() > 2 ? trim(cells[2]) : std::string();
            const std::string rtype = cells.size() > 3 ? trim(cells[3]) : std::string();
            const std::string harvest = cells.size() > 4 ? trim(cells[4]) : std::string();

            if (id >= 0) sqlite3_bind_int(ins, 1, id); else sqlite3_bind_null(ins, 1);
            sqlite3_bind_text(ins, 2, shortname.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(ins, 3, name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(ins, 4, rtype.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(ins, 5, harvest.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(ins);
            sqlite3_reset(ins);
        }
        sqlite3_finalize(ins);
    }

    return true;
}

bool SqlitePointStore::init() {
    // open DB
    int rc = sqlite3_open_v2(db_path_.c_str(), &db_handle_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to open sqlite db: " << sqlite3_errmsg(db_handle_) << "\n";
        if (db_handle_) sqlite3_close(db_handle_);
        db_handle_ = nullptr;
        return false;
    }

    // Ensure base migrations and tables
    if (!ensure_migrations()) {
        std::cerr << "Failed to run migrations on sqlite db\n";
        sqlite3_close(db_handle_);
        db_handle_ = nullptr;
        return false;
    }

    // If points table empty and a CSV exists alongside the DB, migrate
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_handle_, "SELECT COUNT(*) FROM points;", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const int64_t cnt = sqlite3_column_int64(stmt, 0);
            sqlite3_finalize(stmt);
            if (cnt == 0) {
                // look for geoscout.csv in same directory
                std::filesystem::path dbp(db_path_);
                auto csv_path = dbp.parent_path() / "geoscout.csv";
                if (std::filesystem::exists(csv_path)) {
                    auto csv_points = ::load_points(csv_path.string());
                    if (!csv_points.empty()) {
                        if (exec_sql(db_handle_, "BEGIN TRANSACTION;")) {
                            const char* insert_point_sql = "INSERT OR REPLACE INTO points(recordid,server,x,y,z,planet,material,location,quality_min,quality_max,note,poi_type,poi_time,last_modified_ts,last_modified_node) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);";
                            sqlite3_stmt* ins_pt = nullptr;
                            sqlite3_prepare_v2(db_handle_, insert_point_sql, -1, &ins_pt, nullptr);

                            const char* insert_ev_sql = "INSERT OR REPLACE INTO change_events(change_id,node_id,seq,created_ts,op,recordid,payload,applied_ts) VALUES(?,?,?,?,?,?,?,?);";
                            sqlite3_stmt* ins_ev = nullptr;
                            sqlite3_prepare_v2(db_handle_, insert_ev_sql, -1, &ins_ev, nullptr);

                            const int64_t now_ms_local = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());

                            for (const auto& p : csv_points) {
                                sqlite3_bind_int(ins_pt, 1, p.id);
                                sqlite3_bind_text(ins_pt, 2, p.server.c_str(), -1, SQLITE_TRANSIENT);
                                sqlite3_bind_double(ins_pt, 3, p.x);
                                sqlite3_bind_double(ins_pt, 4, p.y);
                                sqlite3_bind_double(ins_pt, 5, p.z);
                                sqlite3_bind_text(ins_pt, 6, p.planet.c_str(), -1, SQLITE_TRANSIENT);
                                sqlite3_bind_text(ins_pt, 7, p.material.c_str(), -1, SQLITE_TRANSIENT);
                                sqlite3_bind_int(ins_pt, 8, p.location ? 1 : 0);
                                sqlite3_bind_int(ins_pt, 9, p.quality_min);
                                sqlite3_bind_int(ins_pt, 10, p.quality_max);
                                sqlite3_bind_text(ins_pt, 11, p.note.c_str(), -1, SQLITE_TRANSIENT);
                                sqlite3_bind_int(ins_pt, 12, static_cast<int>(p.poi_type));
                                sqlite3_bind_text(ins_pt, 13, p.time_info.c_str(), -1, SQLITE_TRANSIENT);
                                sqlite3_bind_int64(ins_pt, 14, static_cast<sqlite3_int64>(now_ms_local));
                                sqlite3_bind_text(ins_pt, 15, node_id_.empty() ? "migration" : node_id_.c_str(), -1, SQLITE_TRANSIENT);
                                sqlite3_step(ins_pt);
                                sqlite3_reset(ins_pt);

                                ChangeEvent ev;
                                ev.change_id = generate_uuid_v4();
                                ev.node_id = node_id_.empty() ? "migration" : node_id_;
                                ev.created_ts = now_ms_local;
                                ev.seq.reset();
                                ev.op = "upsert";
                                ev.recordid = p.id;
                                ev.payload_json = data_point_to_json(p);
                                ev.applied_ts = now_ms_local;

                                sqlite3_bind_text(ins_ev, 1, ev.change_id.c_str(), -1, SQLITE_TRANSIENT);
                                sqlite3_bind_text(ins_ev, 2, ev.node_id.c_str(), -1, SQLITE_TRANSIENT);
                                sqlite3_bind_null(ins_ev, 3);
                                sqlite3_bind_int64(ins_ev, 4, static_cast<sqlite3_int64>(ev.created_ts));
                                sqlite3_bind_text(ins_ev, 5, ev.op.c_str(), -1, SQLITE_TRANSIENT);
                                if (ev.recordid.has_value()) sqlite3_bind_int(ins_ev, 6, ev.recordid.value()); else sqlite3_bind_null(ins_ev, 6);
                                sqlite3_bind_text(ins_ev, 7, ev.payload_json.c_str(), -1, SQLITE_TRANSIENT);
                                sqlite3_bind_int64(ins_ev, 8, static_cast<sqlite3_int64>(ev.applied_ts.value()));
                                sqlite3_step(ins_ev);
                                sqlite3_reset(ins_ev);
                            }

                            if (ins_pt) sqlite3_finalize(ins_pt);
                            if (ins_ev) sqlite3_finalize(ins_ev);
                            exec_sql(db_handle_, "COMMIT;");
                        }
                    }
                }
            }
        } else {
            sqlite3_finalize(stmt);
        }
    }

    return true;
}

std::vector<DataPoint> SqlitePointStore::load_points() {
    std::vector<DataPoint> result;
    if (!db_handle_) return result;

    const char* query = "SELECT recordid,server,x,y,z,planet,material,location,quality_min,quality_max,note,poi_type,poi_time FROM points ORDER BY recordid ASC;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_handle_, query, -1, &stmt, nullptr) != SQLITE_OK) {
        return result;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DataPoint p;
        p.id = sqlite3_column_int(stmt, 0);
        const unsigned char* t0 = sqlite3_column_text(stmt, 1);
        p.server = t0 ? reinterpret_cast<const char*>(t0) : std::string();
        p.x = sqlite3_column_double(stmt, 2);
        p.y = sqlite3_column_double(stmt, 3);
        p.z = sqlite3_column_double(stmt, 4);
        const unsigned char* t5 = sqlite3_column_text(stmt, 5);
        p.planet = t5 ? reinterpret_cast<const char*>(t5) : std::string();
        const unsigned char* t6 = sqlite3_column_text(stmt, 6);
        p.material = t6 ? reinterpret_cast<const char*>(t6) : std::string();
        p.location = sqlite3_column_int(stmt, 7) != 0;
        p.quality_min = sqlite3_column_int(stmt, 8);
        p.quality_max = sqlite3_column_int(stmt, 9);
        const unsigned char* t9 = sqlite3_column_text(stmt, 10);
        p.note = t9 ? reinterpret_cast<const char*>(t9) : std::string();
        p.poi_type = static_cast<PoiType>(sqlite3_column_int(stmt, 11));
        const unsigned char* t11 = sqlite3_column_text(stmt, 12);
        p.time_info = t11 ? reinterpret_cast<const char*>(t11) : std::string();
        result.push_back(p);
    }
    sqlite3_finalize(stmt);
    return result;
}

bool SqlitePointStore::push_change_event(const ChangeEvent& ev) {
    if (!db_handle_) return false;
    const char* insert_sql = "INSERT OR REPLACE INTO change_events(change_id,node_id,seq,created_ts,op,recordid,payload,applied_ts) VALUES(?,?,?,?,?,?,?,?);";
    sqlite3_stmt* ins = nullptr;
    if (sqlite3_prepare_v2(db_handle_, insert_sql, -1, &ins, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(ins, 1, ev.change_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 2, ev.node_id.c_str(), -1, SQLITE_TRANSIENT);
    if (ev.seq.has_value()) sqlite3_bind_int64(ins, 3, static_cast<sqlite3_int64>(ev.seq.value())); else sqlite3_bind_null(ins, 3);
    if (ev.created_ts != 0) sqlite3_bind_int64(ins, 4, static_cast<sqlite3_int64>(ev.created_ts)); else sqlite3_bind_int64(ins, 4, static_cast<sqlite3_int64>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()));
    sqlite3_bind_text(ins, 5, ev.op.c_str(), -1, SQLITE_TRANSIENT);
    if (ev.recordid.has_value()) sqlite3_bind_int(ins, 6, ev.recordid.value()); else sqlite3_bind_null(ins, 6);
    if (!ev.payload_json.empty()) sqlite3_bind_text(ins, 7, ev.payload_json.c_str(), -1, SQLITE_TRANSIENT); else sqlite3_bind_null(ins, 7);
    if (ev.applied_ts.has_value()) sqlite3_bind_int64(ins, 8, static_cast<sqlite3_int64>(ev.applied_ts.value())); else sqlite3_bind_null(ins, 8);

    const int rc = sqlite3_step(ins);
    if (rc != SQLITE_DONE) {
        sqlite3_finalize(ins);
        return false;
    }
    sqlite3_finalize(ins);
    return true;
}

bool SqlitePointStore::append_point(const DataPoint& p, std::string* out_change_id) {
    if (!db_handle_) return false;
    const char* insert_sql = "INSERT OR REPLACE INTO points(recordid,server,x,y,z,planet,material,location,quality_min,quality_max,note,poi_type,poi_time,last_modified_ts,last_modified_node) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);";
    sqlite3_stmt* ins = nullptr;
    if (sqlite3_prepare_v2(db_handle_, insert_sql, -1, &ins, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int(ins, 1, p.id);
    sqlite3_bind_text(ins, 2, p.server.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(ins, 3, p.x);
    sqlite3_bind_double(ins, 4, p.y);
    sqlite3_bind_double(ins, 5, p.z);
    sqlite3_bind_text(ins, 6, p.planet.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 7, p.material.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(ins, 8, p.location ? 1 : 0);
    sqlite3_bind_int(ins, 9, p.quality_min);
    sqlite3_bind_int(ins, 10, p.quality_max);
    sqlite3_bind_text(ins, 11, p.note.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(ins, 12, static_cast<int>(p.poi_type));
    sqlite3_bind_text(ins, 13, p.time_info.c_str(), -1, SQLITE_TRANSIENT);
    const int64_t now_ms = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    sqlite3_bind_int64(ins, 14, static_cast<sqlite3_int64>(now_ms));
    sqlite3_bind_text(ins, 15, node_id_.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(ins);
    if (rc != SQLITE_DONE) {
        sqlite3_finalize(ins);
        return false;
    }
    sqlite3_finalize(ins);

    // create change event (outbox)
    ChangeEvent ev;
    ev.change_id = generate_uuid_v4();
    ev.node_id = node_id_.empty() ? "local" : node_id_;
    ev.created_ts = now_ms;
    ev.op = "upsert";
    ev.recordid = p.id;
    ev.payload_json = data_point_to_json(p);
    ev.applied_ts.reset(); // not yet applied by remote
    if (!push_change_event(ev)) {
        // event insertion failed, but point was inserted - report success for point
    }
    if (out_change_id) *out_change_id = ev.change_id;
    return true;
}

bool SqlitePointStore::overwrite_points(const std::vector<DataPoint>& points) {
    if (!db_handle_) return false;
    // transaction
    if (!exec_sql(db_handle_, "BEGIN TRANSACTION;")) return false;
    bool ok = true;

    // delete existing
    if (!exec_sql(db_handle_, "DELETE FROM points;")) {
        exec_sql(db_handle_, "ROLLBACK;");
        return false;
    }

    const char* insert_sql = "INSERT OR REPLACE INTO points(recordid,server,x,y,z,planet,material,location,quality_min,quality_max,note,poi_type,poi_time,last_modified_ts,last_modified_node) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);";
    sqlite3_stmt* ins = nullptr;
    if (sqlite3_prepare_v2(db_handle_, insert_sql, -1, &ins, nullptr) != SQLITE_OK) {
        exec_sql(db_handle_, "ROLLBACK;");
        return false;
    }

    const int64_t now_ms = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());

    for (const auto& p : points) {
        sqlite3_bind_int(ins, 1, p.id);
        sqlite3_bind_text(ins, 2, p.server.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(ins, 3, p.x);
        sqlite3_bind_double(ins, 4, p.y);
        sqlite3_bind_double(ins, 5, p.z);
        sqlite3_bind_text(ins, 6, p.planet.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins, 7, p.material.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(ins, 8, p.location ? 1 : 0);
        sqlite3_bind_int(ins, 9, p.quality_min);
        sqlite3_bind_int(ins, 10, p.quality_max);
        sqlite3_bind_text(ins, 11, p.note.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(ins, 12, static_cast<int>(p.poi_type));
        sqlite3_bind_text(ins, 13, p.time_info.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(ins, 14, static_cast<sqlite3_int64>(now_ms));
        sqlite3_bind_text(ins, 15, node_id_.c_str(), -1, SQLITE_TRANSIENT);

        int rc = sqlite3_step(ins);
        if (rc != SQLITE_DONE) {
            ok = false;
            break;
        }
        sqlite3_reset(ins);

        // create outbox event for each overwritten point so sync can propagate
        ChangeEvent ev;
        ev.change_id = generate_uuid_v4();
        ev.node_id = node_id_.empty() ? "local" : node_id_;
        ev.created_ts = now_ms;
        ev.op = "upsert";
        ev.recordid = p.id;
        ev.payload_json = data_point_to_json(p);
        ev.applied_ts.reset();
        if (!push_change_event(ev)) {
            // not fatal; continue
        }
    }

    sqlite3_finalize(ins);

    if (ok) {
        exec_sql(db_handle_, "COMMIT;");
        return true;
    } else {
        exec_sql(db_handle_, "ROLLBACK;");
        return false;
    }
}
