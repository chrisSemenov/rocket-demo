# M8: apogee detection and the deployment request

**Purpose:** show that the detector triggers once, only after a full 750 ms
window of descending flight above the configured minimum, and that it cannot
retrigger or extend the indication (FLT-01-05; AT-04, AT-05, AT-06, AT-11).

**Date:** 2026-10-29, estimator revised 2026-10-31, peak estimate added 2026-11-01
**Board:** Olimex STM32-H407, BMP280 on I2C2
**Build:** 49 740 B flash / 24 128 B RAM (default live build)

## 1. What is implemented

| Element | Behaviour |
|---|---|
| History | Fixed 32-entry ring in the detector; capacity (1.55 s at 20 Hz) binds before the 2 s time bound |
| Window | `h(now) - h(now - 750 ms)`, with the start height interpolated between the two samples that bracket the window edge |
| Velocity | `v = delta h / 0.750 s` in m/s, computed outside any lock |
| Slope confirmation | Least-squares slope over the same window, logged as `slope=` and required to pass the same margin as the average |
| Threshold | 1.50 m, about 1.7x the observed peak-to-peak altitude noise (0.89 m) |
| Margin | The average must be at least **0.30 m/s** below zero |
| Peak estimate | Maximum of a three-sample median, stamped with the middle sample's timestamp and logged as `peak=` |
| Confirmation | The altitude must also have fallen at least 25 cm below that estimate |
| Duplicates | A repeated sequence number is ignored, never added twice |
| Gaps | A gap longer than 250 ms clears the history and the drop reference; the peak estimate survives, because a fault must not erase a flight result |
| Latch | Deployed is permanent until a reset; later samples cannot retrigger |
| Event | Timestamped with the **triggering sample**, deadline = event + 10 s; also freezes the estimated apogee, its time and the drop |
| Output | One `request_deployment()` call; the LED manager owns the pin |

## 2. Host tests

61 checks, run by `tests/run-tests.ps1` with no board:

```
ok   stationary at 0 cm does not deploy
ok   descent below the minimum does not deploy
ok   ascent above the minimum does not deploy
ok   peak then descent deploys exactly once
ok   deployment deadline is event + 10 s
ok   continued descent does not retrigger
ok   the original deadline is never extended
ok   endpoint average of a uniform ramp is 0.6 m/s
ok   the slope agrees on a straight ramp
ok   endpoint average of a spike-and-fall is still positive
ok   the slope reads that window as a steep descent
ok   a positive average never fires, whatever the slope reads
...
PASSED (0 failures)
```

The last three matter for the specification: they pin the difference between the
two estimators, and the fact that the average still decides when the slope alone
would fire.

## 3. Sensitivity to noise, and why the margin exists

With an early 0.2 m/s margin, the live bench log showed velocity excursions of
**+/-0.24 m/s** -- the margin sat inside the noise band. A false deployment would
also have needed the altitude to exceed 1.5 m, so it did not fire in practice,
but the margin was not defensible. It was raised to 0.5 m/s and then to 1.0 m/s.

Measured velocity noise on the bench (stationary, 75 s, endpoint estimator):

```
apogee: h=0.09 m  v=0.240 m/s  cov=1550 ms  n=32  state=ready  seq=1420
apogee: h=0.27 m  v=0.240 m/s  cov=1550 ms  n=32  state=ready  seq=1430
apogee: h=0.00 m  v=0.240 m/s  cov=1550 ms  n=32  state=ready  seq=1440
```

That is 18 cm of apparent movement across the 750 ms window -- pressure noise,
not motion -- and it is why a margin exists at all.

## 4. Live behaviour

75 s after reset, 1 200 samples:

| Check | Result |
|---|---|
| State | `ready` once the window filled; never `deployed` |
| Deployments | 0 |
| Faults | 0 |
| Sampling unaffected | fetch 24/24/24 ms, dt 50/50/50 ms, 0 overruns |

The detector runs against live data without false-triggering on a still bench.
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

| Build | `MIN_ALT_CM` | `MIN_DESCENT_MM_S` | `MIN_DROP_CM` |
|---|---|---|---|
| `build/primary` | 150 | 300 | 25 |
| `build/bench` | 50 | 600 | 15 |

The bench preset needs a *larger* margin than the flight default because room air
drift reaches 0.55 m/s; see `bench.conf` for the reasoning.

The 750 ms window, the 10 s indication, the 5 s alarm and the 500 ms blink are
not configurable -- they are the specified behaviour.

Provenance, stated because a reviewer will ask:

| Value | Origin |
|---|---|
| A configurable minimum altitude | Assignment, FLT-02 |
| 750 ms window, "negative" average | Assignment, FLT-03 |
| 1.50 m | **Ours**: about 1.7x the 0.89 m peak-to-peak altitude noise |
| 0.30 m/s | **Ours**: a noise guard, deliberately small. A 0.3 m/s trailing average is reached about 0.41 s / 0.81 m after apogee |
| Slope confirmation | **Ours**: the same margin applied to the least-squares fit. Free on a ballistic fall; it refuses a deployment when one noisy edge sample moves the average |
| Peak estimate | **Ours**: the maximum of a three-sample median. Replaces the raw maximum, which let one bad sample become the peak permanently |
| 25 cm drop confirmation | **Ours**: measured from the estimate. A guard, not a requirement: free on a ballistic fall, but passed by a 35-44 cm hand settle |

## 6. Ready indication (boot aid)

With no console on a battery-powered run, the only way to know the chain is
armed is the LED. Once the sensor answers, the baseline freezes and the 750 ms
history is full, the LED flashes twice:

```
apogee: armed; threshold 150 cm, drop 25 cm, margin 300 mm/s, window 750 ms
       (average velocity, confirmed by least-squares slope)
led:  ready signal
led:  [      2.611] ready flash on
led:  [      2.711] ready flash off
led:  [      2.831] ready flash on
led:  [      2.931] ready flash end
```

Measured: 100 ms on, 120 ms off, 100 ms on -- deliberately not the 250/250 of the
heat alarm, so the two cannot be confused. It fires once per boot, is skipped if
a deployment or heat request is already active, and is disabled by `demo.conf`
for the recorded demonstration.

Absence of the double flash after about 3 s means the sensor or the calibration
failed; nothing will trigger in that state.

## 7. Estimator history: endpoint average, slope, and both

Four versions existed, which is worth recording because the choice is a real
engineering decision rather than a detail.

1. **Endpoint difference** -- `v = (h_now - h(now-750 ms)) / 0.75`. Two samples,
   so it is the most noise-sensitive of the three, and it is what the assignment
   literally asks for.
2. **Least-squares slope** over the same window. Uses every sample, is
   insensitive to a single spike, and is a standard estimator of average rate.
3. **Back to the endpoint difference, with the slope kept as a diagnostic.**
   This version produced the bench false positives described in section 10: the
   average passed while the fit did not, and the fit was ignored.
4. **Both required.** The average still decides, because FLT-03 names it, but the
   least-squares slope over the same window must also pass the margin. Joined
   with `&&`, so it can only refuse a deployment.

The reason the average is still the primary is not that the slope is wrong. It is
that the two are not the same function, and the assignment names the average:

```
endpoint average  = (h(t) - h(t - 750 ms)) / 0.750
slope             = least-squares fit through all samples in the window
```

They agree only when the path is close to a straight line. A rise and fall back
inside one window is the counter-example: a 5.00 m start, a jump to 7.00 m and a
fall to 5.04 m gives an endpoint average of **+0.053 m/s** (a net climb) while
the slope reads **-2.97 m/s** (a steep descent). The shipped code waits, because
the documented rule says the average is not negative -- and with both required,
the slope alone can never fire. `tests/apogee_test.cpp` pins that case.

Both values are logged, so a reviewer can compare them on any trace:

```
apogee: h=3.99 m  v=0.000  slope=0.000  peak=6.03 m  drop=2.04 m  cov=1550 ms  n=32  state=ready  seq=178
apogee: DEPLOY at 5.41 m, v=-0.360 m/s (slope=-0.495); estimated apogee
        6.03 m at 5.01 s (drop 62 cm, delay 0.60 s); LED on until 15611 ms
```

After deployment both velocity fields print `n/a` instead of a frozen number, and
the peak and drop stay at their frozen values.

**Measured effect on a still bench** (113 samples, both estimators over the same
windows):

| Estimator | Velocity extremes |
|---|---|
| Endpoint difference | -0.47 ... +0.24 m/s |
| Least-squares slope | -0.45 ... +0.55 m/s |

Neither is quieter. The dominant disturbance is not per-sample noise but slow
pressure drift from air movement in the room: a 0.4 m change spread over a second
is a real 0.4 m/s as far as any 750 ms estimator is concerned.

**Edge case found while testing:** with a margin of 0, `v <= 0` treats a *flat*
trace as descending, so the detector fired during the stationary phase. The
condition is strictly negative **and** at least the margin below zero, which
makes `CONFIG_APP_APOGEE_MIN_DESCENT_MM_S=0` behave as "any real descent".

## 8. Why the margin is small

The 1.0 m/s margin was justified against bench drift, but it was the wrong
environment. In flight the vehicle is not powered downward; it falls from rest at
apogee, so the *trailing 750 ms average* crosses a threshold much later than the
instantaneous velocity does.

Solving `h = -1/2 g t^2` with a 750 ms trailing average:

| Margin | Average reached at | Height below apogee |
|---|---|---|
| 0.30 m/s | 0.41 s | 0.81 m |
| 1.00 m/s | 0.48 s | 1.12 m |

So 1.0 m/s bought no flight benefit and cost 0.3 m of extra fall. The default is
now 0.30 m/s: just enough above zero that a stationary sensor does not satisfy
it, and close enough to "negative" to stay faithful to FLT-03.

The bench needs the opposite: a hand settle after a fast lift measures about
0.36 m/s and a 35-44 cm drop, so `bench.conf` uses a **larger** margin (0.60 m/s)
and a lower altitude gate (50 cm). With the flight default those settles satisfy
the trigger, which is why a live hand demonstration uses the preset.

## 9. What this does not prove

- **AT-04, AT-05, AT-06 on hardware.** Producing a 1.5 m+ ascent and a 0.30 m/s
  descent needs a lift rig or the replay harness. Replay results are in
  `replay.md`; no real flight was flown.
- **AT-11 (new flight).** `Detector::reset()` is implemented and unit-tested, but
  the running firmware only resets on power cycle or reflash; there is no button
  or command.
- **Threshold realism.** 1.5 m is a bench value chosen against noise, not a
  flight value.
- **Interpolation accuracy under heavy jitter.** Exercised with a 40/70 ms mixed
  cadence in the tests, not at the 250 ms stale boundary.
- **The peak estimate against an independent height reference.** It is checked
  against the trace's own 6.03 m plateau in `at05`, not against a tape measure.
  The median's error is bounded by one sample of curvature (~1.2 cm at 20 Hz) but
  that bound is derived, not measured.

## 10. Physical bench check (operator)

On 2026-10-29 the bench preset (50 cm / 0.60 m/s) was exercised physically by
hand: a deliberate lowering triggered the indication and the behaviour was
accepted.

**Caveat:** the console capture for that session was not retained -- the RTT
stream is restarted on every flash. The evidence there is the operator's
observation of the LED, not a `DEPLOY` line.

### 10.1 Re-measured on 2026-11-01, with captures

The default flight build was exercised by hand with RTT recorded, because the
operator reported deployments while simply lifting the board. Four captures, all
with the 1.50 m gate and the 0.30 m/s margin:

| Capture | Motion | Trigger value | Highest altitude in that capture | Drop at the trigger |
|---|---|---|---|---|
| `lift2` | lift and hold, ~1.6 m | `v=-0.360`, `slope=+0.069` | 1.78 m (raw 500 ms sample) | 27 cm |
| `lift3` | lift and hold, ~2 m | `v=-0.347`, `slope=-0.124` | 2.31 m (raw 500 ms sample) | 44 cm |
| `floor22m` | floor, lift to 1.8 m, hold | `v=-0.360`, `slope=-0.524` | 1.87 m (raw 500 ms sample) | 36 cm |
| `floor22fast` | floor, fast lift to 2.2 m, hold | `v=-0.360`, `slope=-0.307` | **2.04 m (`peak=` estimate)** | **35 cm (logged)** |

Only `floor22fast` was recorded after the peak estimate existed, so only that row
is a `peak=` value; the other three are the highest 500 ms sample printed by the
periodic line, which is why the column is labelled differently. Treat them as
upper bounds on the true peak, not as estimates.

Two conclusions, both measured rather than argued:

- **The trigger value is the same on the old and the new code.** `lift2` and
  `lift3` were captured before the slope confirmation existed and deployed at
  -0.360 and -0.347; the later captures deploy at -0.360 with the slope
  confirmation active. The extra conditions only removed candidates.
- **A hand settle is a real 35-44 cm decrease at about 0.36 m/s.** It is not a
  spike and not an artefact: both estimators agree, and the median peak is
  unaffected. No estimator can separate it from a descent, because at that
  instant the measurement *is* a descent. The separation is scale: a body falling
  from rest reaches 0.30 m/s only after 0.81 m, which is twice the settle.

Consequences, and what is documented as a limit rather than fixed:

- The 25 cm drop is below the settle, so the flight default can fire on a
  lift-and-hold. Raising it to 55 cm would cover every settle measured here
  (35-44 cm) while staying below the 0.81 m a real fall has already covered.
  This is a configuration change, not a code change, and it is not applied.
- The **bench preset** (0.60 m/s margin) is the supported hand demonstration: the
  settle peaks at 0.36 m/s, so lift-and-hold stays quiet, while a deliberate
  lowering at about 0.7 m/s triggers. Verified on the board on 2026-11-01 with the
  peak estimate active (`build/bench`, `bench-live` capture).
- Raising the margin instead of the drop is the flight-visible alternative and it
  costs latency: 1.5 m/s would delay detection by about 0.15 s and 0.7 m of fall.
