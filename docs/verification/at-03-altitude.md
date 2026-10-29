# AT-03: altitude calculation and baseline calibration

**Purpose:** show that pressure from the shared store produces a correctly
timestamped altitude, and that the reference pressure is taken from a stable
standstill and then frozen (ALT-01–03, AT-03).

**Date:** 2026-10-28
**Board:** Olimex STM32-H407, BMP280 on I²C2 (`SCL=PF1`, `SDA=PF0`, `0x76`)
**Build:** 40,208 B flash / 14 KB RAM
**Location:** ~626–630 m above sea level (user-reported bench elevation)

## 1. Unit tests of the pure function

`tests/run-tests.ps1` compiles `src/altitude.cpp` with the host `g++` and runs
`tests/altitude_test.cpp`. No board is involved.

```
ok   P == P0                       -0.000000 m
ok   standard 1000 hPa            110.903981 m
ok   standard 950 hPa             540.431869 m
ok   standard 900 hPa             988.672468 m
ok   bench 95000 -> 94000          89.180839 m
ok   bench 95000 -> 94500          44.494903 m
ok   bench above reference         -8.876217 m
ok   monotonic in pressure        44.495 > 40.037 m
ok   cm agrees with metres        8918 cm
ok   zero pressure                rejected
ok   negative pressure            rejected
ok   below sensor range           rejected
ok   above sensor range           rejected
ok   zero reference               rejected
ok   reference out of range       rejected
ok   lowest in range              accepted (916537 cm)
ok   highest in range             accepted (-69844 cm)

PASSED (0 failures)
```

The reference values were computed independently in 40-digit decimal arithmetic
from `h = 44330 · (1 − (P/P₀)^0.1903)`, with a 1 cm tolerance; they are not
produced by calling the implementation twice.

## 2. Live baseline and altitude

Captured from RTT immediately after reset:

```
BMP280 ready: I2C2 SCL=PF1 SDA=PF0 addr=0x76, forced mode 8x/2x, 50 ms period
threads: sensor(prio 5) altitude(6) telemetry(8) diag(9); stale timeout 250 ms
telemetry: no valid measurement yet (error=0)
alt:  not available yet (baseline 0/38 samples)
[      0.51 s] seq=10  P=95062 Pa  T=22.24 C  age=26 ms
alt:  not available yet (baseline 10/38 samples)
[      1.51 s] seq=30  P=95063 Pa  T=22.25 C  age=26 ms
alt:  baseline frozen P0=95062 Pa after 38 samples (spread 8 Pa)
[      2.01 s] seq=40  P=95060 Pa  T=22.25 C  age=26 ms
alt:  h=0.18 m  P0=95062 Pa  seq=40  age=26 ms
[      2.51 s] seq=50  P=95062 Pa  T=22.25 C  age=26 ms
alt:  h=0.00 m  P0=95062 Pa  seq=50  age=26 ms
```

Over the next 2.5 minutes (255 printed altitude samples):

| Quantity | Result |
|---|---|
| `P₀` | 95062 Pa, frozen after 38 samples |
| Baseline spread at freeze | 8 Pa (≈ 0.7 m) |
| Altitude range while stationary | −0.62 … +0.27 m |
| Peak-to-peak | 0.89 m |
| Thread faults / MPU faults | 0 |
| Altitude sequence | tracks the sensor sequence; no duplicates or gaps |

## 3. Interpretation

- Altitude sits at 0 m by construction, because it is **relative to the frozen
  `P₀`**, not to a sea-level standard. That is the intended behaviour: absolute
  pressure cannot give an absolute height.
- The same pressure through the standard atmosphere would read **535 m**, while
  the bench is at ~626–630 m. The 90 m gap is weather, not a bug: 95062 Pa at
  626 m implies a local sea-level pressure of about 102 440 Pa, i.e. a
  high-pressure day. This is exactly why `P₀` is measured rather than assumed.
- Sensitivity here is about **11 Pa/m**, so the ±0.6 m wander is roughly ±7 Pa of
  sensor noise. The 20 Pa stationarity tolerance is about 1.8 m, comfortably
  above the noise floor and below any real hand movement.
- `age` in the altitude line is computed from the *source measurement*
  timestamp, so a stalled sensor shows up as a growing age even if the altitude
  thread keeps running.

## 4. What this does not yet show

- **A physical height change.** The sensor was not lifted, so no real Δh is
  demonstrated here; the unit tests cover the arithmetic and the trace-replay
  harness (Milestone 10) will cover moving profiles. A ~1 m lift should read
  ≈ +1.0 m (≈ 11 Pa), and is worth capturing for the video.
- **Temperature compensation.** None is applied, by design; see
  `docs/decisions.md` ("Temperature and pressure are coupled").
- **The baseline restart path** (spread > 20 Pa) has not been exercised on
  hardware yet; it triggers if the sensor is moved during the first 2 s.
