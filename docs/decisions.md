# Design decisions

Decisions taken before or during implementation. Each one is either a mandatory
requirement, a recommendation from `docs/requirements.md`, or a resolution of
something the assignment leaves open. File paths are relative to the repository
root.

## Platform and acquisition

| Decision | Value | Why |
|---|---|---|
| Sensor transport | I2C2, SCL=PF1, SDA=PF0, address 0x76 | Both signals sit on the PF header next to 3.3 V and GND; see the README wiring table |
| Measurement period | 50 ms (20 Hz), absolute deadlines | 750 = 15 x 50, so the descent window ends on a stored sample |
| Oversampling | 8x pressure, 2x temperature | 24 ms conversion leaves 26 ms of slack; 16x would leave 5.5 ms for a precision gain nobody can observe |
| Mode | Forced, one conversion per fetch | The value read is always a fresh conversion, not whatever normal mode last left in the data registers |
| IIR filter | Off | The filter needs normal mode to smooth across samples; in forced mode it has no history. The 750 ms window does the averaging |
| Humidity oversampling | 1x although a BMP280 has none | The driver's sample-time constant counts it; at the default 16x it computes 53 ms and every fetch would overrun the period |
| Measurement period evidence | fetch 24 ms, dt 50 ms, 0 overruns | README, "Timing evidence"; `docs/verification/` |

## Data and time

| Decision | Value | Why |
|---|---|---|
| Internal units | pressure Pa (int32), temperature milli-C (int32), altitude cm (int32), velocity mm/s (int32), time ms (`k_uptime_get`, int64) | Integers at the boundary avoid float printf and float in shared state |
| Publication | One `Measurement` struct: pressure, temperature, timestamp, sequence, valid, error | A reader can never pair pressure from one sample with temperature from the next (DATA-02) |
| Access | `SensorDataStore` with a `k_mutex`; copy in, copy out; no mutable references | Locks are short and never held across I2C, printing or computation |
| Read semantics | Non-consuming latest-value snapshot | Any number of consumers read independently at their own rates (DATA-03, SYS-03) |
| Sequence | Incremented only when a sample is successfully published | A failed fetch must not look like a new measurement |
| Timestamp | Acquisition time (`k_uptime_get` after the fetch), not the consumer's read time | Age is measurable; consumers can reject stale data |
| Invalid publication | On fetch failure publish `valid = false` with the error code, sequence unchanged | Consumers stop trusting the store instead of inheriting the last good value |
| Stale timeout | 250 ms (5 periods) | Short enough that a dead sensor cannot drive a decision, long enough to survive a missed sample |
| Overrun handling | Re-anchor and sleep to the new slot | Skipping the slot is honest; falling through would compress one interval and stretch the next |

## Altitude

| Decision | Value | Why |
|---|---|---|
| Model | `h = 44330 * (1 - (P/P0)^0.1903)` m | Standard-atmosphere approximation; limits documented in the README |
| Numeric form | `-44330 * expm1(0.1903 * log1p((P - P0)/P0))` | Near the bench `P` is close to `P0`, where `1 - pow()` subtracts two nearly equal numbers |
| Input validation | 30 000-110 000 Pa, both inputs; non-finite result rejected | The BMP280's own range. A bus fault must produce an invalid altitude, not a plausible one |
| Reference | `P0` = mean of valid samples over 2 s while stationary, then frozen | BMP280 absolute accuracy is about +/-1 hPa, so only a relative calculation is meaningful |
| Stationarity rule | coverage >= 1 800 ms and >= 38 of 40 samples within a 20 Pa spread; otherwise restart from the current sample | 20 Pa is about 1.7 m here, well above the observed noise, so picking the sensor up restarts calibration |
| Coverage constant | `kBaselineMinCoverageMs = 1800` (2 s with 200 ms of slack) | Separate name instead of a `- 200` literal, with the slack written down |
| Gap while calibrating | A gap longer than the stale timeout clears the window | Stationarity across a gap cannot be claimed |
| Gap after calibration | `P0` is kept; only the published altitude goes invalid | A missed sample must not force a recalibration, and must not move the reference mid-flight |
| Duplicate samples | Altitude is recomputed only when the source sequence changes | The thread polls at 50 ms; the same sample must not be counted twice |
| Publication | Separate `AltitudeStore`; source timestamp and sequence copied, never regenerated | Consumers of altitude never depend on pressure, and can still measure age |
| Failure | Publish `valid = false`, keep `P0` | Flight decisions are inhibited upstream rather than fed a last-known height |
| Precision | Publish cm | 1 cm against about 0.9 m of observed baseline noise |

## Flight logic

| Decision | Value | Why |
|---|---|---|
| Trigger | Current altitude at or above the minimum **and** the 750 ms average velocity at or below -0.30 m/s **and** at least 25 cm below the peak | FLT-03 plus a noise guard and a descent confirmation |
| Velocity | `(h(now) - h(now - 750 ms)) / 0.750 s`, window start linearly interpolated between bracketing samples | This is the assignment's "average vertical velocity over the previous 750 ms". Timestamps, not sample counts |
| Diagnostic | Least-squares slope over the same window, logged as `slope=` | Useful for comparison, but it is a different function: it disagrees in sign with the endpoint average on any non-monotonic path, so it must not arm the trigger |
| Trigger condition | Strictly negative and at least the margin below zero | `v <= 0` would treat a flat trace as descending, which matters when the margin is 0 |
| History | Fixed 32 samples, evicted by timestamp after 2 s | Processing state owned by the apogee thread. At 20 Hz the 32-entry capacity (1.55 s) binds first, so "2 s" is an upper bound rather than the working depth |
| Duplicates | Repeated sequence numbers are ignored | The consumer polls faster than the producer publishes |
| Gaps | A hole longer than the stale timeout clears the window | A 750 ms estimate cannot be defended across an outage |
| Threshold | 1.50 m (`CONFIG_APP_APOGEE_MIN_ALT_CM`) | About twice the observed altitude noise; a bench value |
| Descent margin | 0.30 m/s (`CONFIG_APP_APOGEE_MIN_DESCENT_MM_S`) | Noise guard only: a 0.3 m/s trailing average is reached about 0.41 s / 0.8 m after apogee |
| Drop confirmation | 25 cm (`CONFIG_APP_APOGEE_MIN_DROP_CM`) | A lift sets a new peak every sample, so its drop is always zero |
| Bench preset | `bench.conf`: altitude 50 cm, margin 0.60 m/s, drop 15 cm | Room air drift reaches 0.55 m/s, so the bench needs a *larger* margin and a lower gate |
| Configuration | Kconfig symbols in `prj.conf`, compiled in | Satisfies "configurable" without a runtime command path or a second writer |
| Ready indication | Two 100 ms flashes once the window is full, gated on no active request | Without a console this is the only way to know the chain is armed. Disabled by `demo.conf` for the recording |
| Trigger repeat | Latched in the detector; the store also refuses a second event | FLT-04 and FLT-05 enforced twice |
| Event time | The triggering sample's timestamp; deadline = event + 10 s | Deterministic and testable |
| Heat alarm | 5 s window started on detection, renewed while above the release threshold, cancelled on cooling | Resolves the assignment's ambiguity; early cooling truncates the window and returns control to deployment |
| Heat hysteresis | 0.2 C: hot at or above the threshold, cold below threshold - band | Sensor noise would otherwise chatter the alarm at the boundary |
| Heat renewal rule | Renew while above the release threshold, not only above the trigger threshold | Otherwise a temperature in the band left `state() == hot` while the alarm lapsed |
| Heat renewal guard | Renew 60 ms before expiry, poll every 50 ms | Renewing at expiry left the LED dark for one poll |
| Heat and missing data | Missing or stale temperature holds the state, never clears it | "No data" is not "cool"; the window expires on its own deadline |
| LED priority | Active heat blink > unexpired deployment ON > OFF | LED-04, LED-05 |
| Reset | One flight per boot; a reset clears calibration, history and the deployment latch | Avoids a partial re-arm |
| Replay | Traces enter at the measurement publisher, never at the LED | Keeps the tested path identical to live operation |

## I2C recovery

| Decision | Value | Why |
|---|---|---|
| Boot recovery | `bus_recovery.cpp`, `SYS_INIT` at POST_KERNEL priority 51 | After the I2C controller (50) and before the sensor driver (90). A device whose init fails stays not-ready, so recovering later cannot help |
| Ordering enforcement | `BUILD_ASSERT(51 > CONFIG_I2C_INIT_PRIORITY)` and `BUILD_ASSERT(51 < CONFIG_SENSOR_INIT_PRIORITY)` | The number is a literal because `SYS_INIT` stringifies it into the section name; the asserts keep it honest |
| Reporting | Result stored, printed by `main()` | The console is not installed at priority 51 |
| Runtime recovery | After 3 consecutive fetch failures, then every 60 | One attempt at exactly 3 meant a permanently wedged bus got a single try per boot |
| Counters | `recoveries` (returned 0) and `failed` separately | An attempt is not a success; the stats line shows both |
| Probe on failure | Read the chip ID at 0x76 and 0x77 | Distinguishes a wrong address strap from a dead bus. Note the probe ACKing does not mean the sensor driver recovered for this boot |

## LED manager

| Decision | Value | Why |
|---|---|---|
| Ownership | `led_manager` is the only writer of PC13; everyone else publishes requests | Prevents competing GPIO writes |
| Update cadence | 10 ms | Measured request-to-change latency 11-21 ms (M7 self-test) |
| Arbitration | `decide(now, request)`, a pure function | Testable with fake time; 35 host checks |
| Blink | 250 ms on / 250 ms off, phase anchored to the alarm start | 500 ms complete period; anchoring prevents drift when the loop jitters |
| Window boundary | Active while `start <= now < deadline` | One documented rule, tested at the exact instant |
| Deployment latch | First accepted event sets the deadline; later requests are ignored **inside the store** | FLT-04/LED-06 enforced where the state lives |
| Deadline ownership | Producers set deadlines; the manager only reads them | Heat can preempt the output without pausing or restarting the deployment clock |
| Heat renewal | Renewing keeps the original start anchor, extends only the deadline | The blink grid must not jump when an alarm is extended |
| Request snapshot | One store, one lock, both requests copied together | The manager never sees "deployment cleared but heat not yet set" |
| Write failure | Retried every period until it succeeds; counted and reported at most every 200 | A failed write must not be remembered as applied |
| Failure to initialise | Warning and no manager; the rest of the system runs | An unavailable indicator must not stop acquisition |

## Concurrency

| Decision | Value | Why |
|---|---|---|
| Threads | `sensor` (or `replay` in test mode), `altitude`, `apogee`, `heat`, `led`, `telemetry` | SYS-01 |
| Priorities | sensor 5, altitude 6, apogee 7, heat 8, led 9, telemetry 10 | The sensor must preempt everything so printk cannot push the 50 ms deadline; the LED and console sit below the decision logic |
| Stacks | sensor 2048 B, altitude 2048 B, apogee 2048 B, heat 1024 B, led 1024 B, telemetry 1024 B | Sized from measured high-water marks after a live run; see `architecture.md`. A 1024 B altitude stack overflowed on the first flash and tripped the MPU |
| Console logging | Producers publish; consumers print; the LED manager prints only on a transition | The acquisition thread never blocks on RTT. The RTT up-buffer is 8 KB because 2 KB overran under the LED self-test |
