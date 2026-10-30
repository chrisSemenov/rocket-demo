#include "heat_task.hpp"

#include "app_config.hpp"
#include "heat.hpp"
#include "shared_data.hpp"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

namespace {

K_THREAD_STACK_DEFINE(heat_stack, cfg::kHeatStackBytes);
struct k_thread heat_thread;

heat::Config make_config()
{
	heat::Config config = heat::kDefaultConfig;

	config.threshold_mc = CONFIG_APP_HEAT_THRESHOLD_MC;
	config.hysteresis_mc = CONFIG_APP_HEAT_HYSTERESIS_MC;

	return config;
}

/* Signed integer temperature printing; printk has no float support here. */
void print_celsius(int32_t milli_celsius)
{
	const bool negative = milli_celsius < 0;
	const int32_t magnitude = negative ? -milli_celsius : milli_celsius;

	printk("%s%ld.%03ld", negative ? "-" : "", magnitude / 1000,
	       magnitude % 1000);
}

void heat_entry(void *, void *, void *)
{
	const heat::Config config = make_config();
	heat::Monitor monitor(config);

	bool have_sequence = false;
	uint32_t last_sequence = 0;
	unsigned int invalid_reports = 0;

	int64_t next = k_uptime_get();

	printk("heat: threshold ");
	print_celsius(config.threshold_mc);
	printk(" C, hysteresis %d mC, window %lld ms\n",
	       config.hysteresis_mc,
	       static_cast<long long>(config.alarm_ms));

	for (;;) {
		Measurement measurement{};
		sensor_data().read(measurement);

		const int64_t now = k_uptime_get();
		const int64_t age = now - measurement.timestamp_ms;

		const bool usable = measurement.valid &&
				    age <= cfg::kStaleTimeoutMs;
		const bool is_new = !have_sequence ||
				    measurement.sequence != last_sequence;

		if (usable && is_new) {
			have_sequence = true;
			last_sequence = measurement.sequence;

			const heat::Decision decision = monitor.on_sample(
				measurement.temperature_mc,
				measurement.timestamp_ms,
				measurement.sequence);

			if (decision.changed) {
				if (decision.request.active) {
					led_requests().request_heat(
						decision.request.start_ms,
						decision.request.deadline_ms);

					printk("heat: HOT at ");
					print_celsius(measurement.temperature_mc);
					printk(" C; alarm until %lld ms (seq=%u)\n",
					       static_cast<long long>(
						       decision.request.deadline_ms),
					       measurement.sequence);
				} else {
					led_requests().clear_heat();

					printk("heat: cooled at ");
					print_celsius(measurement.temperature_mc);
					printk(" C; alarm cleared (seq=%u)\n",
					       measurement.sequence);
				}
			}
		} else if (!usable) {
			/*
			 * Missing data is not evidence of cooling: hold the state. The
			 * window still expires on its own deadline.
			 */
			(void)monitor.on_missing();

			if (invalid_reports < 3) {
				printk("heat: no usable temperature (age=%lld ms)\n",
				       static_cast<long long>(age));
				++invalid_reports;
			}
		}

		next += cfg::kHeatPeriodMs;

		if (k_uptime_get() > next) {
			next = k_uptime_get() + cfg::kHeatPeriodMs;
		} else {
			k_sleep(K_TIMEOUT_ABS_MS(next));
		}
	}
}

} // namespace

void heat_task_start()
{
	k_thread_create(&heat_thread, heat_stack,
			K_THREAD_STACK_SIZEOF(heat_stack), heat_entry,
			nullptr, nullptr, nullptr, cfg::kHeatPriority, 0,
			K_NO_WAIT);
	(void)k_thread_name_set(&heat_thread, "heat");
}
