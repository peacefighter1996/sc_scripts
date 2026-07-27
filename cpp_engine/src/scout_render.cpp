#include "scout_render.h"
#include "scout_core.h"

#include <vector>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <utility>
#include <imgui.h>
#include <cstdint>


// Convert lat/lon to UV coordinates in [0,1] range. Latitude is expected in [-90,90], longitude in [-180,180].
//static std::pair<float, float> latlon_to_uv(double lat, double lon) {
//	const auto u = static_cast<float>((lon + 180.0) / 360.0);
//	const auto v = static_cast<float>((lat + 90.0) / 180.0);
//	return { u, v };
//}


// Convert lat/lon to normalized device coordinates in [-1,1] range for rendering. Latitude is expected in [-90,90], longitude in [-180,180].
//std::pair<float, float> ScoutRenderer::latlon_to_ndc(double lat, double lon) const {
//	const auto uv = latlon_to_uv(lat, lon);
//	return { (uv.first * 2.0f) - 1.0f, (uv.second * 2.0f) - 1.0f };
//}

Vector2 ScoutRenderer::zone_point_to_ndc(const DisplayMode dpm, const Planet* selected_zone, double a, double b, double grid_spacing_km) const {
	if (dpm == DisplayMode::Asteroid_Field || dpm == DisplayMode::Celestial_Belt) {
		const bbox2d box = selected_zone->bounding_box_km;
		return asteroid_point_to_ndc(box, grid_spacing_km, a, b);
	}
	// Treat a,b as lat,lon
	if (dpm == DisplayMode::Surface) {
		return latlon_to_ndc(a, b);
	}
	return { static_cast<float>(a), static_cast<float>(b) };
}

Vector2 ScoutRenderer::asteroid_point_to_ndc(const bbox2d& box, double grid_spacing_km, double a, double b) const
{
	const double minx = box.min_x;
	const double maxx = box.max_x;
	const double miny = box.min_y;
	const double maxy = box.max_y;
	double cx = 0.0, cy = 0.0;
	double half_w = 0.0, half_h = 0.0;
	if (maxx > minx) {
		cx = (minx + maxx) * 0.5;
		half_w = (maxx - minx) * 0.5;
	}
	if (maxy > miny) {
		cy = (miny + maxy) * 0.5;
		half_h = (maxy - miny) * 0.5;
	}
	if (half_w <= 0.0) half_w = std::max(1.0, grid_spacing_km * 3.0);
	if (half_h <= 0.0) half_h = std::max(1.0, grid_spacing_km * 3.0);
	const double ndc_x = (a - cx) / half_w;
	const double ndc_y = (b - cy) / half_h;
	return { static_cast<float>(ndc_x), static_cast<float>(ndc_y) };
}

std::string ScoutRenderer::excel_column_label(int index) {
	if (index < 0) return "";
	// Local non-cached fallback (header cache lives in object; helper also useful standalone)
	std::string s;
	int n = index;
	while (true) {
		int rem = n % 26;
		s.push_back(static_cast<char>('A' + rem));
		n = (n / 26) - 1;
		if (n < 0) break;
	}
	std::reverse(s.begin(), s.end());
	return s;
}

void ScoutRenderer::render_sector_labels_grid(const DisplayMode dpm, const Planet* selected_zone, double start_x, double start_y, int cols, int rows, double grid_spacing_x_km, double grid_spacing_y_km, bool coords_are_latlon) {
	ImDrawList* dl = ImGui::GetBackgroundDrawList();
	ImVec2 disp = ImGui::GetIO().DisplaySize;
	ImFont* font = ImGui::GetFont();
	const std::string dpm_name = display_mode_name(dpm);
	const std::string name = std::string(selected_zone->name) + ":" + std::string(dpm_name);
	const float font_size = 13.0f;

	Vector2 pan = camera2d.getPan();
	double zoom = camera2d.getZoom();

	auto it1 = zone_label_cache_.find(name);
	const ImU32  col = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.25f));
	if (it1 != zone_label_cache_.end()) {
		std::vector<zone_label>& zone_lables = it1->second;
		Vector2 pan_shift = pan - pan * zoom;
		for (const auto& zl : zone_lables) {
			// apply pan/zoom to label NDCs
			Vector2 tndc = zl.ndc * zoom + pan_shift;
			if (tndc.x < -1.0f || tndc.x > 1.0f || tndc.y < -1.0f || tndc.y > 1.0f) continue;
			Vector2 disp_p = {
				(tndc.x * 0.5f + 0.5f)  * disp.x,
				(0.5f - tndc.y * 0.5f) * disp.y
			};
			dl->AddText(font, font_size, ImVec2(disp_p.x, disp_p.y), col, zl.label.c_str());
		}
	} else {
		std::vector<zone_label> zone_lables{};
		for (int cx = 0; cx < cols; ++cx) {
			for (int ry = 0; ry < rows; ++ry) {
				double a = start_x + (cx * grid_spacing_x_km);
				double b = start_y + (ry * grid_spacing_y_km);
				// For celestial, a=lon,b=lat; for asteroid field a/b are X/Y so zone_point_to_ndc handles both
				auto ndc = zone_point_to_ndc(dpm, selected_zone, a, b, abs(grid_spacing_x_km));
				// compute transformed ndc for drawing
				float tndc_x = (ndc.x - pan.x) * static_cast<float>(zoom) + pan.x;
				float tndc_y = (ndc.y - pan.y) * static_cast<float>(zoom) + pan.y;
				float px = (tndc_x + 1.0f) * 0.5 * disp.x;
				float py = (1.0f - ((tndc_y + 1.0f) * 0.5f)) * disp.y;
				std::string key = name + ":" + std::to_string(cx) + "," + std::to_string(ry) + "@" + std::to_string((int)abs(grid_spacing_x_km)) + (coords_are_latlon ? ":LONLAT" : "");
				std::string label;


				int col_idx = cx;
				int row_idx = ry + 1;
				std::string col_label = excel_column_label(col_idx);
				label = col_label + std::to_string(row_idx);

				const zone_label zl{ ndc.x, ndc.y, label };
				zone_lables.push_back(zl);
				dl->AddText(font, font_size, ImVec2(px, py), col, label.c_str());
			}
		}
		zone_label_cache_.emplace(name, zone_lables);
	}
}

void ScoutRenderer::render_grid_for_zone(const DisplayMode dpm, const Planet* selected_zone, double grid_spacing_km) {
	if (!selected_zone) return;

	auto pan = camera2d.getPan();
	double zoom = camera2d.getZoom();
	std::vector<float> lines;
	size_t idx = 0;
	if (dpm == DisplayMode::Asteroid_Field || dpm == DisplayMode::Celestial_Belt) {
		bbox2d box = selected_zone->bounding_box_km;
		double minx = box.min_x;
		double maxx = box.max_x;
		double miny = box.min_y;
		double maxy = box.max_y;

		// Align to grid
		if (grid_spacing_km <= 100.0) grid_spacing_km = 100.0;
		const double start_x = std::floor(minx / grid_spacing_km) * grid_spacing_km;
		const double end_x = std::ceil(maxx / grid_spacing_km) * grid_spacing_km;
		const double start_y = std::floor(miny / grid_spacing_km) * grid_spacing_km;
		const double end_y = std::ceil(maxy / grid_spacing_km) * grid_spacing_km;

		

		lines.resize(static_cast<size_t>((end_x - start_x) / grid_spacing_km + (end_y - start_y) / grid_spacing_km - 2) * 4);
		// vertical lines
		for (double x = start_x + grid_spacing_km; x < end_x; x += grid_spacing_km) {
			const auto a = zone_point_to_ndc(dpm, selected_zone, x, miny, grid_spacing_km);
			const auto b = zone_point_to_ndc(dpm, selected_zone, x, maxy, grid_spacing_km);
			// Buffer raw NDC; GPU will apply pan/zoom in marker shader
			lines[idx++] = a.x;
			lines[idx++] = a.y;
			lines[idx++] = b.x;
			lines[idx++] = b.y;
		}
		// horizontal lines
		for (double y = start_y + grid_spacing_km; y < end_y; y += grid_spacing_km) {
			const auto a = zone_point_to_ndc(dpm, selected_zone, minx, y, grid_spacing_km);
			const auto b = zone_point_to_ndc(dpm, selected_zone, maxx, y, grid_spacing_km);
			// Buffer raw NDC; GPU will apply pan/zoom in marker shader
			lines[idx++] = a.x;
			lines[idx++] = a.y;
			lines[idx++] = b.x;
			lines[idx++] = b.y;
		}


		// Render sector labels for this asteroid bounding-grid
		
		int cols = static_cast<int>(std::round((end_x - start_x) / grid_spacing_km));
		int rows = static_cast<int>(std::round((end_y - start_y) / grid_spacing_km));
			if (cols > 0 && rows > 0) {
				render_sector_labels_grid(dpm, selected_zone, start_x, end_y, cols, rows, grid_spacing_km, -grid_spacing_km, false);
		}
		
	}
	if (dpm == DisplayMode::Surface) {
		// Draw planetary lat/lon grid lines (treat grid_spacing_km as degrees)
		const double deg_step = grid_spacing_km;
		if (deg_step > 0.0) {
			lines.resize(static_cast<size_t>((360.0 / deg_step) + (180.0 / deg_step) -2) * 4);
			// longitude lines (meridians)
			for (double lon = -180.0 + deg_step; lon < 180.0; lon += deg_step) {
				const auto p1 = latlon_to_ndc(-90.0, lon);
				const auto p2 = latlon_to_ndc(90.0, lon);
				lines[idx++] = p1.x;
				lines[idx++] = p1.y;
				lines[idx++] = p2.x;
				lines[idx++] = p2.y;
			}
			// latitude lines (parallels)
			for (double lat = -90.0 + deg_step; lat < 90.0; lat += deg_step) {
				const auto p1 = latlon_to_ndc(lat, -180.0);
				const auto p2 = latlon_to_ndc(lat, 180.0);
				lines[idx++] = p1.x;
				lines[idx++] = p1.y;
				lines[idx++] = p2.x;
				lines[idx++] = p2.y;
			}

			// Render sector labels for planetary grid: start lon/lat at -180/-90
			int cols = static_cast<int>(std::floor(360.0 / deg_step));
			int rows = static_cast<int>(std::floor(180.0 / deg_step));
			if (cols > 0 && rows > 0) {
				render_sector_labels_grid(dpm, selected_zone, 90.0f, -180.0f, rows, cols, -deg_step, deg_step, true);
			}
		}

		
	}
	if (!lines.empty()) {
			glUseProgram(marker_shader_);
			// set camera2d pan/zoom uniforms for planetary grid rendering
			if (marker_pan_ndc_loc_ != -1) glUniform2f(marker_pan_ndc_loc_, pan.x, pan.y);
			if (marker_zoom_loc_ != -1) glUniform1f(marker_zoom_loc_, static_cast<float>(zoom));
			glBindVertexArray(marker_vao_);
			glBindBuffer(GL_ARRAY_BUFFER, marker_vbo_);
			// buffer raw NDC lat/lon lines; shader applies pan/zoom
			glBufferData(GL_ARRAY_BUFFER, lines.size() * sizeof(float), lines.data(), GL_DYNAMIC_DRAW);
			// gray and opaque; slightly thicker for planetary grid
			if (marker_color_loc_ != -1) {
				glUniform4f(marker_color_loc_, 0.6f, 0.6f, 0.6f, 0.25f);
			} else {
				const GLint color_loc2 = glGetUniformLocation(marker_shader_, "u_color");
				if (color_loc2 != -1) marker_color_loc_ = color_loc2;
				glUniform4f(color_loc2, 0.6f, 0.6f, 0.6f, 0.25f);
			}
			//glLineWidth(20.0f);
			glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lines.size() / 2));
			glBindVertexArray(0);
		}
}

struct DataPoint;

static const char* quad_vs = R"GLSL(#version 330 core
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_uv;
out vec2 uv;
uniform vec2 u_pan_uv;
uniform float u_zoom;
void main() {
	// Do NOT transform vertex positions here; keep quad covering NDC [-1,1].
	// Transform UVs only: sample a different region of the texture to produce
	// the visual pan/zoom effect that matches points/grid rendered in NDC.
	uv = (in_uv - u_pan_uv) / u_zoom + u_pan_uv;
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
uniform vec2 u_pan_ndc;
uniform float u_zoom;
uniform float u_point_size;
void main() {
	v_color = in_color;
	// apply pan/zoom in NDC space on GPU
	vec2 p = (in_pos - u_pan_ndc) * u_zoom + u_pan_ndc;
	gl_Position = vec4(p, 0.0, 1.0);
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
uniform vec2 u_pan_ndc;
uniform float u_zoom;
void main() {
	vec2 p = (in_pos - u_pan_ndc) * u_zoom + u_pan_ndc;
	gl_Position = vec4(p, 0.0, 1.0);
	gl_PointSize = 4.0;
}
)GLSL";


static const char* planet_fs = R"GLSL(#version 330 core
in vec2 uv;
out vec4 out_color;
uniform sampler2D u_texture;
uniform float u_radius; // planet radius in UV units (0..0.5 typical)
uniform float u_vscale; // vertical scale for sampling (1.0 = full image, 0.5 = top-half)
uniform float u_rotate; // rotation angle in UV space (0..1, where 1 = full rotation = 360 degrees)
const float PI = 3.14159265358979323846;
void main() {
	// local coordinates relative to planet center mapped to fragment-space
	// (vertex shader remapped the incoming UV so that planet center == 0.5,0.5)
	vec2 c = uv - vec2(0.5, 0.5);
	vec2 nd = c / u_radius; // normalized disk coords (-1..1)
	float r2 = dot(nd, nd);
	if (r2 > 1.0) discard;

	// Orthographic / top-down sphere mapping:
	// treat nd as X,Y on the unit disk; compute Z on the unit sphere and
	// convert to latitude/longitude for equirectangular sampling.
	float z = sqrt(max(0.0, 1.0 - r2));
	// latitude: asin(1-z) gives range [0 .. PI/2] for visible hemisphere
	float lat = asin(-z);
	// longitude: atan2(x, -y) so that screen-up corresponds to north
	float lon = atan(-nd.x, -nd.y);

	// convert to equirectangular UV (0..1)
	float u = (lon + PI) / (2.0 * PI) + u_rotate;
	float v = (lat + (PI * 0.5)) / PI;

	// sample directly (vertex shader already applied the center translation)
	vec2 sample_uv = vec2(u, v);
	vec4 col = texture(u_texture, sample_uv);
	out_color = col;
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
	if (planet_shader_) glDeleteProgram(planet_shader_);

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

void ScoutRenderer::reset_grid_cache(const std::string& zone) {
	if (zone.empty()) return;
	std::string name = zone;
	// Remove any cached labels for this zone
	for (auto it = zone_label_cache_.begin(); it != zone_label_cache_.end(); ) {
		if (it->first.find(name) != std::string::npos) {
			it = zone_label_cache_.erase(it);
		} else {
			++it;
		}
	}
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

	// Cache the sampler uniform location for the quad shader and set it to texture unit 0.
	glUseProgram(quad_shader_);
	quad_texture_loc_ = glGetUniformLocation(quad_shader_, "u_texture");
	if (quad_texture_loc_ != -1) {
		glUniform1i(quad_texture_loc_, 0);
	}
	// cache pan/zoom uniforms for quad shader
	quad_pan_ndc_loc_ = glGetUniformLocation(quad_shader_, "u_pan_ndc");
	quad_uv_pan_loc_ = glGetUniformLocation(quad_shader_, "u_pan_uv");
	quad_zoom_loc_ = glGetUniformLocation(quad_shader_, "u_zoom");

	// Planet shader: uses same vertex layout as quad, but masks to a circle and samples top-half of texture
	GLuint pvs = compile_shader(GL_VERTEX_SHADER, quad_vs);
	GLuint pfs = compile_shader(GL_FRAGMENT_SHADER, planet_fs);
	if (pvs && pfs) {
		planet_shader_ = link_program(pvs, pfs);
	}
	if (pvs) glDeleteShader(pvs);
	if (pfs) glDeleteShader(pfs);
	if (planet_shader_) {
		glUseProgram(planet_shader_);
		planet_texture_loc_ = glGetUniformLocation(planet_shader_, "u_texture");
		if (planet_texture_loc_ != -1) glUniform1i(planet_texture_loc_, 0);
		// cache and initialize new planet shader uniforms
		planet_center_loc_ = glGetUniformLocation(planet_shader_, "u_center");
		if (planet_center_loc_ != -1) glUniform2f(planet_center_loc_, 0.5f, 0.5f);
		planet_radius_loc_ = glGetUniformLocation(planet_shader_, "u_radius");
		if (planet_radius_loc_ != -1) glUniform1f(planet_radius_loc_, 1.0f);
		planet_vscale_loc_ = glGetUniformLocation(planet_shader_, "u_vscale");
		if (planet_vscale_loc_ != -1) glUniform1f(planet_vscale_loc_, 1.0f);
		planet_rotate_loc_ = glGetUniformLocation(planet_shader_, "u_rotate");
		if (planet_rotate_loc_ != -1) glUniform1f(planet_rotate_loc_, 0.25f);
		// cache pan/zoom uniforms for planet shader
		planet_pan_ndc_loc_ = glGetUniformLocation(planet_shader_, "u_pan_ndc");
		planet_uv_pan_loc_ = glGetUniformLocation(planet_shader_, "u_pan_uv");
		planet_zoom_loc_ = glGetUniformLocation(planet_shader_, "u_zoom");
	}
	glUseProgram(0);

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

	// Cache points shader uniform location for point size
	glUseProgram(points_shader_);
	points_point_size_loc_ = glGetUniformLocation(points_shader_, "u_point_size");
	points_pan_ndc_loc_ = glGetUniformLocation(points_shader_, "u_pan_ndc");
	points_zoom_loc_ = glGetUniformLocation(points_shader_, "u_zoom");
	glUseProgram(0);

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

	// Cache marker shader uniform location for color
	glUseProgram(marker_shader_);
	marker_color_loc_ = glGetUniformLocation(marker_shader_, "u_color");
	marker_pan_ndc_loc_ = glGetUniformLocation(marker_shader_, "u_pan_ndc");
	marker_zoom_loc_ = glGetUniformLocation(marker_shader_, "u_zoom");
	glUseProgram(0);

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

static inline void FillPointBuffer(float x, float y, std::vector<float>& buf, std::vector<float>& border_buf, size_t idx, rgba& colour, const DataPoint& point) {
	colour.r = 1.0f; colour.g = 1.0f; colour.b = 1.0f; colour.a = 1.0f;
	if (point.poi_type == PoiType::Mineral) {
		double quality_norm = (point.quality_max - 0.0) / (1000.0 - 0.0);
		quality_norm = std::clamp(quality_norm, 0.0, 1.0);
		if (quality_norm < 0.5) {
			const auto t = static_cast<float>(quality_norm * 2.0);
			// r = 0.0f;
			// g = t;
			// b = 1.0f - t;
			colour.overide(0.0f, t, 1.0f - t);
		} else {
			const auto t = static_cast<float>((quality_norm - 0.5) * 2.0);
			colour.overide(t, 1.0f - t, 0.0f);
		}
		colour.a = 1.0f;
	}
	if (point.poi_type == PoiType::Cave) {
		// give brown color to caves
		colour.overide(cave);
	}
	if (point.poi_type == PoiType::Wreck) {
		// give rust color to wrecks
		// 	(183, 65, 14)
		colour.overide(wreck);
	}
	if (point.poi_type == PoiType::Location) {


		if (point.subtype == PoiSubType::Onyx_Facility) {
			// give gray color to Onyx facilities
			colour.overide(onyx_facility);
		} else if (!point.qt_persistent) {
			// gray color for locations that can't be quantum targeted.
			colour.overide(qtless_location);
		}
	}

	buf.at(idx) = x;
	buf.at(idx + 1) = y;
	buf.at(idx + 2) = colour.r;
	buf.at(idx + 3) = colour.g;
	buf.at(idx + 4) = colour.b;
	buf.at(idx + 5) = colour.a;

	// choose border color: black if color is light, white if dark
	const float lum = colour.luminance();
	float br = 0.0f, bg = 0.0f, bb = 0.0f, ba = 1.0f;
	if (lum > 0.5f) {
		// light color -> use black border
		br = bg = bb = 0.0f;
	} else {
		// dark color -> use white border
		br = bg = bb = 0.6f;
	}
	border_buf.at(idx) = x;
	border_buf.at(idx + 1) = y;
	border_buf.at(idx + 2) = br;
	border_buf.at(idx + 3) = bg;
	border_buf.at(idx + 4) = bb;
	border_buf.at(idx + 5) = ba;
}

void ScoutRenderer::RenderPlanet(GLuint texture, const Planet* selected_zone, double radius_planet_km, double grid_spacing_km) {
	if (!planet_shader_) return;
	glUseProgram(planet_shader_);
	// bind texture unit 0
	if (!last_bound_texture_unit0_valid_ || last_bound_texture_unit0_ != texture) {
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, texture);
		last_bound_texture_unit0_ = texture;
		last_bound_texture_unit0_valid_ = true;
	}

	// compute center and radius in UV space based on selected zone bbox
	float center_u = 0.5f, center_v = 0.5f;
	float uv_radius = 0.5f;
	
	if (selected_zone) {
		const bbox2d& box = selected_zone->bounding_box_km;
		// compute center in km
		double cx = 0.0, cy = 0.0;
		if (box.max_x > box.min_x) cx = (box.min_x + box.max_x) * 0.5;
		if (box.max_y > box.min_y) cy = (box.min_y + box.max_y) * 0.5;
		// map center (0,0) in world coords -> ndc via asteriod_point_to_ndc
		auto center_ndc = asteroid_point_to_ndc(box, grid_spacing_km, 0.0, 0.0);
		center_u = (center_ndc.x * 0.5f) + 0.5f;
		center_v = (1.0f - center_ndc.y) * 0.5f;

		// compute half extents used by asteriod_point_to_ndc
		double half_w = (box.max_x > box.min_x) ? ((box.max_x - box.min_x) * 0.5) : std::max(1.0, grid_spacing_km * 3.0);
		double half_h = (box.max_y > box.min_y) ? ((box.max_y - box.min_y) * 0.5) : std::max(1.0, grid_spacing_km * 3.0);
		// If no explicit planet radius provided, default to fitting the bounding-box
		if (radius_planet_km <= 0.0) {
			radius_planet_km = std::min(half_w, half_h);
		}
		// ndc radius in X/Y
		double ndc_rx = radius_planet_km / half_w;
		double ndc_ry = radius_planet_km / half_h;
		double ndc_r = std::min(ndc_rx, ndc_ry);
		if (ndc_r < 0.0) ndc_r = 0.0;
		if (ndc_r > 2.0) ndc_r = 2.0; // clamp
		// convert ndc radius to uv radius (ndc range [-1,1] -> uv span 1.0 corresponds to ndc span 2.0)
		uv_radius = static_cast<float>(ndc_r * 0.5);
		// clamp to reasonable range
		uv_radius = std::clamp(uv_radius, 0.0f, 0.5f);
	}

	// set planet shader uniforms (center, radius, sample v-scale for full texture)
	// set pan/zoom uniforms for planet shader using camera2d2D
	auto pan = camera2d.getPan();
	double zoom = camera2d.getZoom();
	if (planet_pan_ndc_loc_ != -1) glUniform2f(planet_pan_ndc_loc_, pan.x, pan.y);
	if (planet_zoom_loc_ != -1) glUniform1f(planet_zoom_loc_, static_cast<float>(zoom));
	// compute uv pan from camera2d
	auto uv_pan = camera2d.uvPan();
	float uv_pan_x = uv_pan.x;
	float uv_pan_y = uv_pan.y;
	if (planet_uv_pan_loc_ != -1) glUniform2f(planet_uv_pan_loc_, uv_pan_x, uv_pan_y);
	// transform planet center by uv pan/zoom so it aligns with background transform
	// UV sampling is inverse of NDC zoom: divide by zoom.
	float center_u_t = (center_u - uv_pan_x) / static_cast<float>(zoom) + uv_pan_x;
	float center_v_t = (center_v - uv_pan_y) / static_cast<float>(zoom) + uv_pan_y;
	if (planet_center_loc_ != -1) glUniform2f(planet_center_loc_, center_u_t, center_v_t);
	if (planet_radius_loc_ != -1) glUniform1f(planet_radius_loc_, uv_radius);
	if (planet_vscale_loc_ != -1) glUniform1f(planet_vscale_loc_, 1.0f);

	// draw full-screen quad; planet shader will mask to circular disk and sample as a north-pole view
	glBindVertexArray(quad_vao_);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	glBindVertexArray(0);
	glUseProgram(0);
}


std::optional<std::string> ScoutRenderer::render_map(const RenderMapParams& params) {

	// Validate input
	if (!params.points) return std::nullopt;

	const GLuint texture = params.texture;
	const auto& points = *params.points;
	const auto mouse_pos = params.mouse_pos;
	const auto* material_catalog = params.material_catalog;
	const Planet* selected_zone = params.selected_zone;
	const DisplayMode dpm = params.display_mode;
	const double grid_spacing_km = params.grid_spacing_km;

	auto pan = camera2d.getPan();
	double zoom = camera2d.getZoom();

	if (dpm == DisplayMode::Asteroid_Field || dpm == DisplayMode::Surface || dpm == DisplayMode::Celestial_Belt) {
		// Draw textured quad. Only change active texture / bind when texture differs

		if(dpm== DisplayMode::Celestial_Belt) {
			// render top-down (north-pole) view of planet for celestial belt
			RenderPlanet(texture, selected_zone, selected_zone ? selected_zone->planet_radius_km : 500.0, grid_spacing_km);

			// Draw faint glow ring for asteroid belt area (if configured)
			RenderAstroidFieldZone(selected_zone, grid_spacing_km);
		}
		else{
			RenderBackground(texture);
		}

		// Draw grid for the selected zone (asteroid bbox or planetary lat/lon)
		if (selected_zone) {
			render_grid_for_zone(dpm, selected_zone, grid_spacing_km);
		}
		// Prepare points buffer (pos + rgba) and border buffer
		std::vector<float> buf;
		std::vector<float> border_buf;
		buf.resize(points.size() * 6); // 2 for pos, 4 for rgba
		border_buf.resize(points.size() * 6);
		rgba colour = { 1.0f, 1.0f, 1.0f, 1.0f };
		int displayed_points = 0;
		for (const auto& point : points) {
			float x = 0.0f, y = 0.0f;
			LatLonAlt latlonalt = point.get_lat_lon_alt();
			// dont display points that are on the south size of the planet when in celestial belt mode when altitude is planetradius + 50km (likely to be planetary features)
			if (dpm == DisplayMode::Celestial_Belt) {
				if (latlonalt.altitude < selected_zone->planet_radius_km + selected_zone->karman_line_km && latlonalt.latitude < 0.0) {
					continue;
				}
			}
			else if (dpm == DisplayMode::Surface && selected_zone->has_asteroid_belt) {
				if (latlonalt.altitude > selected_zone->planet_radius_km + selected_zone->karman_line_km && point.poi_type == PoiType::Mineral) {
					// skip points above the surface when in surface mode
					continue;
				}
			}
			
			auto ndc = dpm == DisplayMode::Asteroid_Field || dpm == DisplayMode::Celestial_Belt
				? asteroid_point_to_ndc(selected_zone->bounding_box_km, grid_spacing_km, point.coord.x, point.coord.y)
				: latlon_to_ndc(latlonalt.latitude, latlonalt.longitude);

			FillPointBuffer(ndc.x, ndc.y, buf, border_buf, displayed_points * 6, colour, point);
			displayed_points++;
		}

		// Upload and draw border (one size larger)
		RenderPointsWithBorder(border_buf, buf);

		// Hover detection (CPU, same logic as before)
		if (!mouse_pos) return std::nullopt;
		const auto [mx, my] = *mouse_pos;
		float closest_dist = 0.001f;
		std::vector<std::pair<float, DataPoint*>> closest_points;

		for (const auto& point : points) {
			float px = 0.0f, py = 0.0f;
			LatLonAlt latlonalt = point.get_lat_lon_alt();

			// dont display points that are on the south size of the planet when in celestial belt mode when altitude is planetradius + 50km (likely to be planetary features)
			if (dpm == DisplayMode::Celestial_Belt) {
				if (latlonalt.latitude < 0.0 && latlonalt.altitude < selected_zone->planet_radius_km + selected_zone->karman_line_km) {
					continue;
				}
			} else if (dpm == DisplayMode::Surface) {
				if (selected_zone->planet_radius_km > 30 && latlonalt.altitude > selected_zone->planet_radius_km + selected_zone->karman_line_km && point.poi_type == PoiType::Mineral) {
					// skip points above the surface when in surface mode
					continue;
				}
			}

			auto pndc = dpm == DisplayMode::Asteroid_Field || dpm == DisplayMode::Celestial_Belt
				? asteroid_point_to_ndc(selected_zone->bounding_box_km, grid_spacing_km, point.coord.x, point.coord.y)
				: latlon_to_ndc(latlonalt.latitude, latlonalt.longitude);
			// transform to screen space consistent with rendering
			auto pt = camera2d.applyToNdc(pndc.x, pndc.y);
			px = pt.x;
			py = pt.y;
			const float dx = mx - px;
			const float dy = my - py;
			const float dist = (dx * dx) + (dy * dy);
			if (dist <= closest_dist) {
				closest_points.push_back({dist, const_cast<DataPoint*>(&point)});
			}
		}
		
		if (!closest_points.empty()) {
			std::string tooltip;
			// sort by distance and keep only those within the closest distance threshold (to allow for multiple points at same location)
			std::sort(closest_points.begin(), closest_points.end(), [](const auto& a, const auto& b) {
				return a.first < b.first;
				});

			
			for (size_t i = 0; i < closest_points.size(); ++i) {
				const auto& [dist, closest_point] = closest_points[i];
				if (closest_point->poi_type == PoiType::Mineral) {
					// if this point is a mineral, prepend material and quality info to the tooltip
					if (material_catalog) {
						auto it = std::find_if(material_catalog->begin(), material_catalog->end(), [&](const Resource& m) {
							return m.name == closest_point->material;
						});
						if (it != material_catalog->end()) {
							const auto material_id = it->short_name;
							tooltip += material_id + " q" + std::to_string(int(closest_point->quality_max));
							if (closest_point->note.size() > 0)
								tooltip += "\n" + closest_point->note;
						}
					}
				}
				else {
					tooltip += closest_point->note;
				}

				if (i < closest_points.size() - 1) {
					tooltip += "\n";
				}

			}
			return tooltip;
		}
	}

	return std::nullopt;
}

void ScoutRenderer::RenderAstroidFieldZone(const Planet* selected_zone, double grid_spacing_km)
{
	if (selected_zone && (selected_zone->has_asteroid_belt || selected_zone->belt_outer_radius_km > 0.0)) {
		auto pan = camera2d.getPan();
		double zoom = camera2d.getZoom();
		ImDrawList* dl = ImGui::GetBackgroundDrawList();
		ImVec2 disp = ImGui::GetIO().DisplaySize;
		const bbox2d& box = selected_zone->bounding_box_km;
		// compute center ndc and pixel center
		auto center_ndc = asteroid_point_to_ndc(box, grid_spacing_km, 0.0, 0.0);
		// transform center by camera2d
		auto center_t = camera2d.applyToNdc(center_ndc.x, center_ndc.y);
		float center_ndc_x_t = center_t.x;
		float center_ndc_y_t = center_t.y;
		float px = (center_ndc_x_t + 1.0f) * 0.5f * disp.x;
		float py = (1.0f - ((center_ndc_y_t + 1.0f) * 0.5f)) * disp.y;

		// compute accurate pixel radii by mapping points at planet-center + radius_km to NDC using same mapping
		double inner_km = selected_zone->belt_inner_radius_km > 0.0 ? selected_zone->belt_inner_radius_km : (std::min(box.max_x - box.min_x, box.max_y - box.min_y) * 0.25);
		double outer_km = selected_zone->belt_outer_radius_km > 0.0 ? selected_zone->belt_outer_radius_km : (std::min(box.max_x - box.min_x, box.max_y - box.min_y) * 0.45);

		// center in world coords is 0,0 per design; map to NDC
		auto center_ndc2 = asteroid_point_to_ndc(box, grid_spacing_km, 0.0, 0.0);
		auto inner_ndc_x_pair = asteroid_point_to_ndc(box, grid_spacing_km, inner_km, 0.0);
		auto inner_ndc_y_pair = asteroid_point_to_ndc(box, grid_spacing_km, 0.0, inner_km);
		auto outer_ndc_x_pair = asteroid_point_to_ndc(box, grid_spacing_km, outer_km, 0.0);
		auto outer_ndc_y_pair = asteroid_point_to_ndc(box, grid_spacing_km, 0.0, outer_km);

		// transform ndc radii points by camera2d
		auto center_ndc2_t = camera2d.applyToNdc(center_ndc2.x, center_ndc2.y);
		auto inner_x_t = camera2d.applyToNdc(inner_ndc_x_pair.x, inner_ndc_x_pair.y);
		auto inner_y_t = camera2d.applyToNdc(inner_ndc_y_pair.x, inner_ndc_y_pair.y);
		auto outer_x_t = camera2d.applyToNdc(outer_ndc_x_pair.x, outer_ndc_x_pair.y);
		auto outer_y_t = camera2d.applyToNdc(outer_ndc_y_pair.x, outer_ndc_y_pair.y);
		auto center_ndc2_tx = center_ndc2_t.x;
		auto center_ndc2_ty = center_ndc2_t.y;
		auto inner_ndc_x_t = inner_x_t.x;
		auto inner_ndc_y_t = inner_y_t.y;
		auto outer_ndc_x_t = outer_x_t.x;
		auto outer_ndc_y_t = outer_y_t.y;

		float center_px_x = (center_ndc2_tx + 1.0f) * 0.5f * disp.x;
		float center_px_y = (1.0f - ((center_ndc2_ty + 1.0f) * 0.5f)) * disp.y;

		// compute separate x/y pixel radii for inner and outer to form ellipses
		float inner_px_x = std::abs((inner_ndc_x_t - center_ndc2_tx)) * 0.5f * disp.x;
		float inner_px_y = std::abs((inner_ndc_y_t - center_ndc2_ty)) * 0.5f * disp.y;
		float outer_px_x = std::abs((outer_ndc_x_t - center_ndc2_tx)) * 0.5f * disp.x;
		float outer_px_y = std::abs((outer_ndc_y_t - center_ndc2_ty)) * 0.5f * disp.y;

		if (outer_px_x > 1.0f && outer_px_x > inner_px_x) {

			// ensure positive radii
			inner_px_x = std::max(1.0f, inner_px_x);
			inner_px_y = std::max(1.0f, inner_px_y);
			outer_px_x = std::max(inner_px_x + 1.0f, outer_px_x);
			outer_px_y = std::max(inner_px_y + 1.0f, outer_px_y);

			const int segments = 64;
			std::vector<ImVec2> inner_pts;
			std::vector<ImVec2> outer_pts;
			inner_pts.reserve(segments);
			outer_pts.reserve(segments);

			for (int i = 0; i < segments; ++i) {
				float t = (2.0f * 3.14159265358979323846f) * (static_cast<float>(i) / static_cast<float>(segments));
				float ox = center_px_x + outer_px_x * std::cos(t);
				float oy = center_px_y + outer_px_y * std::sin(t);
				float ix = center_px_x + inner_px_x * std::cos(t);
				float iy = center_px_y + inner_px_y * std::sin(t);
				outer_pts.emplace_back(ox, oy);
				inner_pts.emplace_back(ix, iy);
			}

			// fill color: faint light blue
			ImU32 fill_col = ImGui::GetColorU32(ImVec4(0.2f, 0.6f, 1.0f, 0.06f));

			// create triangle mesh between inner and outer rings by connecting vertex i and i+1
			for (int i = 0; i < segments; ++i) {
				int ni = (i + 1) % segments;
				// triangle 1: inner[i], outer[i], inner[ni]
				dl->AddTriangleFilled(inner_pts[i], outer_pts[i], inner_pts[ni], fill_col);
				// triangle 2: outer[i], outer[ni], inner[ni]
				dl->AddTriangleFilled(outer_pts[i], outer_pts[ni], inner_pts[ni], fill_col);
			}
		}
	}
}

void ScoutRenderer::RenderBackground(GLuint texture)
{
	glUseProgram(quad_shader_);
	if (!last_bound_texture_unit0_valid_ || last_bound_texture_unit0_ != texture) {
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, texture);
		last_bound_texture_unit0_ = texture;
		last_bound_texture_unit0_valid_ = true;
	}
	// Ensure sampler is initialized at least once (usually set in init()).
	if (quad_texture_loc_ == -1) {
		const GLint tex_loc = glGetUniformLocation(quad_shader_, "u_texture");
		if (tex_loc != -1) {
			quad_texture_loc_ = tex_loc;
			glUniform1i(quad_texture_loc_, 0);
		}
	}
	// set pan/zoom uniforms for quad shader from camera2d2D
	auto pan = camera2d.getPan();
	double zoom = camera2d.getZoom();
	if (quad_pan_ndc_loc_ != -1) glUniform2f(quad_pan_ndc_loc_, pan.x, pan.y);
	// compute uv pan from ndc pan via camera2d
	auto uvpan = camera2d.uvPan();
	float uv_pan_x = uvpan.x;
	float uv_pan_y = uvpan.y;
	if (quad_uv_pan_loc_ != -1) glUniform2f(quad_uv_pan_loc_, uv_pan_x, uv_pan_y);
	if (quad_zoom_loc_ != -1) glUniform1f(quad_zoom_loc_, static_cast<float>(zoom));
	glBindVertexArray(quad_vao_);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	glBindVertexArray(0);
}

void ScoutRenderer::RenderPointsWithBorder(std::vector<float>& border_buf, std::vector<float>& buf)
{
	if (border_buf.empty()) return;

	glUseProgram(points_shader_);
	// set camera2d pan/zoom uniforms for points shader
	auto pan = camera2d.getPan();
	double zoom = camera2d.getZoom();
	if (points_pan_ndc_loc_ != -1) glUniform2f(points_pan_ndc_loc_, pan.x, pan.y);
	if (points_zoom_loc_ != -1) glUniform1f(points_zoom_loc_, static_cast<float>(zoom));
	glBindVertexArray(points_vao_);
	glBindBuffer(GL_ARRAY_BUFFER, points_vbo_);
	glBufferData(GL_ARRAY_BUFFER, border_buf.size() * sizeof(float), border_buf.data(), GL_DYNAMIC_DRAW);
	// Use cached point-size uniform location if available (fallback to query-and-cache)
	GLint size_loc = points_point_size_loc_;
	if (size_loc == -1) {
		size_loc = glad_glGetUniformLocation(points_shader_, "u_point_size");
		if (size_loc != -1)
			points_point_size_loc_ = size_loc;
	}
	const float base_size = 3.0f;
	glUniform1f(size_loc, base_size + 2.0f);
	glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(border_buf.size()));
	// Draw inner points on top
	glBufferData(GL_ARRAY_BUFFER, buf.size() * sizeof(float), buf.empty() ? nullptr : buf.data(), GL_DYNAMIC_DRAW);
	glUniform1f(size_loc, base_size);
	glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(border_buf.size()));
	glBindVertexArray(0);
	
}

void ScoutRenderer::render_marker(float x, float y, float r, float g, float b, float a, float size) {
	glUseProgram(marker_shader_);
	// set camera2d pan/zoom uniforms for marker shader
	auto pan = camera2d.getPan();
	double zoom = camera2d.getZoom();
	if (marker_pan_ndc_loc_ != -1) glUniform2f(marker_pan_ndc_loc_, pan.x, pan.y);
	if (marker_zoom_loc_ != -1) glUniform1f(marker_zoom_loc_, static_cast<float>(zoom));
	glBindVertexArray(marker_vao_);
	float pos[2] = { x, y };
	glBindBuffer(GL_ARRAY_BUFFER, marker_vbo_);
	glBufferData(GL_ARRAY_BUFFER, sizeof(pos), pos, GL_DYNAMIC_DRAW);
	// Use cached marker color uniform location if available
	if (marker_color_loc_ != -1) {
		glUniform4f(marker_color_loc_, r, g, b, a);
	} else {
		const GLint color_loc = glGetUniformLocation(marker_shader_, "u_color");
		if (color_loc != -1) marker_color_loc_ = color_loc;
		glUniform4f(color_loc, r, g, b, a);
	}
	glDrawArrays(GL_POINTS, 0, 1);
	glBindVertexArray(0);
}



void ScoutRenderer::render_track(const DisplayMode dpm, const std::vector<DataPoint>& track, const Planet* selected_zone, double grid_spacing_km, const rgba& track_color) {
	if (track.empty()) return;

	const double segment_km = 25.0;

	// helpers for spherical interpolation
	const auto deg2rad = [](double d) { return d * (3.14159265358979323846 / 180.0); };
	const auto rad2deg = [](double r) { return r * (180.0 / 3.14159265358979323846); };

	auto latlon_to_unit = [&](double lat_deg, double lon_deg) {
		double lat = deg2rad(lat_deg);
		double lon = deg2rad(lon_deg);
		double x = std::cos(lat) * std::cos(lon);
		double y = std::cos(lat) * std::sin(lon);
		double z = std::sin(lat);
		return Vector3{ x, y, z };
	};
	auto unit_to_latlon = [&](const Vector3& v) {
		double norm = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
		if (norm == 0.0) return std::pair<double, double>(0.0, 0.0);
		double x = v.x / norm; double y = v.y / norm; double z = v.z / norm;
		double lat = std::asin(std::clamp(z, -1.0, 1.0));
		double lon = std::atan2(y, x);
		return std::pair<double, double>(rad2deg(lat), rad2deg(lon));
	};
	auto central_angle_rad = [&](const Vector3& a, const Vector3& b) {
		double dot = a.x * b.x + a.y * b.y + a.z * b.z;
		dot = std::clamp(dot, -1.0, 1.0);
		return std::acos(dot);
	};

	// Build expanded lat/lon or planar point list with subdivision <= segment_km
	struct LL { double lat; double lon; double x; double y; bool is_surface; };
	std::vector<LL> expanded;
	expanded.reserve(track.size() * 4);

	// Determine planet radius for spherical distance calculations
	double planet_radius_km = 6371.0; // fallback Earth-like
	if (selected_zone && selected_zone->planet_radius_km > 0.0) planet_radius_km = selected_zone->planet_radius_km;

	// helper to push raw lat/lon converted from DataPoint
	auto push_first_point = [&](const DataPoint& p) {
		if (dpm == DisplayMode::Asteroid_Field || dpm == DisplayMode::Celestial_Belt) {
			expanded.push_back({ 0.0, 0.0, p.coord.x, p.coord.y, false });
		} else {
			auto lla = p.get_lat_lon_alt();
			expanded.push_back({ lla.latitude, lla.longitude, 0.0, 0.0, true });
		}
	};

	push_first_point(track.front());

	for (size_t i = 1; i < track.size(); ++i) {
		const DataPoint& a = track[i - 1];
		const DataPoint& b = track[i];
		if (dpm == DisplayMode::Asteroid_Field || dpm == DisplayMode::Celestial_Belt) {
			double dx = b.coord.x - a.coord.x; double dy = b.coord.y - a.coord.y;
			double dist = std::sqrt(dx*dx + dy*dy);
			int steps = static_cast<int>(std::ceil(dist / segment_km));
			if (steps < 1) steps = 1;
			for (int s = 1; s <= steps; ++s) {
				double t = static_cast<double>(s) / steps;
				double xi = a.coord.x + t * dx;
				double yi = a.coord.y + t * dy;
				expanded.push_back({ 0.0, 0.0, xi, yi, false });
			}
		} else {
			auto la = a.get_lat_lon_alt();
			auto lb = b.get_lat_lon_alt();
			// convert to unit vectors
			Vector3 ua = latlon_to_unit(la.latitude, la.longitude);
			Vector3 ub = latlon_to_unit(lb.latitude, lb.longitude);
			double ang = central_angle_rad(ua, ub);
			double dist_km = ang * planet_radius_km;
			int steps = static_cast<int>(std::ceil(dist_km / segment_km));
			if (steps < 1) steps = 1;
			for (int s = 1; s <= steps; ++s) {
				double t = static_cast<double>(s) / steps;
				// slerp
				Vector3 vi;
				if (ang < 1e-6) {
					// nearly identical, lerp
					vi.x = ua.x * (1.0 - t) + ub.x * t;
					vi.y = ua.y * (1.0 - t) + ub.y * t;
					vi.z = ua.z * (1.0 - t) + ub.z * t;
				} else {
					double sin_ang = std::sin(ang);
					double A = std::sin((1.0 - t) * ang) / sin_ang;
					double B = std::sin(t * ang) / sin_ang;
					vi.x = A * ua.x + B * ub.x;
					vi.y = A * ua.y + B * ub.y;
					vi.z = A * ua.z + B * ub.z;
				}
				auto latlon = unit_to_latlon(vi);
				expanded.push_back({ latlon.first, latlon.second, 0.0, 0.0, true });
			}
		}
	}

	// Now split into drawable segments when crossing the dateline (surface only)
	std::vector<std::vector<float>> draw_segments;
	std::vector<float> cur_seg;
	cur_seg.reserve(expanded.size() * 2);

	auto push_latlon_to_seg = [&](std::vector<float>& seg, double lat, double lon) {
		Vector2 ndc = latlon_to_ndc(lat, lon);
		seg.push_back(ndc.x);
		seg.push_back(ndc.y);
	};

	if (dpm == DisplayMode::Asteroid_Field || dpm == DisplayMode::Celestial_Belt) {
		for (const auto& e : expanded) {
			Vector2 ndc = asteroid_point_to_ndc(selected_zone->bounding_box_km, grid_spacing_km, e.x, e.y);
			cur_seg.push_back(ndc.x);
			cur_seg.push_back(ndc.y);
		}
		if (!cur_seg.empty()) draw_segments.push_back(std::move(cur_seg));
	} else {
		// surface: detect raw lon jumps > 180 and split
		for (size_t i = 0; i < expanded.size(); ++i) {
			const auto& e = expanded[i];
			if (i == 0) {
				push_latlon_to_seg(cur_seg, e.lat, e.lon);
				continue;
			}
			const auto& prev = expanded[i - 1];
			double lon1 = prev.lon; double lon2 = e.lon;
			double dlon = lon2 - lon1;
			if (std::abs(dlon) > 180.0) {
				// crossing dateline
				double lon2_adj = lon2;
				if (dlon > 0) lon2_adj = lon2 - 360.0; else lon2_adj = lon2 + 360.0;
				double crossing_lon = (lon1 >= 0.0) ? 180.0 : -180.0;
				double denom = lon2_adj - lon1;
				double t = (denom == 0.0) ? 0.5 : ((crossing_lon - lon1) / denom);
				t = std::clamp(t, 0.0, 1.0);
				double lat_cross = prev.lat + t * (e.lat - prev.lat);
				// add crossing point to current seg
				push_latlon_to_seg(cur_seg, lat_cross, crossing_lon);
				if (!cur_seg.empty()) draw_segments.push_back(std::move(cur_seg));
				cur_seg.clear();
				double other_border = (crossing_lon > 0.0) ? -180.0 : 180.0;
				push_latlon_to_seg(cur_seg, lat_cross, other_border);
				push_latlon_to_seg(cur_seg, e.lat, e.lon);
			} else {
				push_latlon_to_seg(cur_seg, e.lat, e.lon);
			}
		}
		if (!cur_seg.empty()) draw_segments.push_back(std::move(cur_seg));
	}

	// draw segments
	glUseProgram(marker_shader_);
	auto pan = camera2d.getPan();
	double zoom = camera2d.getZoom();
	if (marker_pan_ndc_loc_ != -1) glUniform2f(marker_pan_ndc_loc_, pan.x, pan.y);
	if (marker_zoom_loc_ != -1) glUniform1f(marker_zoom_loc_, static_cast<float>(zoom));
	glBindVertexArray(marker_vao_);
	glBindBuffer(GL_ARRAY_BUFFER, marker_vbo_);
	if (marker_color_loc_ != -1) {
		glUniform4f(marker_color_loc_, track_color.r, track_color.g, track_color.b, track_color.a);
	} else {
		const GLint color_loc = glGetUniformLocation(marker_shader_, "u_color");
		if (color_loc != -1) marker_color_loc_ = color_loc;
		glUniform4f(color_loc, track_color.r, track_color.g, track_color.b, track_color.a);
	}
	for (const auto& seg : draw_segments) {
		if (seg.empty()) continue;
		glBufferData(GL_ARRAY_BUFFER, seg.size() * sizeof(float), seg.data(), GL_DYNAMIC_DRAW);
		glDrawArrays(GL_LINE_STRIP, 0, static_cast<GLsizei>(seg.size() / 2));
	}

	// draw start marker
	for (const auto& seg : draw_segments) {
		if (!seg.empty()) { render_marker(seg[0], seg[1], 1.0f, 1.0f, 1.0f, 0.95f, 3.0f); break; }
	}

	glBindVertexArray(0);
}
