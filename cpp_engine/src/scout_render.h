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
                                          const std::vector<Resource>& material_catalog);

    // Render a single marker at normalized device coords (x,y in [-1,1]).
    void render_marker(float x, float y, float r, float g, float b, float a, float size = 5.0f);

private:
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
