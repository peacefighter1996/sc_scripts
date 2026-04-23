import cv2
import numpy as np
import os


def extract_characters_rtl(image, char_w=7.5, char_h=12, threshold=200):
    gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)

    h, w = gray.shape

    y_start = (h - char_h) // 2  # center vertically (adjust if needed)

    chars = []

    # start from right side
    int_char_w = int(char_w)
    x = w - int_char_w -1
    

    while x >= 0:
        print(x)
        x_start = int(x)
        x_end = int((x + int_char_w))
        roi = gray[y_start:y_start+char_h, x_start-1:x_end+1]
        # roi_mask = mask[y_start:y_start+char_h, x_start-1:x_end+1]

        # skip empty blocks (no white pixels)
        chars.append(roi)
        

        x -= char_w
        

    return chars


def save_rtl_dataset(chars, ground_truth, output_dir="dataset"):
    os.makedirs(output_dir, exist_ok=True)

    counts = {}
    
    # for each character, check if dataset folder contains it and what the next index should be, then save the ROI with GT label as filename
    for gt_char in ground_truth:
        if gt_char == " ":
            gt_char = "space"
        
        if gt_char == ":": 
            gt_char = "colon"
        elif gt_char == ".":
            gt_char = "dot"
        elif gt_char ==  "-":
            gt_char = "dash"
        elif gt_char == ",":
            gt_char = "comma"
            
        if gt_char not in counts:
            label_dir = os.path.join(output_dir, gt_char)
            if os.path.exists(label_dir):
                counts[gt_char] = len(os.listdir(label_dir))
            else:
                counts[gt_char] = 0

    for img, gt_char in zip(chars, ground_truth):
        if gt_char == ":": 
            gt_char = "colon"
        elif gt_char == ".":
            gt_char = "dot"
        elif gt_char ==  "-":
            gt_char = "dash"
        elif gt_char == ",":
            gt_char = "comma"
        elif gt_char == " ":
            gt_char = "space" 
        
        
        print(f"GT char: '{gt_char}'")
        if img is None or len(gt_char) == 0:
            continue
        
        label_dir = os.path.join(output_dir, gt_char)
        os.makedirs(label_dir, exist_ok=True)

        count = counts.get(gt_char, 0)
        filename = os.path.join(label_dir, f"{count}.png")
        
        print(f"Saving ROI for '{gt_char}' to {label_dir} with index {count}")

        cv2.imwrite(filename, img)
        counts[gt_char] = count + 1


data_set = [
    ["images/test/iron1.jpg", ["       Zone: pyro5e Pos: -57.3461km -86.32m 463.2079km"]],
    ["images/test/rc1.jpg", ["         Zone: rockcrack_assembled_002 Pos: 245.61m 230.61m 1058.94m", "                    ShardId: pub_use1b_11518367_190"]],
    ["images/test/rc2.jpg", ["         Zone: rockcrack_assembled_002 Pos: 563.55m -45.74m 1032.75m","                    ShardId: pub_use1b_11518367_190"]],
    ["images/test/rc3.jpg", ["                           Zone: rockcrack_assembled_002 Pos: 593.61m -272.91m 1020.80m", "ShardId: pub_use1b_11518367_190"]],
    ["images/test/pyro4.jpg", ["                         Zone: SolarSystem_9732643902588 Pos: -3704446.6793km -43071658.6120km -92.0411km"]],
    ["images/test/rc4.jpg", ["                     Zone: Keeger_segment_rckcrk_080 Pos: 6865.44m -31.4662km 18.7257km"]],
    ["images/test/rc5.jpg", ["                         Zone: rockcrack_assembled_002 Pos: -41.34m -190.38m 938.69m", "                         ShardId: pub_euw1b_11592622_180"]],
    ["images/test/rc6.jpg", ["                      Zone: rockcrack_assembled_002 Pos: -147.38m 116.84m 1042.10m", "                                     ShardId: pub_euw1b_11592622_180"]]
]


# remove dataset folder if it exists to start fresh
if os.path.exists("dataset"):
    import shutil
    shutil.rmtree("dataset")

if __name__ == "__main__":

    for input_image_path, ground_truth in data_set:
        
        if len(ground_truth) >= 1 and ground_truth[0] is not None:
            
            input_img = cv2.imread(input_image_path)
            zone_1 = input_img[30:44, -1000:-4]  # crop to region of interest (adjust as needed)
            
            chars = extract_characters_rtl(zone_1,char_w=7.5 ,char_h=14)
            print(f"Extracted {len(chars)} character ROIs from image.")
            # for i, char in enumerate(chars):
            #     print(f"Character {i}: shape {char.shape}, mean pixel value {char.mean():.2f}")
            
            # reverse ground truth to match RTL character order
            gt = ground_truth[0][::-1]

            save_rtl_dataset(chars, gt)
        
        if len(ground_truth) >= 2 and ground_truth[1] is not None:
            input_img = cv2.imread(input_image_path)
            zone_2 = input_img[108:108+15, -1000:-4]  # crop to region of interest (adjust as needed)
            
            chars = extract_characters_rtl(zone_2,char_w=7.5, char_h=14)
            print(f"Extracted {len(chars)} character ROIs from image.")
            # for i, char in enumerate(chars):
            #     print(f"Character {i}: shape {char.shape}, mean pixel value {char.mean():.2f}")
            
            # reverse ground truth to match RTL character order
            gt = ground_truth[1][::-1]

            save_rtl_dataset(chars, gt)