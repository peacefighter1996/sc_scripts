cmake -S . -B build_app -G "Visual Studio 17 2022" -A x64 -DSCOUT_ENABLE_ONNXRUNTIME=ON -DONNXRUNTIME_ROOT="./third_party/onnxruntime-win-x64-gpu-1.25.0"
cmake --build build_app --config Release
