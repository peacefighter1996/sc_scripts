#pragma once

#include <filesystem>
#include <future>
#include <optional>
#include <string>

struct OcrResult {
    std::optional<std::string> rock;
    std::optional<double> x;
    std::optional<double> y;
    std::optional<double> z;
    double task_time_ms{0.0};
};

class ScoutOcr {
public:
    ScoutOcr(std::filesystem::path onnx_model_path, std::filesystem::path label_map_path);

    void request_async();
    std::optional<OcrResult> poll();

private:
    OcrResult run_ocr_task() const;
    std::string get_xyz_ocr_text() const;

    std::filesystem::path onnx_model_path_;
    std::filesystem::path label_map_path_;
    std::future<OcrResult> future_;
    bool has_pending_{false};
};
