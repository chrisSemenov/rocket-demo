# Rocket Flight Controller Demo — Requirements

## 1. Purpose and scope

Develop a bench demonstration of a simple rocket flight controller using **Zephyr RTOS**, an **Olimex STM32-H407**, and a **BMP280** pressure and temperature sensor.

The application demonstrates concurrent processing, decoupled shared-memory communication, barometric altitude estimation, apogee detection, and priority-based control of a single user LED.

**Source of mandatory requirements:** [`assignment.md`](../assignment.md). Recommendations and unresolved details below are explicitly distinguished from assignment requirements.

Physical parachute deployment, guidance, motor control, and flight-safety certification are out of scope. Deployment is simulated using the LED only.

## 2. Mandatory requirements

### Hardware and platform

| ID | Requirement | Verification |
| --- | --- | --- |
| HW-01 | The application shall use the Olimex STM32-H407 development board and Zephyr RTOS. | Build, flash, and run on the specified board. |
| HW-02 | The application shall acquire pressure and temperature from a connected BMP280. | Demonstrate live readings and document wiring. |
| HW-03 | The same controllable user LED shall indicate deployment and overheating. | Observe both patterns on the same LED. |

### Sensor acquisition and shared memory

| ID | Requirement | Verification |
| --- | --- | --- |
| DATA-01 | A dedicated sensor thread shall periodically read pressure and temperature from the BMP280. | Inspect thread architecture and timestamped measurement logs. |
| DATA-02 | Both measurements shall be published in thread-safe shared memory. | Review synchronization and exercise concurrent reads/writes. |
| DATA-03 | Multiple independent consumers shall be able to read the latest measurements without direct task-to-task communication or producer-to-consumer calls. | Review shared-data interfaces and demonstrate altitude and temperature consumers operating independently. |
| DATA-04 | Shared-memory access shall be synchronized using appropriate Zephyr mechanisms. | Review all reader and writer access paths. |

### Altitude calculation

| ID | Requirement | Verification |
| --- | --- | --- |
| ALT-01 | A separate altitude thread shall read pressure from shared memory and calculate barometric altitude. | Compare calculated results with documented reference inputs. |
| ALT-02 | Calculated altitude shall be published to a separate shared-memory variable or object, accessible independently of sensor measurements. | Review data layout and demonstrate the apogee consumer reading the altitude store. |
| ALT-03 | Documentation shall explain the altitude calculation and pressure reference used. | Review README and reproduce an example calculation. |

### Apogee detection and deployment indication

| ID | Requirement | Verification |
| --- | --- | --- |
| FLT-01 | A separate apogee-detection thread shall monitor altitude and its rate of change. | Inspect architecture and recorded altitude/velocity output. |
| FLT-02 | The minimum altitude threshold shall be configurable. | Change the threshold and show the corresponding detection behavior. |
| FLT-03 | Deployment shall trigger when altitude exceeds the configured minimum and average vertical velocity over the preceding **750 ms** becomes negative, indicating descent. | Apply a trace with sufficient history, rising altitude, a peak, and descent; verify the trigger conditions. |
| FLT-04 | Deployment shall trigger only once per simulated flight. | Continue feeding descent and fluctuating altitude after deployment; verify no retrigger. |
| FLT-05 | A deployment event shall request a continuously ON user-LED indication lasting **10 seconds**, subject to overheating priority. | Measure the original deployment interval without an alarm and during overlap. |

### Temperature alarm and LED priority

| ID | Requirement | Verification |
| --- | --- | --- |
| LED-01 | A separate temperature-monitoring thread shall repeatedly read temperature from shared memory. | Inspect architecture and temperature monitoring logs. |
| LED-02 | The overheating threshold shall be configurable. | Change the threshold and demonstrate alarm activation. |
| LED-03 | Temperature exceeding the threshold shall trigger an alarm requesting LED blinking with a **500 ms complete period**, for **5 seconds**. | Measure blink cycle and alarm duration under the documented alarm policy. |
| LED-04 | Overheating shall have higher LED indication priority than deployment. While overheating is detected, the LED shall blink even if deployment is active. | Trigger overlapping heat and deployment conditions. |
| LED-05 | When temperature returns to normal, LED control shall revert to deployment if its **original** 10-second interval has not expired. | Clear heat before the deployment deadline; verify steady ON for only the remaining interval. |
| LED-06 | An overheating interruption shall not restart or extend the deployment interval. | Clear heat after the deployment deadline; verify no renewed deployment indication. |

### Concurrency and timing

| ID | Requirement | Verification |
| --- | --- | --- |
| SYS-01 | Sensor acquisition, altitude calculation, apogee detection, and temperature monitoring shall execute as separate Zephyr threads. | Review thread definitions and runtime operation. |
| SYS-02 | The application shall use appropriate RTOS timing mechanisms for periodic processing and timed indications. | Review sleep, timeout, and deadline handling. |
| SYS-03 | Shared data shall remain available to multiple independent consumers; one consumer shall not consume or remove the value needed by another. | Run altitude and temperature processing concurrently against the same measurement store. |

## 3. Design recommendations — not additional assignment mandates

| Topic | Recommended starting design | Rationale |
| --- | --- | --- |
| Sensor protocol | Start with **I²C**, subject to actual wiring and driver support. SPI is an alternative when bus throughput warrants it. | I²C is adequate for a modest demo rate and requires fewer signal wires. Faster transfers do not eliminate sensor conversion time. |
| Sampling | Begin at **20 Hz / 50 ms**, then measure actual acquisition timing. | Provides approximately 16 samples across a 750 ms interval when both endpoints are included. The assignment specifies no sampling frequency. |
| Sensor publication | Publish pressure, temperature, validity, timestamp, and sequence together. | Supports coherent snapshots and stale/duplicate detection. |
| Synchronization | Use short mutex-protected snapshot copies; perform sensor I/O and calculations outside the lock. | Avoids inconsistent data without holding a lock during slow work. |
| Altitude publication | Store altitude with validity and its source measurement timestamp/sequence. | Prevents counting one measurement repeatedly as new history. |
| Pressure reference | Average stable readings at the starting location to obtain a baseline pressure. | Enables relative height above the demo starting point. |
| Altitude model | Consider `h = 44330 × [1 − (P / P0)^0.1903]` metres, using matching pressure units. | A simple standard-atmosphere approximation; state its limitations. |
| Descent history | Keep timestamped altitude history spanning at least 750 ms in the apogee thread. | Supports an elapsed-time-based velocity estimate rather than assuming exact scheduling. |
| LED ownership | Use one LED-manager thread as the only GPIO writer. Other threads publish requests/state. | Implements explicit output arbitration and prevents competing GPIO writes. |
| Blink duty cycle | Use approximately **250 ms ON / 250 ms OFF**. | Implements a 500 ms full period at 50% duty cycle; duty cycle itself is not prescribed. |
| Indication timing | Store monotonic expiry timestamps; re-evaluate LED output promptly. | Allows overheating to override deployment without pausing its original deadline. |
| Sensor errors | Track errors and stale data; inhibit new apogee decisions when inputs are invalid. | Avoids false decisions from old or failed readings. |
| Flight reset | Define an explicit reset/re-arm action for each new simulated flight. | Makes the one-deployment-per-flight rule testable. |
| Language | Use C, or enable Zephyr C++ application support if useful. | The assignment does not mandate either language. |

Scheduler thread priority and LED indication priority are distinct: scheduler priority alone does not enforce which LED pattern wins.

## 4. Details to resolve and document

The assignment does not fully define the following. Record the chosen behavior in the README before treating these cases as accepted.

1. **Five-second alarm versus persistent heat:** decide how the alarm renews or retriggers if temperature remains high after five seconds. Behavior must still honor the requirement to blink while overheating is detected.
2. **Early cooling:** define how the five-second alarm request is cleared when temperature returns to normal. Honor the stated fallback to an unexpired deployment interval.
3. **Threshold behavior:** choose actual altitude and temperature thresholds; document any hysteresis, filtering, or descent confirmation margin.
4. **Velocity calculation:** define how the previous 750 ms is selected/interpolated and how incomplete history is handled. A negative window average shows net descent, not necessarily uninterrupted descent at every sample.
5. **Timing tolerance:** specify measurable tolerances for the 500 ms period and 5/10-second windows, based on scheduling and measurement resolution. No tolerance is supplied in the assignment.
6. **Hardware configuration:** confirm I²C or SPI, sensor address/chip select, connector pins, GPIO polarity, and compatible driver configuration for the selected Zephyr version.
7. **Failure and reset behavior:** define handling for startup, sensor failures, stale data, and beginning a new simulated flight.

## 5. Acceptance scenarios

| ID | Scenario | Expected evidence |
| --- | --- | --- |
| AT-01 | Build and run on target hardware. | Successful build/flash and runtime output from STM32-H407 with BMP280 attached. |
| AT-02 | Read real sensor measurements. | Live pressure and temperature values with units and timestamps. |
| AT-03 | Calculate altitude from a documented pressure/reference pair. | Result agrees with the chosen formula within documented numerical tolerance. |
| AT-04 | Remain below the minimum altitude or continue ascending. | No deployment trigger. |
| AT-05 | Exceed minimum altitude, then show negative average velocity across a complete 750 ms history. | One deployment event; LED requests steady ON for 10 seconds. |
| AT-06 | Continue descent after deployment. | No second deployment or deadline extension in the same simulated flight. |
| AT-07 | Trigger overheat without deployment. | 500 ms full-period blinking and documented five-second/persistent-heat behavior. |
| AT-08 | Start deployment, then overheat, then cool before the original deployment deadline. | Blink overrides steady ON, then steady resumes only until the original deadline. |
| AT-09 | Keep overheat active past the deployment deadline, then cool. | No resumed deployment indication. |
| AT-10 | Run all consumers concurrently. | Coherent shared snapshots without direct task-to-task dependencies. |
| AT-11 | Restart the simulated flight using the documented reset procedure. | One new deployment is possible after reset. |

Recommended robustness tests additionally cover incomplete velocity history, noisy inputs, repeated timestamps, and sensor read failures. Injected test traces must be clearly identified; they do not replace demonstration of live BMP280 acquisition.

## 6. Required deliverables and definition of done

- [ ] Source code builds, flashes, and runs on the specified hardware.
- [ ] Zephyr configuration and Devicetree files are included.
- [ ] Live sensor readings and altitude calculation are demonstrated.
- [ ] Apogee detection and once-per-flight deployment are demonstrated.
- [ ] The 10-second deployment indication and temperature alarm are demonstrated.
- [ ] Overlap tests demonstrate temperature priority and correct fallback.
- [ ] README documents wiring, Zephyr environment setup, build/flash steps, thread architecture, shared-memory design, altitude/descent algorithms, configurable thresholds, and selected ambiguity resolutions.
- [ ] A pre-recorded working demonstration confirms the required behavior.

This document specifies intended behavior; it does not certify that firmware or hardware verification has been completed.
