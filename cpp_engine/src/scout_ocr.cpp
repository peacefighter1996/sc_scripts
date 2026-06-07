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
#include <memory>
#include <regex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <cstdint>

namespace {

    constexpr const char* kWindowName = "Star Citizen ";
    constexpr const char* kCaptureMode = "screen";

    struct CapturedFrame {
        int width{};
        int height{};
        std::vector<std::uint8_t> bgra;
        std::uint8_t gray(int x, int y) const {
            return bgra[((y * width) + x) * 4 + 0] * 0.114f
                + bgra[((y * width) + x) * 4 + 1] * 0.587f
                + bgra[((y * width) + x) * 4 + 2] * 0.299f;
        }
    };

    // (no thread-local reuse here; per-`ScoutOcr` instance buffers are used)

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
            }
            else if (token == "colon") {
                token = ":";
            }
            else if (token == "dot") {
                token = ".";
            }
            else if (token == "dash") {
                token = "-";
            }
            else if (token == "comma") {
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
        WindowSearchContext context{ title_substring, nullptr };
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

    bool capture_rect(const RECT& rect, void*& reusable_bitmap, void*& reusable_pixels, int& reusable_width, int& reusable_height, void*& reusable_mem_dc, void*& reusable_old_bitmap, int& out_width, int& out_height) {
        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;
        if (width <= 0 || height <= 0) {
            return false;
        }

        HDC screen_dc = GetDC(nullptr);
        HDC mem_dc;
        if (reusable_mem_dc) {
            mem_dc = reinterpret_cast<HDC>(reusable_mem_dc);
        } else {
            mem_dc = CreateCompatibleDC(screen_dc);
            reusable_mem_dc = reinterpret_cast<void*>(mem_dc);
            reusable_old_bitmap = nullptr;
        }

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight = -height;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* pixels = nullptr;
        HBITMAP bitmap = nullptr;

        // If existing reusable bitmap matches size, reuse it. Otherwise recreate.
        if (reusable_bitmap && reusable_width == width && reusable_height == height && reusable_pixels) {
            bitmap = reinterpret_cast<HBITMAP>(reusable_bitmap);
            pixels = reusable_pixels;
            // bitmap already selected into mem_dc (from creation time); just blit into it.
            BitBlt(mem_dc, 0, 0, width, height, screen_dc, rect.left, rect.top, SRCCOPY | CAPTUREBLT);
        } else {
            // Need to create or replace the reusable bitmap. If replacing, deselect old bitmap first.
            if (reusable_bitmap) {
                if (reusable_old_bitmap) {
                    SelectObject(mem_dc, reinterpret_cast<HGDIOBJ>(reusable_old_bitmap));
                }
                DeleteObject(reinterpret_cast<HGDIOBJ>(reusable_bitmap));
                reusable_bitmap = nullptr;
                reusable_pixels = nullptr;
                reusable_width = reusable_height = 0;
            }
            bitmap = CreateDIBSection(screen_dc, &bmi, DIB_RGB_COLORS, &pixels, nullptr, 0);
            if (!bitmap) {
                ReleaseDC(nullptr, screen_dc);
                return false;
            }
            // select the new bitmap into the persistent mem_dc and remember the old selection
            HGDIOBJ old_bitmap = SelectObject(mem_dc, bitmap);
            BitBlt(mem_dc, 0, 0, width, height, screen_dc, rect.left, rect.top, SRCCOPY | CAPTUREBLT);

            // Adopt the created DIBSection as the reusable buffer.
            reusable_bitmap = reinterpret_cast<void*>(bitmap);
            reusable_pixels = pixels;
            reusable_width = width;
            reusable_height = height;
            reusable_old_bitmap = reinterpret_cast<void*>(old_bitmap);
        }

        out_width = width;
        out_height = height;

        // Grayscale conversion moved to caller so this helper only fills the DIBSection.

        // for (int y = 0; y < height; ++y) {
        //     for (int x = 0; x < width; ++x) {
        //         const size_t offset = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4;
        //         const float b = static_cast<float>(frame->bgra[offset + 0]);
        //         const float g = static_cast<float>(frame->bgra[offset + 1]);
        //         const float r = static_cast<float>(frame->bgra[offset + 2]);
        //         frame->gray[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] = static_cast<std::uint8_t>(std::clamp((0.114f * b) + (0.587f * g) + (0.299f * r), 0.0f, 255.0f));
        //     }
        // }

        // Do not delete the persistent mem_dc here; it is owned by the caller and reused.
        ReleaseDC(nullptr, screen_dc);
        return true;
    }

    bool capture_frame(void*& reusable_bitmap, void*& reusable_pixels, int& reusable_width, int& reusable_height, void*& reusable_mem_dc, void*& reusable_old_bitmap, int& out_width, int& out_height) {
        const auto monitors = enumerate_monitors();
        if (monitors.empty()) {
            return false;
        }

        if (std::string(kCaptureMode) == "window") {
            const auto rect = get_window_rect_by_title(std::wstring(kWindowName, kWindowName + std::strlen(kWindowName)));
            if (!rect) {
                return false;
            }
                return capture_rect(*rect, reusable_bitmap, reusable_pixels, reusable_width, reusable_height, reusable_mem_dc, reusable_old_bitmap, out_width, out_height);
        }

        const auto game_window_rect = get_window_rect_by_title(std::wstring(kWindowName, kWindowName + std::strlen(kWindowName)));
        if (!game_window_rect) {
            return false;
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
            return false;
        }

        return capture_rect(monitors[best_index], reusable_bitmap, reusable_pixels, reusable_width, reusable_height, reusable_mem_dc, reusable_old_bitmap, out_width, out_height);
    }

    std::vector<float> extract_characters_rtl(const void* pixels_void, int width, int height) {
        // This function now expects a grayscale buffer pointer. If callers pass BGRA,
        // reinterpretation will still work but we compute grayscale earlier into a buffer.
        const auto gray = reinterpret_cast<const std::uint8_t*>(pixels_void);
        const int start_y = 30;
        const int end_y = 44;
        const int start_x = std::max(0, width - 1000);
        const int end_x = std::max(start_x, width - 4);
        const int text_width = end_x - start_x;
        const int text_height = end_y - start_y;
        if (text_width <= 0 || text_height <= 0 || end_y > height) {
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
                    if (src_y < 0 || src_y >= height) {
                        samples.push_back(0.0f);
                    }
                    else {
                        const size_t idx = (static_cast<size_t>(src_y) * static_cast<size_t>(width) + static_cast<size_t>(src_x));
                        samples.push_back(static_cast<float>(gray[idx]) / 255.0f);
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

    if (std::filesystem::exists(label_map_path_)) {
        try {
            labels_ = load_label_map(label_map_path_);
        }
        catch (const std::exception& ex) {
            std::cerr << "Failed to load label map: " << ex.what() <<
                "\nMake sure the label map path is correct and the file is properly formatted.\n";
            return;
        }
    }
#ifdef SCOUT_HAS_ONNXRUNTIME
    if (std::filesystem::exists(onnx_model_path_)) {
        try {
        std::string error_message;
        onnx_session_ = create_onnx_session(onnx_model_path_.string(), error_message);
        if (onnx_session_ == nullptr) {
            std::cerr << "Failed to create ONNX session: " << error_message << '\n';
            return;
        }
        }
        catch (const std::exception& ex) {
            std::cerr << "Failed to create ONNX session: " << ex.what() <<
                "\nMake sure the model path is correct and ONNX Runtime is properly set up.\n";
            return;
        }
    }
#endif
        
    return;
}

ScoutOcr::~ScoutOcr() {
    // clear subscribers to avoid callbacks into destructed object
    {
        std::lock_guard<std::mutex> lk(subs_mtx_);
        subscribers_.clear();
    }

    // ensure any pending work finishes before destroying the session
    stop_and_wait();

    // remove session after stopping to avoid use-after-free in running tasks
#ifdef SCOUT_HAS_ONNXRUNTIME
    if (onnx_session_) {
        delete onnx_session_;
        onnx_session_ = nullptr;
    }
#endif
    if (reusable_bitmap_) {
        // Ensure the bitmap is deselected from the persistent DC before deleting.
        if (reusable_mem_dc_) {
            HDC memdc = reinterpret_cast<HDC>(reusable_mem_dc_);
            if (reusable_old_bitmap_) {
                SelectObject(memdc, reinterpret_cast<HGDIOBJ>(reusable_old_bitmap_));
            }
            DeleteDC(memdc);
            reusable_mem_dc_ = nullptr;
        }
        DeleteObject(reinterpret_cast<HGDIOBJ>(reusable_bitmap_));
        reusable_bitmap_ = nullptr;
        reusable_pixels_ = nullptr;
        reusable_width_ = reusable_height_ = 0;
        reusable_old_bitmap_ = nullptr;
    }
}


void ScoutOcr::request_async() {
    bool expected = false;
    if (!has_pending_.compare_exchange_strong(expected, true)) {
        return;
    }

    // Launch async OCR work using std::async so we keep a future we can wait on.
    future_ = std::async(std::launch::async, [this]() {
        const auto result = run_ocr_task();
        has_pending_ = false;

        // copy subscribers under lock to avoid holding lock while invoking
        std::vector<Callback> subs;
        {
            std::lock_guard<std::mutex> lk(subs_mtx_);
            subs.reserve(subscribers_.size());
            for (const auto& kv : subscribers_) subs.push_back(kv.second);
        }
        for (const auto& cb : subs) {
            try { cb(result); }
            catch (...) {}
        }

        return result;
        });
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
    }
    catch (...) {
        return std::nullopt;
    }
}

void ScoutOcr::set_callback(Callback cb) {
    // replace any previous single callback subscription
    if (single_sub_id_ != 0) {
        unsubscribe(single_sub_id_);
        single_sub_id_ = 0;
    }
    if (cb) {
        single_sub_id_ = subscribe(std::move(cb));
    }
}

ScoutOcr::SubscriptionId ScoutOcr::subscribe(Callback cb) {
    std::lock_guard<std::mutex> lk(subs_mtx_);
    const SubscriptionId id = next_sub_id_++;
    subscribers_.emplace(id, std::move(cb));
    return id;
}

void ScoutOcr::unsubscribe(SubscriptionId id) {
    std::lock_guard<std::mutex> lk(subs_mtx_);
    subscribers_.erase(id);
}

void ScoutOcr::stop_and_wait() {
    stop();
    // wait for pending future to complete
    if (future_.valid()) {
        try { future_.wait(); }
        catch (...) {}
    }
}

void ScoutOcr::stop() {
    active = false;
}

std::string ScoutOcr::get_coordinates_ocr_text() const {
    int width = 0;
    int height = 0;
    if (!capture_frame(reusable_bitmap_, reusable_pixels_, reusable_width_, reusable_height_, reusable_mem_dc_, reusable_old_bitmap_, width, height)) {
        return {};
    }
    if (width < 200 || height < 100) {
        return {};
    }
        // Compute grayscale into per-instance buffer for fast sampling.
        {
            std::vector<std::uint8_t>& gray = reusable_gray_;
            gray.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
            const std::uint8_t* src = reinterpret_cast<const std::uint8_t*>(reusable_pixels_);
            std::uint8_t* dst = gray.data();
            const size_t px_count = static_cast<size_t>(width) * static_cast<size_t>(height);
            size_t i = 0;
            // Unrolled loop for small speedup; integer approx for coefficients.
            for (; i + 4 <= px_count; i += 4) {
                for (int k = 0; k < 4; ++k) {
                    const std::uint8_t b = src[0];
                    const std::uint8_t g = src[1];
                    const std::uint8_t r = src[2];
                    const unsigned grayv = (29u * b + 150u * g + 77u * r + 128u) >> 8;
                    dst[0] = static_cast<std::uint8_t>(grayv);
                    dst++;
                    src += 4;
                }
            }
            for (; i < px_count; ++i) {
                const std::uint8_t b = src[0];
                const std::uint8_t g = src[1];
                const std::uint8_t r = src[2];
                const unsigned grayv = (29u * b + 150u * g + 77u * r + 128u) >> 8;
                *dst++ = static_cast<std::uint8_t>(grayv);
                src += 4;
            }
        }

        const auto input_values = extract_characters_rtl(reusable_gray_.data(), width, height);

    const int64_t sample_count = static_cast<int64_t>(input_values.size() / (14 * 9));
    std::vector<int64_t> predicted_labels;
    std::string error_message;
#if defined(SCOUT_HAS_ONNXRUNTIME)
    if (!predict_labels_onnx_session(*onnx_session_, input_values, sample_count, predicted_labels, error_message)) {
        std::cerr << "ONNX inference error: " << error_message << '\n';
        return {};
    }
#endif

    std::string text;
    text.reserve(predicted_labels.size());
    for (const auto label : predicted_labels) {
        const auto it = labels_.find(static_cast<int>(label));
        text += (it != labels_.end()) ? it->second : "?";
    }
    std::reverse(text.begin(), text.end());
    return text;
}

OcrResult ScoutOcr::run_ocr_task() const {
    const auto task_start = std::chrono::steady_clock::now();

    OcrResult result;
    const std::string text = get_coordinates_ocr_text();
    if (!text.empty()) {
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
    }

    const auto task_end = std::chrono::steady_clock::now();
    result.task_time_ms = std::chrono::duration<double, std::milli>(task_end - task_start).count();

    return result;
}
