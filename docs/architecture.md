# Architecture

The application is a set of Zephyr threads that share data through
latest-value snapshots. No thread calls another thread; consumers read the
store at their own rate and a read never removes anything.

## Data flow

```text
                 +-------------------+
   BMP280  --->  | sensor thread     |  50 ms, forced mode, one conversion per fetch
   (I2C2)        | priority 5        |  (replay thread instead, in replay mode)
                 +---------+---------+
                           | publish(Measurement)
                           v
                 +-------------------+        (k_mutex, copy in / copy out)
                 | SensorDataStore   |<------- telemetry thread   500 ms, prints
                 | measurement       |<------- heat thread        50 ms, alarm policy
                 | stats             |<------- (sensor thread publishes stats)
                 +---------+---------+
                           ^
                           | read(Measurement)
                 +---------+---------+
                 | altitude thread   |  baseline P0 frozen after 2 s, 50 ms
                 | priority 6        |
                 +---------+---------+
                           | publish(Altitude)
                           v
                 +-------------------+
                 | AltitudeStore     |<------- telemetry thread   prints
                 | altitude_cm       |<------- apogee thread      consumes
                 | calibrated        |
                 +---------+---------+
                           | read(Altitude)
                           v
                 +-------------------+
                 | apogee thread     |  750 ms avg+slope, median peak, latched once
                 | priority 7        |
                 +---------+---------+
                           | request_deployment(event, deadline)
                           v
                 +-------------------+        request_deployment()
                 | LedRequestStore   |<------- apogee thread
                 | deployment        |<------- heat thread        request_heat()
                 | heat              |
                 +---------+---------+
                           | read(led::Request)
                           v
                 +-------------------+
                 | led_manager       |  10 ms, sole owner of PC13
                 | priority 9        |
                 +-------------------+
```

The stores are the only path between threads. The sensor thread owns the sensor
API exclusively; nothing else touches I²C. The altitude thread publishes into its
own store, and the apogee detector will consume that one, so flight logic never
depends on the sensor thread directly.

Next stages, in implementation order:

```text
Measurement ---> altitude thread ---> AltitudeStore ---> apogee detector   [done]
                                                    \--> (history, 750 ms avg)
Apogee --------> deployment request ----> LedRequestStore   [done]
Temperature ---> heat thread ----------> LedRequestStore   [done]
LedRequestStore -> led manager thread --> PC13              [done]
```

## Threads

| Thread | Period | Priority | Stack | Responsibility |
|---|---|---|---|---|
| `main` | — | 0 | default | Initialise stores, start the threads, then idle |
| `sensor` | 50 ms, absolute deadline | 5 | 2048 B | Fetch, timestamp, publish; count errors and overruns; publish timing stats |
| `altitude` | 50 ms, absolute deadline | 6 | 2048 B | Read a measurement copy, calibrate `P₀`, compute altitude, publish a separate snapshot; never prints |
| `apogee` | 50 ms | 7 | 2048 B | Keep a 750 ms altitude window, require the average and the least-squares slope to agree on descent above the minimum, estimate the peak, publish one deployment request || `heat` | 50 ms | 8 | 1024 B | Apply the 5 s overheat window, renewal and hysteresis; publish a heat request |
| `led` | 10 ms | 9 | 1024 B | Read the request snapshot, arbitrate, write PC13; prints only on a transition |
| `telemetry` | 500 ms | 10 | 1024 B | Read the stores, print samples, altitude and the ten-second stats line |

Priority, not period, is what protects the acquisition rate: the sensor thread
preempts every consumer, so a stalled `printk` (RTT back-pressure) cannot move the
50 ms deadline. The console threads are also the only ones that can block for a
long time. Altitude sits at 6 — above the console, below the sensor — because it
consumes every sample but never prints.

## Shared data rules

1. Each piece of state lives in exactly one store and is reached through
   `publish` / `read` copies. There is no direct access to a producer's variable.
2. Locks are held only for the struct copy — never across I²C, `printk`, or
   arithmetic.
3. A failed acquisition publishes `valid = false` with the error code and does
   not advance the sequence, so a consumer cannot mistake a failure for a new
   sample.
4. Consumers check `valid` and the sample age against the 250 ms stale timeout
   before using a value for a decision.
5. One lock is taken at a time; where two snapshots are needed they are copied
   sequentially, never nested. Telemetry reads the measurement and then the
   altitude, released in between.
6. The stores are initialised from `main()` before any thread starts, so no
   consumer can observe a half-built store.
7. An altitude always carries the timestamp and sequence of the measurement it
   came from, so "how old is this height" is answerable without reading the
   sensor store.

## Timing model

Every periodic thread uses an absolute deadline (`next += period`, then
`k_sleep(K_TIMEOUT_ABS_MS(next))`). A thread that overruns counts the overrun and
re-anchors, then still sleeps to the new slot, so a missed deadline costs one
period instead of compressing the next interval.

Measured on hardware: fetch 24 ms every period, inter-sample interval 50 ms,
zero overruns (`docs/verification/`).

## Stack usage (measured)

High-water marks from `CONFIG_THREAD_ANALYZER` after ~45 s of live operation
(2026-10-31). Read these as "worst case observed in this run", not as a proof of
the worst case: the analyzer adds a thread and its own stack, and the run did not
include a deployment, a heat alarm, a bus recovery or a replay. Every path that
adds stack has since been exercised on the target, but the marks were not
re-taken with the analyzer enabled.

| Thread | Used / size | Headroom |
|---|---|---|
| `sensor` | 576 / 2048 (28 %) | 1 472 B |
| `altitude` | 1 056 / 2048 (51 %) | 992 B |
| `apogee` | 1 032 / 2048 (50 %) | 1 016 B |
| `heat` | 488 / 1024 (47 %) | 536 B |
| `led` | 400 / 1024 (39 %) | 624 B |
| `telemetry` | 480 / 1024 (46 %) | 544 B |
| `main` | 416 / 1024 (40 %) | 608 B |

Every thread keeps at least half a kilobyte free. The tightest are `heat` and
`telemetry`; if either grows, raise its stack rather than relying on the margin.

Reproduce with:

```powershell
west build -p always -b olimex_stm32_h407 -d build/analyzer . -- `
  "-DCONFIG_THREAD_ANALYZER=y" "-DCONFIG_THREAD_ANALYZER_USE_PRINTK=y" `
  "-DCONFIG_THREAD_ANALYZER_AUTO=y" "-DCONFIG_THREAD_ANALYZER_AUTO_INTERVAL=30"
```

The analyzer is not part of the release configuration.