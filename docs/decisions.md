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

## Altitude

| Decision | Value | Why |
|---|---|---|
| Model | `h = 44330 · (1 − (P/P₀)^0.1903)` m | Standard-atmosphere approximation; limits documented in README |
| Numeric form | `-44330 · expm1(0.1903 · log1p((P − P₀)/P₀))` | Near the bench `P ≈ P₀`, where `1 − pow()` subtracts two nearly equal numbers; `expm1`/`log1p` keep the small difference |
| Input validation | 30 000–110 000 Pa, both inputs; non-finite result rejected | The BMP280's own range. A bus fault must produce an invalid altitude, not a plausible one |
| Reference | `P₀` = mean of valid samples over 2 s while stationary, then frozen | BMP280 absolute accuracy is ±1 hPa ≈ ±8 m, so only a relative calculation is meaningful |
| Stationarity rule | window ≥ 1 800 ms and ≥ 38 of 40 samples within 20 Pa spread; otherwise restart from the current sample | 20 Pa ≈ 1.7 m here (dP/dh ≈ 11.5 Pa/m), well above the ~8 Pa of observed noise, so picking the sensor up restarts calibration |
| Coverage | A gap longer than the 250 ms stale timeout clears the window | Stationarity across a gap cannot be claimed |
| Duplicate samples | Altitude is recomputed only when the source sequence changes | The thread polls at 50 ms; the same sample must not be counted twice |
| Publication | Separate `AltitudeStore`; source timestamp and sequence copied, never regenerated | Consumers of altitude never depend on pressure, and can still measure age |
| Failure | Publish `valid = false`, keep `P₀` | Flight decisions are inhibited upstream rather than fed a last-known height |
| Precision | Publish cm | 1 cm against ~0.9 m of observed baseline noise |
| Testability | Pure function in `src/altitude.cpp`, host tests in `tests/` | Checks the maths against independent references without a board |

## Flight logic (implemented)

| Decision | Value | Why |
|---|---|---|
| Trigger | Current altitude at or above the minimum **and** average velocity over the preceding 750 ms at or below -0.30 m/s | FLT-03, with a small noise guard above "negative" |
| Velocity | Least-squares slope of altitude over the preceding 750 ms, using every sample in the window | Timestamps, not sample counts; 50 ms divides 750 but cadence gaps still occur. A slope is the average rate, and one spike cannot move it |
| Trigger condition | Strictly negative and at least the margin below zero | "v <= 0" would treat a flat trace as descending, which matters when the margin is configured as 0 |
| History | Fixed 32 samples in the detector, evicted by timestamp after 2 s | Processing state owned by the apogee thread, not a second shared store |
| Duplicates | Repeated sequence numbers are ignored | The consumer polls faster than the producer publishes |
| Gaps | A hole longer than the stale timeout clears the window | A 750 ms estimate cannot be defended across an outage |
| Threshold | 1.50 m (`CONFIG_APP_APOGEE_MIN_ALT_CM`) | About twice the observed altitude noise; a bench value, documented as such |
| Descent margin | 0.30 m/s (`CONFIG_APP_APOGEE_MIN_DESCENT_MM_S`) | Noise guard only: a 0.3 m/s trailing average is reached ~0.41 s / 0.8 m after apogee, so a larger margin would delay detection. Ours, not the assignment's |
| Bench preset | `bench.conf`: altitude 50 cm, margin 0.60 m/s | The lab's air drift reaches 0.55 m/s, so the bench needs a *larger* margin than flight and a lower altitude gate; layered in at build time, never a default |
| Configuration | Kconfig symbols in `prj.conf`, compiled in | Satisfies "configurable" without adding a runtime command path or a second writer |
| Ready indication | Two 100 ms flashes once the window is full, gated on no active request; `CONFIG_APP_READY_INDICATION` | Without a console, this is the only way to know the chain is armed. Documented as a boot aid, not an assignment behaviour |
| Trigger repeat | Latched permanently in the detector; the store also refuses a second event | FLT-04, FLT-05 enforced twice, at the decision and at the state |
| Event time | The triggering sample's timestamp; deadline = event + 10 s | Deterministic and testable; the visible indication starts within ~30 ms |
| Heat alarm | 5 s window started on detection, renewed while still hot, cancelled immediately on cooling | Resolves the assignment's ambiguity; early cooling truncates the window and returns control to deployment |
| Heat hysteresis | 0.2 �C below the threshold | Sensor noise would otherwise chatter the alarm at the boundary |
| Heat renewal guard | Renew 60 ms before expiry, poll every 50 ms | Renewing at expiry left the LED dark for one poll; the guard keeps the blink continuous |
| I2C bus recovery | Nine clocks + STOP over PF0/PF1 at boot, plus a 0x76/0x77 probe on failure | A reset during a transfer can leave the sensor holding SDA; recovery avoids a power cycle, and the probe says whether the chip answers at all |
| Heat and missing data | Missing or stale temperature holds the state, never clears it | "No data" is not "cool"; the window expires on its own deadline |
| Heat threshold | 30.0 �C (`CONFIG_APP_HEAT_THRESHOLD_MC`), configurable | Above a still bench (~22 �C), reachable by hand |
| LED priority | Active heat blink > unexpired deployment ON > OFF | LED-04, LED-05 |
| Thresholds | Configurable Kconfig symbols (`CONFIG_APP_APOGEE_MIN_ALT_CM`, `CONFIG_APP_APOGEE_MIN_DESCENT_MM_S`); bench values set after a baseline measurement | FLT-02, LED-02. Changing them is a rebuild, not a code edit |
| Reset | One flight per boot/reset; a reset clears calibration, history and the deployment latch | Checklist 2.2; avoids a partial re-arm |
| Replay | Traces enter at the measurement publisher, never at the LED | Keeps the tested path identical to live operation (checklist 10.1) |

## LED manager (implemented)

| Decision | Value | Why |
|---|---|---|
| Ownership | `led_manager` is the only writer of PC13; everyone else publishes requests | Checklist 7; the temporary heartbeat in `main()` is removed |
| Update cadence | 10 ms | Measured request→change latency 11–21 ms, well inside any blink tolerance |
| Arbitration | `decide(now, request)`, a pure function | Testable with fake time; 33 host checks |
| Blink | 250 ms on / 250 ms off, phase anchored to the alarm start | 500 ms complete period; anchoring prevents drift when the loop jitters |
| Window boundary | Active while `start <= now < deadline` | One documented rule, tested at the exact instant |
| Deployment latch | First accepted event sets the deadline; later requests are ignored **inside the store** | FLT-04/LED-06 enforced where the state lives, not by trusting the producer |
| Deadline ownership | Producers set deadlines; the manager only reads them | Heat can preempt the output without pausing or restarting the deployment clock |
| Heat renewal | Renewing keeps the original start anchor, extends only the deadline | The blink grid must not jump when an alarm is extended |
| Request snapshot | One store, one lock, both requests copied together | The manager never sees "deployment cleared but heat not yet set" |
| Failure | LED not ready → warning and no manager; the rest of the system runs | An unavailable indicator must not stop acquisition |

## Concurrency

| Decision | Value | Why |
|---|---|---|
| Threads | `sensor`, `altitude`, `apogee`, `led`, `telemetry`, `diag` now; temperature to follow | SYS-01 |
| Priorities | sensor 5, altitude 6, apogee 7, led 8, telemetry 9, diag 10 (lower number = higher priority) | The sensor must preempt everything so printk cannot push the 50 ms deadline; altitude outranks apogee so a decision is never made on a stale estimate; the LED and console sit below both |
| Stacks | sensor 2048 B, altitude 2048 B, apogee 2048 B, led 1024 B, telemetry 1024 B, diag 1024 B | Sized for the driver, the 768 B baseline window, the 32-entry history and printk paths. A 1024 B altitude stack overflowed on the first flash and tripped the MPU |
| Console logging | Producers publish; consumers print; the LED manager prints only on a transition | The acquisition thread never blocks on RTT. The RTT up-buffer is 8 KB because 2 KB overran under the LED self-test |
| LED ownership | `led_manager` is the only writer; the temporary heartbeat loop is removed | Checklist 7; prevents competing GPIO writes |
