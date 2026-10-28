# Console output over ST-Link (SEGGER RTT)

This board has no USB-UART adapter attached, so `printk()` output is delivered by
**SEGGER RTT**: the firmware writes into a ring buffer in its own RAM, and OpenOCD
reads that buffer over the existing SWD connection and republishes it on a TCP port.

**Status: verified on hardware (2026-10-28).** Flash, control-block discovery and live
BMP280 readings were captured from the board. Evidence is recorded at the end of this
document.

## How the pieces connect

```
BMP280 --I2C (PF0/PF1)--> STM32F407 --> printk --> RTT ring buffer in RAM (0x20000810)
                                                          ^
                                          OpenOCD reads RAM over SWD (ST-Link)
                                                          |
                                                  TCP 127.0.0.1:9090
                                                          |
                                              tools/rtt-view.py prints it
```

Three things must all be true, and each one has failed at least once during bring-up:

1. The firmware must contain RTT code (`CONFIG_RTT_CONSOLE=y`).
2. RTT — **not** the UART — must own the printk hook (see `CONFIG_UART_CONSOLE=n` below).
3. OpenOCD must be told where the control block lives and must be allowed to poll it.

## Quick start

Terminal 1 — flash (first time, or after any code change) and serve the stream:

```powershell
C:\SCAE\projects\rocket-demo\tools\rtt-session.ps1 -Flash
```

Or, if the board already runs the current build, attach without re-flashing:

```powershell
C:\SCAE\projects\rocket-demo\tools\rtt-session.ps1
```

Terminal 2 — read the stream (leave terminal 1 running):

```powershell
python C:\SCAE\projects\rocket-demo\tools\rtt-view.py
```

Useful switches:

| Switch | Purpose |
|---|---|
| `-Flash` | Program `build\primary\zephyr\zephyr.elf` and verify it before starting. |
| `-DryRun` | Print the step plan and the exact OpenOCD command line, then exit without touching hardware. |
| `-Port <n>` | Serve on a different TCP port (default `9090`); pass the same port to `rtt-view.py`. |
| `-StartupDelayMs <n>` | Wait longer before searching for the control block (default `1000`). |

`-DryRun` is the safest way to inspect what will happen:

```powershell
C:\SCAE\projects\rocket-demo\tools\rtt-session.ps1 -Flash -DryRun
```

## The underlying OpenOCD command

`rtt-session.ps1` only assembles this; you can run it by hand:

```powershell
& 'C:\zephyr-sdk-1.0.1\hosttools\openocd\bin\openocd.exe' `
  -f 'C:\SCAE\projects\rocket-demo\openocd-stlink.cfg' `
  -c 'init' `
  -c 'program C:/SCAE/projects/rocket-demo/build/primary/zephyr/zephyr.elf verify' `
  -c 'reset run' `
  -c 'sleep 1000' `
  -c 'stm32f4x.cpu rtt setup 0x20000000 0x8B8 {SEGGER RTT}' `
  -c 'stm32f4x.cpu rtt start' `
  -c 'rtt server start 9090 0'
```

| Argument | What it does | Why it is needed |
|---|---|---|
| `-f openocd-stlink.cfg` | Loads `interface/stlink.cfg`, `transport select hla_swd`, `target/stm32f4x.cfg`, 1 MHz, `reset_config none`. | The board's own OpenOCD config expects an Olimex FTDI JTAG pod, not an ST-Link. |
| `-c init` | Opens the probe and examines the target. | **Halts the CPU.** Everything below needs an initialized target. |
| `-c program <elf> verify` | Erases, writes and verifies flash. | Drop this line when the board already runs the current build. |
| `-c reset run` | Resets and resumes the CPU. | Required because `init` halted it; without it the firmware is frozen and never prints. |
| `-c sleep 1000` | Host-side wait. | The control block is created during boot, so searching too early reports `No control block found`. |
| `-c stm32f4x.cpu rtt setup <addr> <size> {SEGGER RTT}` | Sets the RAM search window and the 10-byte ID to look for. | See "Where the address comes from" below. |
| `-c stm32f4x.cpu rtt start` | Searches for the block, then polls channel 0. | This is the command that actually finds `Control block found at 0x...`. |
| `-c rtt server start 9090 0` | Publishes up-channel 0 on TCP port 9090. | This is what the viewer connects to. It is the only top-level `rtt` subcommand. |

### Two quirks of this OpenOCD build

Verified against `OpenOCD 0.12.0-g91bd278` shipped with Zephyr SDK 1.0.1:

- **RTT setup/start/stop are target commands.** They must be prefixed with the target
  name (`stm32f4x.cpu rtt ...`). Typing bare `rtt setup ...` matches the top-level `rtt`
  group, which only contains `server`, and just prints that help text.
- **They must run after `init`.** Calling them earlier fails with
  `The 'stm32f4x.cpu rtt start' command must be used after 'init'.`

## Where the address and size come from

The control block sits in a `NOLOAD` RAM section, so its `"SEGGER RTT"` ID string is
written at runtime and is deliberately **absent from the flash image** (SEGGER does this
so a debugger cannot latch onto a stale copy). OpenOCD therefore cannot discover it
automatically — you must supply a search window.

The window comes from two linker symbols:

```powershell
& 'C:\zephyr-sdk-1.0.1\gnu\arm-zephyr-eabi\bin\arm-zephyr-eabi-nm.exe' `
  'C:\SCAE\projects\rocket-demo\build\primary\zephyr\zephyr.elf' | Select-String '__rtt'
```

```
20000000 B __rtt_buff_data_start
200008b8 B __rtt_buff_data_end
20000810 B _SEGGER_RTT
```

`0x200008B8 - 0x20000000 = 0x8B8` (2232 bytes). `rtt-session.ps1` reads these symbols and
derives the window itself, falling back to `0x20000000 0x1000` only when the ELF or `nm`
is unavailable. The control block lands at `0x20000810`, as the OpenOCD log confirms.

Re-derive the window whenever you change `CONFIG_SEGGER_RTT_BUFFER_SIZE_UP` or add RTT
channels; the script does that for you.

## The one firmware setting that silently breaks this

`drivers/console/uart_console.c` and `drivers/console/rtt_console.c` both install a
printk hook at `CONFIG_CONSOLE_INIT_PRIORITY` (60). The board defconfig enables
`CONFIG_UART_CONSOLE`, and on this board it won the race — so `printk()` went out of
USART2 (PA2/PA3) and RTT stayed empty even though the control block was found and the
server was listening.

Symptom: the OpenOCD log is healthy, the TCP port accepts connections, and **zero bytes**
arrive.

Diagnosis (read the live hook pointer from the target):

```
> mdw 0x200008c0 1
0x200008c0: 08001a59        # = console_out|1 (UART)   ... rtt_console_out is 0x08001a15
```

Fix, already applied in `prj.conf`:

```conf
CONFIG_UART_CONSOLE=n
```

## Reading the stream

```powershell
python C:\SCAE\projects\rocket-demo\tools\rtt-view.py            # port 9090
python C:\SCAE\projects\rocket-demo\tools\rtt-view.py --port 9091
```

`tools/rtt-view.py` is a ~30-line TCP client. Windows ships neither `telnet` nor `nc` by
default, and `rtt server` speaks a raw byte stream rather than telnet, so a plain socket
client is the simplest reliable reader. Ctrl+C closes only the viewer; the firmware and
the OpenOCD session keep running. Reconnect at any time — RTT is a ring buffer, so you
may lose old lines but never corrupt the stream.

## Expected output

Line shape of the 50 ms build (every 10th sample is logged, plus a `stats:` line every 100
samples):

```
*** Booting Zephyr OS build v4.4.2 ***
BMP280 ready: I2C2 SCL=PF1 SDA=PF0 addr=0x76, forced mode 8x/2x, 50 ms period
[      0.05 s] seq=0  P=95005 Pa  T=24.22 C  fetch=42 ms  dt=42 ms
[      0.53 s] seq=10  P=95009 Pa  T=24.31 C  fetch=24 ms  dt=50 ms
[      1.03 s] seq=20  P=95008 Pa  T=24.32 C  fetch=24 ms  dt=50 ms
...
stats: 100 samples  fetch 24/24/24 ms (min/avg/max)  dt 50/50/50 ms  overruns=0  errors=0
```

Sanity checks that distinguish real data from a dead bus:

- Pressure near local absolute pressure. 94940 Pa corresponds to roughly 550 m elevation;
  a sea-level bench reads closer to 101325 Pa.
- Temperature plausible for the room and drifting smoothly, not jumping randomly.
- `P` changes by a few pascals when you move the sensor; `T` rises if you pinch it.
- `seq` increments on every sample and `dt` stays at 50 ms; `overruns` should stay 0.

If the sensor does not answer, the firmware prints the wiring hint and stops sampling
instead of reporting invented values:

```
ERROR: bmp280@76 not ready. Check 3V3, GND, SDA=PF0, SCL=PF1, CSB->3V3, SDO->GND (address 0x76).
```

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `Error: open failed` + `LIBUSB_ERROR_ACCESS` | The ST-Link is present but owned by another program — a leftover `openocd`, a VS Code debug session, Workbench flash, or J-Link tools. | `Get-Process openocd` then `Stop-Process -Id <pid> -Force`; close any debug session. |
| `Error: open failed` with no voltage line | Probe unplugged, or the board is unpowered. | Reconnect; the log should show `Target voltage: ~2.9 V`. |
| `The 'stm32f4x.cpu rtt start' command must be used after 'init'.` | Commands given before `init` or in the wrong order. | Keep the order shown above. |
| Bare `rtt setup` prints `rtt server start <port> <channel>` | Missing target prefix. | Use `stm32f4x.cpu rtt setup ...`. |
| `rtt: No control block found` | Board runs old firmware; searched too early; wrong window. | Flash the current build; raise `-StartupDelayMs`; re-derive the window. |
| Port listens but **0 bytes** arrive | UART console owns the printk hook. | `CONFIG_UART_CONSOLE=n`, rebuild, reflash. |
| Viewer: `No RTT server on 127.0.0.1:9090` | Terminal 1 not running, or different port. | Start the session; match `-Port`. |
| Stream stops after a reset or power cycle | The session lost the target. | Restart terminal 1. |
| Garbled or interleaved text | Two viewers attached, or another program using the port. | Close extra clients; change `-Port`. |

## Verification record (2026-10-28)

Captured from the board via this exact procedure:

```
** Programming Started **
Info : device id = 0x100f6413
Info : flash size = 1024 KiB
** Programming Finished **
** Verify Started **
** Verified OK **
Info : Target voltage: 2.861238
Info : rtt: Searching for control block 'SEGGER RTT'
Info : rtt: Control block found at 0x20000810
Info : Listening on port 9090 for rtt connections
```

Live stream from the earlier 1 Hz build, 27 consecutive samples:

```
BMP280 ready: I2C2 SCL=PF1 SDA=PF0 addr=0x76, sampling every 1000 ms
[     0 s] seq=0  P=94940 Pa  T=26.86 C
...
[    26 s] seq=26  P=94942 Pa  T=25.85 C
```

The attach path (no `-Flash`) was also verified: the log shows
`2. Skip programming (board already flashed)` and the stream resumed at `seq=14`,
confirming the firmware was restarted rather than modified.

The sensor is genuine rather than a lucky register read: the driver only reports the
device ready after reading and validating the BMP280 chip ID over I²C at address `0x76`.

## 20 Hz verification (2026-10-28)

The 8× / 2× forced-mode build was flashed with the same procedure and streamed for
~15 minutes. Fifteen consecutive 100-sample reports were identical:

```
stats: 100 samples  fetch 24/24/24 ms (min/avg/max)  dt 50/50/50 ms  overruns=0  errors=0
```

`fetch` is a flat 24 ms against the 24.8 ms datasheet conversion time and the driver's
23 ms sample-time constant, so one sample uses 24 ms of the 50 ms period and 26 ms are
idle. `dt` never left 50 ms and no deadline was missed. The first fetch after boot takes
42 ms because it starts the first conversion; every later fetch is 24 ms.

An earlier revision reported stack garbage in the first `stats:` line because the
min/avg/max accumulators `max`, `sum` and `count` were left uninitialized; they now have
initializers and the first report is correct.
