$ErrorActionPreference = "Stop"

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 4

Write-Host ""
Write-Host "Build complete: build\sheepdog-demo.exe"
