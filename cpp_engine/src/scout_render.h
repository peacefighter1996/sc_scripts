#pragma once

#include "scout_core.h"

#include <optional>
#include <string>
#include <vector>
#include <unordered_map>
#include <utility>
#include <glad/glad.h>


class ScoutRenderer {
public:
    ScoutRenderer();
    ~ScoutRenderer();

    // Initialize GL resources (shaders/VAO/VBO). Call after OpenGL context is current and GLAD is loaded.
    bool init();

    // Render the textured map and points. Returns optional hover text.
    std::optional<std::string> render_map(GLuint texture,
                                          const std::vector<DataPoint>& points,
                                          std::optional<std::pair<float, float>> mouse_pos,
                                          const std::vector<Resource>& material_catalog,
                                          const Planet* selected_zone = nullptr,
                                          double grid_spacing_km = 100.0);

    // Render a single marker at normalized device coords (x,y in [-1,1]).
    void render_marker(float x, float y, float r, float g, float b, float a, float size = 5.0f);

private:
    // Convert a point (either asteroid-field XY or lat/lon) to normalized device coords [-1,1]
    std::pair<float, float> zone_point_to_ndc(const Planet* selected_zone, double a, double b, double grid_spacing_km) const;
    std::pair<float, float> latlon_to_ndc(double lat, double lon) const;
    // Render grid lines for asteroid fields
    void render_grid_for_zone(const Planet* selected_zone, double grid_spacing_km);
    GLuint compile_shader(GLenum type, const char* src);
    GLuint link_program(GLuint vs, GLuint fs);

    GLuint quad_vao_ = 0;
    GLuint quad_vbo_ = 0;
    GLuint quad_shader_ = 0;

    GLuint points_vao_ = 0;
    GLuint points_vbo_ = 0;
    GLuint points_shader_ = 0;

    GLuint marker_vao_ = 0;
    GLuint marker_vbo_ = 0;
    GLuint marker_shader_ = 0;
};
