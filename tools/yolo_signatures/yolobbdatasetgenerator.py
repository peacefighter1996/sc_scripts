"""
Simple YOLO/COCO dataset generator stub.
Supports reading a CSV of boxes and emitting a COCO JSON.

Usage:
  python yolobbdatasetgenerator.py --input-dir bb_unknown --annotations annotations.csv --output dataset/coco_annotations.json
"""
import os
import json
import argparse
from PIL import Image


def read_csv(path):
    boxes = {}
    if not path or not os.path.exists(path):
        return boxes
    with open(path, 'r', encoding='utf-8') as f:
        for line in f:
            line=line.strip()
            if not line: continue
            parts=line.split(',')
            # support id,image,... and image,... formats
            if len(parts) < 6:
                continue
            if len(parts) >= 8 and parts[0].isdigit():
                mid = int(parts[0]); image = parts[1]; xmin=int(parts[2]); ymin=int(parts[3]); xmax=int(parts[4]); ymax=int(parts[5]); cls=parts[6]; subtype = parts[7] if len(parts)>7 else ''
            else:
                mid = None; image = parts[0]; xmin=int(parts[1]); ymin=int(parts[2]); xmax=int(parts[3]); ymax=int(parts[4]); cls=parts[5]; subtype = parts[6] if len(parts)>6 else ''
            entry = {'xmin':xmin,'ymin':ymin,'xmax':xmax,'ymax':ymax,'class':cls,'subtype':subtype}
            if mid is not None:
                entry['id'] = mid
            boxes.setdefault(image, []).append(entry)
    return boxes


def build_coco(image_dir, boxes_dict):
    images = []
    annotations = []
    img_id = 1; ann_id = 1
    excluded = []
    for fname in sorted(os.listdir(image_dir)):
        if not fname.lower().endswith(('.png','.jpg','.jpeg')):
            continue
        # skip images that have no boxes
        bl = boxes_dict.get(fname, [])
        if not bl:
            excluded.append(fname)
            continue
        path = os.path.join(image_dir, fname)
        try:
            with Image.open(path) as im: w,h = im.size
        except Exception:
            w=h=0
        images.append({'id': img_id, 'file_name': fname, 'width': w, 'height': h})
        for b in bl:
            bbox = [b['xmin'], b['ymin'], b['xmax']-b['xmin'], b['ymax']-b['ymin']]
            cat = 0 if (b.get('class','')=='unknown') else 1
            attrs = {'subtype': b.get('subtype','')}
            if 'id' in b:
                attrs['management_id'] = b['id']
            annotations.append({'id': ann_id, 'image_id': img_id, 'bbox': bbox, 'area': bbox[2]*bbox[3], 'category_id': cat, 'attributes': attrs})
            ann_id += 1
        img_id += 1
    coco = {'images': images, 'annotations': annotations, 'categories': [{'id':0,'name':'unknown'},{'id':1,'name':'mineral'}]}
    return coco, excluded


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--input-dir', default='images/test/bb_unknown')
    parser.add_argument('--annotations', default='annotations.csv')
    parser.add_argument('--output', default='dataset/coco_annotations.json')
    args = parser.parse_args()

    if not os.path.isdir(args.input_dir):
        print('Input dir not found:', args.input_dir); return
    boxes = read_csv(args.annotations)
    coco, excluded = build_coco(args.input_dir, boxes)
    os.makedirs(os.path.dirname(args.output), exist_ok=True)
    with open(args.output, 'w', encoding='utf-8') as f:
        json.dump(coco, f, indent=2)
    print('Wrote', args.output)
    if excluded:
        print(f'Excluded {len(excluded)} images with no bounding boxes. Example: {excluded[:5]}')


if __name__ == '__main__':
    main()
