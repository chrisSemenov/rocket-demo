# Rocket Flight Controller Demo

A **bench demonstration** for the Olimex STM32-H407, Bosch BMP280 pressure/temperature sensor, and Zephyr RTOS. Sensor acquisition, altitude, apogee detection and temperature monitoring are separate threads; measurement data moves through synchronized shared memory, and a single LED manager owns the output priority. Acquisition, the shared-memory contract, relative altitude, apogee detection, the temperature alarm and the LED arbitration are implemented and verified on the target; the demonstration video is not yet recorded. Deployment is simulated **only by the board LED**; this is not flight-ready or safety-certified firmware.

The task and its mandatory requirements are included in the repository:
[docs/assignment.md](docs/assignment.md) and [docs/requirements.md](docs/requirements.md).
Every acceptance row is recorded in [docs/acceptance-matrix.md](docs/acceptance-matrix.md),
with the raw console capture it came from in `tests/evidence/m10/`.

## Current status

- Zephyr 4.4.2 and Zephyr SDK 1.0.1 are installed and have built firmware for `olimex_stm32_h407`.
- **Working hardware baseline:** this project's C++ LED blink test builds successfully with Zephyr 4.4.2 and SDK 1.0.1. ST-Link programming and LED operation on the Olimex STM32-H407 were confirmed by the user after applying the project configuration. The firmware toggles the green user LED every 500 ms (one complete cycle per second).
- The project includes the PC13 LED overlay, ST-Link flash settings, and a VS Code debug launch configuration, carried over from the earlier single-file `blinky` bring-up project (kept outside this repository).
- **BMP280 sensor working (verified on hardware, 2026-10-28):** `app.overlay` enables `i2c2` on PF1 (SCL) and PF0 (SDA) with a `bosch,bme280` node at address `0x76`. Flash programming verified OK, OpenOCD found the RTT control block at `0x20000810`, and live samples were captured (≈94 940 Pa, ≈26 °C). 94.9 kPa is consistent with roughly 550 m elevation, and the driver only reports the device ready after validating the chip ID over I²C at `0x76`.
- **Sampling reworked for the descent test:** the sensor runs 8× pressure / 2× temperature in forced mode on a 50 ms deadline-based loop, so the 750 ms velocity window contains a full set of samples. The loop reports its own fetch duration, inter-sample interval and deadline overruns on the console. See [Sampling design](#sampling-design).
- **Acquisition is its own thread (M4, verified on hardware):** `sensor` owns the BMP280 and the only I²C traffic, fetches on a 50 ms absolute deadline, and publishes a complete snapshot. Measured over 2 750 samples: fetch 24 ms, interval 50 ms, zero overruns, zero errors. Evidence: [docs/verification/at-02-live-acquisition.md](docs/verification/at-02-live-acquisition.md); the failure path is covered in [m4-absent-sensor.md](docs/verification/m4-absent-sensor.md).
- **Shared memory is synchronized (M5, verified on hardware):** readers copy a `Measurement` out of `SensorDataStore` under a mutex and never consume it, so the 500 ms telemetry consumer and an independent 1 s reader observe the same sequence at their own rates. Evidence: [docs/verification/m5-shared-memory.md](docs/verification/m5-shared-memory.md).
- **Altitude is calculated and published (M6, verified on hardware):** a separate `altitude` thread reads the measurement snapshot, freezes a baseline `P₀` after 2 s of stationary pressure, and publishes a separate `AltitudeStore`. The maths is unit-tested on the host against independently computed references. Evidence: [docs/verification/at-03-altitude.md](docs/verification/at-03-altitude.md).
- **The LED has a single owner (M7, verified on hardware):** `led_manager` is the only writer of PC13. It arbitrates heat blink > deployment ON > off from a pure function, and the deployment deadline is latched once. Measured: 250 ms half-period, 10 s indication, 11–21 ms request latency. Evidence: [docs/verification/led-manager.md](docs/verification/led-manager.md).
- **Apogee detection is implemented and verified on the target (M8):** the `apogee` thread keeps its own 750 ms altitude window and triggers when the **average velocity over that window** is at or below -0.30 m/s while above 1.5 m and at least 25 cm below the peak. A least-squares slope over the same window is logged alongside it as a diagnostic, never as the trigger. 41 host checks cover the trigger, the two estimators, gaps, duplicates, the peak confirmation and the latch; replay runs on the board produce exactly one deployment per flight. Evidence: [docs/verification/at-04-at-06-apogee.md](docs/verification/at-04-at-06-apogee.md).
- **The temperature alarm is implemented (M9):** the `heat` thread applies a 5 s window that renews while the temperature stays above the release threshold and clears on cooling, with 0.2 C of hysteresis and a fail-safe that never treats missing data as cool. 37 host checks; replay runs on the target cover a single window, two renewals and both overlap cases. Evidence: [docs/verification/at-07-heat.md](docs/verification/at-07-heat.md).
- **Acceptance evidence:** every mandatory scenario is recorded in [docs/acceptance-matrix.md](docs/acceptance-matrix.md), with the action taken and the observed console values.
- **Not implemented:** the demonstration video and a physical (non-replayed) apogee descent.

## Hardware and known board detail

- Target: Olimex STM32-H407 (revision B) with STM32F407ZGT6; debugger: ST-Link V2 over SWD; power: 5 V from the ST-Link/USB while tethered, or the board's 6–16 V DC jack for untethered runs. No USB-UART adapter is used at any point: the console is RTT over SWD.
- The BMP280 breakout model is not recorded; the straps were verified from the chip ID read at `0x76` (`0x58`), with `CSB` tied to 3.3 V and `SDO` to GND.
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
- a gap longer than the 250 ms stale timeout clears the window **while calibrating**
  and publishes an invalid altitude; once `P₀` is frozen, a gap only invalidates
  the output and never moves the reference;
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

## Apogee detection

The `apogee` thread turns altitude into a deployment decision. It is a separate
thread with its own history; it reads the altitude snapshot and publishes at most
one request per flight.

```
v = (h(now) - h(now - 750 ms)) / 0.750 s     window start interpolated
deploy when  h(now) >= 1.50 m  and  v <= -0.30 m/s  and  h(now) <= peak - 25 cm
```

All three conditions are required, and the event latches. A least-squares slope
over the same window is computed and printed as `slope=`; it is a diagnostic, not
the trigger.

### Why each piece exists

| Piece | Reason |
|---|---|
| **750 ms window** | The assignment's descent criterion. Timestamp-based, so a missed sample does not silently shorten it. |
| **Average over the window** | `(h(now) - h(now-750 ms)) / 0.75`, with the start height interpolated between the bracketing samples. This is the assignment's wording; the endpoint is what decides. |
| **Slope diagnostic** | A least-squares fit over the same window is logged as `slope=`. It is smoother on a straight ramp but differs in sign on a bounce, so it does not arm the trigger. |
| **1.50 m minimum** | About twice the observed altitude noise (0.9 m peak-to-peak). Without it, a bench wobble at "altitude 0" could satisfy a negative velocity. |
| **−0.30 m/s margin** | A noise guard, not a performance threshold. Falling from rest at apogee, a 0.3 m/s trailing average is reached **~0.41 s and ~0.8 m** below the peak. Larger margins only delay detection. |
| **25 cm drop confirmation** | A lift sets a new peak every sample, so its drop is always zero and it cannot fire no matter how noisy the pressure is. |
| **Fixed 32-sample history** | 1.55 s at 20 Hz, which bounds the 750 ms window; the time bound is 2 s but capacity is reached first. Owned by the thread, not a third shared store. |
| **Duplicate rejection** | The thread polls at 50 ms, but a slow pass can read the same sequence twice. A repeated sequence is ignored, not counted as new flight data. |
| **Gap handling** | A hole longer than the 250 ms stale timeout clears the window; the next decision needs 750 ms of fresh samples. |
| **Latch** | Once deployed, the detector ignores everything and the request store refuses a second event. Two independent guards against FLT-04. |

### What "negative average velocity" actually promises

It is the average over 750 ms: the current altitude is lower than it was 750 ms
ago. It does **not** mean every intermediate sample descended - a bounce on the
way down still satisfies it as long as the net change over the window is
downward - and it does not identify the exact peak. That is what FLT-03 asks for,
and the README does not claim more. The logged `slope=` value is a different
estimate of the same window and is there for comparison, not for the decision.

### Configuring the trigger

Two numbers are configurable, in `Kconfig` and `prj.conf`:

```conf
CONFIG_APP_APOGEE_MIN_ALT_CM=150
CONFIG_APP_APOGEE_MIN_DESCENT_MM_S=300
```

A different build changes the behaviour without touching source:

```powershell
west build -p always -b olimex_stm32_h407 -d build/shortdrop . -- `
  -DCONFIG_APP_APOGEE_MIN_ALT_CM=30 -DCONFIG_APP_APOGEE_MIN_DESCENT_MM_S=150
```

For a hand-performed test there is a ready-made preset, `bench.conf` (50 cm,
0.60 m/s), which exists because room air drift reaches 0.55 m/s and would
otherwise mask a hand move:

```powershell
west build -p always -b olimex_stm32_h407 -d build/bench . -- "-DEXTRA_CONF_FILE=bench.conf"
```

The helper script accepts the same build directory, so the bench image can be
flashed (and streamed) with the usual procedure:

```powershell
tools\rtt-session.ps1 -Flash -BuildDir build\bench
```

Without `-BuildDir` it uses `build\primary`, which keeps the default image on the
board.

What is deliberately **not** configurable: the 750 ms window, the 10 s
deployment indication, the 5 s alarm and the 500 ms blink period. Those are the
behaviour the assignment specifies; making them tunable would let the demo drift
away from the requirement.

### Where the numbers came from

This distinction matters, so it is stated rather than implied:

| Value | Origin |
|---|---|
| A minimum altitude threshold | **Assignment** (FLT-02: "configurable minimum threshold") |
| 750 ms averaging window | **Assignment** (FLT-03) |
| Negative average velocity | **Assignment** (FLT-03) |
| **1.50 m** | **Ours.** ~2× the measured altitude noise (0.89 m peak-to-peak), so noise alone cannot reach it |
| **0.30 m/s** margin | **Ours.** A noise guard, deliberately small: it must not delay detection beyond ~0.4 s after apogee. The assignment only says "negative"; 0 is available via Kconfig |

The margin is a deliberate tightening of the requirement, not a reading of it:
without it, a stationary sensor would satisfy "negative" on noise. The measured
numbers behind both choices are in
[docs/verification/at-04-at-06-apogee.md](docs/verification/at-04-at-06-apogee.md).

### Testing

The detector is a pure unit in `src/apogee.cpp`, so the window, gaps, duplicates
and latch are all exercised without hardware:

```powershell
powershell -ExecutionPolicy Bypass -File tests\run-tests.ps1
```

41 checks, including the exact threshold (`150 cm` deploys, `149 cm` does not),
an incomplete window, a 1 s outage, out-of-order timestamps and a second flight
after `reset()`.

## Temperature alarm

The `heat` thread reads the same measurement snapshot as everything else, applies
the overheat policy, and publishes a heat request. It never calls the sensor and
never touches the LED.

```
hot    when  T ≥ 30.0 °C
alarm  starts on the hot sample, lasts 5 s
renew  60 ms before expiry while still hot, another 5 s (anchor unchanged, no gap)
clear  immediately when T < 29.8 °C   (threshold − 0.2 °C hysteresis)
```

### Why each rule exists

| Rule | Reason |
|---|---|
| **Threshold 30.0 °C** | Above a still bench (~22 °C here) and reachable with a finger. Configurable via `CONFIG_APP_HEAT_THRESHOLD_MC` |
| **0.2 °C hysteresis** | Sensor noise around the boundary would otherwise start and clear the alarm repeatedly |
| **Renew just before expiry, keep the anchor** | Renewing exactly at expiry leaves the LED dark for one poll (~100 ms). The guard extends the window before it lapses; the blink phase is anchored to the alarm start, so a renewal does not make it jump |
| **No back-filling** | If samples were missed, the renewal starts a fresh window rather than replaying the ones that would have elapsed |
| **Missing data is not cooling** | A stale or failed sensor must not silently cancel an overheat alarm. The state holds and the window expires on its own deadline |
| **Duplicate sequences ignored** | The thread polls faster than the sensor publishes |

### Testing

The policy is a pure unit (`src/heat.cpp`), so persistence, renewal, cooling and
the hysteresis band are exercised without hardware:

```powershell
powershell -ExecutionPolicy Bypass -File tests\run-tests.ps1
```

37 checks: window start, no restart on repeated hot samples, renewal at expiry,
renewal after a gap, immediate cooling, hysteresis, threshold equality, missing
data and reset.

## Acceptance status

Every mandatory scenario from the requirements is recorded in
[docs/acceptance-matrix.md](docs/acceptance-matrix.md): the action taken and the
observed console values for AT-01 to AT-11. Two things remain open there: the
demonstration video, and a physical (non-replayed) apogee descent.

## Replay mode (acceptance testing)

The acceptance scenarios cannot be flown on a desk, so finished traces are
played into the **same measurement store** the sensor writes to. Everything
downstream - altitude, apogee, heat, LED - runs unmodified; only the source of
the samples differs.

```
TEST MODE: REPLAY at05 (live sensor not used)
input: REPLAY at05, 50 ms cadence (live sensor not used)
```

- Exactly one publisher per boot: replay mode never creates the sensor thread.
- Traces live in `traces/*.csv`, generated by `tools/make-traces.py` from height
  profiles via the inverse barometric model, and embedded as
  `src/traces_data.hpp`.
- The pace comes from the trace's own timestamps (`t[i+1] - t[i]`), replayed in
  real time, so the 10 s indication and the 250 ms blink are genuine and
  filmable, and an irregularly spaced trace is not silently retimed.

```powershell
west build -p always -b olimex_stm32_h407 -d build/replay . -- `
  "-DCONFIG_APP_REPLAY=y" "-DCONFIG_APP_TRACE_AT05=y"
tools\rtt-session.ps1 -Flash -BuildDir build\replay
```

Results for AT-04, AT-05, AT-05b, AT-06, AT-07, AT-08, AT-09 and AT-11 are
recorded in [docs/verification/replay.md](docs/verification/replay.md), with the
raw console captures in `tests/evidence/m10/`.
## LED indication

One thread owns the LED. Nothing else writes the pin; the other tasks publish
*requests* and the manager decides. That is what makes priority enforceable
rather than a race between two `gpio_pin_set_dt()` calls.

| Priority | Condition | Output |
|---|---|---|
| 1 | heat alarm active | 250 ms on / 250 ms off, 500 ms period |
| 2 | deployment, deadline not reached | steady on for 10 s from the event |
| 3 | — | off |

The heat alarm has more than one window: while the temperature stays above the
threshold the 5 s window renews, and the blink phase is anchored to the original
start, so a renewal does not make the LED jump. Cooling clears the alarm at once
and the deployment indication resumes with whatever time it has left.

Plus one **boot indication**, which is not an assignment behaviour: when the
sensor is answering, the baseline is frozen and the 750 ms history is full, the
LED flashes twice (100 ms on, 120 ms off, 100 ms on) and then stays off. It runs
once per boot and only if no flight indication is active. Disable it with
`CONFIG_APP_READY_INDICATION=n`.

For an untethered run this is the only way to know the system is armed:

| What you see | Meaning |
|---|---|
| two short flashes ~3 s after power-on, then dark | sensor OK, baseline frozen, detector armed |
| no flashes at all | sensor not answering or calibration failed — nothing will trigger |
| solid light | deployed; off after 10 s |
| 250/250 blinking | overheating |

Rules that make the overlap cases behave:

- **Deadlines are absolute and set once.** Apogee sets `event + 10 s`; the
  manager only reads it. Heat can take the output for seconds without pausing,
  extending or restarting the deployment clock.
- **The latch lives in the store.** The first accepted deployment sets the
  deadline; later requests are ignored inside `LedRequestStore`, so a producer
  bug cannot extend the indication either.
- **Blink phase is anchored to the alarm start**, not to when the loop runs, so
  the 250 ms grid cannot drift if a period is late.
- **Windows are half-open**: active while `start <= now < deadline`. At the
  deadline instant the condition is already over — one documented rule, tested
  at exactly that instant.
- **Heat renewal keeps the original start** and only moves the deadline, so
  extending an alarm does not make the LED jump.

The decision itself is a pure function, `led::decide(now_ms, request)`, in
`src/led_logic.cpp`. It has no clock, GPIO or Zephyr dependency, so the 10- and
5-second windows are stepped through instantly in tests:

```powershell
powershell -ExecutionPolicy Bypass -File tests\run-tests.ps1
```

### Overlap timing

Two timelines, both measured from the deployment event at `t = 0`:

```text
AT-08  heat starts 2 s in, cools at 7 s

LED   ██████████▓▓▓▓▓▓▓▓▓▓██████
      0         2          7    10  s
      steady ON  blink      steady ON
                 (250/250)  (the remaining 3 s, not a new window)
                             off at 10 exactly

AT-09  heat starts at 8 s and runs past the deadline

LED   ██████████▓▓▓▓▓▓▓▓▓▓▓▓▓▓
      0         8    10      13  s
      steady ON  blink (deadline passes while blinking)
                        cooling at 13 gives OFF, not steady ON
```

### Ready flash

The boot indication is not an assignment behaviour; `demo.conf` disables it for
the recorded demonstration so the LED shows only deployment and overheating.

### Watching the sequence without flight logic

The arbitration can be exercised on the board before apogee and the temperature
monitor exist. Build with the self-test option and the firmware publishes
scripted requests through the real stores:

```powershell
west build -p always -b olimex_stm32_h407 -d build/selftest . -- -DAPP_LED_SELFTEST=ON
```

It prints `TEST MODE: LED SELFTEST is active` at boot and runs an eight-step,
~30 s sequence: deployment, heat override, early cooling, expiry, heat alone,
early cooling, heat to the end of the window, and a second deployment request
that must be ignored. This is a test build only; it is off by default and the
released image never contains it.

## Console output over ST-Link only

There is no USB-UART adapter, so `printk()` goes to a **SEGGER RTT** ring buffer in RAM (`CONFIG_USE_SEGGER_RTT`, `CONFIG_RTT_CONSOLE`) that is read over the existing SWD connection. Two helper tools wrap the whole procedure: `tools\rtt-session.ps1` starts OpenOCD, and `tools\rtt-view.py` prints the stream.

Terminal 1 — add `-Flash` on the first run or after any code change; omit it when the board already runs the current build:

```powershell
tools\rtt-session.ps1 -Flash
```

Terminal 2:

```powershell
python tools\rtt-view.py
```

Captured from the board:

```
*** Booting Zephyr OS build v4.4.2 ***
BMP280 ready: I2C2 SCL=PF1 SDA=PF0 addr=0x76, forced mode 8x/2x, 50 ms period
threads: sensor(5) altitude(6) led(7) telemetry(8) diag(9); stale timeout 250 ms
led:  [      0.011] off
telemetry: no valid measurement yet (error=0)
alt:  not available yet (baseline 0/38 samples)
diag:  seq=0  age=-1 ms  valid=0  published=0
[      0.51 s] seq=10  P=95111 Pa  T=23.24 C  age=26 ms
alt:  not available yet (baseline 10/38 samples)
[      1.01 s] seq=20  P=95111 Pa  T=23.25 C  age=26 ms
diag:  seq=20  age=26 ms  valid=1  published=20
alt:  baseline frozen P0=95110 Pa after 38 samples (spread 5 Pa)
[      2.01 s] seq=40  P=95109 Pa  T=23.26 C  age=26 ms
alt:  h=0.09 m  P0=95110 Pa  seq=40  age=26 ms
diag:  seq=40  age=26 ms  valid=1  published=40
...
stats: 1800 samples  fetch 24/24/24 ms (min/avg/max)  dt 50/50/50 ms  overruns=0  errors=0  seq=1990
```

This capture predates the removal of the temporary `diag:` reader; those lines no longer appear. Each telemetry line carries the values, the acquisition timestamp's `age` (how old the
snapshot was when it was read) and the sequence number, so a consumer's own lag is visible.
The store has several independent readers; reading never removes a sample, so
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
| `apogee` | 50 ms | 7 | 2048 B | 750 ms window, deploy once when above the minimum and descending |
| `heat` | 50 ms | 8 | 1024 B | 5 s overheat window with renewal and hysteresis; publishes a heat request |
| `led` | 10 ms | 9 | 1024 B | Read the request snapshot, arbitrate, write PC13; prints only on a transition |
| `telemetry` | 500 ms | 10 | 1024 B | Read the stores, print samples, altitude and the ten-second `stats:` line |

Lower number means higher priority, so the sensor thread preempts every consumer and a
stalled `printk` under RTT back-pressure cannot push the 50 ms deadline. Altitude outranks
apogee, so a deployment decision is never made from a stale estimate; the LED and console
sit below both. `main` only initialises the stores, starts the threads and idles.

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

- Zephyr/West workspace on this machine: `%USERPROFILE%\zephyrproject` (Zephyr 4.4.2 in `deps\zephyr`).
- Toolchain: `C:\zephyr-sdk-1.0.1`; Zephyr Workbench host tools environment: `%USERPROFILE%\.zinstaller\env.ps1`.
- Workbench application board: `olimex_stm32_h407`; configuration name: `primary`.
- C++ sources live in `src/`: `main.cpp` (startup), `shared_data.cpp` (the stores), `bus_recovery.cpp` (the boot I2C step), the pure units `altitude.cpp`, `led_logic.cpp`, `led_pulse.cpp`, `apogee.cpp` and `heat.cpp`, and one file per thread (`sensor_task.cpp`, `altitude_task.cpp`, `apogee_task.cpp`, `heat_task.cpp`, `led_manager.cpp`, `telemetry_task.cpp`), plus `replay.cpp` for the trace player; `app_config.hpp` holds the timing, stack and priority constants. `led_selftest.cpp` is only compiled when `APP_LED_SELFTEST=ON`. Host tests live in `tests/` and need no board. `prj.conf` enables `CONFIG_CPP=y` and `CONFIG_STD_CPP17=y`.
- Zephyr builds C++ with `-nostdinc++` against its minimal C++ library, so `<cmath>` is unavailable; `altitude.cpp` uses picolibc's `<math.h>` (`expm1`, `log1p`).
- This repository is a freestanding application: it uses the separate West workspace and SDK; it does not contain Zephyr itself. `west-manifest.yml` is the workspace snapshot this project was built against (`west init` from it if you are starting from nothing). `.vscode/settings.json` contains a machine-specific workspace path — point it at your own checkout of that workspace, or ignore it and use the terminal below.
- The existing build output under `build/` is ignored by Git and should be regenerated locally.

With the Zephyr workspace and host tools installed, build from an activated VS
Code PowerShell terminal. Replace `<path-to-your-clone>` with this repository's
location, and `C:\zephyr-sdk-1.0.1` with wherever the SDK is installed (that is
the default installer location, and it is not inside the workspace). Nothing
depends on the repository being under `C:\SCAE`:

```powershell
. "$env:USERPROFILE\.zinstaller\env.ps1"      # Workbench host tools; omit if west is already on PATH
$env:ZEPHYR_BASE = "$env:USERPROFILE\zephyrproject\deps\zephyr"
$env:ZEPHYR_SDK_INSTALL_DIR = "C:\zephyr-sdk-1.0.1"
$env:ZEPHYR_TOOLCHAIN_VARIANT = 'zephyr'
cd <path-to-your-clone>
west build -p always -b olimex_stm32_h407 -d build/primary .
```

Check both paths before building: `$env:ZEPHYR_BASE` must point at the Zephyr
source tree inside the workspace (`<workspace>\deps\zephyr`), and
`$env:ZEPHYR_SDK_INSTALL_DIR` at the SDK root (the directory containing `gnu`
and `hosttools`), not at the workspace.

This build command was verified from a clean clone of the release candidate as
well as locally (see `docs/acceptance-matrix.md`, "Reproducibility check").
Explicit workspace/SDK variables are needed in a standalone terminal; activating
the host tools alone did not locate them here.

With the board correctly powered and the same ST-Link SWD wiring used for the reference project, use Workbench's Flash action or run in the same terminal:

```powershell
west flash -d build/primary --runner openocd -- --config openocd-stlink.cfg
```

For debugging, select **Zephyr Workbench Debug [primary]** in VS Code's Run and Debug view after building. `.vscode/launch.json` is configured to start the SDK's OpenOCD directly, connect GDB on port 3333, program the ELF, and break at `main()`. It does not depend on generated `.debug` helper files. Resume execution to observe blinking; stop any existing debug/OpenOCD session before flashing.

## Next steps

All six requirements are implemented, the replay harness is in place, and every mandatory
scenario is recorded in [docs/acceptance-matrix.md](docs/acceptance-matrix.md). What remains:

1. Record the demonstration video: live readings, a replay deployment with the 10 s light, the
   heat alarm and the overlap case.
2. Tag the commit that matches the video and publish it.

Known limitations, stated rather than hidden:

- No real ascent or descent has been flown. Apogee behaviour is proven by the replay traces and
  by a live run that shows no false trigger; a genuine flight profile is untested.
- A "flight reset" is a power cycle. `Detector::reset()` exists and is unit-tested, but there is
  no button or console command to re-arm.
- Thresholds are bench values (1.5 m, 0.3 m/s, 25 cm, 30 C), chosen against measured noise and
  configurable in Kconfig; they are not flight values.
- There is no persistent run log. An SD-card attempt was built and abandoned: the card never
  mounted reliably and the STM32 SD read path faulted. It is recorded in
  [docs/verification/sd-log-attempt.md](docs/verification/sd-log-attempt.md); evidence comes
  from RTT captures.
- The temperature alarm was exercised with replayed and lowered-threshold temperatures, not by
  warming the real sensor to 30 C.

The work plan, assignment and requirements are included here: `docs/assignment.md`
is the task as given, and `docs/requirements.md` is the derived checklist with the
acceptance IDs used above.
