param([string]$BuildDir = 'build/Release', [string]$Destination = 'build/package')
$ErrorActionPreference = 'Stop'
$sourcePath = (Resolve-Path -LiteralPath $BuildDir).Path
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
$targetPath = (Resolve-Path -LiteralPath $Destination).Path
if ($targetPath -eq $sourcePath) { throw 'Package output must differ from build directory.' }
Get-ChildItem -LiteralPath $sourcePath | Where-Object {
    if ($_.PSIsContainer) { $_.Name -in @('platforms','imageformats','iconengines','networkinformation','styles','tls','generic','licenses','translations') }
    else { $_.Extension -eq '.dll' -or $_.Name -in @('POINTLESS.exe','ffmpeg.exe','LICENSE','THIRD_PARTY_LICENSES.txt','qt.conf','vc_redist.x64.exe') }
} | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $targetPath -Recurse -Force }
$files = Get-ChildItem -LiteralPath $targetPath -Recurse -File | Where-Object Name -ne manifest.json | Sort-Object FullName | ForEach-Object {
    [ordered]@{ path = $_.FullName.Substring($targetPath.Length + 1).Replace('\','/'); bytes = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
}
@{ files = @($files) } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $targetPath 'manifest.json') -Encoding utf8
