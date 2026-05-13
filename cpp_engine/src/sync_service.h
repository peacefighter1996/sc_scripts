#pragma once

#include <functional>
#include <vector>
#include <string>
#include <thread>
#include <atomic>

#include "point_store.h"

struct ISyncService {
    virtual ~ISyncService() = default;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void set_on_points_updated(std::function<void(const std::vector<DataPoint>&)> cb) = 0;
    virtual void notify_new_local_event(const ChangeEvent& ev) = 0;
};

// Minimal SyncService skeleton. Real implementation will manage websocket/http connections,
// send outbound events and persist inbound events to the local store; it will call the
// registered callback when the in-memory points need updating.
class SyncService : public ISyncService {
public:
    explicit SyncService(const std::string& server_url, const std::string& node_id);
    ~SyncService() override;

    void start() override;
    void stop() override;
    void set_on_points_updated(std::function<void(const std::vector<DataPoint>&)> cb) override;
    void notify_new_local_event(const ChangeEvent& ev) override;

private:
    std::string server_url_;
    std::string node_id_;
    std::function<void(const std::vector<DataPoint>&)> on_points_updated_;
    std::thread worker_thread_;
    std::atomic<bool> running_{false};
};
