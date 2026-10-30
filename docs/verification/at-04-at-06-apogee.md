# M8: apogee detection and the deployment request

**Purpose:** show that the detector triggers once, only after a full 750 ms
window of descending flight above the configured minimum, and that it cannot
retrigger or extend the indication (FLT-01–05; AT-04, AT-05, AT-06, AT-11).

**Date:** 2026-10-29
**Board:** Olimex STM32-H407, BMP280 on I²C2
**Build:** 47 844 B flash / 25 344 B RAM (default) / 47 860 B (bench preset)

## 1. What is implemented

| Element | Behaviour |
|---|---|
| History | Fixed 32-entry ring in the detector, timestamp-based eviction after 2 s |
| Window | `h(now) − h(now − 750 ms)`, bracketing sample interpolated by timestamp |
| Velocity | `v = Δh / 0.750 s` in m/s, computed outside any lock |
| Threshold | 1.50 m, about twice the observed peak-to-peak altitude noise |
| Margin | The average must be at least **0.30 m/s** below zero |
| Duplicates | A repeated sequence number is ignored, never added twice |
| Gaps | A gap longer than 250 ms clears the history and starts a new window |
| Latch | Deployed is permanent until a reset; later samples cannot retrigger |
| Event | Timestamped with the **triggering sample**, deadline = event + 10 s |
| Output | One `request_deployment()` call; the LED manager owns the pin |

## 2. Host tests

37 checks, run by `tests/run-tests.ps1` with no board:

```
ok   stationary at 0 cm does not deploy
ok   descent below the minimum does not deploy
ok   ascent above the minimum does not deploy
ok   peak then descent deploys exactly once
ok   deployment deadline is event + 10 s
ok   continued descent does not retrigger
ok   the original deadline is never extended
ok   incomplete 750 ms history does not deploy
ok   a repeated sequence is not added to the history
ok   a backward timestamp is not added to the history
ok   a long gap restarts the history from the new sample
ok   no deployment until a full window is collected again
ok   zero average velocity does not deploy
ok   descent below the margin is ignored
ok   altitude exactly at the minimum with descent deploys
ok   the event is timestamped with the triggering sample
ok   one centimetre below the minimum does not deploy
ok   irregular cadence still detects the descent
ok   a new flight can deploy again after reset
...
PASSED (0 failures)
```

## 3. Two defects this work found

**Velocity unit error.** The first implementation divided centimetres by 0.750 s
and then scaled by 1000, producing cm/s labelled as mm/s — a factor of **100**.
A 0.2 m/s descent read as 1.33 m/s. The ripple test (1 cm alternation, which
should be far below any margin) triggered immediately and exposed it. Fixed by
dividing by 100 to metres before the time division.

**Margin too small for real noise.** With the first margin of 0.2 m/s, the live
bench log showed velocity excursions of **±0.24 m/s** — the margin sat inside the
noise band. A false deployment would have required the altitude to also exceed
1.5 m, so it did not fire in practice, but the margin was not defensible. It was
raised to 0.5 m/s, and later to **1.0 m/s** once the endpoint estimator was
replaced and the worst case was measured properly (see §7).

Measured velocity noise on the bench (stationary, 75 s):

```
apogee: h=0.09 m  v=0.240 m/s  cov=1550 ms  n=32  state=ready  seq=1420
apogee: h=0.27 m  v=0.240 m/s  cov=1550 ms  n=32  state=ready  seq=1430
apogee: h=0.00 m  v=0.240 m/s  cov=1550 ms  n=32  state=ready  seq=1440
```

That is 18 cm of apparent movement across the 750 ms window — pressure noise, not
motion — and it is why a margin exists at all.

## 4. Live behaviour

75 s after reset, 1 200 samples:

| Check | Result |
|---|---|
| State | `ready` once the window filled; never `deployed` |
| Deployments | 0 |
| Faults | 0 |
| Sampling unaffected | fetch 24/24/24 ms, dt 50/50/50 ms, 0 overruns |

So the detector runs against live data without false-triggering on a still bench.
It has **not** seen a real ascent and descent on hardware.

## 5. Configuration and provenance

The numbers are Kconfig symbols, so a test build changes behaviour without
touching source:

```
CONFIG_APP_APOGEE_MIN_ALT_CM=150
CONFIG_APP_APOGEE_MIN_DESCENT_MM_S=300
CONFIG_APP_APOGEE_MIN_DROP_CM=25
```

Verified by building the same tree twice and reading the resolved `.config`:

| Build | `MIN_ALT_CM` | `MIN_DESCENT_MM_S` | `MIN_DROP_CM` | Flash |
|---|---|---|---|---|
| `build/primary` | 150 | 300 | 25 | 47 844 B |
| `build/bench` | 50 | 600 | 15 | 47 860 B |

The bench preset needs a *larger* margin than the flight default because room air
drift reaches 0.55 m/s; see `bench.conf` for the reasoning.

The 750 ms window, the 10 s indication, the 5 s alarm and the 500 ms blink are
not configurable — they are the specified behaviour.

Provenance, stated because a reviewer will ask:

| Value | Origin |
|---|---|
| A configurable minimum altitude | Assignment, FLT-02 |
| 750 ms window, "negative" average | Assignment, FLT-03 |
| 1.50 m | **Ours**: ~2× the 0.89 m peak-to-peak altitude noise |
| 1.00 m/s → **0.30 m/s** | **Ours**: a noise guard, deliberately small. A 0.3 m/s trailing average is reached ~0.41 s / 0.8 m after apogee; 1.0 m/s would have delayed it to ~0.48 s / 1.1 m for no flight benefit |

## 6. Ready indication (boot aid)

With no console on a battery-powered run, the only way to know the chain is
armed is the LED. Once the sensor answers, the baseline freezes and the 750 ms
history is full, the LED flashes twice:

```
apogee: window full; threshold 150 cm, margin 300 mm/s, window 750 ms
led:  ready signal
led:  [      2.661] ready flash on
led:  [      2.761] ready flash off
led:  [      2.881] ready flash on
led:  [      2.981] ready flash end
```

Measured: 100 ms on, 120 ms off, 100 ms on — deliberately not the 250/250 of the
heat alarm, so the two cannot be confused. It fires once per boot, is skipped if
a deployment or heat request is already active, and can be disabled with
`CONFIG_APP_READY_INDICATION=n`.

Absence of the double flash after ~3 s means the sensor or the calibration
failed; nothing will trigger in that state.

## 7. Estimator change: slope instead of endpoints

The first version computed `v = (h_now − h(now−750 ms)) / 0.75`, which uses two
samples. It was replaced with a least-squares slope over the same 750 ms, using
every sample in the window.

**Measured effect on a still bench:**

| Estimator | Velocity extremes (113 samples) |
|---|---|
| Endpoint difference, 0.5 m/s margin | −0.47 … +0.24 m/s |
| Least-squares slope, 1.0 m/s margin | −0.45 … +0.55 m/s |

The slope did **not** reduce the extremes, contrary to the expectation that it
would average noise down by ~1.8×. The reason is that the dominant disturbance
here is not per-sample noise but **slow pressure drift** (air movement in the
room) — a 0.4 m change spread over about a second is a real 0.4 m/s as far as any
750 ms estimator is concerned. Both estimators report it.

The slope is kept because it uses the whole window, is insensitive to a single
spike, and is a standard estimator of average rate. The margin was briefly set to
1.0 m/s, then corrected down to **0.30 m/s** — see §7a.

**Edge case found while testing:** with a margin of 0, `v <= 0` treats a *flat*
trace as descending, so the detector fired during the stationary phase. The
condition is now strictly negative **and** at least the margin below zero, which
makes `CONFIG_APP_APOGEE_MIN_DESCENT_MM_S=0` behave as "any real descent".

## 7a. Why the margin went back down: detection latency

The 1.0 m/s margin was justified against bench drift, but it was the wrong
environment. In flight the vehicle is not powered downward; it falls from rest at
apogee, so the *trailing 750 ms average* crosses a threshold much later than the
instantaneous velocity does.

Solving `h = −½gt²` with a 750 ms trailing average:

| Margin | Average reached at | Height below apogee |
|---|---|---|
| 0.30 m/s | 0.41 s | 0.80 m |
| 1.00 m/s | 0.48 s | 1.12 m |

So 1.0 m/s bought no flight benefit and cost 0.3 m of extra fall. The default is
now 0.30 m/s: just enough above zero that a stationary sensor does not satisfy
it, and close enough to "negative" to stay faithful to FLT-03.

The bench needs the opposite: room air drift reaches 0.55 m/s, so `bench.conf`
uses a **larger** margin (0.60 m/s) and a lower altitude gate (50 cm). The two
configurations optimise different noise environments, and neither is a default.

## 8. What this does not prove

- **AT-04, AT-05, AT-06 on hardware.** Producing a 1.5 m+ ascent and a 0.30 m/s
  descent needs either a lift rig or, as the checklist intends, the replay
  harness. Those acceptance rows stay open until M10.
- **AT-11 (new flight).** `Detector::reset()` is implemented and unit-tested, but
  the running firmware only resets on power cycle; there is no button or command
  yet.
- **Threshold realism.** 1.5 m is a bench value chosen against noise, not a
  flight value. A real vehicle would set it well above ground level.
- **Interpolation accuracy under heavy jitter.** It is exercised with a 70 ms
  cadence in the tests, not with the 250 ms stale boundary.

## 9. Physical bench check (operator)

On 2026-10-29 the bench preset (50 cm / 0.60 m/s) was exercised physically by
hand: a deliberate lowering triggered the indication and the behaviour was
accepted.

**Caveat:** the console capture for that session was not retained - the RTT
stream is restarted on every flash. The evidence here is the operator's
observation of the LED, not a `DEPLOY` line. A captured run remains part of the
M10 replay work, where the trigger is reproducible.
