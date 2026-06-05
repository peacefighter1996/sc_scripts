#include "point_store_sqlite.h"
#include "point_store_csv.h"

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

SqliteStore::SqliteStore(const std::string& db_path, const std::string& node_id)
	: db_path_(db_path), node_id_(node_id), db_handle_(nullptr) {}

SqliteStore::~SqliteStore() {
	if (db_handle_) {
		sqlite3_close(db_handle_);
		db_handle_ = nullptr;
	}
}

std::string SqliteStore::data_point_to_json(const DataPoint& p) {
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
	ss << "\"guid\":\"" << esc(p.uuid.to_string()) << "\",";
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
	ss << "\"subtype\":" << static_cast<int>(p.subtype) << ",";
	ss << "\"poi_type\":" << static_cast<int>(p.poi_type) << ",";
	ss << "\"qt_persistent\":" << (p.qt_persistent ? 1 : 0) << ",";
	ss << "\"time_info\":\"" << esc(p.time_info) << "\"";
	ss << "}";
	return ss.str();
}

bool SqliteStore::ensure_migrations() {
	if (!db_handle_) return false;
		// Use PRAGMA user_version to manage schema version.
	const int current = get_user_version(db_handle_);
	const int target = 3;
	if (current >= target) return true;

	for (int v = current + 1; v <= target; ++v) {
		bool ok = false;
		switch (v) {
			case 1:
				ok = migrate_to_v1();
				break;
			case 2:
				ok = migrate_to_v2();
				break;
			case 3:
				ok = migrate_to_v3();
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

bool SqliteStore::migrate_to_v1() {
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
	return true;
}

bool SqliteStore::migrate_to_v2() {
	if (!db_handle_) return false;
	// create zones table and populate/migrate reference data
	std::string sql = R"SQL(
    CREATE TABLE IF NOT EXISTS zones (
        id INTEGER PRIMARY KEY,
        system TEXT,
        name TEXT,
        image_dir TEXT,
        zone_id TEXT,
        zone_type INTEGER,
        quantumable INTEGER,
        center_x REAL,
        center_y REAL,
        center_z REAL,
        min_x_km INTEGER,
        max_x_km INTEGER,
        min_y_km INTEGER,
        max_y_km INTEGER
    );
    )SQL";
	if (!exec_sql(db_handle_, sql)) return false;

	// populate server_ids/zones/resources from CSV if present and table empty (non-fatal)
	if (!populate_reference_tables_if_empty()) {
		// log but continue
	}
	return true;
}

bool SqliteStore::migrate_to_v3() {
	if (!db_handle_) return false;
	// Add guid (blob), subtype (int), and qt_persistent (int) columns to points table.
	std::string sql = R"SQL(
    ALTER TABLE points ADD COLUMN guid BLOB;
    ALTER TABLE points ADD COLUMN subtype INTEGER DEFAULT 0;
    ALTER TABLE points ADD COLUMN qt_persistent INTEGER DEFAULT 0;
)SQL";
	if (!exec_sql(db_handle_, sql)) return false;
	return true;
}

bool SqliteStore::populate_reference_tables_if_empty() {
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

	// If zones table empty but legacy planets table exists, migrate rows from planets -> zones
	int64_t zcnt_check = table_count("zones");
	int64_t legacy_pcnt = table_count("planets");
	if (zcnt_check == 0 && legacy_pcnt > 0) {
		// simple SQL migration: copy columns we know, set defaults for new columns
		const std::string migrate_sql =
			"INSERT OR REPLACE INTO zones(id,system,name,image_dir,zone_id,zone_type,quantumable,center_x,center_y,center_z,min_x_km,max_x_km,min_y_km,max_y_km) "
			"SELECT id, system, planet, image_dir, zone_id, 0, 1, NULL, NULL, NULL, NULL, NULL, NULL, NULL FROM planets;";
		exec_sql(db_handle_, migrate_sql);
	}

	// zones (previously planets)
	int64_t pcnt = table_count("zones");
	auto zones_csv = base_dir / "zones.csv";
	auto planets_csv = base_dir / "planets.csv"; // fallback for legacy files
	if (pcnt == 0 && std::filesystem::exists(zones_csv)) {
		// Use centralized CSV parser to read catalog and insert rows
		const auto catalog = ::load_planet_catalog(zones_csv, {});
		if (!catalog.empty()) {
			const char* insert_sql = "INSERT OR REPLACE INTO zones(id,system,name,image_dir,zone_id,zone_type,quantumable,center_x,center_y,center_z,min_x_km,max_x_km,min_y_km,max_y_km) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?);";
			sqlite3_stmt* ins = nullptr;
			if (sqlite3_prepare_v2(db_handle_, insert_sql, -1, &ins, nullptr) == SQLITE_OK) {
				for (const auto& plt : catalog) {
					if (plt.id >= 0) sqlite3_bind_int(ins, 1, plt.id); else sqlite3_bind_null(ins, 1);
					sqlite3_bind_text(ins, 2, plt.system.c_str(), -1, SQLITE_TRANSIENT);
					sqlite3_bind_text(ins, 3, plt.name.c_str(), -1, SQLITE_TRANSIENT);
					sqlite3_bind_text(ins, 4, plt.image_dir.c_str(), -1, SQLITE_TRANSIENT);
					sqlite3_bind_text(ins, 5, plt.zone_id.c_str(), -1, SQLITE_TRANSIENT);
					sqlite3_bind_int(ins, 6, zone_type_to_int(plt.zone_type));
					sqlite3_bind_int(ins, 7, plt.quantumable ? 1 : 0);
					sqlite3_bind_double(ins, 8, plt.center_x);
					sqlite3_bind_double(ins, 9, plt.center_y);
					sqlite3_bind_double(ins, 10, plt.center_z);
					sqlite3_bind_int(ins, 11, plt.min_x_km);
					sqlite3_bind_int(ins, 12, plt.max_x_km);
					sqlite3_bind_int(ins, 13, plt.min_y_km);
					sqlite3_bind_int(ins, 14, plt.max_y_km);
					sqlite3_step(ins);
					sqlite3_reset(ins);
				}
				sqlite3_finalize(ins);
			}
		}
	} else if (pcnt == 0 && std::filesystem::exists(planets_csv)) {
		// fallback: load legacy planets.csv into zones table via CSV parser
		const auto catalog = ::load_planet_catalog(planets_csv, {});
		if (!catalog.empty()) {
			const char* insert_sql = "INSERT OR REPLACE INTO zones(id,system,name,image_dir,zone_id,zone_type,quantumable,center_x,center_y,center_z,min_x_km,max_x_km,min_y_km,max_y_km) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?);";
			sqlite3_stmt* ins = nullptr;
			if (sqlite3_prepare_v2(db_handle_, insert_sql, -1, &ins, nullptr) == SQLITE_OK) {
				for (const auto& plt : catalog) {
					if (plt.id >= 0) sqlite3_bind_int(ins, 1, plt.id); else sqlite3_bind_null(ins, 1);
					sqlite3_bind_text(ins, 2, plt.system.c_str(), -1, SQLITE_TRANSIENT);
					sqlite3_bind_text(ins, 3, plt.name.c_str(), -1, SQLITE_TRANSIENT);
					sqlite3_bind_text(ins, 4, plt.image_dir.c_str(), -1, SQLITE_TRANSIENT);
					sqlite3_bind_text(ins, 5, plt.zone_id.c_str(), -1, SQLITE_TRANSIENT);
					sqlite3_bind_int(ins, 6, zone_type_to_int(plt.zone_type));
					sqlite3_bind_int(ins, 7, plt.quantumable ? 1 : 0);
					sqlite3_bind_double(ins, 8, plt.center_x);
					sqlite3_bind_double(ins, 9, plt.center_y);
					sqlite3_bind_double(ins, 10, plt.center_z);
					sqlite3_bind_int(ins, 11, plt.min_x_km);
					sqlite3_bind_int(ins, 12, plt.max_x_km);
					sqlite3_bind_int(ins, 13, plt.min_y_km);
					sqlite3_bind_int(ins, 14, plt.max_y_km);
					sqlite3_step(ins);
					sqlite3_reset(ins);
				}
				sqlite3_finalize(ins);
			}
		}
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
			try { id = std::stoi(trim(cells[0])); }
			catch (...) { id = -1; }
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

bool SqliteStore::init() {
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
	return true;
}

std::vector<DataPoint> SqliteStore::load_points() {
	std::vector<DataPoint> result;
	if (!db_handle_) return result;

	const char* query = "SELECT recordid,server,x,y,z,planet,material,location,quality_min,quality_max,note,poi_type,poi_time,guid,subtype,qt_persistent FROM points ORDER BY recordid ASC;";
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
		// columns 13/14 are last_modified_ts/last_modified_node (ignored here)
		// guid (blob/text) at column 15
		const void* bg = sqlite3_column_blob(stmt, 13);
		int bg_len = sqlite3_column_bytes(stmt, 13);
		if (bg && bg_len > 0) {
			p.uuid = uuid::from_bytes(static_cast<const unsigned char*>(bg), bg_len);
		} else {
			p.uuid = nil_uuid;
		}
		// subtype
		p.subtype = static_cast<PoiSubType>(sqlite3_column_int(stmt, 14));
		// QT persistent
		p.qt_persistent = sqlite3_column_int(stmt, 15) != 0;

		result.push_back(p);
	}
	sqlite3_finalize(stmt);
	return result;
}

bool SqliteStore::push_change_event(const ChangeEvent& ev) {
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

bool SqliteStore::append_point(DataPoint& p, uuid* out_change_id) {
	if (!db_handle_) return false;
	const char* insert_sql = "INSERT OR REPLACE INTO points(server,x,y,z,planet,material,location,quality_min,quality_max,note,poi_type,poi_time,last_modified_ts,last_modified_node,guid,subtype,qt_persistent) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);";
	sqlite3_stmt* ins = nullptr;
	if (sqlite3_prepare_v2(db_handle_, insert_sql, -1, &ins, nullptr) != SQLITE_OK) {
		return false;
	}
	sqlite3_bind_text(ins, 1, p.server.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_double(ins, 2, p.x);
	sqlite3_bind_double(ins, 3, p.y);
	sqlite3_bind_double(ins, 4, p.z);
	sqlite3_bind_text(ins, 5, p.planet.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(ins, 6, p.material.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(ins, 7, p.location ? 1 : 0);
	sqlite3_bind_int(ins, 8, p.quality_min);
	sqlite3_bind_int(ins, 9, p.quality_max);
	sqlite3_bind_text(ins, 10, p.note.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(ins, 11, static_cast<int>(p.poi_type));
	sqlite3_bind_text(ins, 12, p.time_info.c_str(), -1, SQLITE_TRANSIENT);
	const int64_t now_ms = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
	sqlite3_bind_int64(ins, 13, static_cast<sqlite3_int64>(now_ms));
	sqlite3_bind_text(ins, 14, node_id_.c_str(), -1, SQLITE_TRANSIENT);
	auto guid_blob = p.uuid;
	if (guid_blob == nil_uuid)
		guid_blob = uuid::generate_uuid_v4(); // assign new UUID if not set in CSV
	auto guid_bytes = guid_blob.to_bytes();
	sqlite3_bind_blob(ins, 15, guid_bytes.data(), static_cast<int>(guid_bytes.size()), SQLITE_TRANSIENT);
	sqlite3_bind_int(ins, 16, static_cast<int>(p.subtype));
	sqlite3_bind_int(ins, 17, p.qt_persistent ? 1 : 0);

	int rc = sqlite3_step(ins);
	if (rc != SQLITE_DONE) {
		sqlite3_finalize(ins);
		return false;
	}

	// check if value exists
	auto server_id = -1;

	// If the point has a UUID, try to find an existing record with that UUID

	sqlite3_stmt* stmt = nullptr;
	const char* q = "SELECT recordid FROM points WHERE guid = ? LIMIT 1;";
	if (sqlite3_prepare_v2(db_handle_, q, -1, &stmt, nullptr) == SQLITE_OK) {
		auto guid_bytes = guid_blob.to_bytes();
		sqlite3_bind_blob(stmt, 1, guid_bytes.data(), static_cast<int>(guid_bytes.size()), SQLITE_TRANSIENT);
		if (sqlite3_step(stmt) == SQLITE_ROW) {
			server_id = sqlite3_column_int(stmt, 0);
		}
		sqlite3_finalize(stmt);
	}
	p.id = server_id; 
	p.uuid = guid_blob;

	sqlite3_finalize(ins);

	// create change event (outbox)
	ChangeEvent ev;
	ev.change_id = uuid::generate_uuid_v4();
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

std::vector<std::string> SqliteStore::load_server_ids() {
	std::vector<std::string> result;
	if (!db_handle_) return result;
	const char* q = "SELECT value FROM server_ids ORDER BY value ASC;";
	sqlite3_stmt* stmt = nullptr;
	if (sqlite3_prepare_v2(db_handle_, q, -1, &stmt, nullptr) != SQLITE_OK) return result;
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		const unsigned char* t = sqlite3_column_text(stmt, 0);
		if (t) result.emplace_back(reinterpret_cast<const char*>(t));
	}
	sqlite3_finalize(stmt);
	return result;
}

std::vector<Planet> SqliteStore::load_planets() {
	std::vector<Planet> result;
	if (!db_handle_) return result;
	const char* q = "SELECT id, system, name, image_dir, zone_id, zone_type, quantumable, center_x, center_y, center_z, min_x_km, max_x_km, min_y_km, max_y_km FROM zones ORDER BY name ASC;";
	sqlite3_stmt* stmt = nullptr;
	if (sqlite3_prepare_v2(db_handle_, q, -1, &stmt, nullptr) != SQLITE_OK) return result;
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		Planet p;
		p.id = sqlite3_column_int(stmt, 0);
		const unsigned char* t1 = sqlite3_column_text(stmt, 1);
		const unsigned char* t2 = sqlite3_column_text(stmt, 2);
		const unsigned char* t3 = sqlite3_column_text(stmt, 3);
		const unsigned char* t4 = sqlite3_column_text(stmt, 4);
		// 5: zone_type (int), 6: quantumable, 7: center_x, 8: center_y, 9: center_z, 10: min_x_km, 11: max_x_km, 12: min_y_km, 13: max_y_km
		p.system = t1 ? reinterpret_cast<const char*>(t1) : std::string();
		p.name = t2 ? reinterpret_cast<const char*>(t2) : std::string();
		p.image_dir = t3 ? reinterpret_cast<const char*>(t3) : std::string();
		p.zone_id = t4 ? reinterpret_cast<const char*>(t4) : std::string();
		int zt_int = sqlite3_column_int(stmt, 5);
		ZoneType zt = ZoneType::CelestialBody;
		if (!zone_type_from_int(zt_int, zt)) zt = ZoneType::CelestialBody;
		p.zone_type = zt;
		p.quantumable = sqlite3_column_int(stmt, 6) != 0;
		p.center_x = sqlite3_column_double(stmt, 7);
		p.center_y = sqlite3_column_double(stmt, 8);
		p.center_z = sqlite3_column_double(stmt, 9);
		p.min_x_km = sqlite3_column_int(stmt, 10);
		p.max_x_km = sqlite3_column_int(stmt, 11);
		p.min_y_km = sqlite3_column_int(stmt, 12);
		p.max_y_km = sqlite3_column_int(stmt, 13);
		result.push_back(p);
	}
	sqlite3_finalize(stmt);
	return result;
}

std::vector<Resource> SqliteStore::load_resources() {
	std::vector<Resource> result;
	if (!db_handle_) return result;
	const char* q = "SELECT id, shortname, name, resource_type, harvest_type FROM resources;";
	sqlite3_stmt* stmt = nullptr;
	if (sqlite3_prepare_v2(db_handle_, q, -1, &stmt, nullptr) != SQLITE_OK) return result;
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		Resource r;
		r.id = sqlite3_column_int(stmt, 0);
		const unsigned char* t1 = sqlite3_column_text(stmt, 1);
		const unsigned char* t2 = sqlite3_column_text(stmt, 2);
		const unsigned char* t3 = sqlite3_column_text(stmt, 3);
		const unsigned char* t4 = sqlite3_column_text(stmt, 4);
		r.short_name = t1 ? reinterpret_cast<const char*>(t1) : std::string();
		r.name = t2 ? reinterpret_cast<const char*>(t2) : std::string();
		std::string rtype = t3 ? reinterpret_cast<const char*>(t3) : std::string();
		std::string harvest = t4 ? reinterpret_cast<const char*>(t4) : std::string();

		std::string rt = to_lower(trim(rtype));
		if (rt == "mineral") r.type = ResourceType::Mineral;
		else if (rt == "plant") r.type = ResourceType::Plant;
		else r.type = ResourceType::None;

		HarvestType h = HarvestType::None;
		std::string hs = to_lower(trim(harvest));
		if (hs.find("fps") != std::string::npos) h = static_cast<HarvestType>(static_cast<int>(h) | static_cast<int>(HarvestType::FPS));
		if (hs.find("vehicle") != std::string::npos) h = static_cast<HarvestType>(static_cast<int>(h) | static_cast<int>(HarvestType::Vehicle));
		if (hs.find("ship") != std::string::npos) h = static_cast<HarvestType>(static_cast<int>(h) | static_cast<int>(HarvestType::Ship));
		r.harvest_type = h;

		result.push_back(r);
	}
	sqlite3_finalize(stmt);
	return result;
}

bool SqliteStore::overwrite_planets(const std::vector<Planet>& planets) {
	if (!db_handle_) return false;
	if (!exec_sql(db_handle_, "BEGIN TRANSACTION;")) return false;
	if (!exec_sql(db_handle_, "DELETE FROM zones;")) {
		exec_sql(db_handle_, "ROLLBACK;");
		return false;
	}

	static const char* insert_sql = "INSERT OR REPLACE INTO zones(id,system,name,image_dir,zone_id,zone_type,quantumable,center_x,center_y,center_z,min_x_km,max_x_km,min_y_km,max_y_km) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?);";
	sqlite3_stmt* ins = nullptr;
	if (sqlite3_prepare_v2(db_handle_, insert_sql, -1, &ins, nullptr) != SQLITE_OK) {
		exec_sql(db_handle_, "ROLLBACK;");
		return false;
	}

	for (const auto& p : planets) {
		if (p.id >= 0) sqlite3_bind_int(ins, 1, p.id); else sqlite3_bind_null(ins, 1);
		sqlite3_bind_text(ins, 2, p.system.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(ins, 3, p.name.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(ins, 4, p.image_dir.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(ins, 5, p.zone_id.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_int(ins, 6, zone_type_to_int(p.zone_type));
		sqlite3_bind_int(ins, 7, p.quantumable ? 1 : 0);
		sqlite3_bind_double(ins, 8, p.center_x);
		sqlite3_bind_double(ins, 9, p.center_y);
		sqlite3_bind_double(ins, 10, p.center_z);
		sqlite3_bind_int(ins, 11, p.min_x_km);
		sqlite3_bind_int(ins, 12, p.max_x_km);
		sqlite3_bind_int(ins, 13, p.min_y_km);
		sqlite3_bind_int(ins, 14, p.max_y_km);

		int rc = sqlite3_step(ins);
		if (rc != SQLITE_DONE) {
			sqlite3_finalize(ins);
			exec_sql(db_handle_, "ROLLBACK;");
			return false;
		}
		sqlite3_reset(ins);
	}

	sqlite3_finalize(ins);
	if (!exec_sql(db_handle_, "COMMIT;")) {
		exec_sql(db_handle_, "ROLLBACK;");
		return false;
	}
	return true;
}

std::vector<DataPoint> SqliteStore::load_points(const std::string& zone_name, const std::string& server_filter, const std::vector<PoiType>& poi_types, int64_t from_ts, int64_t to_ts){
	std::vector<DataPoint> result;
	if (!db_handle_) return result;
	std::vector < std::string> whereComponents;
	std::string query = "SELECT recordid,server,x,y,z,planet,material,location,quality_min,quality_max,note,poi_type,poi_time,guid,subtype,qt_persistent FROM points";
	if (!server_filter.empty() && to_lower(server_filter) != "all") {
		whereComponents.push_back(" server = '"+ server_filter + "' ");
	}

	if (!zone_name.empty() && to_lower(zone_name) != "all") {
		whereComponents.push_back(" planet = '"+zone_name+ "'");
	}
	if (!poi_types.empty()) {
	    
		std::string poi_str = " poi_type IN (";
		for (size_t i = 0; i < poi_types.size(); ++i) {
			poi_str += std::to_string(static_cast<int>(poi_types[i]));
			if (i < poi_types.size() - 1) poi_str += ",";
		}
		poi_str += ") ";
		whereComponents.push_back(poi_str);

	}

	if (from_ts > 0) {
		whereComponents.push_back(" last_modified_ts >= " + std::to_string(from_ts) + " ");
	}
	if (to_ts > 0) {
		whereComponents.push_back(" last_modified_ts <= " + std::to_string(to_ts) + " ");
	}

	if (whereComponents.size() > 0) {
		query += " WHERE ";
		for (size_t i = 0; i < whereComponents.size(); ++i) {
			query += whereComponents[i];
			if (i < whereComponents.size() - 1) query += " AND ";
		}
	}

	query += "ORDER BY recordid ASC;";

	sqlite3_stmt* stmt = nullptr;
	if (sqlite3_prepare_v2(db_handle_, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
		return result;
	}

	sqlite3_bind_text(stmt, 1, zone_name.c_str(), -1, SQLITE_TRANSIENT);
	if (!server_filter.empty()) {
		sqlite3_bind_text(stmt, 2, server_filter.c_str(), -1, SQLITE_TRANSIENT);
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
		// columns 13/14 are last_modified_ts/last_modified_node (ignored here)
		// guid (blob/text) at column 15
		const void* bg = sqlite3_column_blob(stmt, 13);
		int bg_len = sqlite3_column_bytes(stmt, 13);
		if (bg && bg_len > 0) {
			p.uuid = uuid::from_bytes(static_cast<const unsigned char*>(bg), bg_len);
		} else {
			p.uuid = nil_uuid;
		}
		// subtype
		p.subtype = static_cast<PoiSubType>(sqlite3_column_int(stmt, 14));
		// QT persistent
		p.qt_persistent = sqlite3_column_int(stmt, 15) != 0;

		result.push_back(p);
	}

	sqlite3_finalize(stmt);
	return result;
}




bool SqliteStore::overwrite_points(const std::vector<DataPoint>& points) {
	if (!db_handle_) return false;
	// transaction
	if (!exec_sql(db_handle_, "BEGIN TRANSACTION;")) return false;
	bool ok = true;

	// delete existing

	static const char* insert_sql = "INSERT OR REPLACE INTO points(recordid,server,x,y,z,planet,material,location,quality_min,quality_max,note,poi_type,poi_time,last_modified_ts,last_modified_node,guid,subtype,qt_persistent) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);";
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
		auto guid_blob = p.uuid;
		if (guid_blob == nil_uuid)
			guid_blob = uuid::generate_uuid_v4(); // assign new UUID if not set in CSV
		auto guid_bytes = guid_blob.to_bytes();
		sqlite3_bind_blob(ins, 16, guid_bytes.data(), static_cast<int>(guid_bytes.size()), SQLITE_TRANSIENT);
		sqlite3_bind_int(ins, 17, static_cast<int>(p.subtype));
		sqlite3_bind_int(ins, 18, p.qt_persistent ? 1 : 0);

		int rc = sqlite3_step(ins);
		if (rc != SQLITE_DONE) {
			ok = false;
			break;
		}
		sqlite3_reset(ins);

		// create outbox event for each overwritten point so sync can propagate
		ChangeEvent ev;
		ev.change_id = uuid::generate_uuid_v4();
		ev.node_id = node_id_.empty() ? "local" : node_id_;
		ev.created_ts = now_ms;
		ev.op = "upsert";
		ev.recordid = p.id;
		ev.payload_json = data_point_to_json(p);
		ev.applied_ts.reset();
		if (!push_change_event(ev)) {
			// not fatal; continue
			std::cerr << "Failed to push change event for point " << p.id << "\n";
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

bool SqliteStore::ensure_zone_contains_point(const std::string& zone_name, double x, double y, double grid_spacing_km) {
	if (!db_handle_) return false;
	sqlite3_stmt* stmt = nullptr;
	const char* q = "SELECT id, zone_type, min_x_km, max_x_km, min_y_km, max_y_km FROM zones WHERE name = ? LIMIT 1;";
	if (sqlite3_prepare_v2(db_handle_, q, -1, &stmt, nullptr) != SQLITE_OK) return false;
	sqlite3_bind_text(stmt, 1, zone_name.c_str(), -1, SQLITE_TRANSIENT);
	bool changed = false;
	int zone_id = -1;
	int zt_int = 0;
	int min_x = 0, max_x = 0, min_y = 0, max_y = 0;
	if (sqlite3_step(stmt) == SQLITE_ROW) {
		zone_id = sqlite3_column_int(stmt, 0);
		zt_int = sqlite3_column_int(stmt, 1);
		min_x = sqlite3_column_int(stmt, 2);
		max_x = sqlite3_column_int(stmt, 3);
		min_y = sqlite3_column_int(stmt, 4);
		max_y = sqlite3_column_int(stmt, 5);
	}
	sqlite3_finalize(stmt);
	ZoneType zt;
	if (!zone_type_from_int(zt_int, zt)) return false;
	if (zt != ZoneType::AsteroidField) return false;

	// If bounding box uninitialized (0,0), set defaults
	if (min_x == 0 && max_x == 0 && min_y == 0 && max_y == 0) {
		min_x = -300; max_x = 300; min_y = -300; max_y = 300;
	}

	const double g = std::max(1.0, grid_spacing_km);
	const int px_min = static_cast<int>(std::floor(x / g) * g);
	const int px_max = static_cast<int>(std::ceil(x / g) * g);
	const int py_min = static_cast<int>(std::floor(y / g) * g);
	const int py_max = static_cast<int>(std::ceil(y / g) * g);

	int new_min_x = std::min(min_x, px_min);
	int new_max_x = std::max(max_x, px_max);
	int new_min_y = std::min(min_y, py_min);
	int new_max_y = std::max(max_y, py_max);

	if (new_min_x != min_x || new_max_x != max_x || new_min_y != min_y || new_max_y != max_y) {
		const char* up = "UPDATE zones SET min_x_km = ?, max_x_km = ?, min_y_km = ?, max_y_km = ? WHERE id = ?;";
		sqlite3_stmt* ups = nullptr;
		if (sqlite3_prepare_v2(db_handle_, up, -1, &ups, nullptr) != SQLITE_OK) return false;
		sqlite3_bind_int(ups, 1, new_min_x);
		sqlite3_bind_int(ups, 2, new_max_x);
		sqlite3_bind_int(ups, 3, new_min_y);
		sqlite3_bind_int(ups, 4, new_max_y);
		sqlite3_bind_int(ups, 5, zone_id);
		if (sqlite3_step(ups) == SQLITE_DONE) changed = true;
		sqlite3_finalize(ups);
	}

	return changed;
}

DataPoint SqliteStore::get_datapoint(int recordid) {
	DataPoint p;
	if (!db_handle_) return p;
	const char* query = "SELECT recordid,server,x,y,z,planet,material,location,quality_min,quality_max,note,poi_type,poi_time,guid,subtype,qt_persistent FROM points WHERE recordid = ? LIMIT 1;";
	sqlite3_stmt* stmt = nullptr;
	if (sqlite3_prepare_v2(db_handle_, query, -1, &stmt, nullptr) != SQLITE_OK) {
		return p;
	}
	sqlite3_bind_int(stmt, 1, recordid);
	if (sqlite3_step(stmt) == SQLITE_ROW) {
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
		// columns 13/14 are last_modified_ts/last_modified_node (ignored here)
		// guid (blob/text) at column 15
		const void* bg = sqlite3_column_blob(stmt, 13);
		int bg_len = sqlite3_column_bytes(stmt, 13);
		if (bg && bg_len > 0) {
			p.uuid = uuid::from_bytes(static_cast<const unsigned char*>(bg), bg_len);
		} else {
			p.uuid = nil_uuid;
		}
		// subtype
		p.subtype = static_cast<PoiSubType>(sqlite3_column_int(stmt, 14));
		// QT persistent
		p.qt_persistent = sqlite3_column_int(stmt, 15) != 0;
	}
	sqlite3_finalize(stmt);
	return p;
}

int SqliteStore::uuid_insert_or_update(DataPoint& p, uuid* out_change_id) {
	// check if value exists
	auto id_existing_id = -1;

	// If the point has a UUID, try to find an existing record with that UUID
	if (p.uuid != nil_uuid) {
		sqlite3_stmt* stmt = nullptr;
		const char* q = "SELECT recordid FROM points WHERE guid = ? LIMIT 1;";
		if (sqlite3_prepare_v2(db_handle_, q, -1, &stmt, nullptr) == SQLITE_OK) {
			auto guid_bytes = p.uuid.to_bytes();
			sqlite3_bind_blob(stmt, 1, guid_bytes.data(), static_cast<int>(guid_bytes.size()), SQLITE_TRANSIENT);
			if (sqlite3_step(stmt) == SQLITE_ROW) {
				id_existing_id = sqlite3_column_int(stmt, 0);
			}
			sqlite3_finalize(stmt);
		}
	}

	// if no existing record with same UUID. insert new record
	if (id_existing_id == -1) {
		if (!append_point(p, out_change_id)) {
			exec_sql(db_handle_, "ROLLBACK;");
			return 0;
		}
	} else {
		// otherwise, update existing record with new values from p
		DataPoint updated = p;
		updated.id = id_existing_id; // ensure ID matches existing record
		// read existing record and check if any fields other than ID differ; if not, skip update and event generation
		DataPoint existing = get_datapoint(id_existing_id);
		if (existing == updated) {
			// no changes, skip update and event
			return 2;
		}

		if (!overwrite_points({updated})) {
			exec_sql(db_handle_, "ROLLBACK;");
			return 0;
		}
		if (out_change_id) {
			// create change event for update
			ChangeEvent ev;
			ev.change_id = uuid::generate_uuid_v4();
			ev.node_id = node_id_.empty() ? "local" : node_id_;
			ev.created_ts = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
			ev.op = "upsert";
			ev.recordid = updated.id;
			ev.payload_json = data_point_to_json(updated);
			ev.applied_ts.reset();
			if (!push_change_event(ev)) {
				// not fatal; continue
			}
			if (out_change_id) *out_change_id = ev.change_id;
		}
	}
    return 1;

}

bool SqliteStore::delete_point_by_id(int id) {
	if (!db_handle_) return false;
	const char* del_sql = "DELETE FROM points WHERE recordid = ?;";
	sqlite3_stmt* del = nullptr;
	if (sqlite3_prepare_v2(db_handle_, del_sql, -1, &del, nullptr) != SQLITE_OK) return false;
	sqlite3_bind_int(del, 1, id);
	int rc = sqlite3_step(del);
	sqlite3_finalize(del);

	// record change event for delete
	ChangeEvent ev;
	ev.change_id = uuid::generate_uuid_v4();
	ev.node_id = node_id_.empty() ? "local" : node_id_;
	ev.created_ts = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
	ev.op = "delete";
	ev.recordid = id;
	ev.payload_json.clear();
	ev.applied_ts.reset();
	if (!push_change_event(ev)) {
		// not fatal
	}

	return rc == SQLITE_DONE || rc == SQLITE_ROW;
}
