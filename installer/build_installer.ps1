<#
.SYNOPSIS
    Builds the Blobset Tools installer (installer\output\BlobsetTools-Setup-<version>.exe).

.DESCRIPTION
    1. Gathers the release build into installer\stage: Blobset-Tools.exe, the third-party DLLs (lib\*.dll), the MinGW
       runtime, liblzma / zstd, and the Qt runtime + QML modules (windeployqt).
    2. Adds the read-only files: GameSelectionInfo.xml (the full list; setup writes the picked games), assets\games,
       defaults, vortex, modloader, docs and ReadMe.md.
    3. Compiles installer\BlobsetTools.iss with Inno Setup (the version comes from CMakeLists.txt).

    Build the Release configuration in Qt Creator first. Run from anywhere:
        powershell -ExecutionPolicy Bypass -File installer\build_installer.ps1

.PARAMETER BuildDir
    The Release build folder (default build\Desktop_Qt_6_11_2_MinGW_64_bit_Release).
.PARAMETER QtBin
    Qt's bin folder, with windeployqt (default D:\Qt\6.11.2\mingw_64\bin).
.PARAMETER Iscc
    Inno Setup's compiler (default: the usual Inno Setup 6 folders).
#>
param(
    [string]$BuildDir = "",
    [string]$QtBin = "D:\Qt\6.11.2\mingw_64\bin",
    [string]$Iscc = ""
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Join-Path $Root "build\Desktop_Qt_6_11_2_MinGW_64_bit_Release" }
$Stage = Join-Path $PSScriptRoot "stage"

function Fail($message) { Write-Host "ERROR: $message" -ForegroundColor Red; exit 1 }
function Step($message) { Write-Host "==> $message" -ForegroundColor Cyan }

$Exe = Join-Path $BuildDir "Blobset-Tools.exe"
if (-not (Test-Path $Exe)) { Fail "$Exe not found - build the Release configuration first (or pass -BuildDir)." }
$WinDeployQt = Join-Path $QtBin "windeployqt.exe"
if (-not (Test-Path $WinDeployQt)) { Fail "$WinDeployQt not found - pass -QtBin <Qt>\mingw_64\bin." }
if (-not $Iscc) {
    $Iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
              "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $Iscc -or -not (Test-Path $Iscc)) { Fail "Inno Setup 6 not found - install it from https://jrsoftware.org/isdl.php (or pass -Iscc)." }

# Version: project(Blobset-Tools VERSION x.y.z.w ...) in CMakeLists.txt.
$cmake = Get-Content (Join-Path $Root "CMakeLists.txt") -Raw
if ($cmake -match 'project\s*\(\s*[\w-]+\s+VERSION\s+([\d.]+)') { $Version = $Matches[1] } else { $Version = "0.0.0.0" }
Write-Host "Blobset Tools $Version"

Step "Staging the build in $Stage"
if (Test-Path $Stage) { Remove-Item $Stage -Recurse -Force }
New-Item -ItemType Directory -Path $Stage | Out-Null
Copy-Item $Exe $Stage
$manifest = "$Exe.manifest"
if (Test-Path $manifest) { Copy-Item $manifest $Stage }

# Third-party DLLs: the ones in lib (the FFmpeg / vgmstream codecs the exe links), not stale copies in the build folder.
Get-ChildItem (Join-Path $Root "lib") -Filter *.dll | ForEach-Object { Copy-Item $_.FullName $Stage }
# MinGW runtime, OpenMP, liblzma and zstd: from the build folder (where the exe runs from), else Qt's MinGW.
$mingwBin = Get-ChildItem (Join-Path (Split-Path (Split-Path (Split-Path $QtBin))) "Tools") -Directory -Filter "mingw*_64" -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending | Select-Object -First 1 | ForEach-Object { Join-Path $_.FullName "bin" }
foreach ($dll in "libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll", "libgomp-1.dll", "liblzma-5.dll", "libzstd.dll") {
    $from = @((Join-Path $BuildDir $dll), (Join-Path $QtBin $dll)) + $(if ($mingwBin) { @(Join-Path $mingwBin $dll) } else { @() }) |
            Where-Object { Test-Path $_ } | Select-Object -First 1
    if ($from) { Copy-Item $from $Stage } else { Write-Host "   note: $dll not found (fine if the exe doesn't use it)" -ForegroundColor Yellow }
}

Step "windeployqt (Qt runtime and QML modules)"
$env:PATH = "$QtBin;$env:PATH"
& $WinDeployQt --release --qmldir (Join-Path $Root "qml") --no-translations --no-compiler-runtime (Join-Path $Stage "Blobset-Tools.exe")
if ($LASTEXITCODE -ne 0) { Fail "windeployqt failed ($LASTEXITCODE)." }

Step "Read-only data (game list, pictures, defaults, Vortex extensions, mod loader, docs)"
Copy-Item (Join-Path $Root "GameSelectionInfo.xml") $Stage
New-Item -ItemType Directory -Path (Join-Path $Stage "assets") | Out-Null
Copy-Item (Join-Path $Root "assets\games") (Join-Path $Stage "assets\games") -Recurse
foreach ($folder in "defaults", "vortex", "docs") {
    $from = Join-Path $Root $folder
    if (Test-Path $from) { Copy-Item $from (Join-Path $Stage $folder) -Recurse } else { Write-Host "   note: no $folder folder" -ForegroundColor Yellow }
}
# The mod loader is made by the build (or kept in the source folder).
$modloader = @((Join-Path $BuildDir "modloader"), (Join-Path $Root "modloader")) | Where-Object { Test-Path $_ } | Select-Object -First 1
if ($modloader) { Copy-Item $modloader (Join-Path $Stage "modloader") -Recurse } else { Write-Host "   note: no modloader folder" -ForegroundColor Yellow }
foreach ($text in "ReadMe.md", "changelog.txt", "LICENSE.txt") {
    if (Test-Path (Join-Path $Root $text)) { Copy-Item (Join-Path $Root $text) $Stage }
}

# Nothing writable belongs in the install folder (it lives in Documents\Blobset Tools).
foreach ($writable in "Settings.ini", "games", "cli.log", "cli.lock", "portable.txt") {
    $path = Join-Path $Stage $writable
    if (Test-Path $path) { Remove-Item $path -Recurse -Force }
}

# Any DLL the exe imports that isn't staged (and isn't part of Windows) would stop it from starting.
$objdump = @((Join-Path $QtBin "objdump.exe")) + $(if ($mingwBin) { @(Join-Path $mingwBin "objdump.exe") } else { @() }) |
           Where-Object { Test-Path $_ } | Select-Object -First 1
if ($objdump) {
    $staged = @{}; Get-ChildItem $Stage -Filter *.dll | ForEach-Object { $staged[$_.Name.ToLower()] = $true }
    $missing = @()
    foreach ($binary in @(Join-Path $Stage "Blobset-Tools.exe") + (Get-ChildItem $Stage -Filter *.dll | ForEach-Object FullName)) {
        foreach ($line in (& $objdump -p $binary 2>$null | Select-String 'DLL Name: (.+)$')) {
            $name = $line.Matches[0].Groups[1].Value.Trim().ToLower()
            if (-not $staged[$name] -and -not (Test-Path (Join-Path "$env:SystemRoot\System32" $name)) -and $name -notlike "api-ms-*" -and $name -notlike "ext-ms-*") {
                $missing += "$name (needed by $(Split-Path $binary -Leaf))"
            }
        }
    }
    if ($missing) { Fail "Missing DLLs:`n  $(($missing | Sort-Object -Unique) -join "`n  ")" }
    Write-Host "   every imported DLL is staged"
}

Step "Inno Setup"
& $Iscc "/DAppVersion=$Version" "/DStageDir=$Stage" "/DSourceDir=$Root" (Join-Path $PSScriptRoot "BlobsetTools.iss")
if ($LASTEXITCODE -ne 0) { Fail "ISCC failed ($LASTEXITCODE)." }
Write-Host ""
Write-Host "Done: $(Join-Path $PSScriptRoot "output\BlobsetTools-Setup-$Version.exe")" -ForegroundColor Green
