#include "sensor_task.hpp"

#include "app_config.hpp"
#include "shared_data.hpp"

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

namespace {

const device *const kBmp280 = DEVICE_DT_GET(DT_NODELABEL(bmp280));
const device *const kBus = DEVICE_DT_GET(DT_BUS(DT_NODELABEL(bmp280)));

/*
 * Try a bus recovery after three consecutive failed fetches, then again every
 * kRecoveryRetryErrors failures. Retrying only at exactly three meant a
 * permanently wedged bus got one attempt per boot and then silence.
 */
constexpr uint32_t kRecoverAfterErrors = 3;
constexpr uint32_t kRecoveryRetryErrors = 60; /* ~3 s at 20 Hz */

K_THREAD_STACK_DEFINE(sensor_stack, cfg::kSensorStackBytes);
struct k_thread sensor_thread;

/* The driver reports pressure in kPa: Pa = kPa * 1000 + micro-kPa / 1000. */
long pressure_pa(const sensor_value &pressure)
{
	return static_cast<long>(pressure.val1) * 1000L + pressure.val2 / 1000L;
}

/* The driver reports degrees C with micro-degree resolution. */
long temperature_mc(const sensor_value &temperature)
{
	return static_cast<long>(temperature.val1) * 1000L + temperature.val2 / 1000L;
}

/* Min/avg/max accumulator for the timing counters. */
struct Timing {
	long long min = -1;
	long long max = 0;
	long long sum = 0;
	unsigned int count = 0;

	void add(long long value)
	{
		if (min < 0 || value < min) {
			min = value;
		}

		if (value > max) {
			max = value;
		}

		sum += value;
		++count;
	}

	long long average() const
	{
		return count != 0 ? sum / count : 0;
	}

	int32_t min_ms() const
	{
		return min < 0 ? 0 : static_cast<int32_t>(min);
	}

	void reset()
	{
		min = -1;
		max = 0;
		sum = 0;
		count = 0;
	}
};

bool recovery_due(uint32_t consecutive_errors)
{
	if (consecutive_errors == kRecoverAfterErrors) {
		return true;
	}

	return consecutive_errors > kRecoverAfterErrors &&
	       (consecutive_errors - kRecoverAfterErrors) %
			       kRecoveryRetryErrors ==
		       0;
}

void sensor_entry(void *, void *, void *)
{
	Timing fetch_time;
	Timing sample_interval;

	int64_t next = k_uptime_get();
	int64_t last_sample = next;
	uint32_t sequence = 0;
	uint32_t errors = 0;
	uint32_t recoveries = 0;
	uint32_t recovery_failures = 0;
	uint32_t consecutive_errors = 0;
	uint32_t overruns = 0;
	unsigned int since_stats = 0;

	for (;;) {
		const int64_t fetch_start = k_uptime_get();
		const int fetch_rc = sensor_sample_fetch(kBmp280);
		const long long fetch_ms = k_uptime_get() - fetch_start;

		sensor_value pressure{};
		sensor_value temperature{};

		const bool ok =
			fetch_rc == 0 &&
			sensor_channel_get(kBmp280, SENSOR_CHAN_PRESS, &pressure) == 0 &&
			sensor_channel_get(kBmp280, SENSOR_CHAN_AMBIENT_TEMP,
					   &temperature) == 0;

		Measurement sample{};
		sample.timestamp_ms = k_uptime_get();

		if (!ok) {
			/*
			 * Publish an invalid snapshot so consumers stop trusting the
			 * store, but do not advance the sequence: a failed fetch is not
			 * a new measurement.
			 */
			sample.valid = false;
			sample.sequence = sequence;
			sample.error = static_cast<int8_t>(fetch_rc);

			if (errors == 0) {
				printk("ERROR: BMP280 fetch failed (%d); publishing invalid\n",
				       fetch_rc);
			}

			++errors;
			++consecutive_errors;

			/*
			 * A latched bus (a device holding SDA low) does not clear by
			 * itself. Bit-bang nine clocks and a STOP, then let the next
			 * period try again.
			 */
			if (recovery_due(consecutive_errors)) {
				const int recover_rc = i2c_recover_bus(kBus);

				if (recover_rc == 0) {
					++recoveries;
				} else {
					++recovery_failures;
				}

				const uint32_t attempts = recoveries + recovery_failures;

				if (attempts <= 5 || attempts % 20 == 0) {
					printk("sensor: bus recovery %u after %u"
					       " consecutive failures (%d)\n",
					       attempts, consecutive_errors,
					       recover_rc);
				}
			}

			sensor_data().publish(sample);
		} else {
			consecutive_errors = 0;
			sample.pressure_pa = static_cast<int32_t>(pressure_pa(pressure));
			sample.temperature_mc =
				static_cast<int32_t>(temperature_mc(temperature));
			sample.valid = true;
			sample.error = 0;
			sample.sequence = ++sequence;

			/*
			 * The first two samples cover the boot phase: the first fetch
			 * starts the first conversion and the schedule only begins at
			 * the second. Excluding them keeps the counters describing the
			 * steady state rather than start-up.
			 */
			if (sequence > 2) {
				fetch_time.add(fetch_ms);
				sample_interval.add(sample.timestamp_ms - last_sample);
			}

			last_sample = sample.timestamp_ms;
			sensor_data().publish(sample);
		}

		if (++since_stats >= cfg::kStatsWindowSamples) {
			SensorStats stats{};

			stats.samples = sequence;
			stats.errors = errors;
			stats.recoveries = recoveries;
			stats.recovery_failures = recovery_failures;
			stats.overruns = overruns;
			stats.fetch_min_ms = fetch_time.min_ms();
			stats.fetch_avg_ms = static_cast<int32_t>(fetch_time.average());
			stats.fetch_max_ms = static_cast<int32_t>(fetch_time.max);
			stats.interval_min_ms = sample_interval.min_ms();
			stats.interval_avg_ms =
				static_cast<int32_t>(sample_interval.average());
			stats.interval_max_ms = static_cast<int32_t>(sample_interval.max);

			sensor_data().publish_stats(stats);

			since_stats = 0;
			fetch_time.reset();
			sample_interval.reset();
		}

		/*
		 * Absolute deadlines: sleep until the next slot rather than
		 * "work + 50 ms", which would silently stretch the period. On a missed
		 * deadline, count it and re-anchor, then still sleep to the new slot so
		 * the next interval is a full period instead of an immediate catch-up.
		 */
		next += cfg::kSamplePeriodMs;

		const int64_t now = k_uptime_get();

		if (now > next) {
			++overruns;
			next = now + cfg::kSamplePeriodMs;
		}

		k_sleep(K_TIMEOUT_ABS_MS(next));
	}
}

} // namespace

bool sensor_task_start()
{
	if (!device_is_ready(kBmp280)) {
		return false;
	}

	k_thread_create(&sensor_thread, sensor_stack,
			K_THREAD_STACK_SIZEOF(sensor_stack), sensor_entry, nullptr,
			nullptr, nullptr, cfg::kSensorPriority, 0, K_NO_WAIT);
	(void)k_thread_name_set(&sensor_thread, "sensor");

	return true;
}
