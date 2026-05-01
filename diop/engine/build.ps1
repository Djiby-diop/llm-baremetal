# Compilation script for Windows (using GCC / MinGW)
# Make sure you have gcc installed (from MSYS2 or MinGW)

$EngineDir = "C:\Users\djibi\OneDrive\Bureau\baremetal\llm-baremetal\diop\engine\native"
$PortableDir = "C:\Users\djibi\OneDrive\Bureau\baremetal\llm-baremetal\engine\gguf"

Write-Host "Compiling Native C Engine for Windows (.dll)..." -ForegroundColor Cyan

# Compile as a 64-bit shared library so Python x64 can load it cleanly.
gcc -shared -m64 -o "$EngineDir\diop_engine.dll" `
    "$EngineDir\diop_engine.c" `
    "$EngineDir\legacy_gguf_fallback.c" `
    "$EngineDir\runtime_transition_support.c" `
    "$EngineDir\layout_plan_support.c" `
    "$EngineDir\model_planning_support.c" `
    "$EngineDir\diop_inference_core.c" `
    "$PortableDir\gguf_portable_stub.c" `
    -O3 -Wall

if ($LASTEXITCODE -eq 0) {
    Write-Host "Success! diop_engine.dll generated." -ForegroundColor Green
} else {
    Write-Host "Compilation failed. Check if gcc is in your PATH." -ForegroundColor Red
}

Write-Host "Compiling model planning diagnostic CLI..." -ForegroundColor Cyan

gcc -m64 -o "$EngineDir\model_planning_cli.exe" `
    "$EngineDir\model_planning_cli.c" `
    "$EngineDir\legacy_gguf_fallback.c" `
    "$EngineDir\layout_plan_support.c" `
    "$EngineDir\model_planning_support.c" `
    "$PortableDir\gguf_portable_stub.c" `
    -O3 -Wall

if ($LASTEXITCODE -eq 0) {
    Write-Host "Success! model_planning_cli.exe generated." -ForegroundColor Green
} else {
    Write-Host "CLI compilation failed. Check if gcc is in your PATH." -ForegroundColor Red
}

Write-Host "Compiling runtime transition diagnostic CLI..." -ForegroundColor Cyan

gcc -m64 -o "$EngineDir\runtime_transition_cli.exe" `
    "$EngineDir\runtime_transition_cli.c" `
    "$EngineDir\runtime_transition_support.c" `
    "$EngineDir\legacy_gguf_fallback.c" `
    "$EngineDir\layout_plan_support.c" `
    "$EngineDir\model_planning_support.c" `
    "$PortableDir\gguf_portable_stub.c" `
    -O3 -Wall

if ($LASTEXITCODE -eq 0) {
    Write-Host "Success! runtime_transition_cli.exe generated." -ForegroundColor Green
} else {
    Write-Host "CLI compilation failed. Check if gcc is in your PATH." -ForegroundColor Red
}

Write-Host "Compiling portable GGUF core diagnostic CLI..." -ForegroundColor Cyan

gcc -m64 -o "$PortableDir\gguf_portable_cli.exe" `
    "$PortableDir\gguf_portable_cli.c" `
    "$PortableDir\gguf_portable_stub.c" `
    -O3 -Wall

if ($LASTEXITCODE -eq 0) {
    Write-Host "Success! gguf_portable_cli.exe generated." -ForegroundColor Green
} else {
    Write-Host "CLI compilation failed. Check if gcc is in your PATH." -ForegroundColor Red
}
