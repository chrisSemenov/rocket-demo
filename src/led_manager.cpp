#include "led_manager.hpp"

#include "app_config.hpp"
#include "shared_data.hpp"

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

namespace {

K_THREAD_STACK_DEFINE(led_stack, cfg::kLedStackBytes);
struct k_thread led_thread;

const gpio_dt_spec kLed = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

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
	led::Output previous = led::Output::off;
	bool have_previous = false;

	int64_t next = k_uptime_get();

	for (;;) {
		led::Request request{};
		led_requests().read(request);

		const int64_t now = k_uptime_get();
		const led::Output output = led::decide(now, request);

		/*
		 * Log and write only on a transition. A 10 ms cadence of identical
		 * writes would be harmless for the pin but useless in the console.
		 */
		if (!have_previous || output != previous) {
			(void)gpio_pin_set_dt(&kLed, output_is_on(output) ? 1 : 0);

			printk("led:  [%7lld.%03lld] %s\n",
			       static_cast<long long>(now / 1000),
			       static_cast<long long>(now % 1000),
			       output_name(output));

			previous = output;
			have_previous = true;
		}

		next += cfg::kLedUpdatePeriodMs;

		if (k_uptime_get() > next) {
			next = k_uptime_get() + cfg::kLedUpdatePeriodMs;
		} else {
			k_sleep(K_TIMEOUT_ABS_MS(next));
		}
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
