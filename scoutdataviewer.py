# !pip install PyOpenGL glfw imgui Pillow numpy

import math
import os
import csv
from dataclasses import dataclass
from typing import List
import cv2
import numpy as np
import mss
import pygetwindow as gw
import json
import pytesseract
import concurrent.futures

import numpy as np
from PIL import Image

# OpenGL / windowing
import glfw
from OpenGL.GL import *

# ImGui
import imgui
from imgui.integrations.glfw import GlfwRenderer
import re

CSV_PATH = "data/geoscout.csv"
MAX_QUALITY = 1000.0
MIN_QUALITY = 0.0

# -----------------------------
# Data Models
# -----------------------------

@dataclass
class DataPoint:
    server: str
    x: float
    y: float
    z: float
    planet: str
    material: str
    location: bool
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
# CSV Handling (Global dataset)
# -----------------------------


def load_points_from_csv() -> List[DataPoint]:
    points = []

    if not os.path.exists(CSV_PATH):
        print("geoscout.csv not found, creating new one.")
        os.makedirs(os.path.dirname(CSV_PATH), exist_ok=True)
        with open(CSV_PATH, 'w', newline='') as f:
            writer = csv.writer(f)
            writer.writerow(["server", "x", "y", "z", "planet", "material", "quality_min", "quality_max", "note"])
        return points

    with open(CSV_PATH, 'r', newline='') as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                points.append(DataPoint(
                    row['server'],
                    float(row['x']),
                    float(row['y']),
                    float(row['z']),
                    row['planet'],
                    row['material'],
                    True if row['material'].lower() == 'location' else False,
                    float(row['quality_min']),
                    float(row['quality_max']),
                    row.get('note', "")
                ))
            except Exception as e:
                print("Skipping invalid row:", row, e)

    return points


def append_point_to_csv(point: DataPoint):
    file_exists = os.path.exists(CSV_PATH)

    with open(CSV_PATH, 'a', newline='') as f:
        writer = csv.writer(f)

        if not file_exists:
            writer.writerow(["server","x", "y", "z", "planet", "material", "quality_min", "quality_max", "note"])

        writer.writerow([
            point.server,
            point.x,
            point.y,
            point.z,
            point.planet,
            point.material,
            point.quality_min,
            point.quality_max,
            point.note
        ])

# -------- SETTINGS --------
CAPTURE_MODE = "window"   # "screen" or "window"
WINDOW_NAME = 'Star Citizen '   # change if using window mode
# --------------------------


print(gw.getAllTitles())
def get_window_bbox(name):
    windows = gw.getWindowsWithTitle(name)
    if not windows:
        print("Window not found")
        return None

    win = windows[0]
    return {
        "top": win.top,
        "left": win.left,
        "width": win.width,
        "height": win.height
    }



try:    
    with open("filter_values.json", "r") as f:
        base_values = json.load(f)
except FileNotFoundError:
    base_values = {
        "lower_white": [0, 0, 150],
        "upper_white": [255, 50, 255],
        "lower_red": [50, 150, 0],
        "upper_red": [120, 255, 255]
    }
    
def text_grap_xyz(frame=None, hsv=None):
    if frame is None:
        sct = mss.mss()
        if CAPTURE_MODE == "window":
            monitor = get_window_bbox(WINDOW_NAME)
        else:
            monitor = sct.monitors[2]  # full screen
        screenshot = sct.grab(monitor)
        frame = np.array(screenshot)
    
        frame = cv2.cvtColor(frame, cv2.COLOR_BGRA2BGR)
    
        hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)

    lower_white = np.array(base_values["lower_white"])
    upper_white = np.array(base_values["upper_white"])
    mask = cv2.inRange(hsv, lower_white, upper_white)
    
    view = cv2.bitwise_and(frame, frame, mask=mask)
    size = (view.shape[1], view.shape[0])
    # get right hand corner of the screen and draw a red rectangle there to show where the text is
    lefttop = (size[0]-400, 31)
    rightbottom = (size[0], 44)
    
    # cut this portion of the screen and show it in a separate window to make it easier to read the text
    text_region = view[lefttop[1]:rightbottom[1], lefttop[0]:rightbottom[0]]
    # cv2.imshow("Text Region", text_region)
    
    # perform OCR on the text region using pytesseract to read the text and print it to the console
    ocr_result = pytesseract.image_to_string(text_region, config='--psm 7')
    print("OCR Result:", ocr_result.strip())
    # cv2.rectangle(view, lefttop, rightbottom, (0, 0, 255), 2)
    
    ocr_result = ocr_result.replace("\n", " ").strip()
    data = ocr_result.split(" ")
    # get last 3 strings for x, y, z
    if len(data) >= 3:
        coordinates = data[-3:]
        try:
            for i in range(3):
            
                coordinates[i] = coordinates[i].replace(",", ".")
                # remove . if last character and replace l with 1, O with 0, I with 1 to fix common OCR mistakes
                if coordinates[i].endswith("."):
                    coordinates[i] = coordinates[i][:-1]
                coordinates[i] = coordinates[i].replace("l", "1").replace("O", "0").replace("I", "1")
                if "k" in coordinates[i]:
                    coordinates[i] = coordinates[i].replace("k", "")
                    coordinates[i] = coordinates[i].replace("m", "")
                    coordinates[i] = float(coordinates[i]) 
                else:
                    coordinates[i] = coordinates[i].replace("m", "")
                    coordinates[i] = float(coordinates[i]) / 1000
        except ValueError:
            print("Could not convert coordinates to float:", coordinates)
            return None, None, None
        x, y, z = coordinates
        print(f"X: {x}, Y: {y}, Z: {z}")
        return x, y, z
    else:
        print("Could not parse coordinates from OCR result")
        return None, None, None

def text_capture_rock_type(frame=None, hsv=None):
    if frame is None:
        sct = mss.mss()
        if CAPTURE_MODE == "window":
            monitor = get_window_bbox(WINDOW_NAME)
        else:
            monitor = sct.monitors[2]  # full screen
        screenshot = sct.grab(monitor)
        frame = np.array(screenshot)
    
        frame = cv2.cvtColor(frame, cv2.COLOR_BGRA2BGR)
    
        hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)

    kernel = np.ones((3,3), np.uint8)
    
    lower_red = np.array(base_values["lower_red"])
    upper_red = np.array(base_values["upper_red"])
    mask_red = cv2.inRange(hsv, lower_red, upper_red)
    
    
    mask_red = cv2.dilate(mask_red, kernel, iterations=1)
    view = cv2.bitwise_and(frame, frame, mask=mask_red)
    size = (view.shape[1], view.shape[0])
    # square the rectangle in the middle of the screen to show where the text is being read from
    lefttop2 = (size[0]//2 - 100, size[1]//2 - 20)
    rightbottom2 = (size[0]//2 + 100, size[1]//2 + 20)
    
    # cv2.rectangle(view, lefttop1, rightbottom1, (0, 0, 255), 2)
    text_region = view[lefttop2[1]:rightbottom2[1], lefttop2[0]:rightbottom2[0]]
    # cv2.rectangle(view, lefttop2, rightbottom2, (0, 255, 0), 2)
    
    # cv2.imshow("Text Region", text_region)
    
    ocr_result = pytesseract.image_to_string(text_region, config='--psm 7')
    print("OCR Result:", ocr_result.strip())
    
    # try to match the OCR result to a known rock type and return it
    rock_types = ["Hephaestanite", "Gold", "Janalite", "Aphorite", "Dolivine", "Aslarite", "Beryl", "Iron", "Taranite", "Laranite", "Stileron", "Copper", "Borase", "Tin", "Riccite"]
    for rock in rock_types:
        if rock.lower() in ocr_result.lower():
            print(f"Detected rock type: {rock}")
            return rock
        
    return None
        

# -----------------------------
# Application State
# -----------------------------

MATERIAL_IDS = {
            "Hephaestanite": "HEPH",
            "Iron": "IRON",
            "Gold": "GOLD",
            "Janalite": "JANA",
            "Aphorite": "APHO",
            "Dolivine": "DOLI",
            "Aslarite": "ASLAR",
            "Beryl": "BERY",
            "Taranite": "TARA",
            "Laranite": "LARA",
            "Stileron": "SILI",
            "Copper": "COPP"
        }

class AppState:
    def __init__(self):
        self.points: List[DataPoint] = []
        self.filtered_points: List[DataPoint] = []

        self.serverIds = ["eu10", "eu180", "All"]
        self.planets = ["Pyro_E5_Fuego", "Pyro_Pyro4", "Pyro_A5_Ignis"]
        self.materials = ["Hephaestanite", "Gold", "Janalite", "Aphorite", "Dolivine", "Aslarite", "Beryl", "Iron", "Taranite", "Laranite", "Stileron", "Copper", "All", "Borase", "Tin", "Riccite"]
        
        self.serverIds.sort()
        self.materials.sort()
        self.planets.sort()

        self.selected_planet = self.planets[0]
        self.selected_material = self.materials[0]
        self.selected_server = self.serverIds[0]
        self.last_detected_rock = None

        self.quality_min = 0.0
        self.quality_max = MAX_QUALITY

        self.new_data = DataPoint(self.selected_server, 0, 0, 0, self.selected_planet, self.selected_material,False, 0, MAX_QUALITY)
        
        self.ocr_executor = concurrent.futures.ThreadPoolExecutor(max_workers=1)
        self.ocr_future = None

        self.texture_cache = {}

    def filter_points(self):
        self.filtered_points = [
            p for p in self.points
            if (p.material == self.selected_material or self.selected_material == "All"
            or p.location == True) and p.planet == self.selected_planet and (p.server == self.selected_server or self.selected_server == "All" or p.location== True)
        ]

    def reload_planet_data(self):
        self.points = load_points_from_csv()
        
    def ocr_task(self):
        sct = mss.mss()
        if CAPTURE_MODE == "window":
            monitor = get_window_bbox(WINDOW_NAME)
        else:
            monitor = sct.monitors[2]  # full screen
        screenshot = sct.grab(monitor)
        frame = np.array(screenshot)
    
        frame = cv2.cvtColor(frame, cv2.COLOR_BGRA2BGR)
    
        hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
        try:
            rock = text_capture_rock_type(frame, hsv)
            x, y, z = text_grap_xyz(frame, hsv)
            return rock, x, y, z
        except Exception as e:
            print("OCR task exception:", e)
            return None, None, None, None

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

def draw_map(texture, points: List[DataPoint], mouse_pos=None):
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
        
        r, g, b = 1.0, 1.0, 1.0  # White for locations
        a = 0.85
        if p.location == False:
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

            a = 1
        

        x = u * 2 - 1
        y = v * 2 - 1
        # print(f"Drawing point at lat={lat:.2f}, lon={lon:.2f} → x={x:.2f}, y={y:.2f}, quality={p.quality_max:.1f} → color=({r:.2f}, {g:.2f}, {b:.2f}, {a:.2f})")
        glColor4f(r, g, b, a)
        glVertex2f(x, y)
    glEnd()

    hovered_text = None

    # Hover detection
    if mouse_pos:
        mx, my = mouse_pos
        closest_dist = 0.02  # threshold in NDC

        for p in points:
            lat, lon, _ = p.to_lat_lon_alt()
            u, v = latlon_to_uv(lat, lon)
            px = u * 2 - 1
            py = v * 2 - 1

            dist = math.sqrt((mx - px)**2 + (my - py)**2)
            if dist < closest_dist:
                material_id = MATERIAL_IDS.get(p.material, p.material[:4].upper())
                # print(f"Hovering over point: {material_id} at lat={lat:.2f}, lon={lon:.2f}, quality={p.quality_max:.1f}, Note: {p.note}")
                hovered_text = p.note if p.location else f"{material_id} Quality: {p.quality_max:.2f}\n{p.note}"
                break

    # Re-enable texture for next frame
    # glEnable(GL_TEXTURE_2D)

    return hovered_text
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
        


            

        # background task to run both OCR functions
        

        # submit a job if none running; otherwise, check for completion and consume result
        if state.ocr_future is None:
            state.ocr_future = state.ocr_executor.submit(state.ocr_task)
        elif state.ocr_future.done():
            try:
                last_detected_rock, x, y, z = state.ocr_future.result()
                if x is not None and y is not None and z is not None:
                    state.new_data.x = x
                    state.new_data.y = y
                    state.new_data.z = z
                if last_detected_rock:
                    state.last_detected_rock = last_detected_rock
            except Exception as e:
                print("Error getting OCR result:", e)
            finally:
                state.ocr_future = None

        imgui.new_frame()

        # ---------------- UI ----------------
        imgui.begin("Controls")
        
        current_server_index = state.serverIds.index(state.selected_server)
        changed, new_server_index = imgui.combo("Server", current_server_index, state.serverIds)
        if changed:
            state.selected_server = state.serverIds[new_server_index]
        
        
        # Planet selector
        current_planet_index = state.planets.index(state.selected_planet)
        changed, new_planet_index = imgui.combo("Planet", current_planet_index, state.planets)
        if changed:
            state.selected_planet = state.planets[new_planet_index]
            

        # Material selector
        current_material_index = state.materials.index(state.selected_material)
        changed, new_material_index = imgui.combo("Material", current_material_index, state.materials)
        if changed:
            state.selected_material = state.materials[new_material_index]

        # _, state.quality_min = imgui.input_int("Quality Min", state.quality_min, MIN_QUALITY, MAX_QUALITY)
        # _, state.quality_max = imgui.input_int("Quality Max", state.quality_max, MIN_QUALITY, MAX_QUALITY)

        imgui.separator()
        imgui.text("Add new point:")
        
        if state.last_detected_rock:
            imgui.text(f"Last detected rock type: {state.last_detected_rock}")
            if imgui.button("Use Detected Rock Type"):
                state.selected_material = state.last_detected_rock
                state.new_data.material = state.last_detected_rock

        _, state.new_data.x = imgui.input_float("X", state.new_data.x)
        _, state.new_data.y = imgui.input_float("Y", state.new_data.y)
        _, state.new_data.z = imgui.input_float("Z", state.new_data.z)
        _, state.new_data.quality_min = imgui.input_int("Quality Min", state.new_data.quality_min, MIN_QUALITY, MAX_QUALITY)
        _, state.new_data.quality_max = imgui.input_int("Quality Max", state.new_data.quality_max, MIN_QUALITY, MAX_QUALITY)

        if imgui.button("Add Point"):
            new_point = DataPoint(
                state.selected_server,
                state.new_data.x,
                state.new_data.y,
                state.new_data.z,
                state.selected_planet,
                state.selected_material,
                False,
                state.new_data.quality_min,
                state.new_data.quality_max,
                note=""
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

        # Convert mouse position to NDC
        width, height = glfw.get_window_size(window)
        mx, my = glfw.get_cursor_pos(window)
        if width > 0 and height > 0:
            mx = (mx / width) * 2 - 1
            my = -((my / height) * 2 - 1)

        hovered_text = None

        if texture:
            hovered_text = draw_map(texture, state.filtered_points, (mx, my))

        if hovered_text:
            mouse_x, mouse_y = imgui.get_mouse_pos()
            
            if mouse_x + 200 > width:
                mouse_x = mouse_x - 130
            if mouse_y + 50 > height:
                mouse_y = mouse_y - 60
            
            imgui.set_next_window_position(mouse_x + 10, mouse_y + 10)
            imgui.set_next_window_bg_alpha(0.7)

            imgui.begin("##hover_tooltip",
                        flags=imgui.WINDOW_NO_TITLE_BAR |
                              imgui.WINDOW_NO_RESIZE |
                              imgui.WINDOW_NO_MOVE |
                              imgui.WINDOW_ALWAYS_AUTO_RESIZE)
            imgui.text(hovered_text)
            imgui.end()
            
        glDisable(GL_BLEND)
        
        imgui.render()
        impl.render(imgui.get_draw_data())

        glfw.swap_buffers(window)

    impl.shutdown()
    glfw.terminate()


if __name__ == "__main__":
    main()
