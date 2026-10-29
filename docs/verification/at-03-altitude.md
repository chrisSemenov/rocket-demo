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

## 4. An observed height change

While the stream was running unattended, a transient appeared at t ≈ 30–34 s:

```
[     30.01 s] seq=600  P=95113 Pa  T=23.53 C  age=26 ms
alt:  h=0.35 m  P0=95117 Pa  seq=600  age=26 ms
[     30.51 s] seq=610  P=95105 Pa  T=23.61 C  age=26 ms
alt:  h=1.06 m  P0=95117 Pa  seq=610  age=26 ms
[     33.01 s] seq=660  P=95101 Pa  T=23.93 C  age=26 ms
alt:  h=1.42 m  P0=95117 Pa  seq=660  age=26 ms
[     34.01 s] seq=680  P=95110 Pa  T=24.01 C  age=26 ms
alt:  h=0.62 m  P0=95117 Pa  seq=680  age=26 ms
```

- 16 Pa below the baseline gives 1.45 m by the formula at this pressure
  (11.0 Pa/m), and the reported peak is 1.42 m — consistent.
- `P₀` did not move (95 117 Pa throughout), so the change is in `P`, not in the
  reference.
- The temperature rose 0.48 °C over the same interval, which is what a hand
  holding the breakout looks like.

**Caveat:** this was not a controlled test. Nobody measured the actual height,
the motion was not a clean step, and warming was happening at the same time. It
demonstrates that a real pressure change moves the reported altitude in the right
direction, at the right magnitude — not that the calibration is accurate to a
centimetre against a tape measure.

## 5. What this does not yet show

- **A controlled height step.** A metered lift (e.g. 1.00 m on a rule) would
  separate the ~1.3 cm/°C temperature coupling from the height change.
- **The baseline restart path** (spread > 20 Pa) has not been exercised on
  hardware yet; it triggers if the sensor is moved during the first 2 s.
- **Long-run drift.** The longest uninterrupted capture so far is ~2.5 minutes
  plus this one; a multi-hour run would show whether `P₀` needs a re-baseline
  before each flight.
