$ErrorActionPreference = 'Stop'
$app = Split-Path -Parent $PSScriptRoot
if ((Test-Path -LiteralPath (Join-Path $app 'windows/CMakeLists.txt')) -and
    (Test-Path -LiteralPath (Join-Path $app 'android/app/src/main/AndroidManifest.xml'))) {
    Write-Host 'Windows and Android platform files already exist. Skipping generation.'
    return
}
throw 'Platform files are missing. Restore them from the project backup; do not run flutter create with --overwrite.'
