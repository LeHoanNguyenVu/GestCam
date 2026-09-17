#!/usr/bin/env python3
"""
GestCam - Model Download and Quantization Script (Task 2.1)
Downloads original ONNX models for UltraFace, BlazePalm, and BlazeHand,
applies dynamic INT8 quantization, and verifies model input/output shapes.
"""

import os
import sys
import urllib.request
import tempfile
import numpy as np
import onnxruntime as ort
from onnxruntime.quantization import quantize_dynamic, QuantType

# Project directories
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
MODELS_DIR = os.path.join(PROJECT_ROOT, "models")

# Model definitions
MODELS = [
    {
        "name": "ultraface",
        "url": "https://github.com/Linzaer/Ultra-Light-Fast-Generic-Face-Detector-1MB/raw/master/models/onnx/version-RFB-320.onnx",
        "output_filename": "ultraface_int8.onnx",
        "dummy_input": ("input", np.zeros((1, 3, 240, 320), dtype=np.float32)),
        "expected_outputs": ["scores", "boxes"],
    },
    {
        "name": "blazepalm",
        "url": "https://storage.googleapis.com/ailia-models/blazepalm/blazepalm.onnx",
        "output_filename": "blazepalm_int8.onnx",
        "dummy_input": ("input", np.zeros((1, 3, 256, 256), dtype=np.float32)),
        "expected_outputs": ["regressors", "classificators"],
    },
    {
        "name": "blazehand",
        "url": "https://storage.googleapis.com/ailia-models/blazehand/blazehand.onnx",
        "output_filename": "blazehand_int8.onnx",
        "dummy_input": ("input", np.zeros((1, 3, 256, 256), dtype=np.float32)),
        "expected_outputs": ["hand_flag", "handedness", "landmarks"],
    },
]


def download_file(url: str, dest_path: str):
    print(f"  Downloading from: {url}")
    urllib.request.urlretrieve(url, dest_path)
    size_mb = os.path.getsize(dest_path) / (1024 * 1024)
    print(f"  Downloaded size: {size_mb:.2f} MB")


def main():
    print("================================================================================")
    print("              GESTCAM AI MODELS PREPARATION (TASK 2.1)                          ")
    print("================================================================================")

    os.makedirs(MODELS_DIR, exist_ok=True)
    total_int8_size = 0

    with tempfile.TemporaryDirectory() as tmpdir:
        for m in MODELS:
            print(f"\n[MODEL] Processing: {m['name']}...")
            raw_path = os.path.join(tmpdir, f"{m['name']}_raw.onnx")
            int8_path = os.path.join(MODELS_DIR, m["output_filename"])

            # 1. Download
            download_file(m["url"], raw_path)

            # 2. Dynamic Quantization
            print("  Applying Dynamic INT8 Quantization (QUInt8)...")
            quantize_dynamic(
                model_input=raw_path,
                model_output=int8_path,
                weight_type=QuantType.QUInt8,
            )

            raw_size = os.path.getsize(raw_path) / (1024 * 1024)
            int8_size = os.path.getsize(int8_path) / (1024 * 1024)
            total_int8_size += int8_size
            compression = (1.0 - (int8_size / raw_size)) * 100
            print(f"  Compressed: {raw_size:.2f} MB -> {int8_size:.2f} MB ({compression:.1f}% reduction)")

            # 3. Verification through inference session
            print("  Verifying model integrity with ONNX Runtime...")
            sess = ort.InferenceSession(int8_path, providers=["CPUExecutionProvider"])
            input_name, dummy_val = m["dummy_input"]
            outputs = sess.run(None, {input_name: dummy_val})

            output_names = [o.name for o in sess.get_outputs()]
            print(f"  Inputs:  {[i.name for i in sess.get_inputs()]} {[i.shape for i in sess.get_inputs()]}")
            print(f"  Outputs: {output_names} {[o.shape for o in outputs]}")

            for exp in m["expected_outputs"]:
                assert exp in output_names, f"Missing expected output '{exp}' in {m['name']}!"

            print(f"  [OK] {m['name']} verified successfully!")

    print("\n--------------------------------------------------------------------------------")
    print(f"TOTAL INT8 MODELS SIZE: {total_int8_size:.2f} MB (Target: <= 10.0 MB)")
    if total_int8_size <= 10.0:
        print("[SUCCESS] All models meet Sprint 2 size requirements!")
    else:
        print("[WARNING] Total model size exceeds 10 MB!")
        sys.exit(1)

    print("Target directory:", MODELS_DIR)
    for f in os.listdir(MODELS_DIR):
        fp = os.path.join(MODELS_DIR, f)
        print(f" - {f}: {os.path.getsize(fp) / (1024 * 1024):.2f} MB")
    print("================================================================================")


if __name__ == "__main__":
    main()
