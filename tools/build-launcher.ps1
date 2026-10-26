# Builds POPSoT Launcher (launcher/, Qt 6 Widgets) into bin\popsot-launcher.exe
# and puts the Qt runtime beside it with windeployqt.
#
#   .\tools\build-launcher.ps1                      # Qt from D:\Qt\6.8.3\msvc2022_64
#   .\tools\build-launcher.ps1 -QtDir C:\Qt\6.8.3\msvc2022_64
#
# The icon is the PC version's when bin\pop.ico exists:
#   python tools\extract-icon.py "<PC game folder>\POP.EXE" bin\pop.ico
param(
    [string]$QtDir = $(if ($env:POPSOT_QT_DIR) { $env:POPSOT_QT_DIR } else { 'D:\Qt\6.8.3\msvc2022_64' })
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path "$QtDir\bin\windeployqt.exe")) { throw "Qt 6 (MSVC 64 bit) not found in $QtDir; pass -QtDir" }
$build = Join-Path $root 'build\launcher'
python "$root\tools\run-build-tool.py" cmake -S "$root\launcher" -B $build -A x64 "-DCMAKE_PREFIX_PATH=$QtDir"
if ($LASTEXITCODE) { throw 'configure failed' }
python "$root\tools\run-build-tool.py" cmake --build $build --config Release --parallel
if ($LASTEXITCODE) { throw 'build failed' }
# windeployqt notes a missing translation catalogue on stderr; not an error
$ErrorActionPreference = 'Continue'
& "$QtDir\bin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw --no-compiler-runtime "$root\bin\popsot-launcher.exe" 2>$null | Out-Null
if ($LASTEXITCODE) { throw 'windeployqt failed' }
"Built $root\bin\popsot-launcher.exe"
