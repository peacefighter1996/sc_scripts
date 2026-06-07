#pragma once

#include <filesystem>
#include <future>
#include <optional>
#include <string>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <atomic>
#include <cstddef>

// Do not include Windows headers in this public header; use opaque pointer for DIBSection ownership.

#ifdef SCOUT_HAS_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

struct OcrResult {
    std::optional<std::string> rock;
    std::optional<double> x;
    std::optional<double> y;
    std::optional<double> z;
    std::optional<std::string> locationmarker;
    double task_time_ms{0.0};
};

class ScoutOcr {
public:
    ScoutOcr(std::filesystem::path onnx_model_path, std::filesystem::path label_map_path);
    ~ScoutOcr();

    // Start an async OCR request (no-op if one is already pending).
    void request_async();

    // Poll for a stored future result (legacy path).
    std::optional<OcrResult> poll();

    // Stop flag observed by external timers/loops.
    std::atomic<bool> active{true};

    using SubscriptionId = std::size_t;
    using Callback = std::function<void(const OcrResult&)>;

    // Subscribe to OCR results. Returns a subscription id for later unsubscribe.
    std::size_t subscribe(Callback cb);
    void unsubscribe(SubscriptionId id);

    // Backwards-compatible single-callback setter (wraps subscribe/unsubscribe).
    void set_callback(Callback cb);

    // Stop and wait for any pending async work to complete.
    void stop_and_wait();
    void stop();

private:
    OcrResult run_ocr_task() const;
    std::string get_coordinates_ocr_text() const;

    std::filesystem::path onnx_model_path_;
    std::filesystem::path label_map_path_;
    std::unordered_map<int, std::string> labels_;
#ifdef SCOUT_HAS_ONNXRUNTIME
    Ort::Session* onnx_session_{nullptr};
#endif
    std::future<OcrResult> future_;
    std::atomic<bool> has_pending_{false};

    std::mutex subs_mtx_;
    std::unordered_map<SubscriptionId, Callback> subscribers_;
    SubscriptionId next_sub_id_{1};
    SubscriptionId single_sub_id_{0};
    // Reusable DIBSection ownership to avoid copying pixel data on each capture.
    // Opaque handle to the reusable DIBSection and its pixel pointer.
    mutable void* reusable_bitmap_{nullptr};
    mutable void* reusable_pixels_{nullptr};
    mutable int reusable_width_{0};
    mutable int reusable_height_{0};
    mutable std::vector<std::uint8_t> reusable_gray_;
    // Reusable compatible DC and previous bitmap selected into it.
    mutable void* reusable_mem_dc_{nullptr};
    mutable void* reusable_old_bitmap_{nullptr};
};
