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
        img = cv2.imread(path, cv2.IMREAD_COLOR)
        img = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
        # mask = cv2.threshold(img, 100, 255, cv2.THRESH_BINARY)[1]  # binarize
        # img = cv2.bitwise_and(img, img, mask=mask)  # apply mask to remove background
        if img is None:
            continue
        templates[char_name] = img
    return templates

# =========================
# WHITE FILTER
# =========================
def extract_text_mask(image):
    gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
    _, mask = cv2.threshold(gray,150, 255, cv2.THRESH_BINARY)
    # pad mask to avoid border issues
    mask = cv2.dilate(mask, np.ones((3,3), np.uint8), iterations=1)
    return mask

# =========================
# STRIDE SCAN (DENSE)
# =========================
def scan_image(input_img, templates, stride=1, offset=0):
    gray = cv2.cvtColor(input_img, cv2.COLOR_BGR2GRAY)
    cv2.imshow("Gray", gray)
    # mask = extract_text_mask(input_img)
    # gray = cv2.bitwise_and(gray, gray, mask=mask)
    # cv2.imshow("Masked", gray)

    h, w = gray.shape
    results = []

    for char, template in templates.items():
        th, tw = template.shape

        for y in range(offset, h - th, stride):
            for x in range(offset, w - tw, stride):
                # # skip non-white regions quickly
                # if mask[y:y+th, x:x+tw].mean() < 200:
                #     continue

                roi = gray[y:y+th, x:x+tw]

                res = cv2.matchTemplate(roi, template, cv2.TM_CCOEFF_NORMED)
                _, score, _, _ = cv2.minMaxLoc(res)

                if score > 0.5:  # pre-filter
                    results.append({
                        "x": x,
                        "y": y,
                        "w": tw,
                        "h": th,
                        "char": char,
                        "score": score
                    })
                    print(f"Found '{char}' at ({x},{y}) with score {score:.2f}")

    return results

# =========================
# NON-MAX SUPPRESSION (KEY PART)
# =========================
def nms(results, radius=7):
    results = sorted(results, key=lambda r: r["score"], reverse=True)
    kept = []
    
    print(f"Total detections before NMS: {len(results)}")
    

    for r in results:
        print(f"Checking '{r['char']}' at ({r['x']},{r['y']}) with score {r['score']:.2f}")
        keep = True

        for k in kept:
            if abs(r["x"] - k["x"]) < 6 and abs(r["y"] - k["y"]) < 10:
                keep = False
                break

        if keep:
            kept.append(r)
            
    print(f"Total detections after NMS: {len(kept)}")

    return kept

# =========================
# RECONSTRUCT TEXT GRID
# =========================
def reconstruct_text(results, y_threshold=10, x_gap_threshold=12):
    results = sorted(results, key=lambda r: (r["y"], r["x"]))

    lines = []
    current_line = []
    last_y = None

    for r in results:
        print(f"Processing '{r['char']}' at ({r['x']},{r['y']}) with score {r['score']:.2f}")
        if last_y is None:
            current_line.append(r)
            last_y = r["y"]
            continue

        if abs(r["y"] - last_y) > y_threshold:
            lines.append(current_line)
            current_line = [r]
        else:
            current_line.append(r)

        last_y = r["y"]

    if current_line:
        lines.append(current_line)

    # Build text with spacing reconstruction
    text_lines = []

    for line in lines:
        line = sorted(line, key=lambda r: r["x"])

        reconstructed = ""
        last_x = None

        for r in line:
            x = r["x"]

            if last_x is not None:
                gap = x - last_x

                # Insert spaces based on gap size
                if gap > x_gap_threshold:
                    spaces = int(gap / x_gap_threshold)
                    reconstructed += " " * max(1, spaces)

            reconstructed += r["char"]
            last_x = x

        text_lines.append(reconstructed)

    return "\n".join(text_lines)

# =========================
# ALIGN WITH GROUND TRUTH
# =========================
def align_with_ground_truth(detected_text, results, ground_truth):
    """
    Align detected characters with ground truth per word.
    Only keep samples where word lengths match.
    """
    results = sorted(results, key=lambda r: (r["y"], r["x"]))
    detected_words = detected_text.split(" ")
    gt_words = ground_truth.split(" ")

    aligned = []

    idx = 0  # index in results list
    matched = True
    if len(detected_words) > len(gt_words):
        detected_words = detected_words[-len(gt_words):]
        print("Warning: Detected more words than GT, truncating to last words.")
    elif len(detected_words) < len(gt_words):
        print("Warning: Detected fewer words than GT, skipping alignment.")
        return False, []
    
    
    for d_word, gt_word in zip(detected_words, gt_words):
        length = len(d_word)

        # extract corresponding detections
        word_results = results[idx:idx+length]

        if len(d_word) == len(gt_word) and len(word_results) == length:
            for i in range(length):
                aligned.append({
                    "box": word_results[i],
                    "pred_char": d_word[i],
                    "gt_char": gt_word[i]
                })
        else:
            matched = False
            

        idx += length

    return matched, aligned

# =========================
# SAVE DATASET WITH GT LABELS
# =========================
def save_aligned_dataset(image, aligned_data, output_dir="dataset", size=(10,8)):
    os.makedirs(output_dir, exist_ok=True)

    gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)

    counts = {}
    
    # for each character, check if dataset folder contains it and what the next index should be, then save the ROI with GT label as filename
    for item in aligned_data:
        gt_char = item["gt_char"]
        if gt_char not in counts:
            label_dir = os.path.join(output_dir, gt_char)
            if os.path.exists(label_dir):
                counts[gt_char] = len(os.listdir(label_dir))
            else:
                counts[gt_char] = 0

    for item in aligned_data:
        r = item["box"]
        gt_char = item["gt_char"]

        x, y = r["x"], r["y"]

        roi = gray[y-1:y+11, x-1:x+9]  # slightly larger than template size

        label_dir = os.path.join(output_dir, gt_char)
        os.makedirs(label_dir, exist_ok=True)

        count = counts.get(gt_char, 0)
        
        r = item["box"]
        pred_char = item["pred_char"]
        gt_char = item["gt_char"]
        print(f"GT: '{gt_char}' Pred: '{pred_char}' at ({r['x']},{r['y']}): ({x},{y})")
        print(f"Saving ROI for '{gt_char}' to {label_dir} with index {count}")
        
        filename = os.path.join(label_dir, f"{count}.png")

        cv2.imwrite(filename, roi)
        counts[gt_char] = count + 1

# =========================
# MAIN
# =========================

data_set = [
    ["images/test/iron1.jpg", "-57,3461km -86,32m 463,2079km"],
    ["images/test/rc1.jpg", "Zone: rockcrack_assembled_002 Pos: 245.61m 230.61m 1058.94m"]
]


# remove dataset folder if it exists to start fresh
if os.path.exists("dataset"):
    import shutil
    shutil.rmtree("dataset")

if __name__ == "__main__":
    template_folder = "images/characters"

    templates = load_templates(template_folder)

    for input_image_path, ground_truth in data_set:


        input_img = cv2.imread(input_image_path)
        input_img = input_img[31:44, -500:-5]  # crop to region of interest (adjust as needed)
        if input_img is None:
            raise ValueError("Could not load input image")

        # Dense scan
        raw_results = scan_image(input_img, templates, stride=1, offset=2)

        # Remove overlapping detections
        filtered_results = nms(raw_results, radius=5)

        # Reconstruct detected text
        detected_text = reconstruct_text(filtered_results)

        print("Detected Text: ", detected_text)
        print("Ground Truth: ", ground_truth)

        # Align
        matched, aligned = align_with_ground_truth(detected_text.replace("\n", " "), filtered_results, ground_truth)
        if not matched:
            print("Word length mismatch, skipping dataset save.")
            continue 

        # Save dataset
        save_aligned_dataset(input_img, aligned, output_dir="dataset", size=(8,10))

        print(f"Saved {len(aligned)} aligned samples.")

        # Debug draw
        debug = input_img.copy()
        cv2.destroyAllWindows()