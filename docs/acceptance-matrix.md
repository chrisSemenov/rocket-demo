# Acceptance matrix

Every mandatory scenario from `requirements.md` §5, with the action taken and the
observed result. Rows are quoted from real console captures, not from intent.

**Board:** Olimex STM32-H407, BMP280 on I²C2 (SCL=PF1, SDA=PF0, 0x76)
**Firmware:** `build/primary` 47 520 B flash / 24 128 B RAM (live);
`build/replay` 72 420 B / 25 344 B (trace player)
**Commit:** the runs below were made from the tree that becomes the `v1.0` tag;
the exact hash is recorded in the tag message and in the video slate.

---

## AT-01 — build, flash and run on target

| | |
|---|---|
| **Action** | Pristine build (`west build -p always -b olimex_stm32_h407`), flash over ST-Link, reset with the sensor attached |
| **Expected** | Correct target starts, console alive, no faults |
| **Observed** | `** Verified OK **`; boot banner, `input: LIVE BMP280 … 50 ms period`, `threads: sensor(5) altitude(6) apogee(7) heat(8) led(9) telemetry(10)`, zero MPU faults |
| **Evidence** | This document, §"Live run" below; `docs/console-rtt.md` |

## AT-02 — real sensor measurements for at least 60 s

| | |
|---|---|
| **Action** | Live mode, 1 015 s continuous |
| **Expected** | Plausible pressure/temperature, units, source timestamps, measured rate, error counters |
| **Observed** | `stats: 20000 samples  fetch 24/24/24 ms  dt 50/50/50 ms  overruns=0  errors=0`; ≈95 229 Pa, ≈23.1 °C; `age=26 ms` |
| **Evidence** | `docs/verification/at-02-live-acquisition.md` (earlier 2 750-sample run); the numbers above from the final build |

## AT-03 — altitude from a documented pressure/reference pair

| | |
|---|---|
| **Action** | Host tests against independently computed references, then live baseline |
| **Expected** | Agreement within the stated tolerance; `P == P₀` gives 0 |
| **Observed** | 17 checks pass; live `baseline frozen P0=95110 Pa after 38 samples (spread 5 Pa)`, `h=0.00 m`, stationary spread 0.89 m |
| **Evidence** | `docs/verification/at-03-altitude.md` |

## AT-04a — descend while staying below the minimum

| | |
|---|---|
| **Action** | Replay `at04a`: 2 s baseline, descend to −1.2 m |
| **Expected** | No deployment |
| **Observed** | `deploys: 0`, `h=-1.15 m  state=ready` |
| **Evidence** | `docs/verification/replay.md` §AT-04a |

## AT-04b — above the minimum but ascending or flat

| | |
|---|---|
| **Action** | Replay `at04b`: climb to 4 m and hold |
| **Expected** | No deployment |
| **Observed** | `deploys: 0`, `h=3.99 m  v=0.000 m/s` |
| **Evidence** | `docs/verification/replay.md` §AT-04b |

## AT-05 — peak, then a full 750 ms window of descent

| | |
|---|---|
| **Action** | Replay `at05`: baseline, climb to 6 m, peak, descend at 2 m/s |
| **Expected** | One trigger at the first qualifying sample; steady LED for 10 s |
| **Observed** | `apogee: DEPLOY at 5.41 m, v=-0.495 m/s; LED on until 15611 ms (seq=113)`; `led: [5.611] on (deployment)` → `[15.611] off` = **10.000 s** |
| **Evidence** | `docs/verification/replay.md` §AT-05 |

## AT-05b — literal thresholds (specification cross-check)

| | |
|---|---|
| **Action** | Replay `at05` with `CONFIG_APP_APOGEE_MIN_DESCENT_MM_S=0` and `CONFIG_APP_APOGEE_MIN_DROP_CM=0` — the assignment's wording with no extra guard |
| **Expected** | Triggers as soon as the 750 ms average is negative |
| **Observed** | `apogee: DEPLOY at 5.50 m, v=-0.167 m/s; LED on until 15561 ms` — one event, LED 5.561 → 15.561 |
| **Why it matters** | Confirms the implementation can be run exactly as written in the assignment; the shipped defaults add a 0.3 m/s margin and a 25 cm drop confirmation, which delay the trigger slightly in exchange for immunity to air drift |

## AT-06 — continue descending after deployment

| | |
|---|---|
| **Action** | Replay `at05` continues to −9.4 m for another 15 s |
| **Expected** | No second trigger; original deadline unchanged |
| **Observed** | One `DEPLOY` line only; deadline stayed `15611`; `REPLAY TEST: done, 381 samples published` |
| **Evidence** | `docs/verification/replay.md` §AT-05 |

## AT-07a — heat only, one complete window

| | |
|---|---|
| **Action** | Replay `at07a`: altitude flat at 0, 32 °C from 4 s to 9 s |
| **Expected** | 500 ms full-period blink, ≈250/250 ms, for 5 s; no deployment |
| **Observed** | `led: [4.011] blink on` … `[9.011] off` = **5.000 s**, 250 ms half-periods; `deploys: 0` |
| **Evidence** | `docs/verification/replay.md` §AT-07a |

## AT-07b — heat beyond the first window, cooled early

| | |
|---|---|
| **Action** | Replay `at07b`: 32 °C from 4 s to 14 s |
| **Expected** | Renewal while hot; early cooling clears; no deployment indication |
| **Observed** | Deadlines `9011 → 14011 → 19011`, blink 4.011 → 14.011 with no gap, `cooled … alarm cleared`, `led: [14.011] off` |
| **Evidence** | `docs/verification/replay.md` §AT-07b |

## AT-08 — deployment, then heat, then cooling before the deadline

| | |
|---|---|
| **Action** | Replay `at08`: deployment at ~5.6 s, heat 7.3–11.0 s |
| **Expected** | ON 0–2 s after deploy, blink 2–7 s, steady ON for the remaining 7–10 s, then OFF |
| **Observed** | `[5.611] on` → `[7.311] blink on` → `[11.011] on` → `[15.611] off` (4.6 s of the original window) |
| **Evidence** | `docs/verification/replay.md` §AT-08 |

## AT-09 — heat after the deployment deadline, then cooling

| | |
|---|---|
| **Action** | Replay `at09`: deployment expires at 15.611 s, heat 17.5–22.5 s |
| **Expected** | Blink overrides nothing; cooling after the deadline must not restore steady ON |
| **Observed** | `[15.611] off`; `heat: HOT … 22511`; `[22.511] off` — no steady ON |
| **Evidence** | `docs/verification/replay.md` §AT-09 |

## AT-10 — all consumers concurrently

| | |
|---|---|
| **Action** | Live run, 1 015 s, all threads active |
| **Expected** | Independent coherent reads; no consumption, deadlock, starvation or GPIO race |
| **Observed** | 20 300 samples; `stats: 20000 samples … overruns=0 errors=0`; altitude, apogee, heat and telemetry all reading concurrently (`alt: seq=20320`, `apogee: seq=20318`, single LED owner); zero faults |
| **Evidence** | This document §"Live run"; `docs/verification/m5-shared-memory.md` (two-reader proof) |

## AT-11 — restart the flight

| | |
|---|---|
| **Action** | Reset the board (power cycle or flash), repeat the valid trace |
| **Expected** | Fresh calibration and history; exactly one new deployment |
| **Observed** | Every replay run above followed a reset and produced exactly one deployment; a fresh baseline was rebuilt each time (`baseline frozen P0=…`) |
| **Evidence** | `docs/verification/replay.md` §AT-11 |

---

## Live run (AT-01 / AT-02 / AT-10)

```
*** Booting Zephyr OS build v4.4.2 ***
input: LIVE BMP280, I2C2 SCL=PF1 SDA=PF0 addr=0x76, forced mode 8x/2x, 50 ms period
threads: sensor(5) altitude(6) apogee(7) heat(8) led(9) telemetry(10); stale timeout 250 ms
led:  [      0.011] off
…
stats: 20000 samples  fetch 24/24/24 ms (min/avg/max)  dt 50/50/50 ms  overruns=0  errors=0  seq=20190
[   1015.51 s] seq=20310  P=95229 Pa  T=23.10 C  age=26 ms
```

1 015 s continuous, 20 300 samples, zero overruns, zero errors, zero faults.

## What is **not** demonstrated

- **A real flight.** No ascent/descent was flown; apogee evidence is replay plus a
  live no-false-trigger run.
- **The physical LED on film.** Timing above comes from transition logs at 10 ms
  resolution; the video is the remaining deliverable.
- **AT-07 timing on real hardware heating.** The policy ran with a replayed
  temperature; the physical finger-on-sensor path is untested.
- **Flight-reset control.** "Reset" means power cycle; there is no button or
  command.

---

## Reproducibility check (2026-10-31)

The committed tree (`bfd4c4d`) was cloned to `C:\tmp\rocket-demo2` and built
**using only the README**:

| Step | Result |
|---|---|
| `git clone` of the working remote | all sources, overlay, `prj.conf`, `Kconfig`, `CMakeLists.txt`, OpenOCD config, traces present |
| `west build -p always -b olimex_stm32_h407 -d build/primary .` | 47 844 B flash / 24 128 B RAM |
| `tests\run-tests.ps1` | 158 checks, 6 suites, 0 failures |
| Flash over OpenOCD | `** Verified OK **` |
| Boot from the cloned image | `input: LIVE BMP280 �`, 0 deployments, 0 faults |

Nothing in the build depended on the original working directory.

## Trace drift check

`tools\check-traces.ps1` regenerates `traces/*.csv` and `src/traces_data.hpp`,
compares them with the committed copies, restores them if they differ and fails.
Verified both ways: passes on a clean tree, fails after a deliberate edit.