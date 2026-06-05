#pragma once

#include "point_store.h"
#include <string>
#include <sqlite3.h>

class SqliteStore : public IStore {
public:
    // db_path: path to sqlite file. node_id: optional node identifier used when creating change_events.
    explicit SqliteStore(const std::string& db_path, const std::string& node_id = std::string());
    ~SqliteStore() override;

    // Initialize DB file and create schema if necessary. Returns true on success.
    bool init();

    std::vector<DataPoint> load_points() override;
    std::vector<DataPoint> load_points(const std::string& zone_name, const std::string& server_filter = std::string(), const std::vector<PoiType>& poi_types = std::vector<PoiType>(), int64_t from_ts = 0, int64_t to_ts = 0) override;
    bool append_point(DataPoint& p, uuid* out_change_id = nullptr) override;
    int uuid_insert_or_update(DataPoint& p, uuid* out_change_id = nullptr) override;
    bool overwrite_points(const std::vector<DataPoint>& points) override;
    bool push_change_event(const ChangeEvent& ev) override;
    DataPoint get_datapoint(int id) override;
    bool delete_point_by_id(int id) override;
    // Reference table accessors
    std::vector<std::string> load_server_ids();
    std::vector<Planet> load_planets();
    std::vector<Resource> load_resources();
    bool overwrite_planets(const std::vector<Planet>& planets);
    // Ensure the named zone contains the given point; expand bounding box if necessary.
    bool ensure_zone_contains_point(const std::string& zone_name, double x, double y, double grid_spacing_km);

private:
    std::string db_path_;
    std::string node_id_;
    sqlite3* db_handle_ = nullptr;
    bool ensure_migrations();
    bool populate_reference_tables_if_empty();
    bool migrate_to_v1();
    bool migrate_to_v2();
    bool migrate_to_v3();
    std::string data_point_to_json(const DataPoint& p);
};
