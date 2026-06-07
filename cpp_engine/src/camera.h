#pragma once

#include <utility>
#include <array>

struct Camera2D {
    // Pan is in NDC coords [-1,1]
    std::pair<float,float> pan{0.0f, 0.0f};
    double zoom{1.0};

    Camera2D() = default;
    explicit Camera2D(double z, std::pair<float,float> p) : pan(p), zoom(z) {}

    double getZoom() const { return zoom; }
    void setZoom(double z) { zoom = z; if (zoom < 1.0) zoom = 1.0; if (zoom > 8.0) zoom = 8.0; }
    void zoomBy(double factor) { zoom *= factor; if (zoom < 1.0) zoom = 1.0; if (zoom > 8.0) zoom = 8.0; }
    void increaseZoomBy(float value) { zoom += value; if (zoom < 1.0) zoom = 1.0; if (zoom > 8.0) zoom = 8.0; }

    std::pair<float,float> getPan() const { return pan; }
    void setPan(std::pair<float,float> p) { pan = p; }
    void panBy(float dx, float dy) { pan.first += dx; pan.second += dy; }

    // Apply pan/zoom to a point given in NDC coordinates [-1,1]
    std::pair<float,float> applyToNdc(float x, float y) const {
        float tx = (x - pan.first) * static_cast<float>(zoom) + pan.first;
        float ty = (y - pan.second) * static_cast<float>(zoom) + pan.second;
        return { tx, ty };
    }

    // Convert ndc pan into uv pan (0..1) for texture sampling
    std::pair<float,float> uvPan() const {
        // UV v is flipped relative to NDC, so invert Y when converting
        return { (pan.first + 1.0f) * 0.5f, (1.0f - pan.second) * 0.5f };
    }
};

// Minimal Camera3D stub to be expanded later
struct Camera3D {
    std::array<float,3> pos{0.0f, 0.0f, 5.0f};
    std::array<float,3> target{0.0f, 0.0f, 0.0f};
    float fov_deg{60.0f};
    float near_z{0.1f};
    float far_z{1000.0f};

    Camera3D() = default;
};
