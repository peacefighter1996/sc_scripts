#include "scout_app.h"

#include "scout_ocr.h"
#include "scout_core.h"
#include "timer_display.h"
#include "timer_footer_renderer.h"

#define NOMINMAX
#include <Windows.h>
#include <gdiplus.h>
#include <glad/glad.h>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include "scout_render.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cfloat>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#pragma comment(lib, "gdiplus.lib")

namespace {

    constexpr double kPi = 3.14159265358979323846;
    constexpr int kMaxQuality = 1000;
    constexpr int kMinQuality = 0;
    constexpr double kTargetHz = 30.0;
    constexpr double kFrameTime = 1.0 / kTargetHz;

    const std::vector<std::string> kDefaultServerIds{ "eu10", "eu180", "us170", "All" };
    const std::vector<std::string> kDefaultPlanets{ "Pyro_A5_Ignis", "Pyro_E5_Fuego", "Pyro_Pyro2_Monox", "Pyro_Pyro4" };
    const std::vector<std::string> kDefaultMaterials{ "All", "Aphorite", "Aslarite", "Beryl", "Borase", "Copper", "Dolivine", "Gold", "Hephaestanite", "Iron", "Janalite", "Laranite", "Riccite", "Stileron", "Taranite", "Tin" };

    const std::unordered_map<std::string, std::string> kMaterialIds = {
        {"Hephaestanite", "HEPH"},
        {"Iron", "IRON"},
        {"Gold", "GOLD"},
        {"Janalite", "JANA"},
        {"Aphorite", "APHO"},
        {"Dolivine", "DOLI"},
        {"Aslarite", "ASLAR"},
        {"Beryl", "BERY"},
        {"Taranite", "TARA"},
        {"Laranite", "LARA"},
        {"Stileron", "SILI"},
        {"Copper", "COPP"},
    };

    int find_column_index(const std::vector<std::string>& headers, const std::vector<std::string>& candidates) {
        for (size_t i = 0; i < headers.size(); ++i) {
            const auto header = to_lower(trim(headers[i]));
            for (const auto& candidate : candidates) {
                if (header == candidate) {
                    return static_cast<int>(i);
                }
            }
        }
        return -1;
    }

    // --- Simple datetime helpers for Time editing ---
    inline std::array<int, 5> now_ymdhm() {
        using namespace std::chrono;
        const auto t = system_clock::now();
        std::time_t tt = system_clock::to_time_t(t);
        std::tm tm;
#if defined(_WIN32)
        localtime_s(&tm, &tt);
#else
        localtime_r(&tt, &tm);
#endif
        return { tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min };
    }

    inline bool parse_iso_datetime(const std::string& s, std::array<int, 5>& out) {
        if (s.empty()) return false;
        int y = 0, m = 0, d = 0, hh = 0, mm = 0;
        // Accept formats: YYYY-MM-DD HH:MM or YYYY-MM-DDTHH:MM or YYYY-MM-DD
        if (std::sscanf(s.c_str(), "%4d-%2d-%2d %2d:%2d", &y, &m, &d, &hh, &mm) >= 3) {
            out = { y,m,d,hh,mm };
            return true;
        }
        if (std::sscanf(s.c_str(), "%4d-%2d-%2dT%2d:%2d", &y, &m, &d, &hh, &mm) >= 3) {
            out = { y,m,d,hh,mm };
            return true;
        }
        if (std::sscanf(s.c_str(), "%4d-%2d-%2d", &y, &m, &d) == 3) {
            out = { y,m,d,0,0 };
            return true;
        }
        return false;
    }

    inline std::string format_iso_datetime(const std::array<int, 5>& v) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", v[0], v[1], v[2], v[3], v[4]);
        return std::string(buf);
    }

    struct TextureEntry {
        GLuint texture{};
        int width{};
        int height{};
    };

    class GdiplusSession {
    public:
        GdiplusSession() {
            Gdiplus::GdiplusStartupInput startup_input;
            Gdiplus::GdiplusStartup(&token_, &startup_input, nullptr);
        }

        ~GdiplusSession() {
            if (token_ != 0) {
                Gdiplus::GdiplusShutdown(token_);
            }
        }

    private:
        ULONG_PTR token_{};
    };

    std::filesystem::path detect_repo_root() {
        std::vector<std::filesystem::path> candidates;
        candidates.push_back(std::filesystem::current_path());

        wchar_t exe_path[MAX_PATH];
        const auto len = GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
        if (len > 0) {
            candidates.push_back(std::filesystem::path(exe_path).parent_path());
        }

        for (auto candidate : candidates) {
            for (int depth = 0; depth < 6; ++depth) {
                if (std::filesystem::exists(candidate / "data" / "label_map.json")) {
                    return candidate;
                }
                if (!candidate.has_parent_path()) {
                    break;
                }
                candidate = candidate.parent_path();
            }
        }

        return std::filesystem::current_path();
    }

    std::wstring to_wstring(const std::filesystem::path& path) {
        return path.wstring();
    }

    std::vector<std::string> load_server_ids_csv(const std::filesystem::path& path, const std::vector<std::string>& defaults) {
        std::ifstream in(path);
        if (!in.is_open()) {
            return defaults;
        }

        bool header_parsed = false;
        int value_index = 0;
        std::vector<std::string> values;
        std::string line;
        while (std::getline(in, line)) {
            const auto trimmed = trim(line);
            if (trimmed.empty()) {
                continue;
            }

            const auto cells = split_csv_row(trimmed);
            if (cells.empty()) {
                continue;
            }

            if (!header_parsed) {
                value_index = find_column_index(cells, { "value", "server", "server_id", "id" });
                if (value_index < 0) {
                    value_index = 0;
                }
                header_parsed = true;
                continue;
            }

            if (value_index >= static_cast<int>(cells.size())) {
                continue;
            }

            const auto value = trim(cells[static_cast<size_t>(value_index)]);
            if (value.empty()) {
                continue;
            }

            if (std::find(values.begin(), values.end(), value) == values.end()) {
                values.push_back(value);
            }
        }

        return values.empty() ? defaults : values;
    }

    std::vector<Resource> load_material_catalog(const std::filesystem::path& path, const std::vector<std::string>& default_names, const std::unordered_map<std::string, std::string>& default_shorts) {
        std::ifstream in(path);
        if (!in.is_open()) {
            return {};
        }

        std::vector<Resource> catalog;
        bool header_parsed = false;
        int name_index = -1;
        int shortname_index = -1;
        int id_index = -1;
        int type_index = -1;
        int harvest_type_index = -1;

        std::string line;
        while (std::getline(in, line)) {
            const auto trimmed = trim(line);
            if (trimmed.empty()) {
                continue;
            }

            const auto cells = split_csv_row(trimmed);
            if (cells.empty()) {
                continue;
            }

            if (!header_parsed) {
                id_index = find_column_index(cells, { "id" });
                name_index = find_column_index(cells, { "name", "material", "value" });
                shortname_index = find_column_index(cells, { "shortname", "short_name", "short", "code" });
                if (name_index < 0) {
                    name_index = std::min(1, static_cast<int>(cells.size()) - 1);
                }
                type_index = find_column_index(cells, { "type", "resourcetype", "resource_type" });
                harvest_type_index = find_column_index(cells, { "harvest_type", "harvesttype" });

                header_parsed = true;
                continue;
            }

            if (name_index < 0 || name_index >= static_cast<int>(cells.size())) {
                continue;
            }

            const auto name = trim(cells[static_cast<size_t>(name_index)]);
            if (name.empty()) {
                continue;
            }

            std::string shortname;
            if (shortname_index >= 0 && shortname_index < static_cast<int>(cells.size())) {
                shortname = trim(cells[static_cast<size_t>(shortname_index)]);
            }

            int id = -1;
            if (id_index >= 0 && id_index < static_cast<int>(cells.size())) {
                try {
                    id = std::stoi(trim(cells[static_cast<size_t>(id_index)]));
                }
                catch (const std::exception&) {
                    // ignore parse errors and just use default ids
                }
            }

            ResourceType type = ResourceType::None;
            if (type_index >= 0 && type_index < static_cast<int>(cells.size()))
            {
                const auto type_str = to_lower(trim(cells[static_cast<size_t>(type_index)]));
                if (type_str == "mineral") {
                    type = ResourceType::Mineral;
                } else if (type_str == "plant") {
                    type = ResourceType::Plant;
                }
            }

            HarvestType harvest_type = HarvestType::None;
            if (harvest_type_index >= 0 && harvest_type_index < static_cast<int>(cells.size()))
            {
                
                const auto harvest_str = to_lower(trim(cells[static_cast<size_t>(harvest_type_index)]));
                if (harvest_str.find("fps") != std::string::npos) {
                    harvest_type = static_cast<HarvestType>(static_cast<int>(harvest_type) | static_cast<int>(HarvestType::FPS));
                }
                if (harvest_str.find("vehicle") != std::string::npos) {
                    harvest_type = static_cast<HarvestType>(static_cast<int>(harvest_type) | static_cast<int>(HarvestType::Vehicle));
                }
                if (harvest_str.find("ship") != std::string::npos) {
                    harvest_type = static_cast<HarvestType>(static_cast<int>(harvest_type) | static_cast<int>(HarvestType::Ship));
                }
            }


            Resource material{ id, name, shortname };
            catalog.push_back(material);
        }

        if (catalog.empty()) {
            for (const auto& name : default_names) {
                const auto it = default_shorts.find(name);
                const auto short_name = it != default_shorts.end() ? it->second : name.substr(0, std::min<size_t>(4, name.size()));
                catalog.push_back(Resource{ -1, name, short_name });
            }
        }

        return catalog;
    }

    std::vector<Planet> load_planet_catalog(const std::filesystem::path& path, const std::vector<std::string>& defaults) {
        std::ifstream in(path);
        if (!in.is_open()) {
            std::vector<Planet> catalog;
            for (const auto& key : defaults) {
                catalog.push_back(Planet{ -1, "", key, key, "" });
            }
            return catalog;
        }

        std::vector<Planet> catalog;
        bool header_parsed = false;
        int image_dir_index = -1;
        int planet_name_index = -1;
        int id_index = -1;
        int system_index = -1;
        int zone_id_index = -1;

        std::string line;
        while (std::getline(in, line)) {
            const auto trimmed = trim(line);
            if (trimmed.empty()) {
                continue;
            }

            const auto cells = split_csv_row(trimmed);
            if (cells.empty()) {
                continue;
            }

            if (!header_parsed) {
                //id,system,planet,image_dir,zone_id
                id_index = find_column_index(cells, { "id", "planet_id", "key" });
                image_dir_index = find_column_index(cells, { "image_dir", "imagedir", "image" });
                planet_name_index = find_column_index(cells, { "planet", "name", "value" });
                system_index = find_column_index(cells, { "system" });
                zone_id_index = find_column_index(cells, { "zone_id", "zoneid", "zone" });
                header_parsed = true;
                continue;
            }

            int id = -1;
            std::string system;
            std::string zone_id;
            std::string planet_name;
            std::string img_dir;

            if (id_index >= 0 && id_index < static_cast<int>(cells.size())) {
                try {
                    id = std::stoi(trim(cells[static_cast<size_t>(id_index)]));
                }
                catch (const std::exception&) {
                    // ignore parse errors and just use default ids
                }
            }

            if (system_index >= 0 && system_index < static_cast<int>(cells.size())) {
                system = trim(cells[static_cast<size_t>(system_index)]);
            }

            if (zone_id_index >= 0 && zone_id_index < static_cast<int>(cells.size())) {
                zone_id = trim(cells[static_cast<size_t>(zone_id_index)]);
            }

            if (planet_name_index >= 0 && planet_name_index < static_cast<int>(cells.size())) {
                planet_name = trim(cells[static_cast<size_t>(planet_name_index)]);
            }

            if (image_dir_index >= 0 && image_dir_index < static_cast<int>(cells.size())) {
                img_dir = trim(cells[static_cast<size_t>(image_dir_index)]);
            }

            if (planet_name.empty()) {
                continue;
            }

            Planet planet{ id, system, planet_name, img_dir, zone_id };
            catalog.push_back(planet);
        }

        if (catalog.empty()) {
            for (const auto& key : defaults) {
                catalog.push_back(Planet{ -1, "", key, key, "" });
            }
        }

		// Ensure catalog is sorted by name for consistent ordering in UI
        std::sort(catalog.begin(), catalog.end(), [](const Planet& a, const Planet& b) {
            return a.name < b.name;
			});

        return catalog;
    }

    GLuint load_texture_file(const std::filesystem::path& path) {
        if (!std::filesystem::exists(path)) {
            return 0;
        }

        Gdiplus::Bitmap bitmap(to_wstring(path).c_str());
        if (bitmap.GetLastStatus() != Gdiplus::Ok) {
            return 0;
        }

        const int width = static_cast<int>(bitmap.GetWidth());
        const int height = static_cast<int>(bitmap.GetHeight());
        if (width <= 0 || height <= 0) {
            return 0;
        }

        Gdiplus::Rect rect(0, 0, width, height);
        Gdiplus::BitmapData data{};
        if (bitmap.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok) {
            return 0;
        }

        std::vector<std::uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
        for (int y = 0; y < height; ++y) {
            const auto* src = reinterpret_cast<const std::uint8_t*>(static_cast<const std::uint8_t*>(data.Scan0) + (static_cast<size_t>(y) * static_cast<size_t>(data.Stride)));
            std::memcpy(pixels.data() + (static_cast<size_t>(y) * static_cast<size_t>(width) * 4), src, static_cast<size_t>(width) * 4);
        }
        bitmap.UnlockBits(&data);

        GLuint texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, 0x80E1, GL_UNSIGNED_BYTE, pixels.data());
        return texture;
    }

    std::pair<float, float> latlon_to_uv(double lat, double lon) {
        const auto u = static_cast<float>((lon + 180.0) / 360.0);
        const auto v = static_cast<float>((lat + 90.0) / 180.0);
        return { u, v };
    }

    struct AppSettings {
        bool show_timings{ false };
        bool auto_update_ocr_newpoint_enabled{ true };
        bool ocr_feed_planet_update_enabled{ true };
    };



    struct AppState {
        AppSettings settings;

        std::filesystem::path repo_root;
        std::filesystem::path csv_path;
        std::filesystem::path onnx_model_path;
        std::filesystem::path label_map_path;
        std::filesystem::path planets_dir;
        std::filesystem::path server_ids_path;
        std::filesystem::path planets_csv_path;
        std::filesystem::path materials_path;

        std::vector<DataPoint> points;
        std::vector<DataPoint> filtered_points;
        std::vector<std::string> server_ids;
        std::vector<std::string> planets;
        std::vector<std::string> materials;
        std::vector<Planet> planet_catalog;
        std::vector<Resource> material_catalog;
        ScoutOcr::SubscriptionId ocr_subscription_id;

        double x;
        double y;
        double z;

        std::string selected_planet;
        std::string last_detected_region;
        std::string selected_material;
        std::string selected_server;
        std::optional<std::string> last_detected_rock;
        int quality_min{ kMinQuality };
        int quality_max{ kMaxQuality };
        DataPoint new_data{};
        std::unordered_map<std::string, GLuint> texture_cache;
        std::optional<std::string> hovered_text;

        bool data_form_active{ false };

        // OCR results pushed from worker thread are stored here for main-thread processing
        std::mutex ocr_mutex;
        std::vector<OcrResult> ocr_results;

        AppState()
            : repo_root(detect_repo_root()),
            csv_path(repo_root / "data" / "geoscout.csv"),
            onnx_model_path(repo_root / "data" / "best_pareto_model.onnx"),
            label_map_path(repo_root / "data" / "label_map.json"),
            planets_dir(repo_root / "images" / "planets"),
            server_ids_path(repo_root / "data" / "server_ids.csv"),
            planets_csv_path(repo_root / "data" / "planets.csv"),
            materials_path(repo_root / "data" / "resources.csv") {

            settings = load_settings(repo_root / "config" / "settings.ini");

            server_ids = load_server_ids_csv(server_ids_path, kDefaultServerIds);
            this->planet_catalog = load_planet_catalog(planets_csv_path, kDefaultPlanets);
            for (const auto& planet : planet_catalog) {
                this->planets.push_back(planet.name);
            }
            this->material_catalog = load_material_catalog(materials_path, kDefaultMaterials, kMaterialIds);

            for (const auto& material : material_catalog) {
                materials.push_back(material.name);
            }
            selected_planet = planets.front();
            selected_material = materials.front();
            selected_server = server_ids.front();
            new_data.server = selected_server;
            new_data.planet = selected_planet;
            new_data.material = selected_material;
            new_data.quality_min = 0;
            new_data.quality_max = kMaxQuality;
        }

        ~AppState() {
            for (const auto& [_, texture] : texture_cache) {
                if (texture != 0) {
                    glDeleteTextures(1, &texture);
                }
            }
        }

        void reload_planet_data() {
            points = load_points(csv_path.string());
            int max_id = 0;
            for (const auto& point : points) {
                max_id = std::max(max_id, point.id);
            }
            new_data.id = max_id + 1;
        }

        void filter_points() {
            filtered_points.clear();
            for (const auto& point : points) {
                const bool material_match = point.material == selected_material || selected_material == "All" || point.location;
                const bool planet_match = point.planet == selected_planet;
                const bool server_match = point.server == selected_server || selected_server == "All" || point.location;
                if (material_match && planet_match && server_match) {
                    filtered_points.push_back(point);
                }
            }
        }

        GLuint get_texture_for_selected_planet() {
            auto it = texture_cache.find(selected_planet);
            if (it != texture_cache.end()) {
                return it->second;
            }

            // selected to image dir for planet in planets.csv, if not found fallback to looking for image named after planet key directly in planets dir

            std::string image_dir_name = selected_planet;
            for (const auto& planet : planet_catalog) {
                if (planet.name == selected_planet && !planet.image_dir.empty()) {
                    image_dir_name = planet.image_dir;
                    break;
                }
            }


            std::filesystem::path texture_path = planets_dir / image_dir_name / "planet.jpg";
            const auto dir_it = std::find_if(planet_catalog.begin(), planet_catalog.end(), [this](const Planet& p) {
                return p.name == selected_planet;
                });
            if (dir_it != planet_catalog.end() && !dir_it->image_dir.empty()) {
                texture_path = planets_dir / dir_it->image_dir / "planet.jpg";
            }

            GLuint texture = load_texture_file(texture_path);
            if (texture == 0) {
                texture = load_texture_file(planets_dir / (selected_planet + ".jpg"));
            }
            texture_cache[selected_planet] = texture;
            return texture;
        }
        void save_settings() {
            std::ofstream out(repo_root / "config" / "settings.ini");
            if (!out.is_open()) {
                return;
            }
            out << "show_timings=" << (settings.show_timings ? "1" : "0") << '\n';
            out << "ocr_feed_newpoint_enabled=" << (settings.auto_update_ocr_newpoint_enabled ? "1" : "0") << '\n';
            out << "ocr_feed_planet_update_enabled=" << (settings.ocr_feed_planet_update_enabled ? "1" : "0") << '\n';
        }
        AppSettings load_settings(const std::filesystem::path& path) {
            AppSettings settings;
            std::ifstream in(path);
            if (!in.is_open()) {
                return settings;
            }

            std::string line;
            while (std::getline(in, line)) {
                const auto trimmed = trim(line);
                if (trimmed.empty() || trimmed[0] == '#') {
                    continue;
                }

                const auto delimiter_pos = trimmed.find('=');
                if (delimiter_pos == std::string::npos) {
                    continue;
                }

                const auto key = trim(trimmed.substr(0, delimiter_pos));
                const auto value = trim(trimmed.substr(delimiter_pos + 1));

                if (key == "show_timings") {
                    settings.show_timings = (value == "1");
                }
                else if (key == "ocr_feed_newpoint_enabled") {
                    settings.auto_update_ocr_newpoint_enabled = (value == "1");
                }
                else if (key == "ocr_feed_planet_update_enabled") {
                    settings.ocr_feed_planet_update_enabled = (value == "1");
                }
            }

            return settings;

        }

        std::vector<double> process_ocr_results() {
            std::vector<OcrResult> results_to_process;
            {
                std::lock_guard<std::mutex> lk(ocr_mutex);
                results_to_process.swap(ocr_results);
            }

            if (results_to_process.empty()) {
                return {}; 
            }
            std::vector<double> ocr_values;

            for (const auto& result : results_to_process) {
                if (settings.auto_update_ocr_newpoint_enabled
                    && result.x.has_value()
                    && result.y.has_value()
                    && result.z.has_value()) {
                    new_data.x = result.x.value();
                    new_data.y = result.y.value();
                    new_data.z = result.z.value();
                }
                else if(result.x.has_value()
                    && result.y.has_value()
                    && result.z.has_value() ) {
                    x = result.x.value();
                    y = result.y.value();
                    z = result.z.value();
                }

                //if (result.rock.has_value()) {
                //    if 
                //    new_data.material = result.rock.value();
                //}

                if (settings.ocr_feed_planet_update_enabled) {
                    if (result.locationmarker.has_value()) {
                        for (const auto& planet : planet_catalog) {
                            if (planet.zone_id.empty() ) {
                                continue;
                            }
                            if (result.locationmarker.value().find(planet.zone_id) != std::string::npos) {
                                // only update if different
                                if (planet.name != selected_planet) {
                                    selected_planet = planet.name;
                                    filter_points();
                                }
                                break;
                            }
                        }
                    }
                }
                ocr_values.push_back(result.task_time_ms);
            }
            return ocr_values;
        }

        void update_new_data_from_ocr(const OcrResult& result) {
            std::lock_guard<std::mutex> lk(ocr_mutex);
            ocr_results.push_back(result);
        }
    };

    bool combo_string(const char* label, const std::vector<std::string>& items, std::string& current_value) {
        auto current_it = std::find(items.begin(), items.end(), current_value);
        int current_index = current_it != items.end() ? static_cast<int>(std::distance(items.begin(), current_it)) : 0;
        std::vector<const char*> c_strs;
        c_strs.reserve(items.size());
        for (const auto& item : items) {
            c_strs.push_back(item.c_str());
        }

        if (ImGui::Combo(label, &current_index, c_strs.data(), static_cast<int>(c_strs.size()))) {
            current_value = items[static_cast<size_t>(current_index)];
            return true;
        }
        return false;
    }

} // namespace

// Overwrite the CSV with the provided points vector. Matches append_point header/order.
bool write_points_csv(const std::string& csv_path, const std::vector<DataPoint>& points) {
    const auto parent = std::filesystem::path(csv_path).parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
    }

    std::ofstream out(csv_path, std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    out << "recordid,server,x,y,z,planet,material,quality_min,quality_max,note,poi_type,poi_time\n";
    for (const auto& point : points) {
        out << point.id << ','
            << csv_escape(point.server) << ','
            << std::setprecision(15) << point.x << ','
            << std::setprecision(15) << point.y << ','
            << std::setprecision(15) << point.z << ','
            << csv_escape(point.planet) << ','
            << csv_escape(point.material) << ','
            << std::setprecision(15) << point.quality_min << ','
            << std::setprecision(15) << point.quality_max << ','
            << csv_escape(point.note) << ','
            << csv_escape(poi_type_name(point.poi_type)) << ','
            << csv_escape(point.time_info)
            << '\n';
    }

    return true;
}


int run_scout_app() {
    GdiplusSession gdiplus_session;



    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4); // Specify OpenGL version
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); // Use core profile
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "Scout Engine", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init();

    ScoutRenderer renderer;
    if (!renderer.init()) {
        std::cerr << "Failed to initialize renderer\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    AppState state;

    auto root_location = detect_repo_root();
    auto onnx_model_path = root_location / "data" / "best_pareto_model.onnx";
    auto label_map_path = root_location / "data" / "label_map.json";
    ScoutOcr ocr_instance(onnx_model_path, label_map_path);
    // Register OCR callback to push results into AppState queue
    state.ocr_subscription_id = ocr_instance.subscribe([&state](const OcrResult& result) {
        state.update_new_data_from_ocr(result);
        });

    // Timer to periodically call OCR on the current window content
    auto ocr_timer = std::thread([&ocr_instance]() {
        auto start = std::chrono::steady_clock::now();
        auto end = start;
        while (ocr_instance.active) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100) - (end - start));
            start = std::chrono::steady_clock::now();
            ocr_instance.request_async();
            end = std::chrono::steady_clock::now();
        }
        });
    // keep thread joinable so we can stop it cleanly on shutdown

    state.reload_planet_data();
    state.filter_points();
    TimerDisplay timer_display;
    TimerFooterRenderer timer_footer_renderer;

    bool location_on = false;
    bool f3_was_down = false;
    auto last_toggle = std::chrono::steady_clock::now();
    auto next_tick = std::chrono::steady_clock::now();

    while (!glfwWindowShouldClose(window)) {
        auto now = std::chrono::steady_clock::now();
        timer_display.loop_time_ms().stamp();

        // Simple fixed timestep loop with sleep to limit CPU usage. Not super precise but good enough for this use case.
        double actual_sleep = 0.0;
        if (next_tick > now) {
            const auto sleep_start = std::chrono::steady_clock::now();
            std::this_thread::sleep_for(next_tick - now);
            const auto sleep_end = std::chrono::steady_clock::now();
            actual_sleep = std::chrono::duration<double>(sleep_end - sleep_start).count();
        }


        const auto sleep_pct = kFrameTime > 0.0 ? std::min(100.0, (actual_sleep / kFrameTime) * 100.0) : 0.0;
        timer_display.sleep_percent().add_sample(sleep_pct);
        timer_display.work_time_ms().stamp();

        timer_display.ocr_poll_time_ms().stamp();
        // Process any OCR results pushed by the worker thread
        auto ocr_values = state.process_ocr_results();
        for (const auto& value : ocr_values) {
            timer_display.ocr_task_time_ms().add_sample(value);
        }
        timer_display.ocr_poll_time_ms().record_time_since_stamp();

        glfwPollEvents();

        const bool f3_is_down = glfwGetKey(window, GLFW_KEY_F3) == GLFW_PRESS;
        if (f3_is_down && !f3_was_down) {
            state.settings.show_timings = !state.settings.show_timings;
        }
        f3_was_down = f3_is_down;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("MAIN CONTROLS");
        if (ImGui::BeginTabBar("ControlsTabs")) {
            if (ImGui::BeginTabItem("Controls")) {
                if (ImGui::Button("Open Data Form")) {
                    state.data_form_active = true;
                }

                ImGui::Separator();

                if (combo_string("Server", state.server_ids, state.selected_server)) {
                    state.new_data.server = state.selected_server;
                    state.filter_points();
                }

                if (!state.settings.ocr_feed_planet_update_enabled) {
                    if (state.last_detected_region.empty()) {
                        ImGui::Text("Detected Region: None");
                    }
                    else {
                        ImGui::Text("Detected Region: %s", state.last_detected_region.c_str());
                        if (state.last_detected_region != state.selected_planet) {
                            if (ImGui::Button("Use Detected Region")) {
                                state.selected_planet = state.last_detected_region;
                                state.new_data.planet = state.last_detected_region;
                                state.filter_points();
                            }
                        }
                    }
                }

                if (combo_string("Planet", state.planets, state.selected_planet)) {
                    state.new_data.planet = state.selected_planet;
                    state.filter_points();
                }

                if (combo_string("Resource", state.materials, state.selected_material)) {
                    state.new_data.material = state.selected_material;
                    state.filter_points();
                }

                ImGui::Separator();

                ImGui::Text("New Point:");
                // if (state.last_detected_rock) {
                //     ImGui::Text("Last detected rock type: %s", state.last_detected_rock->c_str());
                //     if (ImGui::Button("Use Detected Rock Type")) {
                //         state.selected_material = *state.last_detected_rock;
                //         state.new_data.material = *state.last_detected_rock;
                //     }
                // }

                float input_x = static_cast<float>(state.new_data.x);
                float input_y = static_cast<float>(state.new_data.y);
                float input_z = static_cast<float>(state.new_data.z);
                ImGui::Checkbox("Auto updates new point coordinates", &state.settings.auto_update_ocr_newpoint_enabled);
                if (!state.settings.auto_update_ocr_newpoint_enabled) {
                    ImGui::Text("Last OCR values: X=%.2f Y=%.2f Z=%.2f", state.x, state.y, state.z);
                    if(ImGui::Button("Use Last OCR Values")) {
                        input_x = static_cast<float>(state.x);
                        input_y = static_cast<float>(state.y);
                        input_z = static_cast<float>(state.z);
                    };
                }
                ImGui::InputFloat("X", &input_x);
                ImGui::InputFloat("Y", &input_y);
                ImGui::InputFloat("Z", &input_z);
                state.new_data.x = input_x;
                state.new_data.y = input_y;
                state.new_data.z = input_z;
                ImGui::InputInt("Quality Min", &state.new_data.quality_min);
                ImGui::InputInt("Quality Max", &state.new_data.quality_max);

                if (ImGui::Button("Add Point")) {
                    DataPoint new_point;
                    new_point.id = state.new_data.id;
                    new_point.server = state.selected_server;
                    new_point.x = state.new_data.x;
                    new_point.y = state.new_data.y;
                    new_point.z = state.new_data.z;
                    new_point.planet = state.selected_planet;
                    new_point.material = state.selected_material;
                    new_point.location = false;
                    new_point.quality_min = state.new_data.quality_min;
                    new_point.quality_max = state.new_data.quality_max;
                    new_point.poi_type = PoiType::Mineral;
                    new_point.time_info = format_iso_datetime(now_ymdhm());
                    if (append_point(state.csv_path.string(), new_point)) {
                        state.points.push_back(new_point);
                        state.new_data.id += 1;
                    }
                }
                ImGui::EndTabItem();
            }
            // ImGui::EndTabItem();

            if (ImGui::BeginTabItem("Settings")) {
                ImGui::Checkbox("Show Timings (F3)", &state.settings.show_timings);
                ImGui::Separator();
                ImGui::Text("OCR Settings:");
                ImGui::Checkbox("Auto updates new point coordinates", &state.settings.auto_update_ocr_newpoint_enabled);
                ImGui::Checkbox("Auto updates active planet", &state.settings.ocr_feed_planet_update_enabled);
                ImGui::EndTabItem();
            }
            // ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
        ImGui::End();

        if (state.data_form_active) {

            ImGui::Begin("Datatable", &state.data_form_active);
            // Editable table for DataPoint entries
            static bool data_dirty = false;
            static std::string save_message;

            ImGui::Text("Edit existing points (changes are in-memory until you Save)");
            ImGui::Separator();

            ImGuiTableFlags table_flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY;
            // Use a distinct table ID to avoid legacy table-layout mismatches across builds
            if (ImGui::BeginTable("DataPointsTable_v3", 13, table_flags, ImVec2(0, ImGui::GetContentRegionAvail().y - 30))) {
                ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 30.0f); // 0
                ImGui::TableSetupColumn("Record Time", ImGuiTableColumnFlags_WidthFixed, 100.0f); // 1
                ImGui::TableSetupColumn("Server"); // 2
                ImGui::TableSetupColumn("X"); // 3
                ImGui::TableSetupColumn("Y"); // 4
                ImGui::TableSetupColumn("Z"); // 5
                ImGui::TableSetupColumn("Planet"); // 6
                ImGui::TableSetupColumn("POI Type", ImGuiTableColumnFlags_WidthFixed, 100.0f); // 7
                ImGui::TableSetupColumn("Resource"); // 8
                ImGui::TableSetupColumn("QMin"); // 9
                ImGui::TableSetupColumn("QMax"); // 10
                ImGui::TableSetupColumn("Note"); // 11
                ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthFixed, 80.0f); //12
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();

                std::vector<size_t> to_erase;
                for (size_t i = 0; i < state.points.size(); ++i) {
                    DataPoint& dp = state.points[i];
                    ImGui::TableNextRow();

                    // ID (read-only)
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("%d", dp.id);

                    // Record Time (read-only, formatted from time_info)
                    ImGui::TableSetColumnIndex(1);
                    if (dp.time_info.empty()) {
                        ImGui::TextDisabled("N/A");
                    }
                    else {
                        ImGui::Text("%s", dp.time_info.c_str());
                    }

                    // Server
                    ImGui::TableSetColumnIndex(2);
                    {
                        char buf[16] = { 0 };
                        strncpy(buf, dp.server.c_str(), sizeof(buf) - 1);
                        std::string lbl = std::string("##server") + std::to_string(i);
                        ImGui::PushItemWidth(-FLT_MIN);
                        if (ImGui::InputText(lbl.c_str(), buf, IM_ARRAYSIZE(buf))) {
                            dp.server = buf;
                            data_dirty = true;
                        }
                        ImGui::PopItemWidth();
                    }
                    // X
                    ImGui::TableSetColumnIndex(3);
                    {
                        double val = dp.x;
                        std::string lbl = std::string("##x") + std::to_string(i);
                        ImGui::PushItemWidth(-FLT_MIN);
                        if (ImGui::InputDouble(lbl.c_str(), &val, 0.0, 0.0, "%.6f")) {
                            dp.x = val;
                            data_dirty = true;
                        }
                        ImGui::PopItemWidth();
                    }

                    // Y
                    ImGui::TableSetColumnIndex(4);
                    {
                        double val = dp.y;
                        std::string lbl = std::string("##y") + std::to_string(i);
                        ImGui::PushItemWidth(-FLT_MIN);
                        if (ImGui::InputDouble(lbl.c_str(), &val, 0.0, 0.0, "%.6f")) {
                            dp.y = val;
                            data_dirty = true;
                        }
                        ImGui::PopItemWidth();
                    }

                    // Z
                    ImGui::TableSetColumnIndex(5);
                    {
                        double val = dp.z;
                        std::string lbl = std::string("##z") + std::to_string(i);
                        ImGui::PushItemWidth(-FLT_MIN);
                        if (ImGui::InputDouble(lbl.c_str(), &val, 0.0, 0.0, "%.6f")) {
                            dp.z = val;
                            data_dirty = true;
                        }
                        ImGui::PopItemWidth();
                    }

                    // Planet (combo)
                    ImGui::TableSetColumnIndex(6);
                    {
                        std::string lbl = std::string("##planet") + std::to_string(i);
                        auto it = std::find(state.planets.begin(), state.planets.end(), dp.planet);
                        int cur = it != state.planets.end() ? static_cast<int>(std::distance(state.planets.begin(), it)) : 0;
                        std::vector<const char*> cstrs;
                        cstrs.reserve(state.planets.size());
                        for (const auto& s : state.planets) cstrs.push_back(s.c_str());
                        ImGui::PushItemWidth(-FLT_MIN);
                        if (ImGui::Combo(lbl.c_str(), &cur, cstrs.data(), static_cast<int>(cstrs.size()))) {
                            dp.planet = state.planets[static_cast<size_t>(cur)];
                            data_dirty = true;
                        }
                        ImGui::PopItemWidth();
                    }
                    // POI Type (enum-backed combo)
                    ImGui::TableSetColumnIndex(7);
                    {
                        std::string lbl = std::string("##poi_type") + std::to_string(i);
                        int cur = static_cast<int>(dp.poi_type);
                        auto names = poi_type_names();
                        std::vector<const char*> cstrs;
                        cstrs.reserve(names.size());
                        for (const auto& s : names) cstrs.push_back(s.c_str());
                        ImGui::PushItemWidth(-FLT_MIN);
                        if (ImGui::Combo(lbl.c_str(), &cur, cstrs.data(), static_cast<int>(cstrs.size()))) {
                            dp.poi_type = static_cast<PoiType>(cur);
                            data_dirty = true;
                        }
                        ImGui::PopItemWidth();
                    }

                    // Resource (combo)
                    ImGui::TableSetColumnIndex(8);
                    {
                        std::string lbl = std::string("##material") + std::to_string(i);
                        auto it = std::find(state.materials.begin(), state.materials.end(), dp.material);
                        int cur = it != state.materials.end() ? static_cast<int>(std::distance(state.materials.begin(), it)) : 0;
                        std::vector<const char*> cstrs;
                        cstrs.reserve(state.materials.size());
                        for (const auto& s : state.materials) cstrs.push_back(s.c_str());
                        ImGui::PushItemWidth(-FLT_MIN);
                        if (ImGui::Combo(lbl.c_str(), &cur, cstrs.data(), static_cast<int>(cstrs.size()))) {
                            dp.material = state.materials[static_cast<size_t>(cur)];
                            data_dirty = true;
                        }
                        ImGui::PopItemWidth();
                    }


                    // QMin
                    ImGui::TableSetColumnIndex(9);
                    {
                        int val = int(dp.quality_min);
                        std::string lbl = std::string("##qmin") + std::to_string(i);
                        ImGui::PushItemWidth(-FLT_MIN);
                        if (ImGui::InputInt(lbl.c_str(), &val, 0, 0)) {
                            dp.quality_min = val;
                            data_dirty = true;
                        }
                        ImGui::PopItemWidth();
                    }

                    // QMax
                    ImGui::TableSetColumnIndex(10);
                    {
                        int val = int(dp.quality_max);
                        std::string lbl = std::string("##qmax") + std::to_string(i);
                        ImGui::PushItemWidth(-FLT_MIN);
                        if (ImGui::InputInt(lbl.c_str(), &val, 0, 0)) {
                            dp.quality_max = val;
                            data_dirty = true;
                        }
                        ImGui::PopItemWidth();
                    }

                    // Note
                    ImGui::TableSetColumnIndex(11);
                    {
                        char buf[256] = { 0 };
                        strncpy(buf, dp.note.c_str(), sizeof(buf) - 1);
                        std::string lbl = std::string("##note") + std::to_string(i);
                        ImGui::PushItemWidth(-FLT_MIN);
                        if (ImGui::InputText(lbl.c_str(), buf, IM_ARRAYSIZE(buf))) {
                            dp.note = buf;
                            data_dirty = true;
                        }
                        ImGui::PopItemWidth();
                    }

                    // Controls (Delete)
                    ImGui::TableSetColumnIndex(12);
                    {
                        std::string del_lbl = std::string("Delete##del") + std::to_string(i);
                        if (ImGui::SmallButton(del_lbl.c_str())) {
                            to_erase.push_back(i);
                        }
                    }
                }

                // erase rows in reverse order
                if (!to_erase.empty()) {
                    std::sort(to_erase.rbegin(), to_erase.rend());
                    for (size_t idx : to_erase) {
                        if (idx < state.points.size()) {
                            state.points.erase(state.points.begin() + static_cast<std::ptrdiff_t>(idx));
                            data_dirty = true;
                        }
                    }
                }

                ImGui::EndTable();
            }

            ImGui::Separator();
            if (ImGui::Button("Save Changes")) {
                if (write_points_csv(state.csv_path.string(), state.points)) {
                    save_message = "Saved successfully";
                    state.reload_planet_data();
                    state.filter_points();
                    data_dirty = false;
                }
                else {
                    save_message = "Save failed";
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Reload From CSV")) {
                state.reload_planet_data();
                state.filter_points();
                save_message = "Reloaded";
                data_dirty = false;
            }
            ImGui::SameLine();
            ImGui::Text("%s", save_message.c_str());
            ImGui::End();
        }



        timer_display.render_time_ms().stamp();

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        const GLuint texture = state.get_texture_for_selected_planet();
        double cursor_x = 0.0;
        double cursor_y = 0.0;
        glfwGetCursorPos(window, &cursor_x, &cursor_y);
        std::optional<std::pair<float, float>> mouse_pos;
        if (width > 0 && height > 0) {
            mouse_pos = {
                static_cast<float>((cursor_x / static_cast<double>(width)) * 2.0 - 1.0),
                static_cast<float>(-(((cursor_y / static_cast<double>(height)) * 2.0) - 1.0))
            };
        }

        state.hovered_text.reset();
        if (texture != 0) {
            state.hovered_text = renderer.render_map(texture, state.filtered_points, mouse_pos, state.material_catalog);
            const auto toggle_now = std::chrono::steady_clock::now();
            if (std::chrono::duration<double>(toggle_now - last_toggle).count() > 0.25) {
                last_toggle = toggle_now;
                location_on = !location_on;
            }
            if (location_on) {
                const auto lat_lon_alt = state.new_data.to_lat_lon_alt();
                const auto [u, v] = latlon_to_uv(lat_lon_alt[0], lat_lon_alt[1]);
                const float px = (u * 2.0f) - 1.0f;
                const float py = (v * 2.0f) - 1.0f;
                renderer.render_marker(px, py, 1.0f, 1.0f, 0.0f, 0.9f, 6.0f);
            }
        }

        if (state.hovered_text) {
            ImVec2 mouse = ImGui::GetMousePos();
            if (mouse.x + 200.0f > width) {
                mouse.x -= 130.0f;
            }
            if (mouse.y + 50.0f > height) {
                mouse.y -= 60.0f;
            }

            ImGui::SetNextWindowPos(ImVec2(mouse.x + 10.0f, mouse.y + 10.0f));
            ImGui::SetNextWindowBgAlpha(0.7f);
            ImGui::Begin("##hover_tooltip", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::TextUnformatted(state.hovered_text->c_str());
            ImGui::End();
        }

        timer_display.render_time_ms().record_time_since_stamp();

        timer_display.loop_time_ms().record_time_since_stamp();
        timer_display.work_time_ms().record_time_since_stamp();

        if (state.settings.show_timings) {
            timer_footer_renderer.render(timer_display, height);
        }

        glDisable(GL_BLEND);
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);

        auto last_now = std::chrono::steady_clock::now();

        next_tick += std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(kFrameTime));
        if (last_now > next_tick + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(kFrameTime))) {
            auto missed_frames = std::chrono::duration<double>(last_now - next_tick).count() / kFrameTime;
            // std::cout << "Warning: Missed " << missed_frames << " frames\n";
            next_tick = missed_frames > 5.0 ? last_now : next_tick + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(kFrameTime * std::ceil(missed_frames)));
        }

    }
    // Signal the OCR thread to stop and wait for it to finish before exiting
    ocr_instance.stop_and_wait();


    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    state.save_settings();

    ocr_timer.join();

    return 0;
}
