#!/usr/bin/env python3
"""
GestCam - Setup ONNX Runtime DirectML (Task 2.2)
Downloads and extracts Microsoft.ML.OnnxRuntime.DirectML and Microsoft.AI.DirectML
NuGet packages for C++ Native x64 Windows development.
"""

import os
import sys
import urllib.request
import zipfile
import io

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
TARGET_DIR = os.path.join(PROJECT_ROOT, "third_party", "onnxruntime")

ORT_VERSION = "1.20.1"
DML_VERSION = "1.15.2"

ORT_URL = f"https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime.directml/{ORT_VERSION}/microsoft.ml.onnxruntime.directml.{ORT_VERSION}.nupkg"
DML_URL = f"https://api.nuget.org/v3-flatcontainer/microsoft.ai.directml/{DML_VERSION}/microsoft.ai.directml.{DML_VERSION}.nupkg"


def download_and_extract():
    os.makedirs(os.path.join(TARGET_DIR, "include"), exist_ok=True)
    os.makedirs(os.path.join(TARGET_DIR, "lib"), exist_ok=True)
    os.makedirs(os.path.join(TARGET_DIR, "bin"), exist_ok=True)

    print("================================================================================")
    print("           GESTCAM - ONNX RUNTIME DIRECTML NATIVE SETUP                         ")
    print("================================================================================")

    # 1. Download & Extract Microsoft.ML.OnnxRuntime.DirectML
    print(f"Downloading Microsoft.ML.OnnxRuntime.DirectML {ORT_VERSION}...")
    res_ort = urllib.request.urlopen(ORT_URL)
    z_ort = zipfile.ZipFile(io.BytesIO(res_ort.read()))
    print("Extracting ONNX Runtime headers, libs and DLLs...")

    for f in z_ort.namelist():
        if f.startswith("build/native/include/"):
            fname = os.path.basename(f)
            if fname:
                with open(os.path.join(TARGET_DIR, "include", fname), "wb") as out:
                    out.write(z_ort.read(f))
        elif f == "runtimes/win-x64/native/onnxruntime.dll":
            with open(os.path.join(TARGET_DIR, "bin", "onnxruntime.dll"), "wb") as out:
                out.write(z_ort.read(f))
        elif f == "runtimes/win-x64/native/onnxruntime.lib":
            with open(os.path.join(TARGET_DIR, "lib", "onnxruntime.lib"), "wb") as out:
                out.write(z_ort.read(f))

    # 2. Download & Extract Microsoft.AI.DirectML
    print(f"Downloading Microsoft.AI.DirectML {DML_VERSION}...")
    res_dml = urllib.request.urlopen(DML_URL)
    z_dml = zipfile.ZipFile(io.BytesIO(res_dml.read()))
    print("Extracting DirectML headers, libs and DLLs...")

    for f in z_dml.namelist():
        if f.startswith("include/"):
            fname = os.path.basename(f)
            if fname:
                with open(os.path.join(TARGET_DIR, "include", fname), "wb") as out:
                    out.write(z_dml.read(f))
        elif f == "bin/x64-win/DirectML.dll":
            with open(os.path.join(TARGET_DIR, "bin", "DirectML.dll"), "wb") as out:
                out.write(z_dml.read(f))
        elif f == "bin/x64-win/DirectML.lib":
            with open(os.path.join(TARGET_DIR, "lib", "DirectML.lib"), "wb") as out:
                out.write(z_dml.read(f))

    print("\n[SUCCESS] ONNX Runtime DirectML installed at:", TARGET_DIR)
    print("Files in include/:", len(os.listdir(os.path.join(TARGET_DIR, "include"))))
    print("Files in lib/:    ", os.listdir(os.path.join(TARGET_DIR, "lib")))
    print("Files in bin/:    ", os.listdir(os.path.join(TARGET_DIR, "bin")))
    print("================================================================================")


if __name__ == "__main__":
    download_and_extract()
