#pragma once

#include <cstddef>
#include <cstdint>

/*
 * Named timing, sizing and priority constants for the whole application.
 * Altitude and temperature thresholds are added with the tasks that use them;
 * the policies behind these values are recorded in docs/decisions.md.
 */
namespace cfg {

/*
 * Acquisition period. 750 ms is exactly 15 periods, so the descent window ends
 * on a stored sample and needs no interpolation. Measured fetch time is 24 ms
 * (README, "Timing evidence").
 */
constexpr int kSamplePeriodMs = 50;

/* Console consumers run at their own rates; neither sets the acquisition rate. */
constexpr int kTelemetryPeriodMs = 500;

/* Altitude reads the measurement store at the acquisition rate. */
constexpr int kAltitudePeriodMs = 50;

/* Apogee reads the altitude store at the same rate it is published. */
constexpr int kApogeePeriodMs = 50;

/* Temperature monitor: poll the measurement store. */
constexpr int kHeatPeriodMs = 50;

/* LED manager: how often it re-evaluates the requests. */
constexpr int kLedUpdatePeriodMs = 10;

/*
 * Baseline calibration: mean pressure over a stationary window, then frozen.
 * 20 Pa is about 1.7 m at this altitude (dP/dh ~= 11.5 Pa/m), well above the
 * observed +/-3 Pa of sensor noise, so someone moving the sensor restarts it.
 */
constexpr int kBaselineWindowMs = 2000;
constexpr unsigned int kBaselineMinSamples = 38;
/*
 * The window must span this much wall time, not just hold 38 samples: 2 s with
 * 200 ms of slack (four sample periods), so a late sample cannot shrink the
 * evidence and a burst of samples cannot calibrate faster than 1.8 s.
 */
constexpr int64_t kBaselineMinCoverageMs = 1800;
constexpr int kBaselineMaxSpreadPa = 20;
constexpr unsigned int kBaselineMaxSamples = 64;

/* A measurement older than this must not drive flight decisions. */
constexpr int kStaleTimeoutMs = 250;

/* How often the sensor thread republishes its timing counters. */
constexpr int kStatsWindowSamples = 200;

/*
 * Stacks are measured rather than guessed. The sensor thread carries the
 * driver and I2C stack; the console threads carry printk/RTT.
 */
constexpr std::size_t kSensorStackBytes = 2048;
constexpr std::size_t kTelemetryStackBytes = 1024;
/*
 * The altitude thread keeps its 64-entry baseline window (768 B) on the stack.
 * 1024 B overflowed it and tripped the MPU; 2048 B leaves the driver-free
 * thread room for the window plus printk on the calibration path.
 */
constexpr std::size_t kAltitudeStackBytes = 2048;
constexpr std::size_t kLedStackBytes = 1024;
constexpr std::size_t kApogeeStackBytes = 2048;
constexpr std::size_t kHeatStackBytes = 1024;
constexpr std::size_t kReplayStackBytes = 2048;

/*
 * Lower number = higher priority. The sensor thread must preempt every
 * consumer, otherwise a slow printk could push the 50 ms deadline. Altitude
 * outranks apogee so a detection decision is never made on a stale estimate;
 * the LED and the console sit below both.
 */
constexpr int kSensorPriority = 5;
constexpr int kAltitudePriority = 6;
constexpr int kApogeePriority = 7;
constexpr int kHeatPriority = 8;
constexpr int kLedPriority = 9;
constexpr int kTelemetryPriority = 10;
constexpr int kSelftestPriority = 11; /* test builds only */
constexpr int kReplayPriority = 5; /* stands in for the sensor thread */

} // namespace cfg
