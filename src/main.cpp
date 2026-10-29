#include "app_config.hpp"
#include "altitude_task.hpp"
#include "apogee_task.hpp"
#include "diag_task.hpp"
#include "led_manager.hpp"
#include "sensor_task.hpp"
#include "shared_data.hpp"
#include "telemetry_task.hpp"

#if defined(APP_LED_SELFTEST)
#include "led_selftest.hpp"
#endif

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

/*
 * Startup only: bring up shared state, start the threads, then idle. No
 * acquisition, calculation or LED pattern logic belongs here.
 */
int main(void)
{
	sensor_data().init();
	altitude_data().init();
	led_requests().init();

	/* The LED manager owns the pin; a failure is reported but not fatal. */
	if (!led_manager_start()) {
		printk("WARNING: LED indication unavailable; everything else runs\n");
	}

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
	apogee_task_start();
	diag_task_start();

#if defined(APP_LED_SELFTEST)
	led_selftest_start();
	printk("TEST MODE: LED SELFTEST is active, requests are scripted\n");
#endif

	printk("BMP280 ready: I2C2 SCL=PF1 SDA=PF0 addr=0x76, forced mode 8x/2x,"
	       " %d ms period\n", cfg::kSamplePeriodMs);
	printk("threads: sensor(%d) altitude(%d) apogee(%d) led(%d) telemetry(%d)"
	       " diag(%d); stale timeout %d ms\n", cfg::kSensorPriority,
	       cfg::kAltitudePriority, cfg::kApogeePriority, cfg::kLedPriority,
	       cfg::kTelemetryPriority, cfg::kDiagPriority, cfg::kStaleTimeoutMs);

	/* All work is in the threads now; main has nothing left to do. */
	for (;;) {
		k_msleep(1000);
	}
}
