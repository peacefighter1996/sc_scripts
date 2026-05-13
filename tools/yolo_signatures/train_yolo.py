"""Trainer for YOLO signatures using Ultralytics (optional).

This script will attempt to use `ultralytics` if available. If you pass a COCO
JSON (`--data dataset/coco_annotations.json`) you must also provide the
corresponding images directory via `--images` (or the script will try to infer
common folders).

Example:
  python train_yolo.py --data dataset/coco_annotations.json --images images/ --model yolov8n.pt --epochs 25
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime


def write_simple_yaml(path, d):
  # create a minimal YAML without requiring PyYAML
  lines = []
  def add(k, v, indent=0):
    pad = '  ' * indent
    if isinstance(v, dict):
      lines.append(f"{pad}{k}:")
      for kk, vv in v.items():
        add(kk, vv, indent + 1)
    elif isinstance(v, list):
      lines.append(f"{pad}{k}:")
      for item in v:
        lines.append(f"{pad}- {item}")
    else:
      lines.append(f"{pad}{k}: {v}")

  for k, v in d.items():
    add(k, v)

  with open(path, 'w', encoding='utf-8') as f:
    f.write('\n'.join(lines))


def infer_images_dir(coco_json_path):
  # try several common locations relative to the repo
  cand = [
    os.path.join(os.path.dirname(coco_json_path)),
    os.path.join(os.getcwd(), 'images', 'test', 'bb_unknown'),
    os.path.join(os.getcwd(), 'dataset', 'images'),
    os.path.join(os.getcwd(), 'data', 'images'),
  ]
  for p in cand:
    if p and os.path.isdir(p):
      return p
  return None


def build_data_yaml_from_coco(coco_json_path, images_dir, out_yaml_path):
  absolute_coco_path = os.path.abspath(coco_json_path)
  print(f'Building YOLO dataset YAML from COCO JSON: {absolute_coco_path}')
  with open(absolute_coco_path , 'r', encoding='utf-8') as f:
    coco = json.load(f)

  dataset_root = os.path.join(os.getcwd(), 'dataset', 'yolodataset')
  images_train_dir = os.path.join(dataset_root, 'images', 'train')
  images_val_dir = os.path.join(dataset_root, 'images', 'validation')
  labels_train_dir = os.path.join(dataset_root, 'labels', 'train')
  labels_val_dir = os.path.join(dataset_root, 'labels', 'validation')

  # Rebuild output to avoid stale images/labels from previous runs.
  if os.path.isdir(dataset_root):
    shutil.rmtree(dataset_root)
  os.makedirs(images_train_dir, exist_ok=True)
  os.makedirs(images_val_dir, exist_ok=True)
  os.makedirs(labels_train_dir, exist_ok=True)
  os.makedirs(labels_val_dir, exist_ok=True)

  cats = coco.get('categories', [])
  sorted_cats = sorted(cats, key=lambda c: c.get('id', 0))
  names = [c.get('name', '') for c in sorted_cats]
  cat_id_to_cls = {c.get('id'): idx for idx, c in enumerate(sorted_cats)}

  images = coco.get('images', [])
  anns = coco.get('annotations', [])

  anns_by_image_id = {}
  for ann in anns:
    anns_by_image_id.setdefault(ann.get('image_id'), []).append(ann)

  images_sorted = sorted(images, key=lambda im: im.get('file_name', ''))
  copied = 0
  for idx, im in enumerate(images_sorted):
    file_name = im.get('file_name')
    if not file_name:
      continue

    src_path = os.path.join(images_dir, file_name)
    if not os.path.exists(src_path):
      src_path = os.path.join(images_dir, os.path.basename(file_name))
    if not os.path.exists(src_path):
      continue

    split_is_val = (idx % 3) == 2  # 2:1 split (train:validation)
    img_out_dir = images_val_dir if split_is_val else images_train_dir
    lbl_out_dir = labels_val_dir if split_is_val else labels_train_dir

    dst_img_path = os.path.join(img_out_dir, os.path.basename(file_name))
    shutil.copy2(src_path, dst_img_path)
    copied += 1

    w = float(im.get('width') or 0.0)
    h = float(im.get('height') or 0.0)
    label_lines = []
    if w > 0 and h > 0:
      for ann in anns_by_image_id.get(im.get('id'), []):
        bbox = ann.get('bbox') or []
        if len(bbox) != 4:
          continue
        cat_id = ann.get('category_id')
        if cat_id not in cat_id_to_cls:
          continue

        x, y, bw, bh = [float(v) for v in bbox]
        xc = (x + bw / 2.0) / w
        yc = (y + bh / 2.0) / h
        nw = bw / w
        nh = bh / h

        # Clamp to YOLO's expected normalized range.
        xc = max(0.0, min(1.0, xc))
        yc = max(0.0, min(1.0, yc))
        nw = max(0.0, min(1.0, nw))
        nh = max(0.0, min(1.0, nh))

        label_lines.append(f"{cat_id_to_cls[cat_id]} {xc:.6f} {yc:.6f} {nw:.6f} {nh:.6f}")

    label_file = os.path.splitext(os.path.basename(file_name))[0] + '.txt'
    dst_label_path = os.path.join(lbl_out_dir, label_file)
    with open(dst_label_path, 'w', encoding='utf-8') as lf:
      lf.write('\n'.join(label_lines))

  print(f'Prepared YOLO dataset at: {dataset_root}')
  print(f'Copied {copied} images from COCO metadata into train/validation splits.')

  data = {
    'path': dataset_root,
    'train': 'images/train',
    'val': 'images/validation',
    'nc': len(names),
    'names': names,
  }
  write_simple_yaml(out_yaml_path, data)


def ensure_ultralytics(install=False):
  try:
    import ultralytics  # noqa: F401
    return True
  except Exception:
    if install:
      print('Installing ultralytics package...')
      subprocess.check_call([sys.executable, '-m', 'pip', 'install', 'ultralytics'])
      try:
        import ultralytics  # noqa: F401
        return True
      except Exception:
        return False
    return False


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('--data', default='dataset/coco_annotations.json', help='COCO JSON annotations or dataset YAML')
  parser.add_argument('--images', default='images/test/bb_unknown', help='Path to images directory (required for COCO JSON inputs)')
  parser.add_argument('--model', default='yolov8n.pt', help='pretrained weights or model name')
  parser.add_argument('--epochs', type=int, default=50)
  parser.add_argument('--batch', type=int, default=8)
  parser.add_argument('--imgsz', type=int, default=640)
  parser.add_argument('--device', default='cuda', help='cuda device or cpu (passes through to ultralytics)')
  parser.add_argument('--project', default='runs/train', help='where to save runs')
  parser.add_argument('--name', default=None, help='experiment name (default timestamp)')
  parser.add_argument('--install', action='store_true', help='auto-install ultralytics if missing')
  args = parser.parse_args()

  if not ensure_ultralytics(install=args.install):
    print('\n`ultralytics` package not available. Install with:')
    print('\n  pip install ultralytics\n')
    print('Or run this script with `--install` to install automatically.')
    sys.exit(1)

  # now import the API
  from ultralytics import YOLO

  data_arg = args.data
  print(args)
  temp_yaml = None
  if args.data.lower().endswith('.json'):
    # COCO JSON -> build a small YAML dataset description
    images_dir = args.images or infer_images_dir(args.data)
    if not images_dir or not os.path.isdir(images_dir):
      print('For COCO JSON inputs you must provide --images pointing to the image folder.')
      print('Tried to infer images dir, none of the common paths existed.')
      sys.exit(2)
    yaml = os.path.join('dataset', 'yolodataset', 'data.yaml')
    #create a temp YAML in case we need to clean up later
    os.makedirs(os.path.dirname(yaml), exist_ok=True)
    with open(yaml, 'w', encoding='utf-8') as f:
      f.write('')  # create an empty file to ensure it exists
    
    build_data_yaml_from_coco(args.data, images_dir, yaml)
    data_arg = yaml

  run_name = args.name or datetime.now().strftime('run_%Y%m%d_%H%M%S')

  print('Starting training with:')
  print(' data:', data_arg)
  print(' model:', args.model)
  print(' epochs:', args.epochs)
  print(' batch:', args.batch)
  print(' imgsz:', args.imgsz)
  print(' device:', args.device)
  print(' project:', args.project)
  print(' name:', run_name)

  try:
    model = YOLO(args.model)
    model.train(
      data=data_arg,
      epochs=args.epochs,
      batch=args.batch,
      imgsz=args.imgsz,
      device=args.device,
      project=args.project,
      name=run_name,
    )
  except Exception as e:
    print('Training failed:', e)
  finally:
    if temp_yaml and os.path.exists(temp_yaml):
      try:
        os.remove(temp_yaml)
      except Exception:
        pass


if __name__ == '__main__':
  main()
