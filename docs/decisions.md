# Design decisions

Decisions taken before the affected code was written. Each one is either a
mandatory requirement, a recommendation from `docs/requirements.md`, or a
resolution of something the assignment leaves open. File paths are relative to
the repository root.

## Platform and acquisition

| Decision | Value | Why |
|---|---|---|
| Sensor transport | I²C2, `SCL=PF1`, `SDA=PF0`, address `0x76` | Both signals sit on the PF header next to 3.3 V and GND; see README wiring table |
| Measurement period | 50 ms (20 Hz), absolute deadlines | `750 = 15 × 50`, so the descent window ends on a stored sample |
| Oversampling | 8× pressure, 2× temperature | 24 ms conversion leaves 26 ms of slack; 16× would leave 5.5 ms for a 0.7 cm precision gain nobody can observe (README, "Sampling design") |
| Mode | Forced, one conversion per fetch | The value read is always a fresh conversion, not whatever normal mode last left in the data registers |
| IIR filter | Off | The filter needs normal mode to smooth across samples; in forced mode it has no history. The 750 ms window does the averaging |
| Humidity oversampling | 1× although a BMP280 has none | The driver's sample-time constant counts it; at the default 16× it computes 53 ms and every fetch would overrun the period |
| Measurement period evidence | fetch 24 ms, dt 50 ms, 0 overruns | README, "Timing evidence"; `docs/verification/` |

## Data and time

| Decision | Value | Why |
|---|---|---|
| Internal units | pressure Pa (int32), temperature milli-°C (int32), altitude cm (int32), velocity mm/s (int32), time ms (`k_uptime_get`, int64) | Integers at the boundary avoid float printf and float in shared state |
| Publication | One `Measurement` struct: pressure, temperature, timestamp, sequence, valid, error | A reader can never pair pressure from one sample with temperature from the next (DATA-02) |
| Access | `SensorDataStore` with a `k_mutex`; copy in, copy out; no mutable references | Locks are short and never held across I²C, printing or computation |
| Read semantics | Non-consuming latest-value snapshot | Any number of consumers read independently at their own rates (DATA-03, SYS-03) |
| Sequence | Incremented only when a sample is successfully published | A failed fetch must not look like a new measurement |
| Timestamp | Acquisition time (`k_uptime_get` after the fetch), not the consumer's read time | Age is measurable; consumers can reject stale data |
| Invalid publication | On fetch failure publish `valid = false` with the error code, sequence unchanged | Consumers stop trusting the store instead of inheriting the last good value |
| Stale timeout | 250 ms (5 periods) | Short enough that a dead sensor cannot drive a decision, long enough to survive a missed sample |

## Altitude (to be implemented with the altitude task)

| Decision | Value | Why |
|---|---|---|
| Model | `h = 44330 · (1 − (P/P₀)^0.1903)` m | Standard-atmosphere approximation; documented limits in README |
| Reference | `P₀` = mean of valid samples over 2 s while stationary, then frozen | The BMP280 absolute accuracy is ±1 hPa ≈ ±8 m, so only a relative calculation is meaningful |
| Start-up | Altitude invalid until the baseline is complete | Prevents flight decisions from a half-calibrated reference |
| Precision | Publish cm | 1 cm resolution against ~2.4 cm of pressure noise |

## Flight logic (to be implemented with the apogee and temperature tasks)

| Decision | Value | Why |
|---|---|---|
| Trigger | Current altitude above the configured minimum **and** negative average velocity over the preceding 750 ms | Literal reading of FLT-03, no extra arming rule |
| Velocity | `(h_now − h(t−750 ms)) / 0.750`, boundary interpolated by timestamp | Timestamps, not sample counts; 50 ms divides 750 but cadence gaps still occur |
| Trigger repeat | Latched once per flight; the deployment deadline is set once | FLT-04, FLT-05 |
| Heat alarm | 5 s window started on detection, renewed while still hot, cancelled immediately on cooling | Resolves the assignment's ambiguity; early cooling truncates the window and returns control to deployment |
| LED priority | Active heat blink > unexpired deployment ON > OFF | LED-04, LED-05 |
| Thresholds | Configurable constants; bench values set after a baseline measurement | FLT-02, LED-02 |
| Reset | One flight per boot/reset; a reset clears calibration, history and the deployment latch | Checklist 2.2; avoids a partial re-arm |
| Replay | Traces enter at the measurement publisher, never at the LED | Keeps the tested path identical to live operation (checklist 10.1) |

## Concurrency

| Decision | Value | Why |
|---|---|---|
| Threads | `sensor`, `telemetry`, `diag` now; altitude, apogee, temperature, LED manager to follow | SYS-01 |
| Priorities | sensor 5, telemetry 8, diag 9 (lower number = higher priority) | The sensor must preempt the console so printk cannot push the 50 ms deadline |
| Stacks | sensor 2048 B, telemetry 1024 B, diag 1024 B | Sized for the driver and printk paths; see `docs/architecture.md` |
| Console logging | Producer publishes stats; a consumer prints them | The acquisition thread never blocks on RTT |
| LED ownership | One writer only; the temporary heartbeat loop is removed when the LED manager lands | Checklist 7; prevents competing GPIO writes |
