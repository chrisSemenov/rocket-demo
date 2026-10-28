# Rocket Flight Controller Demo

A **bench demonstration** for the Olimex STM32-H407, Bosch BMP280 pressure/temperature sensor, and Zephyr RTOS. The planned application uses separate threads for sensor acquisition, altitude, apogee detection, and temperature monitoring; it uses synchronized shared memory and a single LED manager for output priority. Deployment is simulated **only by the board LED**; this is not flight-ready or safety-certified firmware.

## Current status

- Zephyr 4.4.2 and Zephyr SDK 1.0.1 are installed and have built firmware for `olimex_stm32_h407`.
- **Working hardware baseline:** this project's C++ LED blink test builds successfully with Zephyr 4.4.2 and SDK 1.0.1. ST-Link programming and LED operation on the Olimex STM32-H407 were confirmed by the user after applying the project configuration. The firmware toggles the green user LED every 500 ms (one complete cycle per second).
- The project includes the PC13 LED overlay, ST-Link flash settings, and a VS Code debug launch configuration. These were based on the separate working `C:\SCAE\projects\blinky` reference project.
- **BMP280 sensor working (verified on hardware, 2026-10-28):** `app.overlay` enables `i2c2` on PF1 (SCL) and PF0 (SDA) with a `bosch,bme280` node at address `0x76`. Flash programming verified OK, OpenOCD found the RTT control block at `0x20000810`, and live samples were captured (≈94 940 Pa, ≈26 °C). 94.9 kPa is consistent with roughly 550 m elevation, and the driver only reports the device ready after validating the chip ID over I²C at `0x76`.
- **Sampling reworked for the descent test:** the sensor runs 8× pressure / 2× temperature in forced mode on a 50 ms deadline-based loop, so the 750 ms velocity window contains a full set of samples. The loop reports its own fetch duration, inter-sample interval and deadline overruns on the console. See [Sampling design](#sampling-design).
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

## Sampling design

The assignment's descent test averages vertical velocity over the **750 ms** preceding
each decision. At the driver's default 1 Hz that window contains no second sample, so the
average is undefined and apogee detection can never trigger. The sampling rate had to rise
before that task could work at all.

Candidates, using the datasheet conversion time
`1.25 + 2.3·T_ov + (2.3·P_ov + 0.575)` ms:

| Period | Oversampling | Conversion | Slack per period | Samples in 750 ms |
|---|---|---|---|---|
| 1000 ms | 16× / 2× | 43.2 ms | 957 ms | 1 — unusable |
| 50 ms | 16× / 2× | 43.2 ms | 5.5 ms | 15 |
| **50 ms** | **8× / 2×** | **24.8 ms** | **23.9 ms** | **15** |
| 100 ms | 16× / 2× | 43.2 ms | 55.5 ms | 8 — needs interpolation |

**Chosen: 8× pressure, 2× temperature, 50 ms period, forced mode.**

- `750 = 15 × 50`, so the window endpoint is a stored sample and the descent test is an
  index comparison rather than an interpolation.
- 24.8 ms of conversion in a 50 ms period leaves about 24 ms of slack and roughly 2× rate
  headroom; this oversampling tops out near 39.5 Hz.
- 16× at the same period leaves only 5.5 ms of slack and a 1.14× ceiling. It would improve
  altitude noise from ~2.4 cm to ~1.7 cm and velocity noise from ~4.5 to ~3.2 cm/s. Both
  are far below the manoeuvres demonstrated, so the precision gain is not observable and
  does not justify the lost margin.
- 100 ms with 16× is comfortable but does not divide 750 ms, so the window needs
  interpolation and the trace updates half as often.

Forced mode is used so that **one 50 ms period contains exactly one conversion**. In normal
mode the sensor free-runs and the value read is whatever the last conversion left in the data
registers, so the effective rate is set by the standby time rather than by the loop.

Three settings are deliberate:

- **No IIR filter credit.** The datasheet recommends normal mode when the IIR filter is used,
  and forced mode returns to sleep between measurements, so the filter has no history to
  smooth across. `CONFIG_BME280_FILTER_OFF` is therefore explicit, and the 750 ms window
  provides the averaging.
- **Humidity oversampling is 1× although the BMP280 has no humidity sensor.** The driver's
  `BME280_EXPECTED_SAMPLE_TIME_MS` counts it anyway: at the default 16× it computes 53 ms,
  which exceeds the 50 ms period, so every fetch would overrun the schedule with no build
  error. At 1× the constant is 23 ms. The CTRL_HUM register write itself is skipped on a
  BMP280, so this only corrects the driver's timing assumption.
- Standby time is left at its default because it only applies to normal mode.

### Temperature and pressure are coupled

Pressure is not a raw reading. The driver derives `t_fine` from the temperature conversion
and feeds it into the pressure compensation, always temperature first and within the same
fetch. Two consequences:

- Pressure and temperature must be published as **one snapshot** — value, timestamp and
  sequence under a single lock — so a consumer cannot pair pressure from one sample with
  temperature from the next.
- **No additional correction is applied, and none should be.** After compensation the
  residual temperature coefficient of pressure is about ±1.5 Pa/K, roughly 1.3 cm of
  altitude per °C. A 1 °C drift against a ~2 Pa change is that coefficient, not a fault;
  correcting it would need a per-unit calibration.

Altitude is derived from pressure alone against a baseline `P0` captured at the start, so
absolute temperature bias cancels and only drift during a run survives. Warming the sensor
to demonstrate the overheat alarm also shifts pressure by a few Pa (~10–20 cm of apparent
altitude), so the overheat and altitude demonstrations are kept separate.

### Timing evidence

The loop self-reports, so the cadence is measured rather than assumed: each logged sample
carries `fetch=` (how long `sensor_sample_fetch()` blocked) and `dt=` (measured interval
since the previous sample), and every 100 samples it prints

```
stats: 100 samples  fetch min/avg/max ms  dt min/avg/max ms  overruns=N  errors=N
```

A period that looks like 20 Hz but is really 14 Hz shows up as `dt` above 50 ms and
`overruns` climbing. Record the printed `stats:` line from a long run as the evidence.

Measured on hardware (2026-10-28, fifteen consecutive 100-sample blocks):

| Quantity | Datasheet / driver expectation | Measured |
|---|---|---|
| `fetch` min/avg/max | 24.8 ms datasheet, 23 ms driver constant | **24 / 24 / 24 ms** |
| `dt` min/avg/max | 50 ms | **50 / 50 / 50 ms** |
| deadline overruns | 0 | **0** |
| read errors | 0 | **0** |

```
stats: 100 samples  fetch 24/24/24 ms (min/avg/max)  dt 50/50/50 ms  overruns=0  errors=0
```

One sample occupies 24 ms of the 50 ms period, leaving 26 ms idle, and the interval never
deviated by a tick. The only outlier is the first fetch after boot (42 ms), because it
starts the first conversion; every later fetch is 24 ms. `overruns` stayed 0 across the
run, so the absolute-deadline loop is not drifting.

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
BMP280 ready: I2C2 SCL=PF1 SDA=PF0 addr=0x76, forced mode 8x/2x, 50 ms period
[      0.05 s] seq=0  P=95005 Pa  T=24.22 C  fetch=42 ms  dt=42 ms
[      0.53 s] seq=10  P=95009 Pa  T=24.31 C  fetch=24 ms  dt=50 ms
...
stats: 100 samples  fetch 24/24/24 ms (min/avg/max)  dt 50/50/50 ms  overruns=0  errors=0
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

Known limitation: only pressure and temperature are acquired; altitude, apogee detection and the LED priority manager still run in the single main loop rather than in separate threads.

The detailed work plan and assignment/requirements currently live outside this repository at `G:\My Drive\Engineering\SCAE\Interview Test\`.
