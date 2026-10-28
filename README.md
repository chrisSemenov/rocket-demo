# Rocket Flight Controller Demo

A **bench demonstration** for the Olimex STM32-H407, Bosch BMP280 pressure/temperature sensor, and Zephyr RTOS. The planned application uses separate threads for sensor acquisition, altitude, apogee detection, and temperature monitoring; it uses synchronized shared memory and a single LED manager for output priority. Deployment is simulated **only by the board LED**; this is not flight-ready or safety-certified firmware.

## Current status

- Zephyr 4.4.2 and Zephyr SDK 1.0.1 are installed and have built firmware for `olimex_stm32_h407`.
- **Working hardware baseline:** this project's C++ LED blink test builds successfully with Zephyr 4.4.2 and SDK 1.0.1. ST-Link programming and LED operation on the Olimex STM32-H407 were confirmed by the user after applying the project configuration. The firmware toggles the green user LED every 500 ms (one complete cycle per second).
- The project includes the PC13 LED overlay, ST-Link flash settings, and a VS Code debug launch configuration. These were based on the separate working `C:\SCAE\projects\blinky` reference project.
- **BMP280 firmware builds:** `app.overlay` enables `i2c2` on PF1 (SCL) and PF0 (SDA) with a `bosch,bme280` node at address `0x76`, and `src/main.cpp` prints pressure in Pa and temperature in °C over SEGGER RTT. A pristine build succeeds (FLASH 29880 B, RAM 7104 B), and the generated `zephyr.dts` was inspected to confirm the enabled `i2c2` node, `pinctrl-0 = <&i2c2_scl_pf1 &i2c2_sda_pf0>`, and `bmp280@76`.
- **Live sensor readings are not yet verified.** No ST-Link was enumerating on USB when the RTT output path was prepared, so the firmware has not been flashed with the sensor attached, and the breakout's supply input and address/mode straps remain unconfirmed. Flight-state logic and acceptance tests are not implemented.

## Hardware and known board detail

- Target: Olimex STM32-H407 with STM32F407ZGT6; debugger: ST-Link V2 connected over SWD.
- The green **user** LED on the Olimex revision B schematic is **PC13, active-low**; the red power LED is not the application output. In Zephyr 4.4.2, the `olimex_stm32_h407` board definition instead maps `led0` to PA0. This project's `app.overlay` corrects it to PC13.
- The board's default OpenOCD file expects an Olimex FTDI JTAG adapter, not ST-Link. This project's `openocd-stlink.cfg` selects ST-Link/SWD at 1000 kHz, matching the working reference. `.vscode/settings.json` selects it for Workbench flashing.
- Do not wire the BMP280 until its breakout pin labels, supported voltage, I²C mode/address, pull-ups, and the board connector pinout have been checked. No sensor wiring is documented as verified yet.

## BMP280 connection (I²C2)

The chosen bus is **I²C2** because both signals land on the PF expansion header next to 3.3 V and GND, and it does not disturb the PC13 LED (PA0/PC13), the SWD pins, or the board's USART2 console:

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

There is no USB-UART adapter, so `printk()` goes to a **SEGGER RTT** ring buffer in RAM (`CONFIG_USE_SEGGER_RTT`, `CONFIG_RTT_CONSOLE`) that is read through the existing SWD connection. The control block ID is written at runtime into RAM at `0x20000810`, which is the region OpenOCD must scan.

After flashing, start an OpenOCD session that serves RTT channel 0 on TCP port 9090:

```powershell
& 'C:\zephyr-sdk-1.0.1\hosttools\openocd\bin\openocd.exe' `
  -f 'C:\SCAE\projects\rocket-demo\openocd-stlink.cfg' `
  -c 'init' `
  -c 'program C:/SCAE/projects/rocket-demo/build/primary/zephyr/zephyr.elf verify' `
  -c 'reset run' `
  -c 'sleep 1000' `
  -c 'stm32f4x.cpu rtt setup 0x20000000 0x1000 {SEGGER RTT}' `
  -c 'stm32f4x.cpu rtt start' `
  -c 'rtt server start 9090 0'
```

Two details of this OpenOCD build (verified locally, SDK 1.0.1, OpenOCD 0.12.0-g91bd278):

- `rtt setup/start/stop/channels` are **target** commands, so they need the `stm32f4x.cpu` prefix; only `rtt server` is a top-level command. Bare `rtt setup` just prints the `rtt server` help.
- They refuse to run before `init` (`The 'stm32f4x.cpu rtt start' command must be used after 'init'`).

Then, in a second terminal, read the stream:

```powershell
python C:\SCAE\projects\rocket-demo\tools\rtt-view.py
```

Expected output once the sensor answers, one line per second:

```
BMP280 ready: I2C2 SCL=PF1 SDA=PF0 addr=0x76, sampling every 1000 ms
[     1 s] seq=0  P=100412 Pa  T=22.31 C
```

If the sensor is not detected the firmware prints a wiring hint and stops sampling rather than reporting fake values. The command sequence above was validated against this OpenOCD build's command set, but it has **not** been run end-to-end on the board yet; the RTT configuration and control block layout were verified statically in the build.


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

The LED/ST-Link baseline is working and the BMP280 firmware builds. **The next step is proving live readings on hardware.**

1. Confirm the breakout's supply input, I²C mode/address straps, pull-ups, and PF header orientation, then wire it as in the table above.
2. Flash, start the OpenOCD RTT server, and capture **real** values with units and timestamps. Sanity checks: pressure near 101325 Pa at bench altitude, a plausible room temperature, and both values responding to a deliberate change (warm the sensor, or pressurize it gently).
3. Only after live acquisition is verified, implement and test the independent shared-memory/altitude/apogee/temperature threads and LED-priority behavior.

The detailed work plan and assignment/requirements currently live outside this repository at `G:\My Drive\Engineering\SCAE\Interview Test\`.
