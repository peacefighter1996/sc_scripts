import argparse
from pathlib import Path

import tensorflow as tf
import tf2onnx


def export_model(input_model: Path, output_model: Path, opset: int) -> None:
    model = tf.keras.models.load_model(str(input_model))
    output_model.parent.mkdir(parents=True, exist_ok=True)

    input_signature = (tf.TensorSpec((None, 14, 9, 1), tf.float32, name="input"),)

    @tf.function(input_signature=input_signature)
    def model_fn(inputs):
        return model(inputs, training=False)

    tf2onnx.convert.from_function(
        model_fn,
        input_signature=input_signature,
        opset=opset,
        output_path=str(output_model),
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Export XYZ OCR Keras model to ONNX for C++ inference")
    parser.add_argument("--input", default="data/best_pareto_model.keras", help="Path to input Keras model")
    parser.add_argument("--output", default="data/best_pareto_model.onnx", help="Path to output ONNX model")
    parser.add_argument("--opset", type=int, default=17, help="ONNX opset version")
    args = parser.parse_args()

    input_path = Path(args.input)
    output_path = Path(args.output)

    if not input_path.exists():
        raise FileNotFoundError(f"Input model not found: {input_path}")

    export_model(input_path, output_path, args.opset)
    print(f"Exported ONNX model: {output_path}")


if __name__ == "__main__":
    main()
