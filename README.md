# Rocket Flight Controller Demo

A **bench demonstration** for the Olimex STM32-H407, Bosch BMP280 pressure/temperature sensor, and Zephyr RTOS. Sensor acquisition, altitude, apogee detection and temperature monitoring are separate threads; measurement data moves through synchronized shared memory, and a single LED manager owns the output priority. Acquisition, the shared-memory contract and relative altitude are implemented and verified; apogee detection and the LED behaviours are not yet. Deployment is simulated **only by the board LED**; this is not flight-ready or safety-certified firmware.

## Current status

- Zephyr 4.4.2 and Zephyr SDK 1.0.1 are installed and have built firmware for `olimex_stm32_h407`.
- **Working hardware baseline:** this project's C++ LED blink test builds successfully with Zephyr 4.4.2 and SDK 1.0.1. ST-Link programming and LED operation on the Olimex STM32-H407 were confirmed by the user after applying the project configuration. The firmware toggles the green user LED every 500 ms (one complete cycle per second).
- The project includes the PC13 LED overlay, ST-Link flash settings, and a VS Code debug launch configuration. These were based on the separate working `C:\SCAE\projects\blinky` reference project.
- **BMP280 sensor working (verified on hardware, 2026-10-28):** `app.overlay` enables `i2c2` on PF1 (SCL) and PF0 (SDA) with a `bosch,bme280` node at address `0x76`. Flash programming verified OK, OpenOCD found the RTT control block at `0x20000810`, and live samples were captured (≈94 940 Pa, ≈26 °C). 94.9 kPa is consistent with roughly 550 m elevation, and the driver only reports the device ready after validating the chip ID over I²C at `0x76`.
- **Sampling reworked for the descent test:** the sensor runs 8× pressure / 2× temperature in forced mode on a 50 ms deadline-based loop, so the 750 ms velocity window contains a full set of samples. The loop reports its own fetch duration, inter-sample interval and deadline overruns on the console. See [Sampling design](#sampling-design).
- **Acquisition is its own thread (M4, verified on hardware):** `sensor` owns the BMP280 and the only I²C traffic, fetches on a 50 ms absolute deadline, and publishes a complete snapshot. Measured over 2 750 samples: fetch 24 ms, interval 50 ms, zero overruns, zero errors. Evidence: [docs/verification/at-02-live-acquisition.md](docs/verification/at-02-live-acquisition.md); the failure path is covered in [m4-absent-sensor.md](docs/verification/m4-absent-sensor.md).
- **Shared memory is synchronized (M5, verified on hardware):** readers copy a `Measurement` out of `SensorDataStore` under a mutex and never consume it, so the 500 ms telemetry consumer and an independent 1 s reader observe the same sequence at their own rates. Evidence: [docs/verification/m5-shared-memory.md](docs/verification/m5-shared-memory.md).
- **Altitude is calculated and published (M6, verified on hardware):** a separate `altitude` thread reads the measurement snapshot, freezes a baseline `P₀` after 2 s of stationary pressure, and publishes a separate `AltitudeStore`. The maths is unit-tested on the host against independently computed references. Evidence: [docs/verification/at-03-altitude.md](docs/verification/at-03-altitude.md).
- **Not implemented yet:** apogee detection, temperature monitoring, the LED priority manager, and the acceptance test suite.

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

Hardware source: [Olimex STM32-H407 schematic, revision B](https://www.olimex.com/Products/ARM/ST/STM32-H407/resources/STM32-H407_Rev_B_sch.pdf).

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

The sensor thread measures itself, so the cadence is checked rather than assumed: it times
`sensor_sample_fetch()` and the interval between acquisitions, and every 200 samples it
publishes those counters as a snapshot. The console consumer prints them — the acquisition
thread never blocks on printing:

```
stats: 200 samples  fetch min/avg/max ms  dt min/avg/max ms  overruns=N  errors=N  seq=N
```

A period that looks like 20 Hz but is really 14 Hz shows up as `dt` above 50 ms and
`overruns` climbing. A failed fetch shows up as `errors` climbing while `seq` stalls.

Measured on hardware (2026-10-28, twelve consecutive 200-sample blocks up to 2 400 samples,
then 2 750 in total):

| Quantity | Datasheet / driver expectation | Measured |
|---|---|---|
| `fetch` min/avg/max | 24.8 ms datasheet, 23 ms driver constant | **24 / 24 / 24 ms** |
| `dt` min/avg/max | 50 ms | **50 / 50 / 50 ms** |
| deadline overruns | 0 | **0** |
| fetch errors | 0 | **0** |

```
stats: 200 samples  fetch 24/24/24 ms (min/avg/max)  dt 50/50/50 ms  overruns=0  errors=0  seq=390
...
stats: 2400 samples  fetch 24/24/24 ms (min/avg/max)  dt 50/50/50 ms  overruns=0  errors=0  seq=2590
```

One sample occupies 24 ms of the 50 ms period, leaving 26 ms idle, and the interval never
deviated by a tick. `overruns` stayed 0 for the whole run, so the absolute-deadline loop is
not drifting. The first two samples are excluded from the counters: the first fetch starts
the first conversion (42 ms) and the schedule is only established by the second. That
start-up transient was visible while tuning and is deliberately not reported as steady state.

## Altitude

The `altitude` thread turns pressure into height using the standard-atmosphere
approximation:

```
h = 44330 · (1 − (P / P₀)^0.1903)     metres
```

`P` is the latest pressure from the measurement snapshot, `P₀` is a reference
pressure captured on the bench. The implementation computes it as

```
h = −44330 · expm1(0.1903 · log1p((P − P₀) / P₀))
```

which is the same curve but stays accurate when `P` is close to `P₀` — the only
regime that matters here, where **1 hPa is about 8 m**. A plain `1 − pow()`
subtracts two nearly equal numbers and loses most of the digits.

### The reference has to be measured, not assumed

An absolute pressure reading cannot give an absolute height: local weather moves
sea-level pressure by ±20 hPa, which is ±170 m. So `P₀` is measured and then
frozen:

- collect valid, fresh samples until the window covers **2 s** (38 of the 40
  expected samples);
- require the pressure spread across that window to stay within **20 Pa**
  (≈ 1.8 m here);
- if the sensor moved, restart the window from the current sample;
- a gap longer than the 250 ms stale timeout clears the window entirely;
- after calibration, `P₀` never changes for the rest of the run.

On the bench (≈ 626–630 m elevation) this gave `P₀ = 95 062 Pa` and altitude
`0.00 m`. The same pressure through a *standard* atmosphere would read 535 m —
a ~90 m error caused by the weather that day (95 062 Pa at 626 m implies a
sea-level pressure near 102 440 Pa). That gap is the reason for measuring `P₀`.

### What it reports

`AltitudeStore` carries the altitude in centimetres plus the **source** sequence
and timestamp, so a consumer can tell how old the height is without reading the
pressure store. It also carries `calibrated` and the `P₀` used. On invalid or
stale input the altitude is published as `valid = false` and `P₀` is kept; a
last-known height is never presented as current.

Measured while stationary: −0.62 … +0.27 m, i.e. **0.89 m peak-to-peak**, which
is the sensor's ~±7 Pa of noise. No temperature compensation is applied; see
[Temperature and pressure are coupled](#temperature-and-pressure-are-coupled).

### Tests

The calculation is a pure function in `src/altitude.cpp` with no Zephyr or
hardware dependency, so it is tested on the workstation:

```powershell
powershell -ExecutionPolicy Bypass -File tests\run-tests.ps1
```

Seventeen checks: seven reference pairs computed independently in 40-digit
decimal arithmetic (1 cm tolerance), monotonicity, integer/metre agreement, and
six rejection cases. Nothing is verified by calling the same code twice.

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
*** Booting Zephyr OS build v4.4.2 ***
BMP280 ready: I2C2 SCL=PF1 SDA=PF0 addr=0x76, forced mode 8x/2x, 50 ms period
threads: sensor(prio 5) altitude(6) telemetry(8) diag(9); stale timeout 250 ms
telemetry: no valid measurement yet (error=0)
alt:  not available yet (baseline 0/38 samples)
diag:  seq=0  age=-1 ms  valid=0  published=0
[      0.51 s] seq=10  P=95062 Pa  T=22.24 C  age=26 ms
alt:  not available yet (baseline 10/38 samples)
[      1.01 s] seq=20  P=95062 Pa  T=22.24 C  age=26 ms
diag:  seq=20  age=26 ms  valid=1  published=20
alt:  baseline frozen P0=95062 Pa after 38 samples (spread 8 Pa)
[      2.01 s] seq=40  P=95060 Pa  T=22.25 C  age=26 ms
alt:  h=0.18 m  P0=95062 Pa  seq=40  age=26 ms
diag:  seq=40  age=26 ms  valid=1  published=40
...
stats: 2200 samples  fetch 24/24/24 ms (min/avg/max)  dt 50/50/50 ms  overruns=0  errors=0  seq=2390
```

Each telemetry line carries the values, the acquisition timestamp's `age` (how old the
snapshot was when it was read) and the sequence number, so a consumer's own lag is visible.
The `diag` line is the second, independent reader; it exists to prove that reading does not
consume a sample.

`-DryRun` prints the exact OpenOCD command line without touching hardware. The full walkthrough — what every `-c` argument does, why the RTT commands need the `stm32f4x.cpu` prefix and must follow `init`, where the `0x20000000 0x8B8` search window comes from, and why `CONFIG_UART_CONSOLE=n` is mandatory here — is in **[docs/console-rtt.md](docs/console-rtt.md)**.

## Architecture and shared memory

Acquisition, altitude, printing and diagnostics run as separate Zephyr threads. They share nothing
except two latest-value stores, and a read never removes a value, so any number of consumers can look at
the same sample at their own rates.

| Thread | Period | Priority | Stack | Responsibility |
|---|---|---|---|---|
| `sensor` | 50 ms, absolute deadline | 5 | 2048 B | Fetch, timestamp, publish; count errors and overruns; publish timing stats |
| `altitude` | 50 ms, absolute deadline | 6 | 2048 B | Read a measurement copy, calibrate `P₀`, publish altitude; never prints |
| `telemetry` | 500 ms | 8 | 1024 B | Read both stores, print samples, altitude and the ten-second `stats:` line |
| `diag` | 1000 ms | 9 | 1024 B | Second independent reader (temporary) |

Lower number means higher priority, so the sensor thread preempts every consumer and a
stalled `printk` under RTT back-pressure cannot push the 50 ms deadline. Altitude sits at 6
— above the console, below the sensor — because it consumes every sample but never prints.
`main` only initialises the stores, starts the threads and idles.

The contract is small and enforced by the API rather than by convention:

- `Measurement` carries pressure (Pa), temperature (milli-°C), acquisition timestamp,
  sequence and a validity flag — one coherent snapshot, never separate fields.
- `Altitude` carries the height in cm plus the source sequence and timestamp, the frozen
  `P₀`, a validity flag and whether calibration finished.
- `SensorDataStore` and `AltitudeStore` both copy a whole struct under one `k_mutex`; the
  lock is never held across I²C, printing or arithmetic.
- A failed fetch publishes `valid = false` with the error code and does **not** advance the
  sequence, so a failure cannot be mistaken for a new measurement.
- Consumers check `valid` and the age against the 250 ms stale timeout before using a value
  for a decision.
- Timing counters are published as a second snapshot and printed by a consumer, so the
  acquisition thread never blocks on the console.

Details and the reasoning behind each constant: **[docs/architecture.md](docs/architecture.md)**
and **[docs/decisions.md](docs/decisions.md)**.

## Local development environment

- Zephyr/West workspace: `C:\Users\ChristianSemenov\zephyrproject` (Zephyr 4.4.2 in `deps\zephyr`).
- Toolchain: `C:\zephyr-sdk-1.0.1`; Zephyr Workbench host tools environment: `%USERPROFILE%\.zinstaller\env.ps1`.
- Workbench application board: `olimex_stm32_h407`; configuration name: `primary`.
- C++ sources live in `src/`: `main.cpp` (startup), `shared_data.cpp` (the stores), `altitude.cpp` (the pure calculation), and one file per thread (`sensor_task.cpp`, `altitude_task.cpp`, `telemetry_task.cpp`, `diag_task.cpp`); `app_config.hpp` holds the timing, stack and priority constants. Host tests live in `tests/` and need no board. `prj.conf` enables `CONFIG_CPP=y` and `CONFIG_STD_CPP17=y`.
- Zephyr builds C++ with `-nostdinc++` against its minimal C++ library, so `<cmath>` is unavailable; `altitude.cpp` uses picolibc's `<math.h>` (`expm1`, `log1p`).
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

Live acquisition, the synchronized shared-memory path and relative altitude are verified on
hardware. **The next milestone is the LED manager**, which is also the piece that removes
the temporary heartbeat from `main()`.

1. Add the single-owner LED manager: a pure decision function over (`now`, deployment
   request, heat request) with absolute deadlines, so it can be tested without hardware.
2. Add apogee detection: 750 ms history of altitude, negative average velocity above a
   configurable minimum, latched once per flight.
3. Add the temperature monitor, then verify the overlap cases (heat overriding deployment,
   cooling before and after the original deadline).
4. Build the replay harness so synthetic traces enter at the measurement publisher, and
   record the acceptance scenarios.

Known limitation: apogee detection, the temperature monitor and the LED manager are not
implemented. The LED is currently a bare 500 ms heartbeat in `main`, and the `diag` thread is
a temporary reader. Altitude is relative to a bench-measured `P₀`; absolute accuracy is
±1 hPa (≈ ±8 m), and no physical height change has been demonstrated yet — only stationarity
of ±0.9 m peak-to-peak.

The detailed work plan and assignment/requirements currently live outside this repository at `G:\My Drive\Engineering\SCAE\Interview Test\`.
