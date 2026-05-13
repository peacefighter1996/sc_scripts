YOLO Signatures Toolset
=======================

This folder contains a small annotator, a CSV→COCO converter, and a training stub for building a YOLO dataset for Star Citizen signatures.

Quick start
-----------

- Annotate images (put images in `bb_unknown`):

```bash
python tools/yolo_signatures/annotator.py --dir bb_unknown --annotations annotations.csv --port 5000
```

Open http://localhost:5000 to view images and add bounding boxes. Click Save to write `dataset/coco_annotations.json`.

- Convert CSV to COCO (if you have CSV annotations):

```bash
python tools/yolo_signatures/yolobbdatasetgenerator.py --input-dir bb_unknown --annotations annotations.csv --output dataset/coco_annotations.json
```

- Train (stub):

```bash
python tools/yolo_signatures/train_yolo.py --data dataset/coco_annotations.json --model yolov8n.pt --epochs 50
```

Dependencies
------------
- Flask
- Pillow

Install with:

```bash
pip install -r requirements.txt
```

Notes
-----
This is an initial scaffolding. We can extend the annotator with edit/delete, better UI, and CVAT-like features on request.
