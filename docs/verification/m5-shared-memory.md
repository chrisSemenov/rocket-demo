# M5: synchronized shared-memory publication

**Purpose:** prove that measurements are published as coherent snapshots and can
be read independently by more than one consumer without removing the data
(DATA-01–04, SYS-01, SYS-03, AT-10).

**Date:** 2026-10-28
**Relevant code:** `src/shared_data.hpp`, `src/shared_data.cpp`,
`src/sensor_task.cpp`, `src/telemetry_task.cpp`, `src/diag_task.cpp`

## What was built

| Element | Implementation |
|---|---|
| Snapshot | `Measurement`: pressure Pa, temperature milli-°C, sequence, acquisition timestamp, validity, error code |
| Store | `SensorDataStore`, one `k_mutex`, private value, copy-in / copy-out API |
| Producer | `sensor` thread, 50 ms absolute deadline; the only code that touches the BMP280 |
| Consumers | `telemetry` (500 ms) and `diag` (1000 ms); neither calls the sensor or the other thread |
| Failure path | Invalid snapshot with the error code and an unchanged sequence |
| Stats | Timing counters published as a second snapshot; printed by the consumer, not the producer |

## Invariants this test demonstrates

1. **Coherence** — pressure, temperature, timestamp and sequence come from one
   struct copy, so they cannot mix samples.
2. **Non-consuming reads** — two readers at different rates observe the same
   sequence; reading is a copy, not a dequeue.
3. **Independence** — the console threads never call the sensor thread; the
   only shared object is the store.
4. **No lock held during I/O** — the sensor thread locks only to copy the
   struct, after the I²C transfer and after computing the values.

## Observed output

```
[     84.51 s] seq=1690  P=95038 Pa  T=22.64 C  age=26 ms
[     85.01 s] seq=1700  P=95038 Pa  T=22.64 C  age=26 ms
diag:  seq=1680  age=43 ms  valid=1  published=1680
diag:  seq=1700  age=43 ms  valid=1  published=1700
stats: 1400 samples  fetch 24/24/24 ms (min/avg/max)  dt 50/50/50 ms  overruns=0  errors=0  seq=1590
```

- The 500 ms and 1000 ms readers report the **same sequence numbers** at
  different moments (`seq=1700` appears in both), and the 1 s reader keeps
  seeing intermediate sequences such as `1680`, which only exist briefly.
  Neither read removed a sample from the other.
- `published=1700` equals the sequence the telemetry reader saw, so the store
  and the producer agree.
- Over 1400 samples: zero fetch errors, zero invalid publications, zero
  deadline overruns.

## Concurrency behaviour

- The sensor thread runs at priority 5, both console threads at 8 and 9, so a
  blocked `printk` (RTT back-pressure) cannot delay acquisition.
- `k_sleep`/`K_TIMEOUT_ABS_MS` are the only blocking calls in the periodic
  loops; there is no polling of shared state.
- The store is initialised before any thread is created, so no consumer can
  observe a half-built store.

## Limitations of this evidence

- The second reader (`diag`) is a temporary diagnostic. It will be replaced by
  the altitude thread, which will be a real consumer with its own period.
- No test yet exercises a *producer* stall (invalid publication) other than the
  absent-sensor boot in `m4-absent-sensor.md`; the runtime failure path will be
  covered with the replay harness.
- Stack sizes are the configured values (2048/1024/1024 B), not yet measured
  high-water marks.
