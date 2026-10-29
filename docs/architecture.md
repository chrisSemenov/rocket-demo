# Architecture

The application is a set of Zephyr threads that share data through
latest-value snapshots. No thread calls another thread; consumers read the
store at their own rate and a read never removes anything.

## Data flow

```text
                 +-------------------+
   BMP280  --->  | sensor thread     |  50 ms, forced mode, one conversion per fetch
   (I2C2)        | priority 5        |
                 +---------+---------+
                           | publish(Measurement)
                           v
                 +-------------------+        (k_mutex, copy in / copy out)
                 | SensorDataStore   |<------- telemetry thread   500 ms, prints
                 | measurement       |<------- diag thread       1000 ms, prints
                 | stats             |<------- altitude thread    50 ms, computes
                 +---------+---------+<------- temperature thread (next milestone)
                           ^
                           | read(Measurement)
                 +---------+---------+
                 | altitude thread   |  baseline P0 frozen after 2 s, 50 ms period
                 | priority 6        |
                 +---------+---------+
                           | publish(Altitude)
                           v
                 +-------------------+
                 | AltitudeStore     |<------- telemetry thread   prints
                 | altitude_cm       |<------- apogee thread      (next milestone)
                 | calibrated        |<------- LED manager        (next milestone)
                 +-------------------+
```

The stores are the only path between threads. The sensor thread owns the sensor
API exclusively; nothing else touches I²C. The altitude thread publishes into its
own store, and the apogee detector will consume that one, so flight logic never
depends on the sensor thread directly.

Next stages, in implementation order:

```text
Measurement ---> altitude thread ---> AltitudeStore ---> apogee detector   [altitude done]
                                                    \--> (history, 750 ms avg)
Temperature ---> temperature monitor ---> LED request
Apogee -------> deployment request ----> LED request
LED request ---> LED manager thread ---> PC13
```

## Threads

| Thread | Period | Priority | Stack | Responsibility |
|---|---|---|---|---|
| `main` | — | 0 | default | Initialise stores, start threads, then idle with a temporary LED heartbeat |
| `sensor` | 50 ms, absolute deadline | 5 | 2048 B | Fetch, timestamp, publish; count errors and overruns; publish timing stats |
| `altitude` | 50 ms, absolute deadline | 6 | 2048 B | Read a measurement copy, calibrate `P₀`, compute altitude, publish a separate snapshot; never prints |
| `telemetry` | 500 ms | 8 | 1024 B | Read both stores, print samples, altitude and the ten-second stats line |
| `diag` | 1000 ms | 9 | 1024 B | Independent reader proving reads do not consume; replaced by a real consumer later |
| apogee, temperature, LED manager | — | — | — | Added in later milestones |

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

Every periodic thread uses an absolute deadline (`next += period`,
`k_sleep(K_TIMEOUT_ABS_MS(next))`). A thread that overruns counts the overrun and
re-anchors instead of bursting to catch up, so the reported period stays honest.

Measured on hardware: fetch 24 ms every period, inter-sample interval 50 ms,
zero overruns (`docs/verification/`).
