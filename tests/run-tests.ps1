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

$exe = Join-Path $outDir 'altitude_test.exe'

& $compiler.Source -std=c++17 -O2 -Wall -Wextra -Werror `
    -o $exe `
    (Join-Path $PSScriptRoot 'altitude_test.cpp') `
    (Join-Path $root 'src\altitude.cpp')

if ($LASTEXITCODE -ne 0) {
    throw "Compilation failed (exit $LASTEXITCODE)"
}

& $exe
exit $LASTEXITCODE
