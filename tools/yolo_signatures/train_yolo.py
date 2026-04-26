"""
Trainer stub for YOLO signatures using pretrained weights.
This script is a minimal scaffold that calls Ultralytics if installed.

Usage example:
  python train_yolo.py --data dataset/coco_annotations.json --model yolov8n.pt --epochs 10
"""
import argparse
import os
import json


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--data', required=True, help='COCO JSON annotations')
    parser.add_argument('--model', default='yolov8n.pt', help='pretrained weights or model name')
    parser.add_argument('--epochs', type=int, default=50)
    parser.add_argument('--batch', type=int, default=8)
    args = parser.parse_args()

    print('Trainer stub:')
    print(' data:', args.data)
    print(' model:', args.model)
    print(' epochs:', args.epochs)
    print('\nIf you have `ultralytics` installed you can replace this stub with actual training code using their API.')


if __name__ == '__main__':
    main()
