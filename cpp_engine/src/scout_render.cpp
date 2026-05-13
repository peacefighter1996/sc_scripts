#include "scout_render.h"
#include "scout_core.h"

#include <vector>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <utility>

static std::pair<float, float> latlon_to_uv(double lat, double lon) {
    const auto u = static_cast<float>((lon + 180.0) / 360.0);
    const auto v = static_cast<float>((lat + 90.0) / 180.0);
    return {u, v};
}

struct DataPoint;

static const char* quad_vs = R"GLSL(#version 330 core
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_uv;
out vec2 uv;
void main() {
    uv = in_uv;
    gl_Position = vec4(in_pos, 0.0, 1.0);
}
)GLSL";

static const char* quad_fs = R"GLSL(#version 330 core
in vec2 uv;
out vec4 out_color;
uniform sampler2D u_texture;
void main() {
    out_color = texture(u_texture, uv);
}
)GLSL";

static const char* point_vs = R"GLSL(#version 330 core
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec4 in_color;
out vec4 v_color;
uniform float u_point_size;
void main() {
    v_color = in_color;
    gl_Position = vec4(in_pos, 0.0, 1.0);
    gl_PointSize = u_point_size;
}
)GLSL";

static const char* point_fs = R"GLSL(#version 330 core
in vec4 v_color;
out vec4 out_color;
void main() {
    out_color = v_color;
}
)GLSL";

static const char* marker_vs = R"GLSL(#version 330 core
layout(location = 0) in vec2 in_pos;
void main() {
    gl_Position = vec4(in_pos, 0.0, 1.0);
    gl_PointSize = 4.0;
}
)GLSL";

static const char* marker_fs = R"GLSL(#version 330 core
uniform vec4 u_color;
out vec4 out_color;
void main() {
    out_color = u_color;
}
)GLSL";

ScoutRenderer::ScoutRenderer() {}

ScoutRenderer::~ScoutRenderer() {
    if (quad_vbo_) glDeleteBuffers(1, &quad_vbo_);
    if (quad_vao_) glDeleteVertexArrays(1, &quad_vao_);
    if (quad_shader_) glDeleteProgram(quad_shader_);

    if (points_vbo_) glDeleteBuffers(1, &points_vbo_);
    if (points_vao_) glDeleteVertexArrays(1, &points_vao_);
    if (points_shader_) glDeleteProgram(points_shader_);

    if (marker_vbo_) glDeleteBuffers(1, &marker_vbo_);
    if (marker_vao_) glDeleteVertexArrays(1, &marker_vao_);
    if (marker_shader_) glDeleteProgram(marker_shader_);
}

GLuint ScoutRenderer::compile_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[1024];
        glGetShaderInfoLog(s, sizeof(buf), nullptr, buf);
        std::cerr << "Shader compile error: " << buf << '\n';
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint ScoutRenderer::link_program(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[1024];
        glGetProgramInfoLog(p, sizeof(buf), nullptr, buf);
        std::cerr << "Program link error: " << buf << '\n';
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

bool ScoutRenderer::init() {
    // Enable program point size so gl_PointSize in vertex shader takes effect
    glEnable(GL_PROGRAM_POINT_SIZE);
    // Ensure blending for alpha in point colors
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // Quad
    GLuint vs = compile_shader(GL_VERTEX_SHADER, quad_vs);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, quad_fs);
    if (!vs || !fs) return false;
    quad_shader_ = link_program(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!quad_shader_) return false;

    float quad_verts[] = {
        // pos(x,y)    uv
        -1.0f, -1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 1.0f,
         1.0f,  1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f,  0.0f, 0.0f,
    };
    glGenVertexArrays(1, &quad_vao_);
    glGenBuffers(1, &quad_vbo_);
    glBindVertexArray(quad_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_verts), quad_verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);

    // Points program
    vs = compile_shader(GL_VERTEX_SHADER, point_vs);
    fs = compile_shader(GL_FRAGMENT_SHADER, point_fs);
    if (!vs || !fs) return false;
    points_shader_ = link_program(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!points_shader_) return false;

    glGenVertexArrays(1, &points_vao_);
    glGenBuffers(1, &points_vbo_);
    glBindVertexArray(points_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, points_vbo_);
    // initially empty; we'll buffer data each frame
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    // layout: vec2 pos, vec4 color
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);

    // Marker program (single point, uniform color)
    vs = compile_shader(GL_VERTEX_SHADER, marker_vs);
    fs = compile_shader(GL_FRAGMENT_SHADER, marker_fs);
    if (!vs || !fs) return false;
    marker_shader_ = link_program(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!marker_shader_) return false;

    glGenVertexArrays(1, &marker_vao_);
    glGenBuffers(1, &marker_vbo_);
    glBindVertexArray(marker_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, marker_vbo_);
    glBufferData(GL_ARRAY_BUFFER, 2 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindVertexArray(0);

    return true;
}

std::optional<std::string> ScoutRenderer::render_map(GLuint texture,
                                                     const std::vector<DataPoint>& points,
                                                     std::optional<std::pair<float, float>> mouse_pos,
                                                     const std::vector<Material>& material_catalog) {
    // Draw textured quad
    glUseProgram(quad_shader_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    const GLint tex_loc = glGetUniformLocation(quad_shader_, "u_texture");
    glUniform1i(tex_loc, 0);
    glBindVertexArray(quad_vao_);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);

    // Prepare points buffer (pos + rgba)
    std::vector<float> buf;
    buf.reserve(points.size() * 6);
    for (const auto& point : points) {
        const auto lat_lon_alt = point.to_lat_lon_alt();
        const auto uv = latlon_to_uv(lat_lon_alt[0], lat_lon_alt[1]);
        const float x = (uv.first * 2.0f) - 1.0f;
        const float y = (uv.second * 2.0f) - 1.0f;

        float r = 1.0f, g = 1.0f, b = 1.0f, a = 0.85f;
        if (point.poi_type == PoiType::Mineral) {
            double quality_norm = (point.quality_max - 0.0) / (1000.0 - 0.0);
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

        buf.push_back(x);
        buf.push_back(y);
        buf.push_back(r);
        buf.push_back(g);
        buf.push_back(b);
        buf.push_back(a);
    }

    // Upload and draw points
    glUseProgram(points_shader_);
    glBindVertexArray(points_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, points_vbo_);
    glBufferData(GL_ARRAY_BUFFER, buf.size() * sizeof(float), buf.empty() ? nullptr : buf.data(), GL_DYNAMIC_DRAW);
    const GLint size_loc = glGetUniformLocation(points_shader_, "u_point_size");
    glUniform1f(size_loc, 4.0f);
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(points.size()));
    glBindVertexArray(0);

    // Hover detection (CPU, same logic as before)
    if (!mouse_pos) return std::nullopt;
    const auto [mx, my] = *mouse_pos;
    constexpr float closest_dist = 0.02f;
    for (const auto& point : points) {
        const auto lat_lon_alt = point.to_lat_lon_alt();
        const auto uv = latlon_to_uv(lat_lon_alt[0], lat_lon_alt[1]);
        const float px = (uv.first * 2.0f) - 1.0f;
        const float py = (uv.second * 2.0f) - 1.0f;
        const float dx = mx - px;
        const float dy = my - py;
        const float dist = std::sqrt((dx * dx) + (dy * dy));
        if (dist < closest_dist) {
            const auto it = std::find_if(material_catalog.begin(), material_catalog.end(), [&](const Material& m) {
                return m.name == point.material;
            });
            const auto material_id = it != material_catalog.end() ? it->short_name : point.material.substr(0, std::min<size_t>(4, point.material.size()));
            if (point.poi_type == PoiType::Mineral) {
                return material_id + " Quality: " + std::to_string(int(point.quality_max)) + "\n" + point.note;
            }
            return point.note;
        }
    }

    return std::nullopt;
}

void ScoutRenderer::render_marker(float x, float y, float r, float g, float b, float a, float size) {
    glUseProgram(marker_shader_);
    glBindVertexArray(marker_vao_);
    float pos[2] = { x, y };
    glBindBuffer(GL_ARRAY_BUFFER, marker_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(pos), pos, GL_DYNAMIC_DRAW);
    const GLint color_loc = glGetUniformLocation(marker_shader_, "u_color");
    glUniform4f(color_loc, r, g, b, a);
    glDrawArrays(GL_POINTS, 0, 1);
    glBindVertexArray(0);
}
