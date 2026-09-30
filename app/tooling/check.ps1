$ErrorActionPreference = 'Stop'
foreach ($step in @(@('pub', 'get'), @('analyze'), @('test'))) {
    & (Join-Path $PSScriptRoot 'flutter-safe.ps1') @step
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
