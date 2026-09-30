# Remaining arguments are passed verbatim as an array, never evaluated as code.
$ErrorActionPreference = 'Stop'
$flutterArgs = @($args)
if ($flutterArgs.Count -eq 0) { throw 'Example: .\tooling\flutter-safe.ps1 run -d windows' }
$app = Split-Path -Parent $PSScriptRoot
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $app)).TrimEnd('\')
$drive = 'O:'
$mappedRoot = $null
foreach ($entry in (& subst.exe)) {
    if ($entry -match '^O:\\:\s*=>\s*(.+)$') { $mappedRoot = $Matches[1].TrimEnd('\') }
}
if ($projectRoot -eq $drive -and $mappedRoot) { $projectRoot = $mappedRoot }
if ($mappedRoot) {
    if (-not [String]::Equals($mappedRoot, $projectRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "O: already maps to $mappedRoot. Refusing to replace another mapping."
    }
} elseif (Test-Path 'O:\') {
    throw 'O: is already occupied by another drive. Choose a different unused drive in this script.'
} else {
    & subst.exe $drive $projectRoot
    if ($LASTEXITCODE -ne 0) { throw 'Could not map the project to O:.' }
}
if (-not (Test-Path 'O:\app\pubspec.yaml')) { throw 'Mapped app/pubspec.yaml was not found.' }
$flutter = Get-Command flutter.bat -ErrorAction SilentlyContinue
if (-not $flutter) {
    # Pick up a recently installed SDK without requiring this host to restart.
    $registeredPaths = [Environment]::GetEnvironmentVariable('Path', 'User') + ';' + [Environment]::GetEnvironmentVariable('Path', 'Machine')
    foreach ($directory in ($registeredPaths -split ';')) {
        if (-not $directory) { continue }
        $candidate = Join-Path ([Environment]::ExpandEnvironmentVariables($directory)) 'flutter.bat'
        if (Test-Path -LiteralPath $candidate) { $flutter = Get-Item -LiteralPath $candidate; break }
    }
}
if (-not $flutter) { throw 'Flutter is not in PATH. Install Flutter and reopen PowerShell.' }
$executable = if ($flutter -is [IO.FileInfo]) { $flutter.FullName } else { $flutter.Source }
# Honor mirror settings the user added after this terminal was opened.
foreach ($setting in @('PUB_HOSTED_URL', 'FLUTTER_STORAGE_BASE_URL')) {
    if (-not [Environment]::GetEnvironmentVariable($setting, 'Process')) {
        $value = [Environment]::GetEnvironmentVariable($setting, 'User')
        if (-not $value) { $value = [Environment]::GetEnvironmentVariable($setting, 'Machine') }
        if ($value) { [Environment]::SetEnvironmentVariable($setting, $value, 'Process') }
    }
}
$nativeWorkingDirectory = [Environment]::CurrentDirectory
Push-Location 'O:\app'
try {
    [Environment]::CurrentDirectory = 'O:\app'
    Write-Host "Flutter path alias: O:\app (files stay inside $projectRoot\app)"
    & $executable @flutterArgs
    $result = $LASTEXITCODE
} finally {
    Pop-Location
    [Environment]::CurrentDirectory = $nativeWorkingDirectory
}
exit $result
