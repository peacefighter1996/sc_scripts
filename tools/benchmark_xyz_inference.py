import argparse
import json
import os
import subprocess
import time
from collections import defaultdict
from pathlib import Path
from typing import Dict, List, Tuple

import cv2
import numpy as np
import tensorflow as tf

SPECIAL_LABEL_TO_CHAR = {
    "space": " ",
    "colon": ":",
    "dot": ".",
    "dash": "-",
    "comma": ",",
}


def label_token_to_char(label: str) -> str:
    return SPECIAL_LABEL_TO_CHAR.get(label, label)


def load_label_map(label_map_path: Path) -> Tuple[Dict[str, int], Dict[int, str]]:
    with label_map_path.open("r", encoding="utf-8") as f:
        data = json.load(f)

    token_to_index = {str(token): int(index) for token, index in data.items()}
    index_to_token = {index: token for token, index in token_to_index.items()}
    return token_to_index, index_to_token


def load_dataset(dataset_dir: Path, token_to_index: Dict[str, int]) -> Tuple[np.ndarray, np.ndarray, List[str], List[Path]]:
    images: List[np.ndarray] = []
    labels: List[int] = []
    label_tokens: List[str] = []
    paths: List[Path] = []

    for label_dir in sorted(dataset_dir.iterdir()):
        if not label_dir.is_dir():
            continue

        token = label_dir.name
        if token not in token_to_index:
            continue

        image_files = sorted(label_dir.glob("*.png"))
        for image_path in image_files:
            img = cv2.imread(str(image_path), cv2.IMREAD_GRAYSCALE)
            if img is None:
                continue

            if img.shape != (14, 9):
                img = cv2.resize(img, (9, 14), interpolation=cv2.INTER_AREA)

            img = img.astype(np.float32) / 255.0
            images.append(img[..., np.newaxis])
            labels.append(token_to_index[token])
            label_tokens.append(token)
            paths.append(image_path)

    if not images:
        raise RuntimeError(f"No dataset images found under: {dataset_dir}")

    X = np.stack(images, axis=0)
    y = np.array(labels, dtype=np.int64)
    return X, y, label_tokens, paths


def run_python_tf_inference(model_path: Path, X: np.ndarray) -> Tuple[np.ndarray, float]:
    model = tf.keras.models.load_model(str(model_path))
    start = time.perf_counter()
    predictions = model.predict(X, verbose=0)
    elapsed_ms = (time.perf_counter() - start) * 1000.0
    predicted = np.argmax(predictions, axis=1).astype(np.int64)
    return predicted, elapsed_ms


def run_cpp_onnx_inference(engine_path: Path, onnx_model_path: Path, X: np.ndarray) -> Tuple[np.ndarray, float]:
    payload = "\n".join(str(float(v)) for v in X.reshape(-1))

    start = time.perf_counter()
    result = subprocess.run(
        [str(engine_path), "predict-labels-onnx", str(onnx_model_path), str(X.shape[0])],
        input=payload,
        capture_output=True,
        text=True,
        check=True,
    )
    elapsed_ms = (time.perf_counter() - start) * 1000.0

    out = result.stdout.strip()
    if not out:
        raise RuntimeError("C++ ONNX inference returned empty output")

    predicted = np.array([int(v) for v in out.split()], dtype=np.int64)
    if len(predicted) != X.shape[0]:
        raise RuntimeError(f"Expected {X.shape[0]} labels from C++ ONNX, got {len(predicted)}")

    return predicted, elapsed_ms


def compute_accuracy(y_true: np.ndarray, y_pred: np.ndarray) -> float:
    return float((y_true == y_pred).mean())


def per_class_accuracy(y_true: np.ndarray, y_pred: np.ndarray, index_to_token: Dict[int, str]) -> List[Tuple[str, int, int, float]]:
    buckets: Dict[int, List[int]] = defaultdict(list)
    for idx, label in enumerate(y_true.tolist()):
        buckets[int(label)].append(idx)

    rows = []
    for label in sorted(buckets):
        idxs = buckets[label]
        true_vals = y_true[idxs]
        pred_vals = y_pred[idxs]
        correct = int((true_vals == pred_vals).sum())
        total = len(idxs)
        acc = correct / total if total else 0.0
        token = index_to_token.get(label, str(label))
        rows.append((token, correct, total, acc))
    return rows


def collect_mismatches(
    y_true: np.ndarray,
    y_pred: np.ndarray,
    index_to_token: Dict[int, str],
    paths: List[Path],
    limit: int,
) -> List[Tuple[Path, str, str, str, str]]:
    mismatches = []
    wrong = np.where(y_true != y_pred)[0]
    for i in wrong[:limit]:
        true_token = index_to_token.get(int(y_true[i]), "?")
        pred_token = index_to_token.get(int(y_pred[i]), "?")
        true_char = label_token_to_char(true_token)
        pred_char = label_token_to_char(pred_token)
        mismatches.append((paths[int(i)], true_token, pred_token, true_char, pred_char))
    return mismatches


def print_functionality_report(name: str, y_true: np.ndarray, y_pred: np.ndarray, index_to_token: Dict[int, str], paths: List[Path]) -> None:
    acc = compute_accuracy(y_true, y_pred)
    print(f"[{name}] Accuracy: {acc * 100:.2f}% ({int((y_true == y_pred).sum())}/{len(y_true)})")

    print(f"[{name}] Per-class accuracy:")
    for token, correct, total, class_acc in per_class_accuracy(y_true, y_pred, index_to_token):
        converted = label_token_to_char(token)
        print(f"  - token='{token}' char='{converted}' -> {correct}/{total} ({class_acc * 100:.1f}%)")

    mismatches = collect_mismatches(y_true, y_pred, index_to_token, paths, limit=20)
    if mismatches:
        print(f"[{name}] Sample mismatches (up to 20):")
        for path, true_token, pred_token, true_char, pred_char in mismatches:
            print(
                f"  - {path}: true token='{true_token}' ({repr(true_char)}) "
                f"pred token='{pred_token}' ({repr(pred_char)})"
            )
    else:
        print(f"[{name}] No mismatches found.")


def main() -> None:
    parser = argparse.ArgumentParser(description="Benchmark and functional-test XYZ OCR character inference")
    parser.add_argument("--dataset", default="dataset", help="Path to dataset root")
    parser.add_argument("--label-map", default="data/label_map.json", help="Path to label_map.json")
    parser.add_argument("--keras-model", default="data/best_pareto_model.keras", help="Path to Keras model")
    parser.add_argument("--onnx-model", default="data/best_pareto_model.onnx", help="Path to ONNX model")
    parser.add_argument("--engine", default="cpp_engine/build/scout_engine.exe", help="Path to scout_engine.exe")
    parser.add_argument("--skip-cpp", action="store_true", help="Skip C++ ONNX benchmark")
    args = parser.parse_args()

    dataset_dir = Path(args.dataset)
    label_map_path = Path(args.label_map)
    keras_model_path = Path(args.keras_model)
    onnx_model_path = Path(args.onnx_model)
    engine_path = Path(args.engine)

    token_to_index, index_to_token = load_label_map(label_map_path)
    X, y, _, paths = load_dataset(dataset_dir, token_to_index)

    print(f"Loaded dataset: {len(y)} samples from {dataset_dir}")
    print(f"Input tensor shape: {X.shape}")

    py_pred, py_ms = run_python_tf_inference(keras_model_path, X)
    py_per_sample = py_ms / len(y)
    print(f"[Python TF] Inference time: {py_ms:.2f} ms total ({py_per_sample:.4f} ms/sample)")
    print_functionality_report("Python TF", y, py_pred, index_to_token, paths)

    if args.skip_cpp:
        print("Skipping C++ ONNX benchmark by request.")
        return

    if not engine_path.exists():
        print(f"Skipping C++ ONNX benchmark: engine not found at {engine_path}")
        return

    if not onnx_model_path.exists():
        print(f"Skipping C++ ONNX benchmark: ONNX model not found at {onnx_model_path}")
        return

    try:
        cpp_pred, cpp_ms = run_cpp_onnx_inference(engine_path, onnx_model_path, X)
    except subprocess.CalledProcessError as ex:
        stderr = ex.stderr.strip() if ex.stderr else ""
        print(f"Skipping C++ ONNX benchmark: command failed ({stderr})")
        return

    cpp_per_sample = cpp_ms / len(y)
    speedup = py_ms / cpp_ms if cpp_ms > 0 else float("inf")
    agree = float((cpp_pred == py_pred).mean())

    print(f"[C++ ONNX] Inference time: {cpp_ms:.2f} ms total ({cpp_per_sample:.4f} ms/sample)")
    print(f"[C++ ONNX] Speedup vs Python TF: {speedup:.2f}x")
    print(f"[Cross-check] Prediction agreement with Python TF: {agree * 100:.2f}%")
    print_functionality_report("C++ ONNX", y, cpp_pred, index_to_token, paths)


if __name__ == "__main__":
    main()
