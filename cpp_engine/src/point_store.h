#pragma once

#include <string>
#include <vector>
#include <optional>

#include "scout_core.h"

struct ChangeEvent {
    std::string change_id;
    std::string node_id;
    int64_t created_ts{};
    std::optional<int64_t> seq;
    std::string op; // "upsert"|"delete"
    std::optional<int> recordid;
    std::string payload_json;
    std::optional<int64_t> applied_ts;
};

struct IPointStore {
    virtual ~IPointStore() = default;
    virtual std::vector<DataPoint> load_points() = 0;
    virtual bool append_point(const DataPoint& p, std::string* out_change_id = nullptr) = 0;
    virtual bool overwrite_points(const std::vector<DataPoint>& points) = 0;
    virtual bool push_change_event(const ChangeEvent& ev) = 0;
    virtual std::vector<std::string> load_server_ids() = 0;
    virtual std::vector<Planet> load_planets() = 0;
    virtual std::vector<Resource> load_resources() = 0;
};
