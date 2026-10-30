#include "apogee_task.hpp"

#include "apogee.hpp"
#include "app_config.hpp"
#include "shared_data.hpp"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

namespace {

K_THREAD_STACK_DEFINE(apogee_stack, cfg::kApogeeStackBytes);
struct k_thread apogee_thread;

const char *state_name(apogee::State state)
{
	switch (state) {
	case apogee::State::filling:
		return "filling";
	case apogee::State::ready:
		return "ready";
	case apogee::State::deployed:
		return "deployed";
	}

	return "?";
}

/* Signed integer printing without depending on float support in printk. */
void print_metres(int32_t centimetres)
{
	const bool negative = centimetres < 0;
	const int32_t magnitude = negative ? -centimetres : centimetres;

	printk("%s%ld.%02ld", negative ? "-" : "", magnitude / 100,
	       magnitude % 100);
}

void print_mm_s(int32_t mm_s)
{
	const bool negative = mm_s < 0;
	const int32_t magnitude = negative ? -mm_s : mm_s;

	printk("%s%ld.%03ld", negative ? "-" : "", magnitude / 1000,
	       magnitude % 1000);
}

/*
 * The two numbers that decide a deployment. Both come from Kconfig so a test
 * build can change them without touching source; the assignment only requires
 * the altitude minimum to be configurable, the descent margin is ours.
 */
apogee::Config make_config()
{
	apogee::Config config = apogee::kDefaultConfig;

	config.min_altitude_cm = CONFIG_APP_APOGEE_MIN_ALT_CM;
	config.min_descent_mm_s = CONFIG_APP_APOGEE_MIN_DESCENT_MM_S;
	config.min_drop_cm = CONFIG_APP_APOGEE_MIN_DROP_CM;

	return config;
}

void apogee_entry(void *, void *, void *)
{
	const apogee::Config config = make_config();
	apogee::Detector detector(config);

	bool have_sequence = false;
	uint32_t last_sequence = 0;
	bool last_ready = false;
	bool last_deployed = false;
	unsigned int invalid_reports = 0;

	int64_t next = k_uptime_get();
	int64_t next_report = next + 500;

	for (;;) {
		Altitude altitude{};
		altitude_data().read(altitude);

		const int64_t now = k_uptime_get();
		const int64_t age = now - altitude.source_timestamp_ms;

		const bool usable = altitude.valid && altitude.calibrated &&
				    age <= cfg::kStaleTimeoutMs;
		const bool is_new = !have_sequence ||
				    altitude.source_sequence != last_sequence;

		if (usable && is_new) {
			have_sequence = true;
			last_sequence = altitude.source_sequence;

			const apogee::Sample sample{altitude.source_timestamp_ms,
						    altitude.altitude_cm,
						    altitude.source_sequence};

			const apogee::Event event = detector.on_sample(sample);

			if (event.deployed) {
				/*
				 * The store latches the first event, so a repeated
				 * request from here could never extend the deadline.
				 */
				led_requests().request_deployment(event.event_ms,
								  event.deadline_ms);

				printk("apogee: DEPLOY at ");
				print_metres(altitude.altitude_cm);
				printk(" m, v=");
				print_mm_s(detector.velocity_mm_s());
				printk(" m/s; LED on until %lld ms (seq=%u)\n",
				       static_cast<long long>(event.deadline_ms),
				       altitude.source_sequence);
			}

			const apogee::State state = detector.state();
			const bool ready = state != apogee::State::filling;

			if (ready && !last_ready) {
				printk("apogee: armed; threshold %d cm, drop %d cm,"
				       " margin %d mm/s, window %lld ms\n",
				       config.min_altitude_cm, config.min_drop_cm,
				       config.min_descent_mm_s,
				       static_cast<long long>(config.window_ms));

#if defined(CONFIG_APP_READY_INDICATION)
				/* Tell the LED manager the chain is armed (seen once). */
				led_requests().signal_ready();
#endif
			}

			last_ready = ready;
			last_deployed = state == apogee::State::deployed;
		} else if (!usable && invalid_reports < 3) {
			printk("apogee: no usable altitude (valid=%d calibrated=%d"
			       " age=%lld ms)\n",
			       altitude.valid ? 1 : 0, altitude.calibrated ? 1 : 0,
			       static_cast<long long>(age));
			++invalid_reports;
		}

		/* Periodic insight, not a decision: altitude, velocity and coverage. */
		if (now >= next_report) {
			printk("apogee: h=");
			print_metres(altitude.altitude_cm);
			printk(" m  v=");
			print_mm_s(detector.velocity_mm_s());
			printk(" m/s  cov=%lld ms  n=%d  state=%s  seq=%u\n",
			       static_cast<long long>(detector.coverage_ms()),
			       detector.samples(), state_name(detector.state()),
			       altitude.source_sequence);

			next_report = now + 500;
		}

		(void)last_deployed;

		next += cfg::kApogeePeriodMs;

		if (k_uptime_get() > next) {
			next = k_uptime_get() + cfg::kApogeePeriodMs;
		} else {
			k_sleep(K_TIMEOUT_ABS_MS(next));
		}
	}
}

} // namespace

void apogee_task_start()
{
	k_thread_create(&apogee_thread, apogee_stack,
			K_THREAD_STACK_SIZEOF(apogee_stack), apogee_entry,
			nullptr, nullptr, nullptr, cfg::kApogeePriority, 0,
			K_NO_WAIT);
	(void)k_thread_name_set(&apogee_thread, "apogee");
}
