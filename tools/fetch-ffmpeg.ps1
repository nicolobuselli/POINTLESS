# Downloads a static Windows ffmpeg.exe into tools/ (not committed to git —
# see .gitignore). Used by CI before build, and can be run locally for the
# same reason: video import/export needs tools/ffmpeg.exe present.
$ErrorActionPreference = "Stop"

$dest = Join-Path $PSScriptRoot "ffmpeg.exe"
if (Test-Path $dest) {
    Write-Host "tools/ffmpeg.exe already present, skipping download."
    exit 0
}

$zipUrl = "https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip"
$zipPath = Join-Path $env:TEMP "ffmpeg-essentials.zip"
$extractDir = Join-Path $env:TEMP "ffmpeg-essentials-extract"

Write-Host "Downloading ffmpeg from $zipUrl ..."
Invoke-WebRequest -Uri $zipUrl -OutFile $zipPath

if (Test-Path $extractDir) { Remove-Item -Recurse -Force $extractDir }
Expand-Archive -Path $zipPath -DestinationPath $extractDir

$exe = Get-ChildItem -Path $extractDir -Recurse -Filter "ffmpeg.exe" | Select-Object -First 1
if (-not $exe) {
    throw "ffmpeg.exe not found inside downloaded archive."
}
Copy-Item $exe.FullName $dest -Force

Remove-Item -Recurse -Force $extractDir -ErrorAction SilentlyContinue
Remove-Item $zipPath -ErrorAction SilentlyContinue
Write-Host "tools/ffmpeg.exe ready."
