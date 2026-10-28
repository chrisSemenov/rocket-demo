# M4 failure-path test: sensor not answering

**Purpose:** prove the firmware reports a failure instead of publishing
invented or stale-repeat measurements when the sensor cannot be reached
(checklist 4.3).

**Date:** 2026-10-28
**Firmware:** same source as the working build; only the devicetree address was
changed.

## Method

`app.overlay` was edited temporarily so the sensor node became `bmp280@77` with
`reg = <0x77>`. The breakout's `SDO` is tied to GND, so the real device answers
only at `0x76` and nothing responds at `0x77` — the same electrical situation as
an unplugged sensor, without touching the wiring. The board was flashed and the
RTT console captured. The overlay was then restored and the working image
reflashed.

## Observed output

```
*** Booting Zephyr OS build v4.4.2 ***
ERROR: bmp280@77 not ready. Check 3V3, GND, SDA=PF0, SCL=PF1, CSB->3V3, SDO->GND (address 0x76 = SDO low, 0x77 = SDO high).
```

OpenOCD reported `** Verified OK **` and found the RTT control block at
`0x20000810`, so the image was running. Observations:

- `device_is_ready()` failed, so the sensor thread was never created and
  `SensorDataStore` was never written.
- No `[ s] seq=` line, no `diag:` line and no `stats:` line appeared, i.e. no
  consumer ever reported a measurement.
- The board stayed alive with a usable console instead of resetting in a loop.

## Fix found by this test

The message previously ended with a hard-coded `(address 0x76)`, which
contradicted the node name in the test build. It now names both strapping
options and takes the address from the node.

## Not covered

The physically unplugged-sensor variant (power off, connector removed) has not
been performed; the address test exercises the same driver failure path without
risking the wiring.
