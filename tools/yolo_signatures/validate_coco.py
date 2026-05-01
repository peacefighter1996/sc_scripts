"""Validate COCO annotations for common issues that break training.

Checks performed:
- dataset file exists and parses
- categories are contiguous starting at 0
- 'nc' (in YAML generated) will match number of categories
- every annotation refers to an existing image
- bboxes have positive width/height
- reports images with zero annotations (possible empty val set)

Usage:
  python tools/yolo_signatures/validate_coco.py dataset/coco_annotations.json --images images/

Exit code 0 => OK (no severe issues). Non-zero => problems found.
"""
import sys
import os
import json
import argparse


def main():
    p = argparse.ArgumentParser()
    p.add_argument('coco_json', help='path to COCO JSON file')
    p.add_argument('--images', default='', help='path to images dir (optional)')
    args = p.parse_args()

    path = args.coco_json
    if not os.path.exists(path):
        print('ERROR: coco json not found:', path)
        return 2
    with open(path, 'r', encoding='utf-8') as f:
        coco = json.load(f)

    images = coco.get('images', [])
    anns = coco.get('annotations', [])
    cats = coco.get('categories', [])

    print(f'Images: {len(images)}, Annotations: {len(anns)}, Categories: {len(cats)}')

    # map image id -> filename
    img_id_map = {im.get('id'): im.get('file_name') for im in images}
    img_ann_counts = {im.get('file_name'): 0 for im in images}

    problems = False

    # check category ids
    cat_ids = sorted([c.get('id') for c in cats])
    if not cat_ids:
        print('ERROR: no categories found')
        problems = True
    else:
        expected = list(range(0, len(cat_ids)))
        if cat_ids != expected:
            print('WARNING: category ids are not contiguous starting at 0.')
            print(' Found ids:', cat_ids)
            print(' Expected:', expected)
            print(' Ultralytics expects class indices from 0..nc-1. Convert or remap category ids.')
            problems = True

    # check annotations
    zero_area = 0
    missing_image_refs = set()
    bad_cat = 0
    for a in anns:
        img_id = a.get('image_id')
        if img_id not in img_id_map:
            missing_image_refs.add(img_id)
        else:
            img_ann_counts[img_id_map[img_id]] += 1
        bbox = a.get('bbox', [])
        if not bbox or len(bbox) < 4:
            zero_area += 1
        else:
            w = bbox[2]
            h = bbox[3]
            if w <= 0 or h <= 0:
                zero_area += 1
        cid = a.get('category_id')
        if cid not in cat_ids:
            bad_cat += 1

    if missing_image_refs:
        print('ERROR: annotations reference missing image ids:', sorted(list(missing_image_refs)))
        problems = True
    if zero_area:
        print(f'ERROR: {zero_area} annotations have zero or invalid bbox area')
        problems = True
    if bad_cat:
        print(f'ERROR: {bad_cat} annotations reference category ids not present in categories list')
        problems = True

    # images with zero annotations
    imgs_no_ann = [k for k, v in img_ann_counts.items() if v == 0]
    if imgs_no_ann:
        print(f'WARNING: {len(imgs_no_ann)} images have zero annotations. Example: {imgs_no_ann[:5]}')
        print(' If your validation split contains images with zero annotations, metric computations may produce NaNs.')

    # optionally check if files exist
    if args.images:
        missing_files = []
        for im in images:
            fn = im.get('file_name')
            pth = os.path.join(args.images, fn)
            if not os.path.exists(pth):
                missing_files.append(fn)
        if missing_files:
            print(f'ERROR: {len(missing_files)} image files referenced by the COCO JSON were not found in', args.images)
            print(' Example missing:', missing_files[:5])
            problems = True

    if problems:
        print('\nValidation failed — fix the reported issues (category ids, missing images, zero-area bboxes, or empty val set).')
        return 3
    print('\nValidation OK: dataset looks consistent.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
