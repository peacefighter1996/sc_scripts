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
  with open(coco_json_path, 'r', encoding='utf-8') as f:
    coco = json.load(f)
  cats = coco.get('categories', [])
  names = [c.get('name', '') for c in sorted(cats, key=lambda c: c.get('id', 0))]
  data = {
    'path': images_dir,
    'train': images_dir,
    'val': images_dir,
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
  parser.add_argument('--images', default='', help='Path to images directory (required for COCO JSON inputs)')
  parser.add_argument('--model', default='yolov8n.pt', help='pretrained weights or model name')
  parser.add_argument('--epochs', type=int, default=50)
  parser.add_argument('--batch', type=int, default=8)
  parser.add_argument('--imgsz', type=int, default=1280)
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
    images_dir = args.images or infer_images_dir(args.images)
    if not images_dir or not os.path.isdir(images_dir):
      print('For COCO JSON inputs you must provide --images pointing to the image folder.')
      print('Tried to infer images dir, none of the common paths existed.')
      sys.exit(2)
    fd, tmp = tempfile.mkstemp(suffix='.yaml')
    os.close(fd)
    temp_yaml = tmp
    build_data_yaml_from_coco(args.data, images_dir, temp_yaml)
    data_arg = temp_yaml

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
