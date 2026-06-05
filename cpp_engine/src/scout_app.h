#pragma once
#include "scout_core.h"
#include <string>
#include <vector>
#include <functional>

#define DISPLAY_MODE_LIST(X) \
    X(Default, 0)        \
    X(Surface, 1)        \
    X(Asteroid_Field, 2) \
    X(Solar, 3) \
    X(Celestial_Belt, 4)

enum class DisplayMode : int {
#define DISPLAY_MODE_ENUM(name, val) name = val,
    DISPLAY_MODE_LIST(DISPLAY_MODE_ENUM)
#undef DISPLAY_MODE_ENUM
};

namespace display_mode_impl {
    constexpr size_t display_mode_count = 5;
    inline constexpr std::array<const char*, display_mode_count> display_mode_names_arr = {
#define DISPLAY_MODE_STR(name, val) #name,
        DISPLAY_MODE_LIST(DISPLAY_MODE_STR)
#undef DISPLAY_MODE_STR
    };
}   

inline std::vector<std::string> display_mode_names() {
    return std::vector<std::string>(std::begin(display_mode_impl::display_mode_names_arr), std::end(display_mode_impl::display_mode_names_arr));
}

inline const char* display_mode_name(DisplayMode t) {
    int idx = static_cast<int>(t);
    if (idx < 0 || static_cast<size_t>(idx) >= display_mode_impl::display_mode_count) return "Unknown";
    return display_mode_impl::display_mode_names_arr[static_cast<size_t>(idx)];
}

inline bool display_mode_from_string(const std::string& s, DisplayMode& out) {
    for (size_t i = 0; i < display_mode_impl::display_mode_count; ++i) {
        if (s == display_mode_impl::display_mode_names_arr[i]) {
            out = static_cast<DisplayMode>(static_cast<int>(i));
            return true;
        }
    }
    return false;
}

inline DisplayMode get_zone_default_display_mode(const ZoneType zonetype) {
    switch (zonetype) {
    case ZoneType::CelestialBody:
        return DisplayMode::Surface;
    case ZoneType::AsteroidField:
        return DisplayMode::Asteroid_Field;
    case ZoneType::Solar:
        return DisplayMode::Solar;
    default:
        return DisplayMode::Default;
    }   
}



int run_scout_app();

bool write_starmap_json(std::string &starmap_json_path);

void popup_filter(std::string& filtertext, std::string& local_selected, const std::vector<std::string>& items, const std::string& app_selected_item, std::function<void(const std::string&)> on_select);
