# Scout Engine

`scout_engine` now contains the standalone application functionality that was previously hosted in `scoutdataviewer.py`.

Current capabilities:

- Standalone GLFW/OpenGL/ImGui desktop app
- CSV data operations (`load` and `append`)
- XYZ OCR parsing (`parse-xyz`)
- OCR task result resolution (`ocr-task`)
- Optional native XYZ label inference (`predict-labels-onnx`)
- Planet map rendering, point filtering, hover tooltips, and add-point UI
- Windows screen capture for XYZ OCR updates

## Build (Windows PowerShell)

```powershell
Set-Location "C:\path\to\sc_scripts\cpp_engine"
cmake -S . -B build_app -G "Visual Studio 17 2022" -A x64 -DSCOUT_ENABLE_ONNXRUNTIME=ON -DONNXRUNTIME_ROOT="C:/path/to/sc_scripts/third_party/onnxruntime-win-x64-gpu-1.25.0"
cmake --build build_app --config Release
```

This build fetches `glfw` and `imgui` automatically and links against the local ONNX Runtime SDK.

### Minimal CLI-only build

```powershell
Set-Location "C:\path\to\sc_scripts\cpp_engine"
cmake -S . -B build -DSCOUT_BUILD_APP=OFF -DSCOUT_ENABLE_ONNXRUNTIME=ON
cmake --build build --config Release
```

Example using the downloaded GPU SDK:

```powershell
Set-Location "C:\path\to\sc_scripts\cpp_engine"
cmake -S . -B build_ort -G "Visual Studio 17 2022" -A x64 -DSCOUT_ENABLE_ONNXRUNTIME=ON -DONNXRUNTIME_ROOT="C:/path/to/sc_scripts/third_party/onnxruntime-win-x64-gpu-1.25.0"
cmake --build build_ort --config Release
```

The CMake project copies `onnxruntime.dll` next to `scout_engine.exe` automatically when `ONNXRUNTIME_ROOT` is used.

## Run the App

Launch the standalone app with no arguments:

```powershell
Set-Location "C:\path\to\sc_scripts\cpp_engine"
.\build_app\Release\scout_engine.exe
```

Equivalent explicit command:

```powershell
.\build_app\Release\scout_engine.exe app
```

## Commands

```powershell
.\build_app\Release\scout_engine.exe dump ..\data\geoscout.csv
.\build_app\Release\scout_engine.exe next-id ..\data\geoscout.csv
.\build_app\Release\scout_engine.exe append ..\data\geoscout.csv 999 eu10 1.0 2.0 3.0 Pyro_Pyro4 Gold 10 20 ""
.\build_app\Release\scout_engine.exe parse-xyz "abc 123m 456m 7k"
.\build_app\Release\scout_engine.exe ocr-task "abc 123m 456m 7k"
.\build_app\Release\scout_engine.exe ocr-task "Gold" "abc 123m 456m 7k"
.\build_app\Release\scout_engine.exe predict-labels-onnx ..\data\best_pareto_model.onnx 2
```

`predict-labels-onnx` reads flattened float input from `stdin` (shape per sample: `14x9x1`).

## Export Keras model to ONNX

```powershell
Set-Location "C:\path\to\sc_scripts"
python .\tools\export_xyz_model_to_onnx.py --input .\data\best_pareto_model.keras --output .\data\best_pareto_model.onnx
```

## Benchmark + Functional Test (`dataset/` + `label_map.json`)

This benchmark validates character-classification functionality on `dataset/` and converts special tokens to actual characters for reporting:

- `space` -> ` `
- `colon` -> `:`
- `dot` -> `.`
- `dash` -> `-`
- `comma` -> `,`

```powershell
Set-Location "C:\path\to\sc_scripts"
python .\tools\benchmark_xyz_inference.py --dataset .\dataset --label-map .\data\label_map.json --keras-model .\data\best_pareto_model.keras --onnx-model .\data\best_pareto_model.onnx --engine .\cpp_engine\build_ort\Release\scout_engine.exe
```

Python-only benchmark:

```powershell
Set-Location "C:\path\to\sc_scripts"
python .\tools\benchmark_xyz_inference.py --skip-cpp
```
