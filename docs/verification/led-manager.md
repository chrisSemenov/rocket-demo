# M7: LED manager and priority arbitration

**Purpose:** prove that one thread owns the LED, that the three output states are
arbitrated by the documented priority, and that the deployment deadline is never
extended (HW-03, FLT-05, LED-03–06, SYS-02).

**Date:** 2026-10-29
**Board:** Olimex STM32-H407 (LED = PC13, active low)
**Build:** 42 716 B flash / 23 104 B RAM with the self-test; 41 336 B / 21 824 B release

## 1. Unit tests of the pure decision function

`tests/run-tests.ps1` compiles `src/led_logic.cpp` on the host and steps fake time
through every branch — 10-second deployments and 5-second alarms take no wall
clock, and boundary instants are exact.

```
=== led_logic_test.cpp ===
ok   idle                                     t=     0 off
ok   deployment at t=0                        t=     0 on
ok   deployment last ms                       t=  9999 on
ok   deployment at deadline (expired)         t= 10000 off
ok   blink first half                         t=     0 blink_on
ok   blink second half                        t=   250 blink_off
ok   blink second cycle                       t=   500 blink_on
ok   heat overrides mid-cycle                 t=  2250 blink_off
ok   cooled at 7 s -> deployment resumes      t=  7000 on
ok   overlap at deployment expiry             t= 10000 blink_on
ok   stale heat ignored                       t=  5000 on
ok   heat at deadline-1                       t=  4999 blink_off
ok   heat at deadline                         t=  5000 on
...
PASSED (0 failures)
```

35 checks in total; the full list is the suite output.

## 2. On-target scripted sequence

`APP_LED_SELFTEST=ON` adds a thread that publishes **real requests through the
real stores** — the only thing faked is who asks, not how the manager decides.
The firmware prints `TEST MODE: LED SELFTEST is active` at boot so the log cannot
be mistaken for normal operation.

| Time | Request | LED | Observed |
|---|---|---|---|
| 0.011 s | none | off | `off` |
| 2.021 s | deployment, deadline 12 s | on | `on (deployment)` |
| 4.021 s | heat | blink | `blink on` … 250 ms alternation |
| 9.021 s | heat cleared early | on | `on (deployment)` — same deadline |
| 12.011 s | deployment expires | off | `off` |
| 14.021 s | heat only | blink | `blink on` … |
| 17.021 s | cooled early | off | `off` (nothing to resume) |
| 20.021 s | heat again | blink | `blink on` … |
| 25.011 s | cooled at window end | off | `off` |
| 26.000 s | second deployment request | off | ignored, `deployment events=1` |

Measured from the transition log (59 transitions):

| Quantity | Result |
|---|---|
| Blink half-period | 250 ms in 50 of 52 intervals; two at 240 ms |
| Full blink period | 500 ms |
| Deployment steady ON | 2.021 → 12.011 s = **9.990 s** |
| Request → visible change | **11–21 ms** (10 ms poll + log) |
| Deployment events after the 26 s re-request | **1** |
| Deadline change while heat owned the output | none |

**Repeatability:** the whole sequence was run three times from a cold reset in
one capture (release build restored and reflashed between runs). Every run
produced the same timestamps — 2.021 → 12.011 s for the deployment, the 250 ms
grid, `deployment events=1`, no faults — and only the sensor readings differed.

The raw console capture for all three runs is kept verbatim in
`tests/evidence/m7-led-selftest-2026-10-29.log` (1 129 lines). Grep it for
`SELFTEST:` and `led:` to see the script and the resulting transitions
interleaved with the live sensor stream.

## 3. What this demonstrates

- **Single owner.** The only GPIO write is in `led_manager.cpp`; the heartbeat
  loop that used to live in `main()` is gone, so nothing competes for the pin.
- **Priority.** Heat blinking starts even while a deployment is running, and
  steady ON resumes the moment heat clears — with the *original* deadline.
- **No extension.** The store latches the first accepted deployment event, so a
  later descending sample cannot move the deadline. The manager never writes a
  deadline at all; it only reads them.
- **Anchored phase.** Blinking is computed from the alarm start, not from when
  the thread happens to run, so the 250 ms grid does not drift.
- **Half-open windows.** A window is active while `start <= now < deadline`; the
  deadline instant itself is expired. This is the documented boundary and is
  tested both ways.

## 4. Defect found by this test

The first run lost about nine seconds of console output, including one line cut
mid-word (`...publis[ 13.01 s]`), while `seq` continued advancing at exactly
20 Hz. That is an RTT ring-buffer overrun, not a firmware stall: the LED
self-test emits four transition lines per second on top of the normal telemetry,
and the 2 KB up-buffer could not absorb a slow reader.

`CONFIG_SEGGER_RTT_BUFFER_SIZE_UP` is now 8192. The re-run captured all 59
transitions with no truncated lines.

## 5. Not covered

- The LED was not filmed or measured with a logic analyser; timing comes from
  the transition log at 10 ms resolution, so 250 ms means 250 ± 10 ms.
- The real heat and deployment producers do not exist yet (M8, M9). They must
  publish `request_heat()` / `request_deployment()`; this test proves the
  consumer side, not those producers.
- Duty cycle is nominally 50% by construction; it was not measured with a meter.

## 6. Operational note: resetting mid-transfer wedges the I²C bus

Replaying the sequence with a target reset (rather than a reflash) worked twice
and then failed: every subsequent boot reported

```
ERROR: bmp280@76 not ready. Check 3V3, GND, SDA=PF0, SCL=PF1, ...
```

A reset issued while the BMP280 is mid-conversion can leave the sensor holding
SDA low, and an MCU reset does not clear it — a software reset repeated the same
failure. Recovery required a **power cycle**. Implication for the demo: replay
the sequence by reflashing, or reset only when the LED sequence is not running,
and keep a power cycle available if the bus latches.
