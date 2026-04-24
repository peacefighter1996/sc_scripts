#include "scout_app.h"

#include "scout_ocr.h"
#include "scout_core.h"
#include "timer_display.h"
#include "timer_footer_renderer.h"

#define NOMINMAX
#include <Windows.h>
#include <gdiplus.h>
#include <gl/GL.h>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl2.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
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
constexpr double kMaxQuality = 1000.0;
constexpr double kMinQuality = 0.0;
constexpr double kTargetHz = 30.0;
constexpr double kFrameTime = 1.0 / kTargetHz;

const std::vector<std::string> kDefaultServerIds{"eu10", "eu180", "us170", "All"};
const std::vector<std::string> kDefaultPlanets{"Pyro_A5_Ignis", "Pyro_E5_Fuego", "Pyro_Pyro2_Monox", "Pyro_Pyro4"};
const std::vector<std::string> kDefaultMaterials{"All", "Aphorite", "Aslarite", "Beryl", "Borase", "Copper", "Dolivine", "Gold", "Hephaestanite", "Iron", "Janalite", "Laranite", "Riccite", "Stileron", "Taranite", "Tin"};

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

struct Material{
    int id;
    std::string name;
    std::string short_name;
};

struct MaterialCatalog {
    int next_id;
    std::vector<std::string> names;
    std::unordered_map<std::string, std::string> short_name;
};



struct PlanetCatalog {
    std::vector<std::string> keys;
    std::unordered_map<std::string, std::string> image_dir_by_key;
};

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
            value_index = find_column_index(cells, {"value", "server", "server_id", "id"});
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

MaterialCatalog load_material_catalog(const std::filesystem::path& path, const std::vector<std::string>& default_names, const std::unordered_map<std::string, std::string>& default_shorts) {
    std::ifstream in(path);
    if (!in.is_open()) {
        return {0, default_names, default_shorts};
    }

    MaterialCatalog catalog;
    bool header_parsed = false;
    int name_index = -1;
    int shortname_index = -1;
    int id_index = -1;

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
            id_index = find_column_index(cells, {"id"});
            name_index = find_column_index(cells, {"name", "material", "value"});
            shortname_index = find_column_index(cells, {"shortname", "short_name", "short", "code"});
            if (name_index < 0) {
                name_index = std::min(1, static_cast<int>(cells.size()) - 1);
            }
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
            } catch (const std::exception&) {
                // ignore parse errors and just use default ids
            }
        }


        if (std::find(catalog.names.begin(), catalog.names.end(), name) == catalog.names.end()) {
            catalog.names.push_back(name);
        }

        if (!shortname.empty()) {
            catalog.short_name[name] = shortname;
        }

        if (id >= 0) {
            catalog.short_name[name] = std::to_string(id);
        }
        
        
    }

    if (catalog.names.empty()) {
        catalog.names = default_names;
    }
    if (catalog.short_name.empty()) {
        catalog.short_name = default_shorts;
    }

    return catalog;
}

PlanetCatalog load_planet_catalog(const std::filesystem::path& path, const std::vector<std::string>& defaults) {
    std::ifstream in(path);
    if (!in.is_open()) {
        PlanetCatalog catalog;
        catalog.keys = defaults;
        for (const auto& key : defaults) {
            catalog.image_dir_by_key[key] = key;
        }
        return catalog;
    }

    PlanetCatalog catalog;
    bool header_parsed = false;
    int image_dir_index = -1;
    int planet_name_index = -1;
    int key_index = -1;

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
            image_dir_index = find_column_index(cells, {"image_dir", "imagedir", "image"});
            planet_name_index = find_column_index(cells, {"planet", "name", "value"});
            key_index = find_column_index(cells, {"key", "planet_key", "planet_id"});
            header_parsed = true;
            continue;
        }

        std::string key;
        if (planet_name_index >= 0 && planet_name_index < static_cast<int>(cells.size())) {
            key = trim(cells[static_cast<size_t>(planet_name_index)]);
        }
        if (key_index >= 0 && key_index < static_cast<int>(cells.size())) {
            const auto parsed = trim(cells[static_cast<size_t>(key_index)]);
            if (!parsed.empty() && key.empty()) {
                key = parsed;
            }
        }
        if (key.empty() && image_dir_index >= 0 && image_dir_index < static_cast<int>(cells.size())) {
            key = trim(cells[static_cast<size_t>(image_dir_index)]);
        }
        if (key.empty()) {
            continue;
        }

        std::string image_dir = key;
        if (image_dir_index >= 0 && image_dir_index < static_cast<int>(cells.size())) {
            const auto parsed = trim(cells[static_cast<size_t>(image_dir_index)]);
            if (!parsed.empty()) {
                image_dir = parsed;
            }
        }

        if (std::find(catalog.keys.begin(), catalog.keys.end(), key) == catalog.keys.end()) {
            catalog.keys.push_back(key);
        }
        catalog.image_dir_by_key[key] = image_dir;
    }

    if (catalog.keys.empty()) {
        catalog.keys = defaults;
        for (const auto& key : defaults) {
            catalog.image_dir_by_key[key] = key;
        }
    }

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
    return {u, v};
}

std::optional<std::string> draw_map(
    GLuint texture,
    const std::vector<DataPoint>& points,
    std::optional<std::pair<float, float>> mouse_pos,
    const std::unordered_map<std::string, std::string>& material_ids) {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glColor3f(1.0f, 1.0f, 1.0f);

    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f, -1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0f, -1.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0f, 1.0f);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, 1.0f);
    glEnd();

    glDisable(GL_TEXTURE_2D);
    glPointSize(5.0f);
    glBegin(GL_POINTS);
    for (const auto& point : points) {
        const auto lat_lon_alt = point.to_lat_lon_alt();
        const auto [u, v] = latlon_to_uv(lat_lon_alt[0], lat_lon_alt[1]);

        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 0.85f;
        if (!point.location) {
            double quality_norm = (point.quality_max - kMinQuality) / (kMaxQuality - kMinQuality);
            quality_norm = std::clamp(quality_norm, 0.0, 1.0);
            if (quality_norm < 0.5) {
                const auto t = static_cast<float>(quality_norm * 2.0);
                r = 0.0f;
                g = t;
                b = 1.0f - t;
            } else {
                const auto t = static_cast<float>((quality_norm - 0.5) * 2.0);
                r = t;
                g = 1.0f - t;
                b = 0.0f;
            }
            a = 1.0f;
        }

        const float x = (u * 2.0f) - 1.0f;
        const float y = (v * 2.0f) - 1.0f;
        glColor4f(r, g, b, a);
        glVertex2f(x, y);
    }
    glEnd();

    if (!mouse_pos) {
        return std::nullopt;
    }

    const auto [mx, my] = *mouse_pos;
    constexpr float closest_dist = 0.02f;
    for (const auto& point : points) {
        const auto lat_lon_alt = point.to_lat_lon_alt();
        const auto [u, v] = latlon_to_uv(lat_lon_alt[0], lat_lon_alt[1]);
        const float px = (u * 2.0f) - 1.0f;
        const float py = (v * 2.0f) - 1.0f;
        const float dx = mx - px;
        const float dy = my - py;
        const float dist = std::sqrt((dx * dx) + (dy * dy));
        if (dist < closest_dist) {
            const auto it = material_ids.find(point.material);
            const auto material_id = it != material_ids.end() ? it->second : point.material.substr(0, std::min<size_t>(4, point.material.size()));
            if (point.location) {
                return point.note;
            }
            return material_id + " Quality: " + std::to_string(point.quality_max) + "\n" + point.note;
        }
    }

    return std::nullopt;
}

struct AppState {
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
    std::unordered_map<std::string, std::string> material_ids;
    std::unordered_map<std::string, std::string> planet_image_dirs;

    std::string selected_planet;
    std::string selected_material;
    std::string selected_server;
    std::optional<std::string> last_detected_rock;
    double quality_min{kMinQuality};
    double quality_max{kMaxQuality};
    DataPoint new_data{};
    std::unordered_map<std::string, GLuint> texture_cache;
    std::optional<std::string> hovered_text;
        ScoutOcr ocr;

    AppState()
        : repo_root(detect_repo_root()),
          csv_path(repo_root / "data" / "geoscout.csv"),
          onnx_model_path(repo_root / "data" / "best_pareto_model.onnx"),
          label_map_path(repo_root / "data" / "label_map.json"),
                    planets_dir(repo_root / "images" / "planets"),
                    server_ids_path(repo_root / "data" / "server_ids.csv"),
                    planets_csv_path(repo_root / "data" / "planets.csv"),
                    materials_path(repo_root / "data" / "materials.csv"),
                    ocr(onnx_model_path, label_map_path) {
        server_ids = load_server_ids_csv(server_ids_path, kDefaultServerIds);
        const auto planet_catalog = load_planet_catalog(planets_csv_path, kDefaultPlanets);
        planets = planet_catalog.keys;
        planet_image_dirs = planet_catalog.image_dir_by_key;
        const auto material_catalog = load_material_catalog(materials_path, kDefaultMaterials, kMaterialIds);
        materials = material_catalog.names;
        material_ids = material_catalog.short_name;
        selected_planet = planets.front();
        selected_material = materials.front();
        selected_server = server_ids.front();
        new_data.server = selected_server;
        new_data.planet = selected_planet;
        new_data.material = selected_material;
        new_data.quality_min = 0.0;
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

        std::filesystem::path texture_path = planets_dir / selected_planet / "planet.jpg";
        const auto dir_it = planet_image_dirs.find(selected_planet);
        if (dir_it != planet_image_dirs.end() && !dir_it->second.empty()) {
            texture_path = planets_dir / dir_it->second / "planet.jpg";
        }

        GLuint texture = load_texture_file(texture_path);
        if (texture == 0) {
            texture = load_texture_file(planets_dir / (selected_planet + ".jpg"));
        }
        texture_cache[selected_planet] = texture;
        return texture;
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

int run_scout_app() {
    GdiplusSession gdiplus_session;

    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return 1;
    }

    GLFWwindow* window = glfwCreateWindow(1280, 720, "Scout Engine", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL2_Init();

    AppState state;
    state.reload_planet_data();
    state.filter_points();
    TimerDisplay timer_display;
    TimerFooterRenderer timer_footer_renderer;

    bool location_on = false;
    bool show_timings = false;
    bool f3_was_down = false;
    auto last_toggle = std::chrono::steady_clock::now();
    auto next_tick = std::chrono::steady_clock::now();

    while (!glfwWindowShouldClose(window)) {
        const auto now = std::chrono::steady_clock::now();
        double actual_sleep = 0.0;
        if (next_tick > now ) {
            const auto sleep_start = std::chrono::steady_clock::now();
            std::this_thread::sleep_for(next_tick - now );
            const auto sleep_end = std::chrono::steady_clock::now();
            actual_sleep = std::chrono::duration<double>(sleep_end - sleep_start).count();
        }
        const auto loop_start = std::chrono::steady_clock::now();

        const auto sleep_pct = kFrameTime > 0.0 ? std::min(100.0, (actual_sleep / kFrameTime) * 100.0) : 0.0;
        timer_display.record_sleep(sleep_pct);

        const auto ocr_poll_start = std::chrono::steady_clock::now();
        state.ocr.request_async();
        if (const auto result = state.ocr.poll()) {
            timer_display.record_ocr_task(result->task_time_ms);
            if (result->x && result->y && result->z) {
                state.new_data.x = *result->x;
                state.new_data.y = *result->y;
                state.new_data.z = *result->z;
            }
            if (result->rock) {
                state.last_detected_rock = result->rock;
            }
        }
        const auto ocr_poll_end = std::chrono::steady_clock::now();
        const double ocr_poll_ms = std::chrono::duration<double, std::milli>(ocr_poll_end - ocr_poll_start).count();

        glfwPollEvents();

        const bool f3_is_down = glfwGetKey(window, GLFW_KEY_F3) == GLFW_PRESS;
        if (f3_is_down && !f3_was_down) {
            show_timings = !show_timings;
        }
        f3_was_down = f3_is_down;

        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Controls");
        if (combo_string("Server", state.server_ids, state.selected_server)) {
            state.new_data.server = state.selected_server;
            state.filter_points();
        }
        if (combo_string("Planet", state.planets, state.selected_planet)) {
            state.new_data.planet = state.selected_planet;
            state.filter_points();
        }
        if (combo_string("Material", state.materials, state.selected_material)) {
            state.new_data.material = state.selected_material;
            state.filter_points();
        }

        ImGui::Separator();
        ImGui::Text("Add new point:");
        if (state.last_detected_rock) {
            ImGui::Text("Last detected rock type: %s", state.last_detected_rock->c_str());
            if (ImGui::Button("Use Detected Rock Type")) {
                state.selected_material = *state.last_detected_rock;
                state.new_data.material = *state.last_detected_rock;
            }
        }

        float input_x = static_cast<float>(state.new_data.x);
        float input_y = static_cast<float>(state.new_data.y);
        float input_z = static_cast<float>(state.new_data.z);
        ImGui::InputFloat("X", &input_x);
        ImGui::InputFloat("Y", &input_y);
        ImGui::InputFloat("Z", &input_z);
        state.new_data.x = input_x;
        state.new_data.y = input_y;
        state.new_data.z = input_z;
        ImGui::InputDouble("Quality Min", &state.new_data.quality_min);
        ImGui::InputDouble("Quality Max", &state.new_data.quality_max);

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
            if (append_point(state.csv_path.string(), new_point)) {
                state.points.push_back(new_point);
                state.new_data.id += 1;
            }
        }
        ImGui::End();

        const auto render_start = std::chrono::steady_clock::now();

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
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
            state.hovered_text = draw_map(texture, state.filtered_points, mouse_pos, state.material_ids);
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
                glColor4f(1.0f, 1.0f, 0.0f, 0.9f);
                glPointSize(5.0f);
                glBegin(GL_POINTS);
                glVertex2f(px, py);
                glEnd();
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

        const auto render_end = std::chrono::steady_clock::now();
        const double render_time_ms = std::chrono::duration<double, std::milli>(render_end - render_start).count();

        const auto loop_end = std::chrono::steady_clock::now();
        const double loop_time_ms = std::chrono::duration<double, std::milli>(loop_end - loop_start).count();
        timer_display.record_frame(loop_time_ms, loop_time_ms, render_time_ms, ocr_poll_ms);
        if (show_timings) {
            timer_footer_renderer.render(timer_display, height);
        }

        glDisable(GL_BLEND);
        ImGui::Render();
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);

        next_tick += std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(kFrameTime));
        if (loop_end > next_tick + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(kFrameTime))) {
            next_tick = loop_end;
        }
        
    }

    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
