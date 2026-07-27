#pragma once

#include <utility>
#include <array>

struct Camera2D {
    // Pan is in NDC coords [-1,1]
    Vector2 pan{0.0, 0.0};
    double zoom{1.0};

    Camera2D() = default;
    explicit Camera2D(double z, Vector2 p) : pan(p), zoom(z) {}

    double getZoom() const { return zoom; }
    void setZoom(double z) { zoom = z; if (zoom < 1.0) zoom = 1.0; if (zoom > 8.0) zoom = 8.0; }
    void zoomBy(double factor) { zoom *= factor; if (zoom < 1.0) zoom = 1.0; if (zoom > 8.0) zoom = 8.0; }
    void increaseZoomBy(float value) { zoom += value; if (zoom < 1.0) zoom = 1.0; if (zoom > 8.0) zoom = 8.0; }

    Vector2 getPan() const { return pan; }
    void setPan(Vector2 p) { pan = p; }
    void panBy(float dx, float dy) { pan.x += dx; pan.y += dy; }

    // Apply pan/zoom to a point given in NDC coordinates [-1,1]
    Vector2 applyToNdc(float x, float y) const {
        float tx = (x - pan.x) * static_cast<float>(zoom) + pan.x;
        float ty = (y - pan.y) * static_cast<float>(zoom) + pan.y;
        return { tx, ty };
    }

    // Convert ndc pan into uv pan (0..1) for texture sampling
    Vector2 uvPan() const {
        // UV v is flipped relative to NDC, so invert Y when converting
        return { (pan.x + 1.0f) * 0.5f, (1.0f - pan.y) * 0.5f };
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
