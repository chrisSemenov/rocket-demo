# Fails if the committed traces differ from what the generator produces.
#
#   powershell -ExecutionPolicy Bypass -File tools\check-traces.ps1
#
# The generator is run in a throwaway directory rather than in place, so a
# failing check never leaves the working tree modified. Content is compared
# after normalising line endings: the generator writes LF and .gitattributes
# pins the same, but a checkout made before that attribute existed can still
# carry CRLF, and that is not a content difference.

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$scratch = Join-Path ([System.IO.Path]::GetTempPath()) ("trace-check-" + [guid]::NewGuid())

function Read-Normalised {
    param([string]$Path)

    $bytes = [System.IO.File]::ReadAllBytes($Path)

    return [System.Text.Encoding]::UTF8.GetString($bytes) -replace "`r`n", "`n"
}

function Get-TraceFiles {
    param([string]$Root)

    $files = @()

    foreach ($csv in Get-ChildItem (Join-Path $Root 'traces') -Filter '*.csv' -ErrorAction SilentlyContinue) {
        $files += [pscustomobject]@{ Name = "traces/$($csv.Name)"; Path = $csv.FullName }
    }

    $header = Join-Path $Root 'src\traces_data.hpp'

    if (Test-Path -LiteralPath $header) {
        $files += [pscustomobject]@{ Name = 'src/traces_data.hpp'; Path = $header }
    }

    return $files
}

try {
    New-Item -ItemType Directory -Force -Path $scratch | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $scratch 'tools') | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $scratch 'src') | Out-Null

    Copy-Item (Join-Path $PSScriptRoot 'make-traces.py') (Join-Path $scratch 'tools\make-traces.py')

    & python (Join-Path $scratch 'tools\make-traces.py') | Out-Null

    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAIL the generator exited with code $LASTEXITCODE" -ForegroundColor Red
        exit 1
    }

    $committed = @(Get-TraceFiles -Root $root)
    $generated = @(Get-TraceFiles -Root $scratch)

    $problems = @()
    $crlf = @()

    foreach ($item in $generated) {
        $committedItem = $committed | Where-Object { $_.Name -eq $item.Name } | Select-Object -First 1

        if ($null -eq $committedItem) {
            $problems += "missing from the repository: $($item.Name)"
            continue
        }

        if ((Read-Normalised $committedItem.Path) -ne (Read-Normalised $item.Path)) {
            $problems += "differs from the generator: $($item.Name)"
        }

        if (([System.IO.File]::ReadAllText($committedItem.Path)) -match "`r`n") {
            $crlf += $committedItem.Name
        }
    }

    foreach ($item in $committed) {
        if (-not ($generated | Where-Object { $_.Name -eq $item.Name })) {
            $problems += "not produced by the generator: $($item.Name)"
        }
    }

    if ($problems.Count -ne 0) {
        Write-Host 'FAIL traces are out of date:' -ForegroundColor Red

        foreach ($problem in $problems) {
            Write-Host "     $problem"
        }

        Write-Host 'Run tools\make-traces.py and commit the regenerated files.'
        exit 1
    }

    if ($crlf.Count -ne 0) {
        Write-Host 'NOTE these files carry CRLF in the working tree; .gitattributes pins LF:' -ForegroundColor Yellow

        foreach ($name in $crlf) {
            Write-Host "     $name"
        }

        Write-Host 'Fix with: git add --renormalize .'
    }

    Write-Host "PASS $($generated.Count) generated files match the generator"
    exit 0
}
finally {
    if (Test-Path -LiteralPath $scratch) {
        Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
    }
}
