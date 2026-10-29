#include "telemetry_task.hpp"

#include "app_config.hpp"
#include "shared_data.hpp"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

namespace {

K_THREAD_STACK_DEFINE(telemetry_stack, cfg::kTelemetryStackBytes);
struct k_thread telemetry_thread;

/* 500 ms period, so twenty ticks is a ten-second stats window. */
constexpr unsigned int kStatsEveryTicks =
	10000 / cfg::kTelemetryPeriodMs;

void telemetry_entry(void *, void *, void *)
{
	int64_t next = k_uptime_get();
	unsigned int ticks = 0;
	unsigned int invalid_reports = 0;
	unsigned int altitude_reports = 0;

	for (;;) {
		Measurement sample{};
		sensor_data().read(sample);

		const int64_t now = k_uptime_get();

		if (sample.valid) {
			const int64_t age = now - sample.timestamp_ms;
			const long centi = sample.temperature_mc / 10L;
			const bool below_zero = centi < 0;
			const long abs_centi = below_zero ? -centi : centi;

			printk("[%7lld.%02lld s] seq=%u  P=%ld Pa  T=%s%ld.%02ld C"
			       "  age=%lld ms\n",
			       static_cast<long long>(now / 1000),
			       static_cast<long long>((now % 1000) / 10),
			       sample.sequence, static_cast<long>(sample.pressure_pa),
			       below_zero ? "-" : "", abs_centi / 100,
			       abs_centi % 100, static_cast<long long>(age));
		} else if (invalid_reports < 3) {
			/* Report the first few failures, then stay quiet. */
			printk("telemetry: no valid measurement yet (error=%d)\n",
			       sample.error);
			++invalid_reports;
		}

		/* Altitude is a separate store, read independently of the above. */
		Altitude altitude{};
		altitude_data().read(altitude);

		if (altitude.valid) {
			const long centi = altitude.altitude_cm;
			const bool below_zero = centi < 0;
			const long abs_centi = below_zero ? -centi : centi;

			printk("alt:  h=%s%ld.%02ld m  P0=%ld Pa  seq=%u  age=%lld ms\n",
			       below_zero ? "-" : "", abs_centi / 100,
			       abs_centi % 100, static_cast<long>(altitude.reference_pa),
			       altitude.source_sequence,
			       static_cast<long long>(now -
						      altitude.source_timestamp_ms));
		} else if (altitude_reports < 3) {
			printk("alt:  not available yet (baseline %u/%u samples)\n",
			       altitude.baseline_samples, cfg::kBaselineMinSamples);
			++altitude_reports;
		}

		if (++ticks % kStatsEveryTicks == 0) {
			SensorStats stats{};
			sensor_data().read_stats(stats);

			/* The sensor publishes its first window ten seconds in. */
			if (stats.samples != 0) {
				printk("stats: %u samples  fetch %d/%d/%d ms (min/avg/max)"
				       "  dt %d/%d/%d ms  overruns=%u  errors=%u"
				       "  seq=%u\n",
				       stats.samples, stats.fetch_min_ms,
				       stats.fetch_avg_ms, stats.fetch_max_ms,
				       stats.interval_min_ms, stats.interval_avg_ms,
				       stats.interval_max_ms, stats.overruns,
				       stats.errors, sensor_data().publish_count());
			}
		}

		next += cfg::kTelemetryPeriodMs;

		const int64_t after = k_uptime_get();

		if (after > next) {
			next = after + cfg::kTelemetryPeriodMs;
		} else {
			k_sleep(K_TIMEOUT_ABS_MS(next));
		}
	}
}

} // namespace

void telemetry_task_start()
{
	k_thread_create(&telemetry_thread, telemetry_stack,
			K_THREAD_STACK_SIZEOF(telemetry_stack), telemetry_entry,
			nullptr, nullptr, nullptr, cfg::kTelemetryPriority, 0,
			K_NO_WAIT);
	(void)k_thread_name_set(&telemetry_thread, "telemetry");
}
