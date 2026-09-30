$ErrorActionPreference = 'Stop'
$launcher = Join-Path $PSScriptRoot 'flutter-safe.ps1'
if (-not (Test-Path -LiteralPath $launcher)) {
    throw "Flutter launcher not found: $launcher"
}
& $launcher run -d windows
exit $LASTEXITCODE
