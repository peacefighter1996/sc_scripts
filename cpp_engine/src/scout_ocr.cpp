#include "scout_ocr.h"

#include "scout_core.h"

#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr const char* kWindowName = "Star Citizen ";
constexpr const char* kCaptureMode = "screen";

struct CapturedFrame {
    int width{};
    int height{};
    std::vector<std::uint8_t> bgra;
    std::vector<std::uint8_t> gray;
};

std::unordered_map<int, std::string> load_label_map(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        throw std::runtime_error("Failed to open label map: " + path.string());
    }

    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::regex entry_regex("\"([^\"]+)\"\\s*:\\s*(\\d+)");
    std::unordered_map<int, std::string> labels;

    for (std::sregex_iterator it(content.begin(), content.end(), entry_regex), end; it != end; ++it) {
        std::string token = (*it)[1].str();
        if (token == "space") {
            token = " ";
        } else if (token == "colon") {
            token = ":";
        } else if (token == "dot") {
            token = ".";
        } else if (token == "dash") {
            token = "-";
        } else if (token == "comma") {
            token = ",";
        }
        labels[std::stoi((*it)[2].str())] = token;
    }

    return labels;
}

std::vector<RECT> enumerate_monitors() {
    std::vector<RECT> rects;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR, HDC, LPRECT rect, LPARAM data) -> BOOL {
        auto* out = reinterpret_cast<std::vector<RECT>*>(data);
        out->push_back(*rect);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&rects));
    return rects;
}

struct WindowSearchContext {
    std::wstring needle;
    HWND found{};
};

BOOL CALLBACK enum_window_callback(HWND hwnd, LPARAM lparam) {
    auto* context = reinterpret_cast<WindowSearchContext*>(lparam);
    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }

    wchar_t title[512];
    GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
    std::wstring window_title(title);
    if (window_title.find(context->needle) != std::wstring::npos) {
        context->found = hwnd;
        return FALSE;
    }
    return TRUE;
}

std::optional<RECT> get_window_rect_by_title(const std::wstring& title_substring) {
    WindowSearchContext context{title_substring, nullptr};
    EnumWindows(enum_window_callback, reinterpret_cast<LPARAM>(&context));
    if (!context.found) {
        return std::nullopt;
    }

    RECT rect{};
    if (!GetWindowRect(context.found, &rect)) {
        return std::nullopt;
    }
    return rect;
}

std::optional<CapturedFrame> capture_rect(const RECT& rect) {
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) {
        return std::nullopt;
    }

    HDC screen_dc = GetDC(nullptr);
    HDC mem_dc = CreateCompatibleDC(screen_dc);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen_dc, &bmi, DIB_RGB_COLORS, &pixels, nullptr, 0);
    HGDIOBJ old_bitmap = SelectObject(mem_dc, bitmap);

    BitBlt(mem_dc, 0, 0, width, height, screen_dc, rect.left, rect.top, SRCCOPY | CAPTUREBLT);

    CapturedFrame frame;
    frame.width = width;
    frame.height = height;
    frame.bgra.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    std::memcpy(frame.bgra.data(), pixels, frame.bgra.size());
    frame.gray.resize(static_cast<size_t>(width) * static_cast<size_t>(height));

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t offset = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4;
            const float b = static_cast<float>(frame.bgra[offset + 0]);
            const float g = static_cast<float>(frame.bgra[offset + 1]);
            const float r = static_cast<float>(frame.bgra[offset + 2]);
            frame.gray[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] = static_cast<std::uint8_t>(std::clamp((0.114f * b) + (0.587f * g) + (0.299f * r), 0.0f, 255.0f));
        }
    }

    SelectObject(mem_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(mem_dc);
    ReleaseDC(nullptr, screen_dc);
    return frame;
}

std::optional<CapturedFrame> capture_frame() {
    const auto monitors = enumerate_monitors();
    if (monitors.empty()) {
        return std::nullopt;
    }

    if (std::string(kCaptureMode) == "window") {
        const auto rect = get_window_rect_by_title(std::wstring(kWindowName, kWindowName + std::strlen(kWindowName)));
        if (!rect) {
            return std::nullopt;
        }
        return capture_rect(*rect);
    }

    const auto game_window_rect = get_window_rect_by_title(std::wstring(kWindowName, kWindowName + std::strlen(kWindowName)));
    if (!game_window_rect) {
        return std::nullopt;
    }

    long long best_overlap = -1;
    size_t best_index = 0;
    for (size_t i = 0; i < monitors.size(); ++i) {
        RECT intersection{};
        if (IntersectRect(&intersection, &monitors[i], &(*game_window_rect))) {
            const long long width = static_cast<long long>(intersection.right - intersection.left);
            const long long height = static_cast<long long>(intersection.bottom - intersection.top);
            const long long overlap = width * height;
            if (overlap > best_overlap) {
                best_overlap = overlap;
                best_index = i;
            }
        }
    }

    if (best_overlap <= 0) {
        return std::nullopt;
    }

    return capture_rect(monitors[best_index]);
}

std::vector<float> extract_characters_rtl(const CapturedFrame& frame) {
    const int start_y = 30;
    const int end_y = 44;
    const int start_x = std::max(0, frame.width - 1000);
    const int end_x = std::max(start_x, frame.width - 4);
    const int text_width = end_x - start_x;
    const int text_height = end_y - start_y;
    if (text_width <= 0 || text_height <= 0 || end_y > frame.height) {
        return {};
    }

    constexpr double char_w = 7.5;
    constexpr int char_h = 14;
    const int y_start = (text_height - char_h) / 2;
    const int int_char_w = static_cast<int>(char_w);

    std::vector<float> samples;
    double x = static_cast<double>(text_width - int_char_w - 1);
    while (x >= 0.0) {
        const int x_start = static_cast<int>(x);
        const int x_end = static_cast<int>(x + int_char_w);
        for (int yy = 0; yy < char_h; ++yy) {
            for (int xx = x_start - 1; xx < x_end + 1; ++xx) {
                const int clamped_x = std::clamp(xx, 0, text_width - 1);
                const int src_x = start_x + clamped_x;
                const int src_y = start_y + y_start + yy;
                if (src_y < 0 || src_y >= frame.height) {
                    samples.push_back(0.0f);
                } else {
                    const auto value = frame.gray[static_cast<size_t>(src_y) * static_cast<size_t>(frame.width) + static_cast<size_t>(src_x)];
                    samples.push_back(static_cast<float>(value) / 255.0f);
                }
            }
        }
        x -= char_w;
    }

    return samples;
}

} // namespace

ScoutOcr::ScoutOcr(std::filesystem::path onnx_model_path, std::filesystem::path label_map_path)
    : onnx_model_path_(std::move(onnx_model_path)),
      label_map_path_(std::move(label_map_path)) {
}

void ScoutOcr::request_async() {
    if (has_pending_) {
        return;
    }

    // Launch the OCR task. When complete, either store the future (legacy)
    // or invoke the registered callback with the result.
    has_pending_ = true;
    std::thread([this]() {
        const auto result = run_ocr_task();
        has_pending_ = false;
        if (callback_) {
            callback_(result);
        } else {
            // If no callback registered, keep behavior similar to before by
            // storing the result in the future for poll() to pick up.
            // We can't set a std::future from here, so use a packaged_task.
            std::packaged_task<OcrResult()> task([result]() { return result; });
            future_ = task.get_future();
            task(); // run immediately to make future ready
        }
    }).detach();
}

std::optional<OcrResult> ScoutOcr::poll() {
    if (!future_.valid()) {
        return std::nullopt;
    }

    if (future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return std::nullopt;
    }

    try {
        return future_.get();
    } catch (...) {
        return std::nullopt;
    }
}

void ScoutOcr::set_callback(Callback cb) {
    callback_ = std::move(cb);
}

std::string ScoutOcr::get_xyz_ocr_text() const {
    if (!std::filesystem::exists(onnx_model_path_) || !std::filesystem::exists(label_map_path_)) {
        return {};
    }

    const auto frame = capture_frame();
    if (!frame || (frame->width < 500 && frame->height < 500)) {
        return {};
    }

    const auto labels = load_label_map(label_map_path_);
    const auto input_values = extract_characters_rtl(*frame);
    if (input_values.empty()) {
        return {};
    }

    const int64_t sample_count = static_cast<int64_t>(input_values.size() / (14 * 9));
    std::vector<int64_t> predicted_labels;
    std::string error_message;
    if (!predict_labels_onnx(onnx_model_path_.string(), input_values, sample_count, predicted_labels, error_message)) {
        std::cerr << "ONNX inference error: " << error_message << '\n';
        return {};
    }

    std::string text;
    text.reserve(predicted_labels.size());
    for (const auto label : predicted_labels) {
        const auto it = labels.find(static_cast<int>(label));
        text += (it != labels.end()) ? it->second : "?";
    }
    std::reverse(text.begin(), text.end());
    return text;
}

OcrResult ScoutOcr::run_ocr_task() const {
    const auto task_start = std::chrono::steady_clock::now();
    auto stamp_task_time = [&](OcrResult& value) {
        const auto task_end = std::chrono::steady_clock::now();
        value.task_time_ms = std::chrono::duration<double, std::milli>(task_end - task_start).count();
    };

    OcrResult result;
    const std::string text = get_xyz_ocr_text();
    if (text.empty()) {
        stamp_task_time(result);
        return result;
    }

    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    std::string locationmarker;
    if (try_parse_xyz_from_ocr_text(text, x, y, z, locationmarker)) {
        result.x = x;
        result.y = y;
        result.z = z;
        result.locationmarker = locationmarker;
    }

    stamp_task_time(result);
    return result;
}
