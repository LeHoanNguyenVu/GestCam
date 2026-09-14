param (
    [string]$Task = "",
    [string]$Suite = "",
    [switch]$CheckLeaks
)

$ErrorActionPreference = "Stop"

Write-Host "================================================================================" -ForegroundColor Cyan
Write-Host "                    GESTCAM AUTOMATED TEST RUNNER                               " -ForegroundColor Cyan
Write-Host "================================================================================" -ForegroundColor Cyan

# 1. Tu dong bo sung CMake & Compiler vao PATH
$cmake_locations = @(
    "C:\Program Files\CMake\bin",
    "C:\Program Files (x86)\CMake\bin",
    "$env:LOCALAPPDATA\Programs\CMake\bin"
)

foreach ($loc in $cmake_locations) {
    if (Test-Path "$loc\cmake.exe") {
        if ($env:PATH -notlike "*$loc*") {
            $env:PATH = "$loc;$env:PATH"
            Write-Host "[CONFIG] Found CMake at: $loc" -ForegroundColor DarkGray
        }
        break
    }
}

$msys_gcc = "C:\Program Files\msys64\ucrt64\bin"
if (Test-Path "$msys_gcc\g++.exe") {
    if ($env:PATH -notlike "*$msys_gcc*") {
        $env:PATH = "$msys_gcc;$env:PATH"
        Write-Host "[CONFIG] Auto-detected GCC 13.2 UCRT64: $msys_gcc" -ForegroundColor DarkGray
    }
}

$cmakeCmd = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmakeCmd) {
    Write-Host "[ERROR] CMake not found in system PATH!" -ForegroundColor Red
    exit 1
}

$compilerCmd = Get-Command g++ -ErrorAction SilentlyContinue
if (-not $compilerCmd) {
    $compilerCmd = Get-Command cl -ErrorAction SilentlyContinue
}
if (-not $compilerCmd) {
    Write-Host "[ERROR] C++ compiler (g++ or cl.exe) not found!" -ForegroundColor Red
    exit 1
}

$cmakeVer = cmake --version | Select-Object -First 1
Write-Host "[INFO] CMake: $cmakeVer" -ForegroundColor Gray
Write-Host "[INFO] Compiler: $($compilerCmd.Source)" -ForegroundColor Gray

# 2. Configure CMake Project
$buildDir = "build"
Write-Host "`n[STEP 1/3] Configuring CMake project..." -ForegroundColor Yellow

$cmakeGenerator = ""
if ($compilerCmd.Name -eq "g++.exe") {
    $makeCmd = Get-Command mingw32-make -ErrorAction SilentlyContinue
    if ($makeCmd) {
        $cmakeGenerator = "-G `"MinGW Makefiles`""
    }
}

$configCmd = "cmake -B $buildDir $cmakeGenerator"
Write-Host "[EXEC] $configCmd" -ForegroundColor DarkGray
Invoke-Expression $configCmd
if ($LASTEXITCODE -ne 0) {
    Write-Host "[FAIL] CMake configuration failed!" -ForegroundColor Red
    exit $LASTEXITCODE
}

# 3. Build project
Write-Host "`n[STEP 2/3] Building project (C++20, AVX2)..." -ForegroundColor Yellow
$buildCmd = "cmake --build $buildDir --config Release -j 4"
Write-Host "[EXEC] $buildCmd" -ForegroundColor DarkGray
Invoke-Expression $buildCmd
if ($LASTEXITCODE -ne 0) {
    Write-Host "[FAIL] Compilation failed!" -ForegroundColor Red
    exit $LASTEXITCODE
}

# 4. Run automated tests (CTest)
Write-Host "`n[STEP 3/3] Running automated test suite..." -ForegroundColor Yellow

$ctestFilter = ""
if ($Task -ne "") {
    $ctestFilter = "-R `"$Task`""
} elseif ($Suite -ne "") {
    $ctestFilter = "-R `"$Suite`""
}

$testCmd = "ctest --test-dir $buildDir --output-on-failure $ctestFilter"
Write-Host "[EXEC] $testCmd" -ForegroundColor DarkGray
Invoke-Expression $testCmd
$testResult = $LASTEXITCODE

Write-Host "`n================================================================================" -ForegroundColor Cyan
if ($testResult -eq 0) {
    Write-Host "              ALL TEST SUITES PASSED - 100% QUALITY GATE VERIFIED               " -ForegroundColor Green
} else {
    Write-Host "              SOME TESTS FAILED - PLEASE CHECK LOGS ABOVE                       " -ForegroundColor Red
}
Write-Host "================================================================================" -ForegroundColor Cyan

exit $testResult
