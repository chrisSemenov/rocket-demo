# Rocket Flight Controller Demo

A **bench demonstration** for the Olimex STM32-H407, Bosch BMP280 pressure/temperature sensor, and Zephyr RTOS. The planned application uses separate threads for sensor acquisition, altitude, apogee detection, and temperature monitoring; it uses synchronized shared memory and a single LED manager for output priority. Deployment is simulated **only by the board LED**; this is not flight-ready or safety-certified firmware.

## Current status

- Zephyr 4.4.2 and Zephyr SDK 1.0.1 are installed and have built firmware for `olimex_stm32_h407`.
- **Working hardware baseline:** this project's C++ LED blink test builds successfully with Zephyr 4.4.2 and SDK 1.0.1. ST-Link programming and LED operation on the Olimex STM32-H407 were confirmed by the user after applying the project configuration. The firmware toggles the green user LED every 500 ms (one complete cycle per second).
- The project includes the PC13 LED overlay, ST-Link flash settings, and a VS Code debug launch configuration. These were based on the separate working `C:\SCAE\projects\blinky` reference project.
- **BMP280 sensor working (verified on hardware, 2026-10-28):** `app.overlay` enables `i2c2` on PF1 (SCL) and PF0 (SDA) with a `bosch,bme280` node at address `0x76`; `src/main.cpp` samples once per second and prints pressure and temperature over SEGGER RTT. Flash programming verified OK, OpenOCD found the RTT control block at `0x20000810`, and 27 consecutive live samples were captured (≈94 940 Pa, ≈26 °C, drifting smoothly rather than jumping).
- **Not implemented yet:** the planned acquisition/altitude/apogee/temperature threads, shared-memory synchronisation, LED priority manager, and acceptance tests.

## Hardware and known board detail

- Target: Olimex STM32-H407 with STM32F407ZGT6; debugger: ST-Link V2 connected over SWD.
- The green **user** LED on the Olimex revision B schematic is **PC13, active-low**; the red power LED is not the application output. In Zephyr 4.4.2, the `olimex_stm32_h407` board definition instead maps `led0` to PA0. This project's `app.overlay` corrects it to PC13.
- The board's default OpenOCD file expects an Olimex FTDI JTAG adapter, not ST-Link. This project's `openocd-stlink.cfg` selects ST-Link/SWD at 1000 kHz, matching the working reference. `.vscode/settings.json` selects it for Workbench flashing.
- The BMP280 is wired to the PF header as in the table below and reads correctly on hardware. The ~94.9 kPa result corresponds to roughly 550 m elevation; the driver only reports the device ready after reading and validating the chip ID over I²C at `0x76`.

## BMP280 connection (I²C2)

The chosen bus is **I²C2** because both signals land on the PF expansion header next to 3.3 V and GND.

| BMP280 signal | Olimex connection | Chip pin |
|---|---|---|
| Supply input (must accept 3.3 V) | PF header pin 1 — 3.3 V | — |
| `GND` | PF header pin 2 — GND | — |
| `SDA` / `SDI` | PF header pin 3 | PF0 |
| `SCL` / `SCK` | PF header pin 4 | PF1 |
| `CSB` | 3.3 V (selects I²C, not SPI) | — |
| `SDO` / `SA0` | GND (selects address `0x76`) | — |

The PF header pin numbers are connector contacts, not MCU package pins. The Zephyr I²C controller uses internal pull-ups only weakly, so the bus normally relies on the breakout's own pull-ups; check them before adding more. `CSB` must not float, and `SDO` selects `0x76` (to GND) or `0x77` (to 3.3 V).

## Console output over ST-Link only

There is no USB-UART adapter, so `printk()` goes to a **SEGGER RTT** ring buffer in RAM (`CONFIG_USE_SEGGER_RTT`, `CONFIG_RTT_CONSOLE`) that is read over the existing SWD connection. Two helper tools wrap the whole procedure: `tools\rtt-session.ps1` starts OpenOCD, and `tools\rtt-view.py` prints the stream.

Terminal 1 — add `-Flash` on the first run or after any code change; omit it when the board already runs the current build:

```powershell
C:\SCAE\projects\rocket-demo\tools\rtt-session.ps1 -Flash
```

Terminal 2:

```powershell
python C:\SCAE\projects\rocket-demo\tools\rtt-view.py
```

Captured from the board:

```
BMP280 ready: I2C2 SCL=PF1 SDA=PF0 addr=0x76, sampling every 1000 ms
[     0 s] seq=0  P=94940 Pa  T=26.86 C
[     1 s] seq=1  P=94940 Pa  T=26.84 C
```

`-DryRun` prints the exact OpenOCD command line without touching hardware. The full walkthrough — what every `-c` argument does, why the RTT commands need the `stm32f4x.cpu` prefix and must follow `init`, where the `0x20000000 0x8B8` search window comes from, and why `CONFIG_UART_CONSOLE=n` is mandatory here — is in **[docs/console-rtt.md](docs/console-rtt.md)**.


Hardware source: [Olimex STM32-H407 schematic, revision B](https://www.olimex.com/Products/ARM/ST/STM32-H407/resources/STM32-H407_Rev_B_sch.pdf).

## Local development environment

- Zephyr/West workspace: `C:\Users\ChristianSemenov\zephyrproject` (Zephyr 4.4.2 in `deps\zephyr`).
- Toolchain: `C:\zephyr-sdk-1.0.1`; Zephyr Workbench host tools environment: `%USERPROFILE%\.zinstaller\env.ps1`.
- Workbench application board: `olimex_stm32_h407`; configuration name: `primary`.
- C++ source: `src/main.cpp`; `prj.conf` enables `CONFIG_CPP=y` and `CONFIG_STD_CPP17=y`.
- This repository is a freestanding application: it uses the separate West workspace and SDK; it does not contain Zephyr itself. `.vscode/settings.json` currently includes machine-specific relative paths, so a different machine must select its own workspace and SDK.
- The existing build output under `build/` is ignored by Git and should be regenerated locally.

With the Zephyr workspace and host tools installed, build from an activated VS Code PowerShell terminal:

```powershell
. "$env:USERPROFILE\.zinstaller\env.ps1"
$env:ZEPHYR_BASE = "$env:USERPROFILE\zephyrproject\deps\zephyr"
$env:ZEPHYR_SDK_INSTALL_DIR = 'C:\zephyr-sdk-1.0.1'
$env:ZEPHYR_TOOLCHAIN_VARIANT = 'zephyr'
cd C:\SCAE\projects\rocket-demo
west build -p always -b olimex_stm32_h407 -d build/primary .
```

This build command was tested locally, **not from a clean clone on another machine**. Explicit workspace/SDK variables are needed in a standalone terminal; activating the host tools alone did not locate them here.

With the board correctly powered and the same ST-Link SWD wiring used for the reference project, use Workbench's Flash action or run in the same terminal:

```powershell
west flash -d build/primary --runner openocd -- --config "C:/SCAE/projects/rocket-demo/openocd-stlink.cfg"
```

For debugging, select **Zephyr Workbench Debug [primary]** in VS Code's Run and Debug view after building. `.vscode/launch.json` is configured to start the SDK's OpenOCD directly, connect GDB on port 3333, program the ELF, and break at `main()`. It does not depend on generated `.debug` helper files. Resume execution to observe blinking; stop any existing debug/OpenOCD session before flashing.

## Next steps

Live pressure and temperature acquisition is verified on hardware. **The next milestone is the threaded architecture.**

1. Split acquisition, altitude calculation, apogee detection, and temperature monitoring into separate threads with synchronised shared state.
2. Add the LED priority manager (highest-priority condition wins) and verify it by forcing each state.
3. Add acceptance tests and record the evidence, keeping the RTT console as the observability path.

Known limitation: sampling runs at 1 Hz, matching the driver's default 1 s standby time. Higher rates need different oversampling and standby settings.

The detailed work plan and assignment/requirements currently live outside this repository at `G:\My Drive\Engineering\SCAE\Interview Test\`.
