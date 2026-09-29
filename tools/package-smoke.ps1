param([Parameter(Mandatory=$true)][string]$PackageDir)
$ErrorActionPreference = 'Stop'
$packagePath = (Resolve-Path -LiteralPath $PackageDir).Path
foreach ($file in @('POINTLESS.exe','ffmpeg.exe','Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','platforms/qwindows.dll','LICENSE','THIRD_PARTY_LICENSES.txt','licenses/Funnel-OFL.txt')) {
    if (-not (Test-Path -LiteralPath (Join-Path $packagePath $file))) { throw "Missing package file: $file" }
}
$smokeOutput = Join-Path $packagePath 'smoke-result.txt'
$oldPath = $env:PATH
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
try {
$process = Start-Process -FilePath (Join-Path $packagePath 'POINTLESS.exe') -ArgumentList @('--smoke-test') -WorkingDirectory $packagePath -WindowStyle Hidden -PassThru
if (-not $process.WaitForExit(30000)) { Stop-Process -Id $process.Id -Force; throw 'Application smoke test timed out.' }
if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $smokeOutput)) { throw 'Application smoke test failed.' }
} finally { $env:PATH = $oldPath }
Write-Host (Get-Content -LiteralPath $smokeOutput)
Remove-Item -LiteralPath $smokeOutput
