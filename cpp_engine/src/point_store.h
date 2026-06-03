#pragma once

#include <string>
#include <vector>
#include <optional>

#include "scout_core.h"

struct ChangeEvent {
    uuid change_id;
    std::string node_id;
    int64_t created_ts{};
    std::optional<int64_t> seq;
    std::string op; // "upsert"|"delete"
    std::optional<int> recordid;
    std::string payload_json;
    std::optional<int64_t> applied_ts;
};

struct IStore {
    virtual ~IStore() = default;
    virtual std::vector<DataPoint> load_points() = 0;
    virtual std::vector<DataPoint> load_points(const std::string& zone_name, const std::string& server_filter = std::string(), const std::vector<PoiType>& poi_types = std::vector<PoiType>()) = 0;
    virtual bool append_point(DataPoint& p, uuid* out_change_id = nullptr) = 0;
	virtual int uuid_insert_or_update(DataPoint& p, uuid* out_change_id = nullptr) = 0;
    virtual bool overwrite_points(const std::vector<DataPoint>& points) = 0;
    virtual bool push_change_event(const ChangeEvent& ev) = 0;
    virtual std::vector<std::string> load_server_ids() = 0;
    virtual std::vector<Planet> load_planets() = 0;
    virtual std::vector<Resource> load_resources() = 0;
};

struct import_json_datapoints_result {
    int imported_count{};
    int skipped_count{};
    int failed_count{};
    std::vector<std::string> errors;
};


struct JsonExchangeDatapoint {
    JsonExchangeDatapoint(IStore* store) : store(store) {}
    IStore* store;

    import_json_datapoints_result import_json_datapoints(const std::filesystem::path& json_path);
    int export_json_datapoints(const std::filesystem::path& json_path, const std::vector<DataPoint>& points);
    int export_json_datapoints(const std::filesystem::path& json_path, const std::vector<PoiType>& poi_types);
};
