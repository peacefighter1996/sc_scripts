#pragma once

#include "scout_core.h"

#include <optional>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <glad/glad.h>
#include "camera.h"

struct zone_label {
    Vector2 ndc;
    std::string label;
};

inline Vector2 uv_to_ndc(const Vector2& uv) {
    return { (uv.x * 2.0) - 1.0, (-uv.y * 2.0) + 1.0 };
}
inline Vector2 ndc_to_uv(const Vector2& ndc) {
    return { ndc.x * 0.5 + 0.5 , 0.5f -ndc.y * 0.5f };
}
// Convert lat/lon to UV coordinates in [0,1] range. Latitude is expected in [-90,90], longitude in [-180,180].
inline Vector2 uv_to_latlon(const Vector2& uv) {
    return { (uv.y * 180.0) - 90.0, (uv.x * 360.0) - 180.0 };
}
// lat/lon to UV coordinates in [0,1] range. Latitude is expected in [-90,90], longitude in [-180,180].
inline Vector2 latlon_to_uv(const Vector2& latlon) {
	return { (latlon.y + 180.0) / 360.0, (latlon.x + 90.0) / 180.0 };
}
// Convert lat/lon to normalized device coordinates in [-1,1] range for rendering. Latitude is expected in [-90,90], longitude in [-180,180].
inline Vector2 latlonalt_to_ndc(const LatLonAlt& latlon) {
	return { (latlon.longitude) / 180.0, (latlon.latitude) / 90.0 };
}
inline Vector2 latlon_to_ndc(const double& lat, const double& lon) {
    return { lon / 180.0, lat / 90.0 };
}


struct rgba {
	float r, g, b, a;
	const void overide(float r_, float g_, float b_, float a_) {
		r = r_; g = g_; b = b_; a = a_;
	};
	const void overide(float r_, float g_, float b_) {
		r = r_; g = g_; b = b_;
	};
	const void overide(const rgba& c) {
		r = c.r; g = c.g; b = c.b; a = c.a;
	};
	const float luminance() const {
		return 0.2126f * r + 0.7152f * g + 0.0722f * b;
	};
};

inline rgba make_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	return { r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f };
}


//rgb(183, 65, 14)
static rgba wreck = make_rgba(183, 65, 14, 255);
//rgb(150, 90, 50)
static rgba cave = make_rgba(150, 90, 50, 255);

//rgb(203, 58, 51)
static rgba onyx_facility = make_rgba(203, 58, 51, 255);
//rgb(128, 128, 128)
static rgba qtless_location = make_rgba(128, 128, 128, 255);

//rgb(148, 0, 148)
const rgba default_track_color = make_rgba( 148, 0, 148, 192); // subtle purple with some transparency

class ScoutRenderer {
public:
    ScoutRenderer();
    ~ScoutRenderer();

    // Initialize GL resources (shaders/VAO/VBO). Call after OpenGL context is current and GLAD is loaded.
    bool init();

    // Parameters for rendering the textured map and points.
    struct RenderMapParams {
        GLuint texture{0};
        const std::vector<DataPoint>* points{nullptr};
        std::optional<std::pair<float, float>> mouse_pos{std::nullopt};
        const std::vector<Resource>* material_catalog{nullptr};
        const Planet* selected_zone{nullptr};
        DisplayMode display_mode{DisplayMode::Default};
        double grid_spacing_km{100.0};
    };

    // Render the textured map and points. Returns optional hover text.
    std::optional<std::string> render_map(const RenderMapParams& params);

    void RenderAstroidFieldZone(const Planet* selected_zone, double grid_spacing_km);

    void RenderBackground(GLuint texture);
    // Render the planet disk positioned and scaled according to the selected zone's bounding box.
    void RenderPlanet(GLuint texture, const Planet* selected_zone, double radius_planet_km, double grid_spacing_km);


    // Render a travel track (connected line strip) in NDC using the supplied DataPoint list.
    void render_track(const DisplayMode dpm, 
                      const std::vector<DataPoint>& track,
                      const Planet* selected_zone = nullptr,
                      double grid_spacing_km = 100.0, 
                      const rgba& track_color = default_track_color);

    Camera2D camera2d;
    Camera3D camera3d;

    // Render a single marker at normalized device coords (x,y in [-1,1]).
    // (Declaration with Camera2D is provided later near uniform fields.)
    void render_marker(float x, float y, float r, float g, float b, float a, float size = 5.0f);
	void reset_grid_cache(const std::string& selected_zone);

private:
    // Convert a point (either asteroid-field XY or lat/lon) to normalized device coords [-1,1]
    Vector2 zone_point_to_ndc(const DisplayMode dpm, const Planet* selected_zone, double a, double b, double grid_spacing_km) const;
    Vector2 asteroid_point_to_ndc(const bbox2d& box, double grid_spacing_km, double a, double b) const;
    //std::pair<float, float> latlon_to_ndc(double lat, double lon) const;
    // Render grid lines for asteroid fields
    void render_grid_for_zone(const DisplayMode dpm, const Planet* selected_zone, double grid_spacing_km);
    // Excel-style column label helper (A..Z, AA..ZZ, etc.)
    std::string excel_column_label(int index);
    // Compute sector label for a point in the selected zone (e.g. "B12")
    //std::string sector_label_for_point(const DisplayMode dpm, const Planet* selected_zone, double a, double b, double grid_spacing_km);
    // Render cached sector labels for a rectangular grid defined by start, cols, rows.
    void render_sector_labels_grid(const DisplayMode dpm, const Planet* selected_zone, double start_x, double start_y, int cols, int rows, double grid_spacing_x_km, double grid_spacing_y_km, bool coords_are_latlon = false);
    GLuint compile_shader(GLenum type, const char* src);
    GLuint link_program(GLuint vs, GLuint fs);

    void RenderPointsWithBorder(std::vector<float>& border_buf, std::vector<float>& buf);

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
    GLint planet_rotate_loc_ = -1;
    // Whether to apply planet-centering remap in the vertex shader (program-specific uniform)
    GLint planet_apply_center_loc_ = -1;

    GLuint points_vao_ = 0;
    GLuint points_vbo_ = 0;
    GLuint points_shader_ = 0;

    GLuint marker_vao_ = 0;
    GLuint marker_vbo_ = 0;
    GLuint marker_shader_ = 0;
    // Cached uniform locations
    GLint marker_color_loc_ = -1;
    GLint points_point_size_loc_ = -1;
    // Points/marker pan & zoom uniform locations (for GPU transforms)
    GLint points_pan_ndc_loc_ = -1;
    GLint points_zoom_loc_ = -1;
    GLint marker_pan_ndc_loc_ = -1;
    GLint marker_zoom_loc_ = -1;
        
    // Quad/planet pan & zoom uniform locations
    GLint quad_pan_ndc_loc_ = -1;
    GLint quad_uv_pan_loc_ = -1;
    GLint quad_zoom_loc_ = -1;
    GLint planet_pan_ndc_loc_ = -1;
    GLint planet_uv_pan_loc_ = -1;
    GLint planet_zoom_loc_ = -1;
    // Cache last texture bound to texture unit 0 to avoid redundant glActiveTexture/glBindTexture calls
    GLuint last_bound_texture_unit0_ = 0;
    bool last_bound_texture_unit0_valid_ = false;
    // Cache for computed column labels and per-zone cell labels
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

