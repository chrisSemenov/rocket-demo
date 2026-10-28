#pragma once

#include <cstddef>

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
constexpr int kDiagPeriodMs = 1000;

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
constexpr std::size_t kDiagStackBytes = 1024;

/*
 * Lower number = higher priority. The sensor thread must preempt both console
 * consumers, otherwise a slow printk could push the 50 ms deadline.
 */
constexpr int kSensorPriority = 5;
constexpr int kTelemetryPriority = 8;
constexpr int kDiagPriority = 9;

} // namespace cfg
