#include "app_config.hpp"
#include "altitude_task.hpp"
#include "diag_task.hpp"
#include "sensor_task.hpp"
#include "shared_data.hpp"
#include "telemetry_task.hpp"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

namespace {

const gpio_dt_spec kLed = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

bool configure_led()
{
	if (!gpio_is_ready_dt(&kLed)) {
		printk("ERROR: LED device not ready\n");
		return false;
	}

	if (gpio_pin_configure_dt(&kLed, GPIO_OUTPUT_INACTIVE) < 0) {
		printk("ERROR: LED configuration failed\n");
		return false;
	}

	return true;
}

} // namespace

/*
 * Startup only: bring up shared state, start the threads, then idle. No
 * acquisition, calculation or LED pattern logic belongs here.
 */
int main(void)
{
	sensor_data().init();
	altitude_data().init();
	const bool led_ok = configure_led();

	if (!sensor_task_start()) {
		printk("ERROR: %s not ready. Check 3V3, GND, SDA=PF0, SCL=PF1, "
		       "CSB->3V3, SDO->GND (address 0x76 = SDO low, "
		       "0x77 = SDO high).\n",
		       DEVICE_DT_GET(DT_NODELABEL(bmp280))->name);

		/* Stay alive with a usable console; never fabricate measurements. */
		for (;;) {
			k_msleep(5000);
		}
	}

	telemetry_task_start();
	altitude_task_start();
	diag_task_start();

	printk("BMP280 ready: I2C2 SCL=PF1 SDA=PF0 addr=0x76, forced mode 8x/2x,"
	       " %d ms period\n", cfg::kSamplePeriodMs);
	printk("threads: sensor(prio %d) altitude(%d) telemetry(%d) diag(%d);"
	       " stale timeout %d ms\n", cfg::kSensorPriority,
	       cfg::kAltitudePriority, cfg::kTelemetryPriority,
	       cfg::kDiagPriority, cfg::kStaleTimeoutMs);

	/*
	 * Temporary liveness heartbeat on PC13. Removed when the LED manager
	 * becomes the only LED writer in the next milestone.
	 */
	for (;;) {
		k_msleep(500);

		if (led_ok) {
			(void)gpio_pin_toggle_dt(&kLed);
		}
	}
}
