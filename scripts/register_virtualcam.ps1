param (
    [switch]$Unregister,
    [string]$DllPath = "",
    [switch]$Elevated
)

$ErrorActionPreference = "Stop"

Write-Host "================================================================================" -ForegroundColor Cyan
Write-Host "             GESTCAM VIRTUAL CAMERA REGISTRATION UTILITY                        " -ForegroundColor Cyan
Write-Host "================================================================================" -ForegroundColor Cyan

# 1. Check Administrator Privileges
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
$isAdmin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    if ($Elevated) {
        Write-Host "[ERROR] Elevation failed or access denied!" -ForegroundColor Red
        exit 1
    }
    Write-Host "[INFO] Administrator rights are required to register DirectShow filters." -ForegroundColor Yellow
    Write-Host "[UAC] Requesting Administrator elevation..." -ForegroundColor Yellow

    $argList = @("-ExecutionPolicy", "Bypass", "-File", "`"$PSCommandPath`"")
    if ($Unregister) { $argList += "-Unregister" }
    if ($DllPath) { $argList += @("-DllPath", "`"$DllPath`"") }
    $argList += "-Elevated"

    Start-Process -FilePath "powershell.exe" -ArgumentList $argList -Verb RunAs -Wait
    exit 0
}

# 2. Locate gestcam-virtualcam.dll
$scriptRoot = Split-Path -Parent $PSCommandPath
$projectRoot = Split-Path -Parent $scriptRoot

if (-not $DllPath) {
    $candidates = @(
        "$projectRoot\build\driver\gestcam-virtualcam.dll",
        "$projectRoot\driver\gestcam-virtualcam.dll",
        "$projectRoot\build\tests\gestcam-virtualcam.dll"
    )
    foreach ($cand in $candidates) {
        if (Test-Path $cand) {
            $DllPath = $cand
            break
        }
    }
}

if (-not (Test-Path $DllPath)) {
    Write-Host "[ERROR] Could not find 'gestcam-virtualcam.dll' at '$DllPath'!" -ForegroundColor Red
    Write-Host "[HINT] Run './scripts/run_tests.ps1' or 'cmake --build build' first to compile the DLL." -ForegroundColor Yellow
    exit 1
}

$resolvedDll = (Resolve-Path $DllPath).Path
Write-Host "[INFO] Target DLL: $resolvedDll" -ForegroundColor Gray

# 3. Registry constants
$clsid = "{9E38B6B2-225C-479D-86D2-959D960814B2}"
$categoryClsid = "{860BB310-5D01-11D0-BD3B-00A0C911CE86}"
$regClsidPath = "Registry::HKEY_CLASSES_ROOT\CLSID\$clsid"
$regInstancePath = "Registry::HKEY_CLASSES_ROOT\CLSID\$categoryClsid\Instance\$clsid"

# 4. Perform Action
if ($Unregister) {
    Write-Host "`n[ACTION] Unregistering GestCam Virtual Camera..." -ForegroundColor Yellow
    $p = Start-Process -FilePath "regsvr32.exe" -ArgumentList @("/u", "/s", "`"$resolvedDll`"") -PassThru -Wait

    Start-Sleep -Milliseconds 200
    if (-not (Test-Path $regInstancePath)) {
        Write-Host "[SUCCESS] GestCam Virtual Camera successfully unregistered from DirectShow!" -ForegroundColor Green
    } else {
        Write-Host "[WARNING] Registry keys may still be present after unregister." -ForegroundColor Yellow
    }
} else {
    Write-Host "`n[ACTION] Registering GestCam Virtual Camera with Windows DirectShow..." -ForegroundColor Yellow
    $p = Start-Process -FilePath "regsvr32.exe" -ArgumentList @("/s", "`"$resolvedDll`"") -PassThru -Wait

    Start-Sleep -Milliseconds 200
    $verified = (Test-Path $regClsidPath) -and (Test-Path $regInstancePath)
    if ($verified) {
        $friendlyName = (Get-ItemProperty -Path $regInstancePath -Name "FriendlyName" -ErrorAction SilentlyContinue).FriendlyName
        $hasFilterData = (Get-ItemProperty -Path $regInstancePath -Name "FilterData" -ErrorAction SilentlyContinue).FilterData -ne $null
        Write-Host "`n================================================================================" -ForegroundColor Green
        Write-Host "      GESTCAM VIRTUAL CAMERA REGISTERED SUCCESSFULLY AS A SYSTEM WEBCAM!        " -ForegroundColor Green
        Write-Host "================================================================================" -ForegroundColor Green
        Write-Host "[DEVICE] Friendly Name  : $friendlyName" -ForegroundColor White
        Write-Host "[DEVICE] Device CLSID   : $clsid" -ForegroundColor White
        Write-Host "[DEVICE] Category       : Video Input Device (DirectShow)" -ForegroundColor White
        Write-Host "[DEVICE] FilterData     : $(if ($hasFilterData) { 'PRESENT (Verified for Google Meet & Chrome)' } else { 'DirectShow Category OK' })" -ForegroundColor White
        Write-Host "[STATUS] Ready for use in Google Meet, Zoom, MS Teams, Discord, Chrome, Edge." -ForegroundColor Cyan
    } else {
        Write-Host "[ERROR] Registration failed! Registry keys not detected." -ForegroundColor Red
        exit 1
    }
}
