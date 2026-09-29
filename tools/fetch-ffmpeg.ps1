# Reproducible dependency: reviewed Gyan essentials 8.1.1 binary.
$ErrorActionPreference = 'Stop'
$expected = '228D7A8556258DE907FDB55F36850078EBC7680B84EC30D84EA02E99BEC1D1EB'
$dest = Join-Path $PSScriptRoot 'ffmpeg.exe'
if (Test-Path -LiteralPath $dest) {
    if ((Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash -ne $expected) { throw 'Existing ffmpeg.exe does not match the pinned build.' }
    Write-Host 'Pinned ffmpeg 8.1.1 verified.'
    exit 0
}
$url = 'https://github.com/GyanD/codexffmpeg/releases/download/8.1.1/ffmpeg-8.1.1-essentials_build.zip'
$taskTemp = Join-Path ([IO.Path]::GetTempPath()) ('pointless-ffmpeg-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskTemp | Out-Null
try {
    $zipPath = Join-Path $taskTemp 'ffmpeg.zip'
    Invoke-WebRequest -Uri $url -OutFile $zipPath
    Expand-Archive -LiteralPath $zipPath -DestinationPath (Join-Path $taskTemp 'expanded')
    $exe = Get-ChildItem -LiteralPath (Join-Path $taskTemp 'expanded') -Recurse -Filter ffmpeg.exe | Select-Object -First 1
    if (-not $exe -or (Get-FileHash -LiteralPath $exe.FullName -Algorithm SHA256).Hash -ne $expected) { throw 'Downloaded ffmpeg does not match the pinned SHA-256.' }
    Copy-Item -LiteralPath $exe.FullName -Destination $dest
} finally {
    $resolved = [IO.Path]::GetFullPath($taskTemp)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if ($resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -and [IO.Path]::GetFileName($resolved).StartsWith('pointless-ffmpeg-')) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
