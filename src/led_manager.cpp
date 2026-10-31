#include "led_manager.hpp"

#include "app_config.hpp"
#include "led_pulse.hpp"
#include "shared_data.hpp"

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

namespace {

K_THREAD_STACK_DEFINE(led_stack, cfg::kLedStackBytes);
struct k_thread led_thread;

const gpio_dt_spec kLed = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

/* Report a persistent write failure at most this often. */
constexpr uint32_t kWriteErrorReportEvery = 200;

const char *output_name(led::Output output)
{
	switch (output) {
	case led::Output::off:
		return "off";
	case led::Output::on:
		return "on (deployment)";
	case led::Output::blink_on:
		return "blink on (heat)";
	case led::Output::blink_off:
		return "blink off (heat)";
	}

	return "?";
}

bool output_is_on(led::Output output)
{
	return output == led::Output::on || output == led::Output::blink_on;
}

void led_entry(void *, void *, void *)
{
	/* What the policy wants now, and what the pin actually holds. */
	led::Output requested = led::Output::off;
	led::Output applied = led::Output::off;
	bool have_requested = false;
	bool have_applied = false;

	uint32_t write_errors = 0;

	led::ReadyPulse pulse;

	int64_t next = k_uptime_get();

	for (;;) {
		led::Request request{};
		led_requests().read(request);

		const int64_t now = k_uptime_get();
		const led::Output decided = led::decide(now, request);

		if (IS_ENABLED(CONFIG_APP_READY_INDICATION) && !pulse.active() &&
		    led_requests().take_ready() && decided == led::Output::off) {
			pulse.start(now);
			printk("led:  ready signal\n");
		}

		/* A real request always wins over the boot flash. */
		if (pulse.active() && decided != led::Output::off) {
			pulse.cancel();
		}

		const bool was_pulsing = pulse.active();

		pulse.advance(now);

		const led::Output output =
			pulse.active() ? (pulse.lit() ? led::Output::on : led::Output::off)
				     : decided;
		const char *label;

		if (pulse.active()) {
			label = pulse.lit() ? "ready flash on" : "ready flash off";
		} else if (was_pulsing) {
			label = "ready flash end";
		} else {
			label = output_name(decided);
		}

		/*
		 * Log on a change of what is *wanted*. Logging on a change of what
		 * was written would repeat the same line every period while a write
		 * kept failing.
		 */
		if (!have_requested || output != requested) {
			printk("led:  [%7lld.%03lld] %s\n",
			       static_cast<long long>(now / 1000),
			       static_cast<long long>(now % 1000), label);

			requested = output;
			have_requested = true;
		}

		/*
		 * Write whenever the pin does not match the request, which includes
		 * retrying after a failed write. A 10 ms cadence of identical writes
		 * costs one register store and keeps a failed transition from being
		 * remembered as a success.
		 */
		if (!have_applied || output != applied) {
			const int rc = gpio_pin_set_dt(&kLed,
						       output_is_on(output) ? 1 : 0);

			if (rc == 0) {
				applied = output;
				have_applied = true;
			} else {
				++write_errors;

				if (write_errors == 1 ||
				    write_errors % kWriteErrorReportEvery == 0) {
					printk("led: gpio write failed (%d), %u so far\n",
					       rc, write_errors);
				}
			}
		}

		next += cfg::kLedUpdatePeriodMs;

		if (k_uptime_get() > next) {
			next = k_uptime_get() + cfg::kLedUpdatePeriodMs;
		}

		k_sleep(K_TIMEOUT_ABS_MS(next));
	}
}

} // namespace

bool led_manager_start()
{
	if (!gpio_is_ready_dt(&kLed)) {
		printk("ERROR: LED device not ready\n");
		return false;
	}

	if (gpio_pin_configure_dt(&kLed, GPIO_OUTPUT_INACTIVE) < 0) {
		printk("ERROR: LED configuration failed\n");
		return false;
	}

	k_thread_create(&led_thread, led_stack, K_THREAD_STACK_SIZEOF(led_stack),
			led_entry, nullptr, nullptr, nullptr, cfg::kLedPriority,
			0, K_NO_WAIT);
	(void)k_thread_name_set(&led_thread, "led");

	return true;
}
