#pragma once

#include "scout_core.h"
#include "scout_app.h"

#include <optional>
#include <string>
#include <vector>
#include <unordered_map>
#include <utility>
#include <glad/glad.h>

struct zone_label {
    double ndc_x;
    double ndc_y;
    std::string label;
};

class ScoutRenderer {
public:
    ScoutRenderer();
    ~ScoutRenderer();

    // Initialize GL resources (shaders/VAO/VBO). Call after OpenGL context is current and GLAD is loaded.
    bool init();

    // Render the textured map and points. Returns optional hover text.
    std::optional<std::string> render_map(GLuint texture,
                                          const std::vector<DataPoint> &points,
                                          std::optional<std::pair<float, float>> mouse_pos,
                                          const std::vector<Resource> &material_catalog,
                                          const Planet *selected_zone = nullptr,
                                          const DisplayMode display_mode = DisplayMode::Default,
                                          double grid_spacing_km = 100.0);

    void RenderAstroidFieldZone(const Planet* selected_zone, double grid_spacing_km);

    void RenderBackground(GLuint texture);
    // Render the planet disk positioned and scaled according to the selected zone's bounding box.
    void RenderPlanet(GLuint texture, const Planet* selected_zone, double radius_planet_km, double grid_spacing_km);

    void RenderPointsWithBorder(std::vector<float> &border_buf, const std::vector<DataPoint> &points, std::vector<float> &buf);

    void NewFunction(std::vector<float> &border_buf, const std::vector<DataPoint> &points, std::vector<float> &buf);

    // Render a travel track (connected line strip) in NDC using the supplied DataPoint list.
    void render_track(const DisplayMode dpm, 
                      const std::vector<DataPoint>& track,
                      const Planet* selected_zone = nullptr,
                      double grid_spacing_km = 100.0);

    // Render a single marker at normalized device coords (x,y in [-1,1]).
    void render_marker(float x, float y, float r, float g, float b, float a, float size = 5.0f);

private:
    // Convert a point (either asteroid-field XY or lat/lon) to normalized device coords [-1,1]
    std::pair<float, float> zone_point_to_ndc(const DisplayMode dpm, const Planet* selected_zone, double a, double b, double grid_spacing_km) const;
    std::pair<float, float> asteriod_point_to_ndc(const bbox2d& box, double grid_spacing_km, double a, double b) const;
    std::pair<float, float> latlon_to_ndc(double lat, double lon) const;
    // Render grid lines for asteroid fields
    void render_grid_for_zone(const DisplayMode dpm, const Planet* selected_zone, double grid_spacing_km);
    // Excel-style column label helper (A..Z, AA..ZZ, etc.)
    std::string excel_column_label(int index);
    // Compute sector label for a point in the selected zone (e.g. "B12")
    std::string sector_label_for_point(const DisplayMode dpm, const Planet* selected_zone, double a, double b, double grid_spacing_km);
    // Render cached sector labels for a rectangular grid defined by start, cols, rows.
    void render_sector_labels_grid(const DisplayMode dpm, const Planet* selected_zone, double start_x, double start_y, int cols, int rows, double grid_spacing_x_km, double grid_spacing_y_km, bool coords_are_latlon = false);
    GLuint compile_shader(GLenum type, const char* src);
    GLuint link_program(GLuint vs, GLuint fs);

    GLuint quad_vao_ = 0;
    GLuint quad_vbo_ = 0;
    GLuint quad_shader_ = 0;
    GLuint planet_shader_ = 0;
    // Cached location of the quad shader's sampler uniform to avoid expensive
    // glGetUniformLocation calls each frame.
    GLint quad_texture_loc_ = -1;
    GLint planet_texture_loc_ = -1;
    // Planet shader uniform locations
    GLint planet_center_loc_ = -1;
    GLint planet_radius_loc_ = -1;
    GLint planet_vscale_loc_ = -1;

    GLuint points_vao_ = 0;
    GLuint points_vbo_ = 0;
    GLuint points_shader_ = 0;

    GLuint marker_vao_ = 0;
    GLuint marker_vbo_ = 0;
    GLuint marker_shader_ = 0;
    // Cached uniform locations
    GLint marker_color_loc_ = -1;
    GLint points_point_size_loc_ = -1;
    // Cache last texture bound to texture unit 0 to avoid redundant glActiveTexture/glBindTexture calls
    GLuint last_bound_texture_unit0_ = 0;
    bool last_bound_texture_unit0_valid_ = false;
    // Cache for computed column labels and per-zone cell labels
    std::unordered_map<int, std::string> col_label_cache_;
    std::unordered_map<std::string, std::vector<zone_label>> zone_label_cache_;
};



struct zone_label_cache {
    std::string zone_name;
    int rows;
    int cols;
    double grid_spacing_km;
    bool coords_are_latlon;


    bool operator==(const zone_label_cache& other) const {
        return zone_name == other.zone_name &&
            cols == other.cols &&
            rows == other.rows &&
            grid_spacing_km == other.grid_spacing_km &&
            coords_are_latlon == other.coords_are_latlon;
    }
};

