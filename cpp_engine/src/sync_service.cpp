#include "sync_service.h"
#include <iostream>
#include <chrono>

SyncService::SyncService(const std::string& server_url, const std::string& node_id)
    : server_url_(server_url), node_id_(node_id) {}

SyncService::~SyncService() {
    stop();
}

void SyncService::start() {
    if (running_.exchange(true)) return;
    worker_thread_ = std::thread([this]() {
        // Placeholder worker loop: in a real implementation this would
        // manage websocket connection, process inbound events, ack outbound.
        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    });
}

void SyncService::stop() {
    if (!running_.exchange(false)) return;
    if (worker_thread_.joinable()) worker_thread_.join();
}

void SyncService::set_on_points_updated(std::function<void(const std::vector<DataPoint>&)> cb) {
    on_points_updated_ = std::move(cb);
}

void SyncService::notify_new_local_event(const ChangeEvent& ev) {
    // Placeholder: in a real implementation this would serialize the event and send it
    // to the remote server. For now, just log.
    (void)ev;
    std::cerr << "SyncService::notify_new_local_event() called (not implemented)\n";
}
