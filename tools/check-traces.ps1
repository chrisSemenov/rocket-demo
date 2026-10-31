# Fails if the committed traces differ from what the generator produces.
#
#   powershell -ExecutionPolicy Bypass -File tools\check-traces.ps1
#
# The generator writes traces/*.csv and src/traces_data.hpp. This script runs it,
# compares against the committed copies, and restores them if they changed, so a
# forgotten regeneration is reported rather than silently committed.

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$backup = Join-Path $env:TEMP ("trace-backup-" + [guid]::NewGuid())

New-Item -ItemType Directory -Force -Path $backup | Out-Null

$files = @(Get-ChildItem (Join-Path $root 'traces') -Filter '*.csv') +
         @(Get-Item (Join-Path $root 'src\traces_data.hpp'))

foreach ($file in $files) {
    Copy-Item $file.FullName (Join-Path $backup $file.Name)
}

& python (Join-Path $PSScriptRoot 'make-traces.py') | Out-Null

$changed = @()

foreach ($file in $files) {
    $original = Join-Path $backup $file.Name
    $current = $file.FullName

    if ((Get-FileHash $original).Hash -ne (Get-FileHash $current).Hash) {
        $changed += $file.Name
        Copy-Item $original $current -Force
    }
}

Remove-Item $backup -Recurse -Force

if ($changed.Count -ne 0) {
    Write-Host "FAIL traces are out of date:" -ForegroundColor Red

    foreach ($name in $changed) {
        Write-Host "     $name"
    }

    Write-Host "Run tools\make-traces.py and commit the regenerated files."
    exit 1
}

Write-Host "PASS traces match the generator"
exit 0
