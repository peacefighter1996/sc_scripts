# !pip install PyOpenGL glfw imgui Pillow numpy

import math
import os
import csv
from dataclasses import dataclass
from typing import List

import numpy as np
from PIL import Image

# OpenGL / windowing
import glfw
from OpenGL.GL import *

# ImGui
import imgui
from imgui.integrations.glfw import GlfwRenderer

CSV_PATH = "data/geoscout.csv"
MAX_QUALITY = 1000.0
MIN_QUALITY = 0.0

# -----------------------------
# Data Models
# -----------------------------

@dataclass
class DataPoint:
    x: float
    y: float
    z: float
    planet: str
    material: str
    quality_min: float
    quality_max: float
    note: str = ""

    def to_lat_lon_alt(self):
        r = math.sqrt(self.x**2 + self.y**2 + self.z**2)
        lat = math.degrees(math.asin(self.z / r))
        lon = math.degrees(math.atan2(self.y, self.x))
        alt = r
        return lat, lon, alt

# -----------------------------
# CSV Handling
# -----------------------------


def load_points_from_csv() -> List[DataPoint]:
    points = []

    if not os.path.exists(CSV_PATH):
        print("geoscout.csv not found, creating new one.")
        os.makedirs(os.path.dirname(CSV_PATH), exist_ok=True)
        with open(CSV_PATH, 'w', newline='') as f:
            writer = csv.writer(f)
            writer.writerow(["x", "y", "z", "planet", "material", "quality_min", "quality_max"])
        return points

    with open(CSV_PATH, 'r', newline='') as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                points.append(DataPoint(
                    float(row['x']),
                    float(row['y']),
                    float(row['z']),
                    row['planet'],
                    row['material'],
                    float(row['quality_min']),
                    float(row['quality_max'])
                ))
            except Exception as e:
                print("Skipping invalid row:", row, e)

    return points


def append_point_to_csv(point: DataPoint):
    file_exists = os.path.exists(CSV_PATH)

    with open(CSV_PATH, 'a', newline='') as f:
        writer = csv.writer(f)

        if not file_exists:
            writer.writerow(["x", "y", "z", "planet", "material", "quality_min", "quality_max"])

        writer.writerow([
            point.x,
            point.y,
            point.z,
            point.planet,
            point.material,
            point.quality_min,
            point.quality_max   
        ])

# -----------------------------
# Application State
# -----------------------------

class AppState:
    def __init__(self):
        self.points: List[DataPoint] = []
        self.filtered_points: List[DataPoint] = []

        self.planets = ["Pyro_E5_Fuego", "None"]
        self.materials = ["Hephaestanite", "Iron", "Gold", "None"]

        self.selected_planet = self.planets[0]
        self.selected_material = self.materials[0]

        self.quality_min = 0.0
        self.quality_max = MAX_QUALITY

        self.new_data = DataPoint(0, 0, 0, self.selected_planet, self.selected_material, 0, 1000)

        self.texture_cache = {}

    def filter_points(self):
        self.filtered_points = [
            p for p in self.points
            if p.planet == self.selected_planet
            and p.material == self.selected_material
            and self.quality_min <= p.quality_min
            and p.quality_max <= self.quality_max
        ]

    def reload_planet_data(self):
        self.points = load_points_from_csv()

# -----------------------------
# Texture Loading
# -----------------------------

def load_texture(path):
    if not os.path.exists(path):
        print(f"Texture not found: {path}")
        return None

    img = Image.open(path).convert("RGB")
    img_data = np.array(img)

    texture = glGenTextures(1)
    glBindTexture(GL_TEXTURE_2D, texture)

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, img.width, img.height, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, img_data)

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR)

    return texture

# -----------------------------
# Coordinate Mapping
# -----------------------------

def latlon_to_uv(lat, lon):
    u = (lon + 180.0) / 360.0
    v = (lat + 90.0) / 180.0
    return u, v

# -----------------------------
# Rendering
# -----------------------------

def draw_map(texture, points: List[DataPoint]):
    glEnable(GL_TEXTURE_2D)
    glBindTexture(GL_TEXTURE_2D, texture)

    # IMPORTANT: ensure texture is not tinted
    glColor3f(1.0, 1.0, 1.0)

    glBegin(GL_QUADS)
    glTexCoord2f(0, 0); glVertex2f(-1, -1)
    glTexCoord2f(1, 0); glVertex2f(1, -1)
    glTexCoord2f(1, 1); glVertex2f(1, 1)
    glTexCoord2f(0, 1); glVertex2f(-1, 1)
    glEnd()

    # Draw points (disable texture so color is not affected)
    glDisable(GL_TEXTURE_2D)
    glPointSize(5)
    glBegin(GL_POINTS)
    for p in points:
        lat, lon, _ = p.to_lat_lon_alt()
        u, v = latlon_to_uv(lat, lon)
        
        # Map quality to color (red = high, green = medium, blue = low, and alpha = 50%)
        quality_norm = (p.quality_max - MIN_QUALITY) / (MAX_QUALITY - MIN_QUALITY)
        quality_norm = max(0.0, min(1.0, quality_norm))  # clamp
        if quality_norm < 0.5:
            # Blue → Green
            t = quality_norm * 2
            r = 0.0
            g = t
            b = 1.0 - t
        else:
            # Green → Red
            t = (quality_norm - 0.5) * 2
            r = t
            g = 1.0 - t
            b = 0.0

        a = 0.50
        

        x = u * 2 - 1
        y = v * 2 - 1
        print(f"Drawing point at lat={lat:.2f}, lon={lon:.2f}, quality={p.quality_max:.1f} → color=({r:.2f}, {g:.2f}, {b:.2f}, {a:.2f})")
        glColor4f(r, g, b, a)
        glVertex2f(x, y)
    glEnd()

# -----------------------------
# Main Application
# -----------------------------

def main():
    if not glfw.init():
        return

    window = glfw.create_window(1280, 720, "Planet Visualizer", None, None)
    glfw.make_context_current(window)

    imgui.create_context()
    impl = GlfwRenderer(window)

    state = AppState()
    state.reload_planet_data()

    while not glfw.window_should_close(window):
        glfw.poll_events()
        impl.process_inputs()

        imgui.new_frame()

        # ---------------- UI ----------------
        imgui.begin("Controls")

        current_planet_index = state.planets.index(state.selected_planet)
        changed, new_planet_index = imgui.combo(
            "Planet", current_planet_index, state.planets
        )

        if changed:
            state.selected_planet = state.planets[new_planet_index]

        current_material_index = state.materials.index(state.selected_material)
        changed, new_material_index = imgui.combo(
            "Material", current_material_index, state.materials
        )

        if changed:
            state.selected_material = state.materials[new_material_index]

        _, state.quality_min = imgui.slider_float("Quality Min", state.quality_min, 0, 1000)
        _, state.quality_max = imgui.slider_float("Quality Max", state.quality_max, 0, 1000)

        imgui.separator()
        imgui.text("Add new point:")

        _, state.new_data.x = imgui.input_float("X", state.new_data.x)
        _, state.new_data.y = imgui.input_float("Y", state.new_data.y)
        _, state.new_data.z = imgui.input_float("Z", state.new_data.z)
        _, state.new_data.quality_min = imgui.slider_float("np Quality Min", state.new_data.quality_min, 0, 1000)
        _, state.new_data.quality_max = imgui.slider_float("np Quality Max", state.new_data.quality_max, 0, 1000)

        if imgui.button("Add Point"):
            new_point = DataPoint(
                state.new_data.x,
                state.new_data.y,
                state.new_data.z,
                state.selected_planet,
                state.selected_material,
                state.quality_min,
                state.quality_max
            )
            
            state.points.append(new_point)
            append_point_to_csv(new_point)

        imgui.end()

        # ---------------- Render ----------------
        glClear(GL_COLOR_BUFFER_BIT)
        
        glEnable(GL_BLEND)
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
        
        state.filter_points()
        
        

        texture_path = f"images/planets/{state.selected_planet}.jpg"
        if state.selected_planet not in state.texture_cache:
            state.texture_cache[state.selected_planet] = load_texture(texture_path)

        texture = state.texture_cache[state.selected_planet]

        if texture:
            draw_map(texture, state.filtered_points)
            
        glDisable(GL_BLEND)
        
        imgui.render()
        impl.render(imgui.get_draw_data())

        glfw.swap_buffers(window)

    impl.shutdown()
    glfw.terminate()


if __name__ == "__main__":
    main()
