# Builds and runs the host-side tests. Needs no board and no Zephyr.
#
#   powershell -ExecutionPolicy Bypass -File tests\run-tests.ps1
#
# Exit code is 0 only when every check passes.

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$compiler = (Get-Command g++ -ErrorAction SilentlyContinue)
if (-not $compiler) {
    throw 'g++ not found on PATH. Install a host C++ compiler or add it to PATH.'
}

$suites = @(
    @{ Name = 'altitude'; Test = 'altitude_test.cpp'; Sources = @('src\altitude.cpp') },
    @{ Name = 'led'; Test = 'led_logic_test.cpp'; Sources = @('src\led_logic.cpp') },
    @{ Name = 'apogee'; Test = 'apogee_test.cpp'; Sources = @('src\apogee.cpp') }
)

$failed = 0

foreach ($suite in $suites) {
    $exe = Join-Path $outDir ("{0}_test.exe" -f $suite.Name)
    $sources = @($suite.Sources | ForEach-Object { Join-Path $root $_ })
    $testFile = Join-Path $PSScriptRoot $suite.Test

    Write-Host ("=== {0} ===" -f $suite.Test)

    & $compiler.Source -std=c++17 -O2 -Wall -Wextra -Werror -o $exe $testFile $sources
    if ($LASTEXITCODE -ne 0) {
        throw ("Compilation failed: {0} (exit {1})" -f $suite.Test, $LASTEXITCODE)
    }

    & $exe
    if ($LASTEXITCODE -ne 0) {
        $failed = 1
    }
}

exit $failed
