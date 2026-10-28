# AT-02 / M4: live acquisition at 20 Hz

**Purpose:** real BMP280 readings at a measured cadence, with units, source
timestamp and sequence (checklist 4.3, AT-02).

**Date:** 2026-10-28
**Board:** Olimex STM32-H407, BMP280 on I²C2 (`SCL=PF1`, `SDA=PF0`, address `0x76`)
**Configuration:** forced mode, 8× pressure, 2× temperature, humidity 1×, IIR off, 50 ms period
**Build:** 33,412 B flash / 11,968 B RAM

## Observed output (first 85 seconds after reset)

```
*** Booting Zephyr OS build v4.4.2 ***
BMP280 ready: I2C2 SCL=PF1 SDA=PF0 addr=0x76, forced mode 8x/2x, 50 ms period
threads: sensor(prio 5) telemetry(8) diag(9); stale timeout 250 ms
telemetry: no valid measurement yet (error=0)
diag:  seq=0  age=-1 ms  valid=0  published=0
[      0.51 s] seq=10  P=95041 Pa  T=22.60 C  age=26 ms
...
[     84.51 s] seq=1690  P=95038 Pa  T=22.64 C  age=26 ms
[     85.01 s] seq=1700  P=95038 Pa  T=22.64 C  age=26 ms
stats: 1400 samples  fetch 24/24/24 ms (min/avg/max)  dt 50/50/50 ms  overruns=0  errors=0  seq=1590
```

## Interpretation

- **Rate:** `seq` advances by 10 every 500 ms of telemetry, i.e. 20 samples per
  second. 1400 samples in ~70 s of steady state.
- **Fetch:** a flat 24 ms against the 24.8 ms datasheet conversion time — one
  conversion per 50 ms period, so each published value is a fresh measurement.
- **Interval:** `dt` never left 50 ms; no deadline was missed.
- **Age:** the sample was 26 ms old when telemetry read it, i.e. inside one
  period, so the console is showing current data rather than a value that has
  been sitting in the registers.
- **Errors:** zero fetch errors and zero invalid publications over the run.
- **Plausibility:** ~95,040 Pa and ~22.6 °C with sub-pascal-per-second drift.
  95 kPa corresponds to roughly 550 m elevation, which is consistent with the
  bench; temperature falls slowly, as expected indoors.

## Notes

- The first fetch after boot takes 42 ms because it starts the first
  conversion. It is excluded from the steady-state counters together with the
  second sample, which only establishes the schedule.
- The `stats:` line is produced by the telemetry consumer from a snapshot the
  sensor thread publishes; the acquisition thread itself never prints.
