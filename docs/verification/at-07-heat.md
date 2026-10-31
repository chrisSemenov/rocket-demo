# M9: temperature monitor and heat policy

**Purpose:** show that overheat is detected from the shared measurement snapshot,
that the alarm renews while hot and clears on cooling, and that it never treats
missing data as a cool reading (LED-01, LED-02, LED-03, SYS-01, SYS-03; AT-07).

**Date:** 2026-10-29, renewal rule revised 2026-10-31
**Board:** Olimex STM32-H407, BMP280 on I2C2
**Build:** 48 864 B flash / 24 128 B RAM

## 1. What is implemented

| Element | Behaviour |
|---|---|
| Thread | `heat`, 50 ms, priority 8 -- reads the measurement store only |
| Threshold | 30.0 C (`CONFIG_APP_HEAT_THRESHOLD_MC`), hot at or above |
| Release | Below `threshold - hysteresis`, i.e. 29.8 C (`CONFIG_APP_HEAT_HYSTERESIS_MC` = 200) |
| Window | 5 s from the hot sample (`heat::kHeatAlarmMs`) |
| Renewal | While still above the release threshold: another 5 s, original start kept |
| Late renewal | A window that has already lapsed restarts in full from the current sample |
| Cooling | Clears immediately, below the release threshold |
| Missing data | State held; never treated as cooling. The window still expires on its deadline |
| Duplicates | A repeated sequence is ignored |
| Output | `request_heat(start, deadline)` / `clear_heat()`; the LED manager owns the pin |

**Why renewal uses the release threshold and not the trigger threshold.** An
earlier version renewed only while the sample was at or above 30.0 C. A
temperature sitting at 29.9 C -- above the release threshold, below the trigger
threshold -- left the monitor reporting `hot` while the alarm lapsed and the LED
quietly fell back to the deployment indication. Renewing on the release threshold
makes `state() == hot` and `request().active` agree at all times, which is the
property a reader of the log would assume. The cost is that the alarm continues
while the temperature hovers in a 0.2 C band, which is the intended meaning of
hysteresis: the alarm latches until the condition clears.

## 2. Host tests

37 checks, no board:

```
ok   cool samples stay cool
ok   hot raises the request
ok   window starts at the hot sample
ok   window is 5 s
ok   hot samples inside the window do not restart it
ok   a repeated sequence changes nothing
ok   renewal keeps the alarm anchor
ok   renewal extends by 5 s
ok   a band sample just before expiry renews
ok   the alarm stays active inside the band
ok   a band sample after expiry restarts the window
ok   a lapsed window restarts in full
ok   a late renewal starts a fresh window, no back-filling
ok   cooling clears the request at once
ok   the hysteresis band holds the state
ok   temperature equal to the threshold is hot
ok   missing data does not clear the alarm
ok   a new alarm has a new anchor
ok   each expiry renews by another 5 s
ok   the anchor never moves
ok   reset returns to cool
...
PASSED (0 failures)
```

## 3. Live run

Captured on 2026-10-29. The threshold was deliberately set *below* ambient
(`-DCONFIG_APP_HEAT_THRESHOLD_MC=20000`, i.e. 20.0 C against a 21.6 C bench) so
the policy could be exercised without a hand on the sensor. This is a test
configuration, not the shipped default, and it predates the renewal change, so
the log uses the older wording.

```
heat: threshold 20.000 C, hysteresis 200 mC, window 5000 ms
heat: HOT at 21.560 C; alarm until 5053 ms (seq=1)
led:  [      4.061] blink on (heat)
led:  [      4.311] blink off (heat)
led:  [      4.561] blink on (heat)
led:  [      4.811] blink off (heat)
led:  [      5.061] blink on (heat)
led:  [      5.311] blink off (heat)
heat: HOT at 21.680 C; alarm until 10053 ms (seq=101)
heat: HOT at 21.670 C; alarm until 15053 ms (seq=201)
heat: HOT at 21.670 C; alarm until 20053 ms (seq=301)
```

| Check | Result |
|---|---|
| Alarm starts on the first hot sample | yes, `HOT` within one sample |
| Blink half-period | 250 ms, continuously |
| Renewal cadence | +5 000 ms per window, exactly (5053 -> 10053 -> 15053) |
| Blink phase across a renewal | continuous, no jump |
| Dark gap at renewal | **none** after the fix below |
| Alarm anchor | unchanged across renewals |

Under the current build the same lines read `still hot at ...; alarm extended to
...`, which distinguishes a renewal from a new alarm. The current captures are in
`replay.md` (AT-07a, AT-07b) and `tests/evidence/m10/`.

## 3a. Defect found: a dark gap at every renewal

The first build renewed only when the deadline had already passed, and the heat
thread polled at 100 ms. The result was a visible gap once per window:

```
led:  [      4.841] blink off (heat)
led:  [      5.091] off            <- window expired
led:  [      5.111] blink on (heat) <- renewal arrived
```

Two changes removed it:

- renew **60 ms before** expiry (`renew_guard_ms`), so the deadline is extended
  before the LED manager can see it lapse;
- poll the measurement store every **50 ms** instead of 100 ms, so a sample always
  lands inside that guard.

Renewal still advances the deadline by whole windows, so the five-second grid is
preserved rather than drifting earlier each time.

## 4. What this does not prove

- **A hand-triggered heat event.** The live capture used a lowered threshold and
  the acceptance runs use a replayed temperature. The policy is identical, but
  the physical path (finger on the breakout, about 30 C) has not been filmed.
- **Threshold realism.** 30 C is a bench value; a real vehicle would set it from
  its own thermal envelope.
