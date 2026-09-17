param (
    [switch]$Elevated
)

$ErrorActionPreference = "Stop"

# Check Administrator Privileges
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
$isAdmin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    Write-Host "[UAC] Requesting Administrator elevation..." -ForegroundColor Yellow
    $argList = @("-ExecutionPolicy", "Bypass", "-File", "`"$PSCommandPath`"", "-Elevated")
    Start-Process -FilePath "powershell.exe" -ArgumentList $argList -Verb RunAs -Wait
    exit 0
}

Write-Host "================================================================================" -ForegroundColor Cyan
Write-Host "         GESTCAM VIRTUAL CAMERA - MEDIA FOUNDATION FIX                         " -ForegroundColor Cyan
Write-Host "================================================================================" -ForegroundColor Cyan

# ============================================================
# Step 1: Enable FrameServer Mode (Critical for Chrome/Edge)
# ============================================================
Write-Host "`n[STEP 1] Enabling Windows Media Foundation FrameServer Bridge..." -ForegroundColor Yellow
Write-Host "         (Required for Chrome, Edge, Google Meet to detect virtual cameras)" -ForegroundColor Gray

reg add "HKLM\SOFTWARE\Microsoft\Windows Media Foundation\Platform" /v EnableFrameServerMode /t REG_DWORD /d 1 /f
reg add "HKLM\SOFTWARE\WOW6432Node\Microsoft\Windows Media Foundation\Platform" /v EnableFrameServerMode /t REG_DWORD /d 1 /f

$val1 = (Get-ItemProperty -Path "HKLM:\SOFTWARE\Microsoft\Windows Media Foundation\Platform" -Name EnableFrameServerMode -ErrorAction SilentlyContinue).EnableFrameServerMode
$val2 = (Get-ItemProperty -Path "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows Media Foundation\Platform" -Name EnableFrameServerMode -ErrorAction SilentlyContinue).EnableFrameServerMode

if ($val1 -eq 1 -and $val2 -eq 1) {
    Write-Host "[OK] EnableFrameServerMode = 1 (both 64-bit and 32-bit registry hives)" -ForegroundColor Green
} else {
    Write-Host "[WARNING] One or both keys may not have been set correctly." -ForegroundColor Red
    Write-Host "  64-bit: $val1  |  32-bit (WOW): $val2" -ForegroundColor Red
}

# ============================================================
# Step 2: Re-register the DLL (fresh registration)
# ============================================================
Write-Host "`n[STEP 2] Re-registering GestCam Virtual Camera DLL..." -ForegroundColor Yellow

$scriptRoot = Split-Path -Parent $PSCommandPath
$projectRoot = Split-Path -Parent $scriptRoot
$dllPath = "$projectRoot\build\driver\gestcam-virtualcam.dll"

if (-not (Test-Path $dllPath)) {
    Write-Host "[ERROR] DLL not found at: $dllPath" -ForegroundColor Red
    Write-Host "[HINT] Build first by running this in MSYS2 bash:" -ForegroundColor Yellow
    Write-Host "       bash '$projectRoot\scripts\build_driver.sh'" -ForegroundColor White
    Read-Host "Press Enter to exit"
    exit 1
}

# Verify DLL is x64
$dllBytes = [System.IO.File]::ReadAllBytes($dllPath)
$peOffset = [System.BitConverter]::ToInt32($dllBytes, 0x3C)
$machine = [System.BitConverter]::ToInt16($dllBytes, $peOffset + 4)
$arch = if ($machine -eq 0x8664) { "x64 (OK)" } elseif ($machine -eq 0x014c) { "x86 (WARNING: will not work with 64-bit browsers!)" } else { "Unknown: $([string]::Format('{0:X4}', $machine))" }
Write-Host "  DLL architecture: $arch" -ForegroundColor $(if ($machine -eq 0x8664) { 'Green' } else { 'Red' })

# Unregister first (ignore errors)
Start-Process -FilePath "regsvr32.exe" -ArgumentList @("/u", "/s", "`"$dllPath`"") -PassThru -Wait | Out-Null
Start-Sleep -Milliseconds 500

# Register again
$p = Start-Process -FilePath "regsvr32.exe" -ArgumentList @("/s", "`"$dllPath`"") -PassThru -Wait

if ($p.ExitCode -eq 0) {
    Write-Host "[OK] DLL registered successfully: $dllPath" -ForegroundColor Green
} else {
    Write-Host "[ERROR] regsvr32 returned exit code: $($p.ExitCode)" -ForegroundColor Red
    Read-Host "Press Enter to exit"
    exit 1
}

# ============================================================
# Step 3: Verify Registry
# ============================================================
Write-Host "`n[STEP 3] Verifying registry entries..." -ForegroundColor Yellow

$clsid = "{9E38B6B2-225C-479D-86D2-959D960814B2}"
$categoryClsid = "{860BB310-5D01-11D0-BD3B-00A0C911CE86}"
$regClsidPath = "HKCR:\CLSID\$clsid"
$regInstancePath = "HKCR:\CLSID\$categoryClsid\Instance\$clsid"

$hasClsid = Test-Path $regClsidPath
$hasInstance = Test-Path $regInstancePath
$filterData = (Get-ItemProperty -Path $regInstancePath -Name "FilterData" -ErrorAction SilentlyContinue).FilterData
$hasFilterData = $filterData -ne $null

Write-Host "  CLSID key:      $(if ($hasClsid) { 'PRESENT' } else { 'MISSING!' })" -ForegroundColor $(if ($hasClsid) { 'Green' } else { 'Red' })
Write-Host "  Instance key:   $(if ($hasInstance) { 'PRESENT' } else { 'MISSING!' })" -ForegroundColor $(if ($hasInstance) { 'Green' } else { 'Red' })
Write-Host "  FilterData:     $(if ($hasFilterData) { 'PRESENT (Chrome/Edge compatible)' } else { 'MISSING! Camera will not appear in browsers.' })" -ForegroundColor $(if ($hasFilterData) { 'Green' } else { 'Red' })

# ============================================================
# Step 4: Windows Camera Privacy Check
# ============================================================
Write-Host "`n[STEP 4] Checking Windows Camera Privacy Settings..." -ForegroundColor Yellow
$privacyKey = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\webcam"
$privacyVal = (Get-ItemProperty -Path $privacyKey -Name "Value" -ErrorAction SilentlyContinue).Value
Write-Host "  System-wide camera access: $(if ($privacyVal -ne $null) { $privacyVal } else { 'Key not found (default = Allow)' })" -ForegroundColor White

# ============================================================
# Summary
# ============================================================
Write-Host "`n================================================================================" -ForegroundColor Cyan
Write-Host " NEXT STEPS TO TEST:" -ForegroundColor Cyan
Write-Host "================================================================================" -ForegroundColor Cyan
Write-Host " 1. RESTART your computer (EnableFrameServerMode requires a full reboot)" -ForegroundColor Yellow
Write-Host " 2. After reboot, run: D:\GestCam\build\gestcam_core.exe" -ForegroundColor White
Write-Host " 3. Open Google Meet in Chrome or Edge" -ForegroundColor White
Write-Host " 4. Check camera selector - 'GestCam Virtual Camera' should appear" -ForegroundColor White
Write-Host "`n If camera still does NOT appear after reboot:" -ForegroundColor Gray
Write-Host "   - Open Windows Settings > Privacy > Camera and ensure apps have camera access" -ForegroundColor Gray
Write-Host "   - Try: chrome.exe --use-fake-device-for-media-stream to test MF stack" -ForegroundColor Gray
Write-Host "================================================================================" -ForegroundColor Cyan

Read-Host "`nPress Enter to close"
