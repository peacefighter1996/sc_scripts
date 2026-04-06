#%%
# !pip install opencv-python mss pygetwindow pytesseract


#%%
import cv2
import numpy as np
import mss
import pygetwindow as gw
import json
import pytesseract

# -------- SETTINGS --------
capture_mode = "window"   # "screen" or "window"
window_name = 'Star Citizen '   # change if using window mode
# --------------------------

sct = mss.mss()
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

if capture_mode == "window":
    monitor = get_window_bbox(window_name)
else:
    monitor = sct.monitors[2]  # full screen

print("Press Q to exit")


show_ocr_filter = False
show_red_textFilter =True

# load filter values from json file if exists
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

first_pass = True


while True:
    screenshot = sct.grab(monitor)

    frame = np.array(screenshot)
    frame = cv2.cvtColor(frame, cv2.COLOR_BGRA2BGR)
    
    hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
    
    # filter only white text 
    lower_white = np.array(base_values["lower_white"])
    upper_white = np.array(base_values["upper_white"])
    mask = cv2.inRange(hsv, lower_white, upper_white)
    
    #filter red text 
    lower_red = np.array(base_values["lower_red"])
    upper_red = np.array(base_values["upper_red"])
    mask_red = cv2.inRange(hsv, lower_red, upper_red)
    
    
    if cv2.waitKey(1) & 0xFF == ord('o'):
        show_ocr_filter = True
        show_red_textFilter = False
    if cv2.waitKey(1) & 0xFF == ord('f'):
        show_ocr_filter = False
        show_red_textFilter = False
    if cv2.waitKey(1) & 0xFF == ord('b'):
        show_ocr_filter = False
        show_red_textFilter = True
    if cv2.waitKey(1) & 0xFF == ord('s'):
        # save values to json file 
        with open("filter_values.json", "w") as f:
            json.dump(base_values, f)
            
            
    # spread the mask by dilating it to make text more visible
    # kernel = np.ones((3,3), np.uint8)
    # mask = cv2.dilate(mask, kernel, iterations=1)
    # mask_red = cv2.dilate(mask_red, kernel, iterations=1)q
    
    # 
    
        
    if show_red_textFilter:
        kernel = np.ones((3,3), np.uint8)
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
    elif show_ocr_filter:
        view = cv2.bitwise_and(frame, frame, mask=mask)
        size = (view.shape[1], view.shape[0])
        # get right hand corner of the screen and draw a red rectangle there to show where the text is
        lefttop1 = (size[0]-400, 31)
        rightbottom1 = (size[0], 43)
        
        # cut this portion of the screen and show it in a separate window to make it easier to read the text
        text_region = view[lefttop1[1]:rightbottom1[1], lefttop1[0]:rightbottom1[0]]
        cv2.imshow("Text Region", text_region)
        
        # perform OCR on the text region using pytesseract to read the text and print it to the console
        ocr_result = pytesseract.image_to_string(text_region, config='--psm 7')
        print("OCR Result:", ocr_result.strip())
        
        # square the rectangle in the middle of the screen to show where the text is being read from
        lefttop2 = (size[0]//2 - 100, size[1]//2 - 100)
        rightbottom2 = (size[0]//2 + 100, size[1]//2 + 100)
        
        cv2.rectangle(view, lefttop1, rightbottom1, (0, 0, 255), 2)
        cv2.rectangle(view, lefttop2, rightbottom2, (0, 255, 0), 2)
        
        
        
    else:
        view = frame
    # show screen with sliders to adjust filter valuqes
    cv2.imshow("Screen Capture", view)
    
    if show_red_textFilter and first_pass:
        first_pass = False
        cv2.createTrackbar("Lower White - H", "Screen Capture", base_values["lower_white"][0], 255, lambda x: base_values["lower_white"].__setitem__(0, x) )
        cv2.createTrackbar("Lower White - S", "Screen Capture", base_values["lower_white"][1], 255, lambda x: base_values["lower_white"].__setitem__(1, x) )
        cv2.createTrackbar("Lower White - V", "Screen Capture", base_values["lower_white"][2], 255, lambda x: base_values["lower_white"].__setitem__(2, x) )
        cv2.createTrackbar("Upper White - H", "Screen Capture", base_values["upper_white"][0], 255, lambda x: base_values["upper_white"].__setitem__(0, x) )
        cv2.createTrackbar("Upper White - S", "Screen Capture", base_values["upper_white"][1], 255, lambda x: base_values["upper_white"].__setitem__(1, x) )
        cv2.createTrackbar("Upper White - V", "Screen Capture", base_values["upper_white"][2], 255, lambda x: base_values["upper_white"].__setitem__(2, x) )
        # cv2.createTrackbar("Lower red - H", "Screen Capture", base_values["lower_red"][0], 255, lambda x: base_values["lower_red"].__setitem__(0, x))
        # cv2.createTrackbar("Lower red - S", "Screen Capture", base_values["lower_red"][1], 255, lambda x: 
        #     base_values["lower_red"].__setitem__(1, x))
        # cv2.createTrackbar("Lower red - V", "Screen Capture", base_values["lower_red"][2], 255, lambda x: base_values["lower_red"].__setitem__(2, x))
        # cv2.createTrackbar("Upper red - H", "Screen Capture", base_values["upper_red"][0], 255, lambda x: base_values["upper_red"].__setitem__(0, x))
        # cv2.createTrackbar("Upper red - S", "Screen Capture", base_values["upper_red"][1], 255, lambda x: base_values["upper_red"].__setitem__(1, x))
        # cv2.createTrackbar("Upper red - V", "Screen Capture", base_values["upper_red"][2], 255, lambda x: base_values["upper_red"].__setitem__(2, x)) 
    if cv2.waitKey(1) & 0xFF == ord('q'):
        break

cv2.destroyAllWindows()
# %%
