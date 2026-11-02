<#
.SYNOPSIS
    Stream the board's SEGGER RTT output over ST-Link/SWD (no USB-UART needed).

.DESCRIPTION
    Starts an OpenOCD session that bridges the firmware's RTT ring buffer in RAM
    to a TCP port on this PC. Leave this running, then read the stream from a
    second terminal with tools\rtt-view.py.

    OpenOCD halts the CPU as soon as it connects, so the session always restarts
    the firmware before activating RTT. Dropping the -Flash switch skips
    programming; it does not skip the restart.

.PARAMETER Flash
    Program build\primary\zephyr\zephyr.elf before starting the session. Omit it
    when the board already runs the current build.

.PARAMETER Port
    TCP port that serves RTT channel 0. Default 9090.

.PARAMETER StartupDelayMs
    Wait after reset before searching for the RTT control block. The firmware
    creates the block during boot, so searching too early reports
    "No control block found".

.PARAMETER DryRun
    Print the OpenOCD command line and the step plan, then exit without touching
    hardware.

.EXAMPLE
    .\tools\rtt-session.ps1 -Flash
    First run after a build: flash, start the firmware, serve RTT on port 9090.

.EXAMPLE
    .\tools\rtt-session.ps1
    Board already flashed: attach, restart, serve.

.NOTES
    Only one program can own the ST-Link. Close VS Code debugging and any
    Workbench flash session before running this.
#>
[CmdletBinding()]
param(
    [switch]$Flash,
    [string]$Port = '9090',
    [int]$StartupDelayMs = 1000,
    [switch]$DryRun,
    [string]$BuildDir = 'build\primary'
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
# SDK location: the environment variable if it is set (the standard Zephyr way),
# otherwise the version this project was built with. Never hard-code a user path.
$sdk = if ($env:ZEPHYR_SDK_INSTALL_DIR) { $env:ZEPHYR_SDK_INSTALL_DIR } else { 'C:\zephyr-sdk-1.0.1' }
$openocd = Join-Path $sdk 'hosttools\openocd\bin\openocd.exe'
$nm = Join-Path $sdk 'gnu\arm-zephyr-eabi\bin\arm-zephyr-eabi-nm.exe'
$cfg = Join-Path $repo 'openocd-stlink.cfg'
$elf = Join-Path $repo (Join-Path $BuildDir 'zephyr\zephyr.elf')

function Get-RttWindow {
    <#
      The RTT control block lives in a NOLOAD RAM section, so its ID string is
      written at runtime, not stored in flash. OpenOCD therefore has to be told
      which RAM range to search: __rtt_buff_data_start .. __rtt_buff_data_end.
      Read them from the ELF so the numbers cannot drift out of date.
    #>
    param([string]$ElfPath, [string]$NmPath)

    $fallback = [pscustomobject]@{
        Address = 0x20000000
        Size    = 0x1000
        Source  = 'fallback window (ELF symbols unavailable)'
    }

    if (-not (Test-Path -LiteralPath $ElfPath) -or -not (Test-Path -LiteralPath $NmPath)) {
        return $fallback
    }

    try {
        $start = $null
        $end = $null
        foreach ($line in (& $NmPath $ElfPath 2>$null)) {
            if ($line -match '^([0-9a-fA-F]{8})\s+\S\s+__rtt_buff_data_start$') {
                $start = [Convert]::ToUInt32($Matches[1], 16)
            }
            elseif ($line -match '^([0-9a-fA-F]{8})\s+\S\s+__rtt_buff_data_end$') {
                $end = [Convert]::ToUInt32($Matches[1], 16)
            }
        }

        if ($null -ne $start -and $null -ne $end -and $end -gt $start) {
            $size = $end - $start
            if ($size -lt 0x100) { $size = 0x100 }
            return [pscustomobject]@{
                Address = $start
                Size    = $size
                Source  = 'derived from ELF symbols'
            }
        }
    }
    catch {
        # Fall through to the fallback window.
    }

    return $fallback
}

if (-not (Test-Path -LiteralPath $openocd)) { throw "OpenOCD not found: $openocd" }
if (-not (Test-Path -LiteralPath $cfg)) { throw "ST-Link config not found: $cfg" }
if ($Flash -and -not (Test-Path -LiteralPath $elf)) {
    throw "Firmware image not found: $elf`nBuild it first: west build -p always -b olimex_stm32_h407 -d $BuildDir ."
}

$window = Get-RttWindow -ElfPath $elf -NmPath $nm

$ocdArgs = @('-f', $cfg, '-c', 'init')

if ($Flash) {
    $image = $elf -replace '\\', '/'
    $ocdArgs += @('-c', "program $image verify")
}

$ocdArgs += @(
    '-c', 'reset run'
    '-c', "sleep $StartupDelayMs"
    '-c', ('stm32f4x.cpu rtt setup 0x{0:X8} 0x{1:X} {{SEGGER RTT}}' -f $window.Address, $window.Size)
    '-c', 'stm32f4x.cpu rtt start'
    '-c', "rtt server start $Port 0"
)

Write-Host 'RTT session plan' -ForegroundColor Cyan
Write-Host '  1. Connect over SWD and halt the CPU            (-c init)'
if ($Flash) {
    Write-Host '  2. Program and verify the firmware image        (-c program ... verify)'
} else {
    Write-Host '  2. Skip programming (board already flashed)'
}
Write-Host '  3. Start the firmware                           (-c reset run)'
Write-Host "  4. Wait ${StartupDelayMs} ms for the RTT block to exist  (-c sleep $StartupDelayMs)"
Write-Host ('  5. Search 0x{0:X8}, {1} bytes for the block ID  (-c stm32f4x.cpu rtt setup)' -f $window.Address, $window.Size)
Write-Host "     range source: $($window.Source)"
Write-Host '  6. Activate RTT polling                         (-c stm32f4x.cpu rtt start)'
Write-Host "  7. Serve channel 0 on TCP port $Port              (-c rtt server start)"
Write-Host ''
Write-Host 'Read the stream in a second terminal:' -ForegroundColor Cyan
Write-Host "  python `"$repo\tools\rtt-view.py`" --port $Port"
Write-Host ''

if ($DryRun) {
    Write-Host 'Dry run - OpenOCD command line:' -ForegroundColor Yellow
    Write-Host ("  `"$openocd`" " + ($ocdArgs -join ' '))
    return
}

$previous = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
& $openocd @ocdArgs
$exitCode = $LASTEXITCODE
$ErrorActionPreference = $previous

if ($exitCode -ne 0) {
    Write-Host ''
    Write-Host "OpenOCD exited with code $exitCode." -ForegroundColor Red
    Write-Host 'Likely causes: ST-Link not connected, or already in use by another program'
    Write-Host '(VS Code debug session, Workbench flash, J-Link tools).'
    exit $exitCode
}

Write-Host 'RTT session ended.' -ForegroundColor Cyan
