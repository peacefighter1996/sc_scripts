import cv2
import numpy as np
import os

# =========================
# TEMPLATE LOADING
# =========================
def load_templates(folder_path):
    templates = {}
    for file in os.listdir(folder_path):
        path = os.path.join(folder_path, file)
        if not os.path.isfile(path):
            continue
        char_name = os.path.splitext(file)[0]
        img = cv2.imread(path, cv2.IMREAD_GRAYSCALE)
        if img is None:
            continue
        templates[char_name] = img
    return templates

# =========================
# STEP 1: WHITE TEXT MASK
# =========================
def extract_text_mask(image):
    gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)

    # Threshold for white text
    _, mask = cv2.threshold(gray, 200, 255, cv2.THRESH_BINARY)

    # Clean up noise
    # kernel = np.ones((2, 2), np.uint8)
    # mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel)

    return mask

# =========================
# STEP 2: FIND CHARACTER REGIONS
# =========================
def find_character_regions(mask):
    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    print(f"Found {len(contours)} contours")

    boxes = []
    for cnt in contours:
        x, y, w, h = cv2.boundingRect(cnt)

        # Filter noise but keep small chars like ',' and '-'
        # if w <= 2 or h <= 2:
        #     continue

        boxes.append((x, y, w, h))
    print (f"Filtered to {len(boxes)} character regions")

    return boxes

# =========================
# STEP 3: CLASSIFY REGIONS
# =========================
def classify_regions(image, boxes, templates):
    gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)

    results = []

    for (x, y, w, h) in boxes:
        roi = gray[1:13, x-2:x+7]

        best_char = None
        best_score = -1

        for char, template in templates.items():
            try:
                resized = cv2.resize(roi, (template.shape[1], template.shape[0]))
            except:
                continue

            res = cv2.matchTemplate(resized, template, cv2.TM_CCOEFF_NORMED)
            _, score, _, _ = cv2.minMaxLoc(res)

            if score > best_score:
                best_score = score
                best_char = char

        results.append({
            "box": (x, y, w, h),
            "char": best_char,
            "score": best_score
        })

    return results

# =========================
# STEP 4: RECONSTRUCT TEXT
# =========================
def reconstruct_text(results, y_threshold=10, score_threshold=0.5):
    # Filter low confidence
    results = [r for r in results if r["score"] >= score_threshold and r["char"] is not None]

    # Sort top-to-bottom, left-to-right
    results = sorted(results, key=lambda r: (r["box"][1], r["box"][0]))

    lines = []
    current_line = []
    last_y = None

    for r in results:
        x, y, w, h = r["box"]

        if last_y is None:
            current_line.append(r)
            last_y = y
            continue

        if abs(y - last_y) > y_threshold:
            # New line
            lines.append(current_line)
            current_line = [r]
        else:
            current_line.append(r)

        last_y = y

    if current_line:
        lines.append(current_line)

    # Build text
    text_lines = []
    for line in lines:
        line = sorted(line, key=lambda r: r["box"][0])
        text_lines.append("".join(r["char"] for r in line))

    return "\n".join(text_lines)

# =========================
# OPTIONAL: DRAW RESULTS
# =========================
def draw_results(image, results, threshold=0.5):
    output = image.copy()

    for r in results:
        x, y, w, h = r["box"]
        char = r["char"]
        score = r["score"]

        if char is None or score < threshold:
            continue

        cv2.rectangle(output, (x, y), (x+w, y+h), (0, 255, 0), 1)
        cv2.putText(output, char, (x, y-2), cv2.FONT_HERSHEY_SIMPLEX,
                    0.4, (0, 255, 0), 1, cv2.LINE_AA)

    return output

# Build reconstructed text grid
def build_text_grid(results, threshold=0.5):
    # Determine grid size
    max_row = max(r["grid_pos"][0] for r in results)
    max_col = max(r["grid_pos"][1] for r in results)

    grid = [[" " for _ in range(max_col + 1)] for _ in range(max_row + 1)]

    for r in results:
        row, col = r["grid_pos"]
        char = r["char"]
        score = r["score"]

        if char is not None and score >= threshold:
            grid[row][col] = char
        else:
            grid[row][col] = " "  # placeholder for low confidence

    return grid


# Convert grid to printable string
def grid_to_string(grid):
    return "\n".join("".join(row) for row in grid)

# =========================
# MAIN
# =========================
if __name__ == "__main__":
    template_folder = "images/characters"
    input_image_path = "images/test/iron1.jpg"

    templates = load_templates(template_folder)

    input_img = cv2.imread(input_image_path)
    input_img = input_img[30:44, -230:]  # crop to region of interest (adjust as needed)
    if input_img is None:
        raise ValueError("Could not load input image")

    # Pipeline
    mask = extract_text_mask(input_img)
    boxes = find_character_regions(mask)
    results = classify_regions(input_img, boxes, templates)

    text_output = reconstruct_text(results, y_threshold=10, score_threshold=0.6)

    print("\nReconstructed Text:\n")
    print(text_output)

    # Visual debug
    output_img = draw_results(input_img, results, threshold=0.6)

    cv2.imshow("Mask", mask)
    cv2.imshow("Result", output_img)
    cv2.waitKey(0)
    cv2.destroyAllWindows()





# # Optional: draw results on image
# def draw_results(image, results, threshold=0.5):
#     output = image.copy()

#     for r in results:
#         x, y = r["position"]
#         char = r["char"]
#         score = r["score"]

#         if char is None or score < threshold:
#             continue

#         cv2.putText(output, char, (x, y), cv2.FONT_HERSHEY_SIMPLEX,
#                     0.4, (0, 255, 0), 1, cv2.LINE_AA)

#     return output


# if __name__ == "__main__":
#     template_folder = "images/characters"
#     input_image_path = "images/test/iron1.jpg"

#     templates = load_templates(template_folder)

#     input_img = cv2.imread(input_image_path)
#     input_img = input_img[30:44, -230:]  # crop to region of interest (adjust as needed)
#     if input_img is None:
#         raise ValueError("Could not load input image")

#     results = scan_image(input_img, templates,
#                          stride_x=1,
#                          stride_y=12,
#                          offset_x=0,
#                          offset_y=2)

#     # Build and print reconstructed grid
#     grid = build_text_grid(results, threshold=0.6)
#     text_output = grid_to_string(grid)

#     print("\nReconstructed Text Grid:\n")
#     print(text_output)

#     # Visual output
#     output_img = draw_results(input_img, results, threshold=0.6)

#     cv2.imshow("Result", output_img)
#     cv2.waitKey(0)
#     cv2.destroyAllWindows()
