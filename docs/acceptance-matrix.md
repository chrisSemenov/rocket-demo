# Acceptance matrix

Every mandatory scenario from `requirements.md` section 5, with the action taken
and the observed result.

| | |
|---|---|
| Board | Olimex STM32-H407 rev B, BMP280 on I2C2 (SCL=PF1, SDA=PF0, address 0x76) |
| Firmware commit | `db3531c` (the tree these captures came from) |
| Live build | 48 864 B flash / 24 128 B RAM at `db3531c`; **49 740 B** after the 2026-11-01 peak estimate (`build/primary`) |
| Replay build | 74 248 B flash / 24 128 B RAM (`build/replay`); 74 256 B for the literal-threshold build; **75 124 B** for the 2026-11-01 `at05` re-run (`build/at05peak`) |
| Captured | 2026-10-31, RTT over ST-Link, no breakpoints, continuous runs |
| Raw captures | [`tests/evidence/m10/`](../tests/evidence/m10) -- unedited console output, one file per row |

Every "Observed" value below is a line from those files. Where a row is *not*
demonstrated, it says so instead of quoting an older run.

---

## AT-01 - build, flash and run on target

| | |
|---|---|
| **Action** | `west build -p always -b olimex_stm32_h407 -d build/primary .`, flash with `tools\rtt-session.ps1 -Flash`, reset with the sensor attached |
| **Expected** | Correct target starts, console alive, no faults |
| **Observed** | `** Verified OK **`; `i2c: boot recovery ok (ran before the sensor driver)`; `input: LIVE BMP280, I2C2 SCL=PF1 SDA=PF0 addr=0x76, forced mode 8x/2x, 50 ms period`; `threads: sensor(5) altitude(6) apogee(7) heat(8) led(9) telemetry(10)`; zero MPU faults in the run |
| **Evidence** | [live-2026-10-31.log](../tests/evidence/m10/live-2026-10-31.log), first 5 lines |

## AT-02 - real sensor measurements for at least 60 s

| | |
|---|---|
| **Action** | Live mode, 73.5 s continuous |
| **Expected** | Plausible pressure and temperature with units, source timestamps, measured rate, error counters |
| **Observed** | `stats: 1200 samples  fetch 24/24/24 ms (min/avg/max)  dt 50/50/50 ms  overruns=0  errors=0  recoveries=0 failed=0  seq=1390`; `[0.51 s] seq=10 P=95142 Pa T=22.98 C age=27 ms` through `[73.51 s] seq=1470 P=95143 Pa T=22.93 C age=27 ms` |
| **Note** | The stats snapshot is published every 200 successful samples, so the last published block covers up to sequence 1390 while telemetry had already reached 1470. That is the designed behaviour, not a gap. |
| **Evidence** | [live-2026-10-31.log](../tests/evidence/m10/live-2026-10-31.log) |

## AT-03 - altitude from a documented pressure/reference pair

| | |
|---|---|
| **Action** | Host reference tests, then live baseline calibration |
| **Expected** | Agreement within the stated tolerance; `P == P0` gives 0 m |
| **Observed** | 17 host checks pass, including seven pairs computed independently in 40-digit decimal arithmetic; live `alt: baseline frozen P0=95140 Pa after 38 samples (spread 8 Pa)`, then `alt: h=0.00 m P0=95140 Pa seq=50 age=27 ms` |
| **Evidence** | [at-03-altitude.md](verification/at-03-altitude.md); `tests/altitude_test.cpp` |

## AT-04a - descend while staying below the minimum

| | |
|---|---|
| **Action** | Replay `at04a`: 2 s baseline, descend to -1.2 m |
| **Expected** | No deployment, even though the average velocity is negative |
| **Observed** | `apogee: h=-1.15 m  v=-0.120  slope=-0.146  cov=1550 ms  n=32  state=ready  seq=158`; zero `DEPLOY` lines; LED `off` throughout |
| **Evidence** | [at04a-2026-10-31.log](../tests/evidence/m10/at04a-2026-10-31.log) |

## AT-04b - above the minimum but not descending

| | |
|---|---|
| **Action** | Replay `at04b`: climb to 4 m and hold |
| **Expected** | No deployment |
| **Observed** | `apogee: h=3.99 m  v=0.000  slope=0.000  cov=1550 ms  n=32  state=ready  seq=178`; zero `DEPLOY` lines |
| **Evidence** | [at04b-2026-10-31.log](../tests/evidence/m10/at04b-2026-10-31.log) |

## AT-05 - peak, then a full 750 ms window of descent

| | |
|---|---|
| **Action** | Replay `at05`: baseline, climb to 6 m, peak, descend at 2 m/s |
| **Expected** | One trigger at the first qualifying sample; steady LED for 10 s |
| **Observed** | `apogee: DEPLOY at 5.41 m, v=-0.360 m/s (slope=-0.495); LED on until 15611 ms (seq=113)`; `led: [5.611] on (deployment)` to `led: [15.611] off` = **10.000 s** |
| **Note** | `v` is the trigger value: `(h(now) - h(now-750 ms)) / 0.750 s` with the window start interpolated. `slope` is the least-squares fit over the same window and must also pass the margin; both are negative here, so the extra condition does not change the outcome on this trace. |
| **Re-run 2026-11-01** | The same binary logic with the peak estimate: `apogee: DEPLOY at 5.41 m, v=-0.360 m/s (slope=-0.495); estimated apogee 6.03 m at 5.01 s (drop 62 cm, delay 0.60 s); LED on until 15611 ms (seq=113)` -- same sample, same deadline. The linked capture predates the added fields. |
| **Evidence** | [at05-2026-10-31.log](../tests/evidence/m10/at05-2026-10-31.log) |

## AT-05b - the literal rule, with no added guard

| | |
|---|---|
| **Action** | Replay `at05` built with `CONFIG_APP_APOGEE_MIN_DESCENT_MM_S=0` and `CONFIG_APP_APOGEE_MIN_DROP_CM=0` -- "current altitude above the minimum and the 750 ms average negative", exactly as written |
| **Expected** | Triggers as soon as the 750 ms average is negative |
| **Observed** | `apogee: armed; threshold 150 cm, drop 0 cm, margin 0 mm/s`; `apogee: DEPLOY at 5.50 m, v=-0.120 m/s (slope=-0.167); LED on until 15561 ms (seq=112)`; `led: [5.561] on` to `led: [15.561] off` = 10.000 s; one event. On the current build the same line also carries `estimated apogee ... (drop ..., delay ...)` |
| **Why it matters** | Shows the implementation runs the assignment's wording unchanged. The shipped defaults add a 0.30 m/s margin, the same margin on the least-squares slope, and a 25 cm drop confirmation, which move the trigger from 5.50 m to 5.41 m on this trace. |
| **Evidence** | [at05b-2026-10-31.log](../tests/evidence/m10/at05b-2026-10-31.log) |

## AT-06 - continue descending after deployment

| | |
|---|---|
| **Action** | Replay `at05` continues to -17.3 m, 19.4 s after the deployment |
| **Expected** | No second trigger; original deadline unchanged |
| **Observed** | Exactly one `DEPLOY` line in 501 samples; deadline stayed `15611`; the trace ran to `h=-17.28 m` at `seq=498`; `REPLAY TEST: done, 501 samples published` |
| **Evidence** | [at05-2026-10-31.log](../tests/evidence/m10/at05-2026-10-31.log), tail |

## AT-07a - heat only, one complete window

| | |
|---|---|
| **Action** | Replay `at07a`: altitude flat at 0, 32 C from 4.0 s to 9.0 s |
| **Expected** | 500 ms full-period blink, 250/250 ms, for 5 s; no deployment |
| **Observed** | `heat: HOT at 32.000 C; alarm until 9011 ms (seq=81)`; `led: [4.011] blink on (heat)` alternating every 250 ms; `led: [9.011] off` -- **5.000 s**; zero `DEPLOY` lines |
| **Note** | At 8.95 s the renewal guard fired (`still hot ... extended to 14011`) and the cooling sample arrived 60 ms later, which cleared it. The visible indication still ends at 9.011 s. |
| **Evidence** | [at07a-2026-10-31.log](../tests/evidence/m10/at07a-2026-10-31.log) |

## AT-07b - heat beyond the first window, then cooling

| | |
|---|---|
| **Action** | Replay `at07b`: 32 C from 4.0 s to 14.0 s |
| **Expected** | Renewal while hot; cooling clears; no deployment indication |
| **Observed** | `alarm until 9011 ms`, `alarm extended to 14011 ms`, `alarm extended to 19011 ms`, then `heat: cooled at 24.000 C; alarm cleared (seq=281)`; blink ran 4.011 s to 14.011 s with no dark gap at either renewal; `led: [14.011] off` |
| **Evidence** | [at07b-2026-10-31.log](../tests/evidence/m10/at07b-2026-10-31.log) |

## AT-08 - deployment, then heat, then cooling before the deadline

| | |
|---|---|
| **Action** | Replay `at08`: deployment at 5.611 s, heat from 8.011 s, cooling at 13.011 s (deadline 15.611 s) |
| **Expected** | Steady ON, blink while hot, steady ON for the remainder of the original window, then OFF |
| **Observed** | `led: [5.611] on (deployment)` -> `[8.011] blink on (heat)` -> `[13.011] on (deployment)` -> `[15.611] off` |
| **Measured** | ON 2.400 s, blink 5.000 s, ON 2.600 s: total 10.000 s, and the deadline did not move |
| **Evidence** | [at08-2026-10-31.log](../tests/evidence/m10/at08-2026-10-31.log) |

## AT-09 - overheating across the deployment deadline, then cooling

| | |
|---|---|
| **Action** | Replay `at09`: deployment at 5.611 s (deadline 15.611 s), heat from 12.011 s, still hot past 15.611 s, cooling at 17.011 s |
| **Expected** | Blink overrides the deployment indication; the deadline passes while blinking; cooling afterwards must not restore steady ON |
| **Observed** | `led: [5.611] on`; `heat: HOT at 32.000 C; alarm until 17011 ms`; blink transitions at 15.011, 15.261, 15.511 and **15.761** -- i.e. continuing after the deadline; zero `off` lines anywhere between 12 s and 16.9 s; `heat: cooled ... alarm cleared (seq=341)`; `led: [17.011] off` |
| **Evidence** | [at09-2026-10-31.log](../tests/evidence/m10/at09-2026-10-31.log) |

## AT-10 - all consumers running concurrently

| | |
|---|---|
| **Action** | Live run, all threads active, 73.5 s |
| **Expected** | Independent coherent reads; no consumption, deadlock, starvation or GPIO ownership race |
| **Observed** | Telemetry, altitude, apogee and heat all reading the same store at their own rates (`alt: seq=50`, `apogee: seq=48` in the first second, `apogee: seq=1468` at the end); exactly one thread writes PC13; `overruns=0 errors=0`; zero faults |
| **Evidence** | [live-2026-10-31.log](../tests/evidence/m10/live-2026-10-31.log); the two-reader proof in [m5-shared-memory.md](verification/m5-shared-memory.md) (historical -- it predates the removal of the temporary `diag` reader) |

## AT-11 - restart the simulated flight

| | |
|---|---|
| **Action** | Reflash the same `at05` image (the documented reset procedure is a power cycle or a reflash; there is no button or console command), then run again |
| **Expected** | Fresh calibration and history; exactly one new deployment |
| **Observed** | First flight: `baseline frozen P0=95120 Pa after 38 samples (spread 0 Pa)`, `DEPLOY ... until 15611 ms`. Second boot, same image: identical `P0=95120`, `38 samples`, `DEPLOY ... until 15611 ms`, one event |
| **Evidence** | [at05-2026-10-31.log](../tests/evidence/m10/at05-2026-10-31.log) and [at11-run2-2026-10-31.log](../tests/evidence/m10/at11-run2-2026-10-31.log) |

---

## Live run excerpt (AT-01 / AT-02 / AT-10)

```
*** Booting Zephyr OS build v4.4.2 ***
i2c: boot recovery ok (ran before the sensor driver)
input: LIVE BMP280, I2C2 SCL=PF1 SDA=PF0 addr=0x76, forced mode 8x/2x, 50 ms period
threads: sensor(5) altitude(6) apogee(7) heat(8) led(9) telemetry(10); stale timeout 250 ms
alt:  baseline frozen P0=95140 Pa after 38 samples (spread 8 Pa)
apogee: armed; threshold 150 cm, drop 25 cm, margin 300 mm/s, window 750 ms (average velocity, confirmed by least-squares slope)
[     73.51 s] seq=1470  P=95143 Pa  T=22.93 C  age=27 ms
stats: 1200 samples  fetch 24/24/24 ms (min/avg/max)  dt 50/50/50 ms  overruns=0  errors=0  recoveries=0 failed=0  seq=1390
```

## What is **not** demonstrated

- **A real flight.** No ascent or descent was flown. Apogee behaviour is proven
  by replay traces on the target and by a live run that produces no false
  trigger; a genuine flight profile is untested.
- **The physical LED on film.** All timings above come from transition logs at
  10 ms resolution. The demonstration video is still outstanding.
- **AT-07 with a genuinely warm sensor.** The temperature comes from a replay
  trace. The finger-on-sensor path has not been captured.
- **An in-flight re-arm.** "Reset" is a power cycle or reflash.

## Reproducibility check (2026-10-31)

Commit `db3531c` was cloned to `C:\tmp\rocket-demo-clean` and used with only the
README:

| Step | Result |
|---|---|
| `git clone` | all sources, overlay, `prj.conf`, `Kconfig`, `CMakeLists.txt`, OpenOCD config, traces present |
| `west build -p always -b olimex_stm32_h407 -d build/primary .` | 48 864 B flash / 24 128 B RAM at that commit, zero warnings |
| `tests\run-tests.ps1` | 203 checks, 7 suites, 0 failures (223 checks after the 2026-11-01 peak-estimate change) |
| `tools\check-traces.ps1` | `PASS 8 generated files match the generator` |
| Flash over OpenOCD | `** Verified OK **` |
| Boot from the cloned image | `i2c: boot recovery ok`, `alt: baseline frozen P0=95137 Pa`, 0 deployments |

The checkout also confirmed the line-ending fix: `git ls-files --eol` reports
`w/lf` for `traces/*.csv` and `src/traces_data.hpp` even with
`core.autocrlf=true`, so the drift check no longer reports every generated file
as stale.

## Trace drift check

`tools\check-traces.ps1` runs the generator in a throwaway directory, compares
normalised content against the committed copies, checks the generator's exit
code and detects added or removed files. It never modifies the working tree.

Verified both ways: passes on the committed tree, and fails with
`differs from the generator: traces/at05.csv` after a deliberate one-character
edit.
