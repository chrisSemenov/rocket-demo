#include "app_config.hpp"
#include "altitude_task.hpp"
#include "apogee_task.hpp"
#include "diag_task.hpp"
#include "heat_task.hpp"
#include "led_manager.hpp"
#include "sensor_task.hpp"
#include "shared_data.hpp"
#include "telemetry_task.hpp"

#if defined(APP_LED_SELFTEST)
#include "led_selftest.hpp"
#endif

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

namespace {

const device *const kSensorBus = DEVICE_DT_GET(DT_BUS(DT_NODELABEL(bmp280)));

/*
 * When the sensor does not answer, probe the two addresses the breakout can
 * take and read the chip ID register. Distinguishes "wrong address strap" from
 * "nothing on the bus at all", instead of guessing at wiring.
 */
void probe_sensor_addresses()
{
	struct {
		uint16_t address;
		const char *strap;
	} const candidates[] = {
		{0x76, "SDO to GND"},
		{0x77, "SDO to 3V3 or floating"},
	};

	printk("i2c: probing 0x76 and 0x77 (chip id register 0xD0)\n");

	for (const auto &candidate : candidates) {
		uint8_t chip_id = 0;
		const int rc = i2c_reg_read_byte(kSensorBus, candidate.address,
						 0xD0, &chip_id);

		if (rc == 0) {
			printk("i2c:   0x%02x ACK, chip id 0x%02x (%s)\n",
			       candidate.address, chip_id, candidate.strap);
		} else {
			printk("i2c:   0x%02x no answer (%d, %s)\n",
			       candidate.address, rc, candidate.strap);
		}
	}
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
	led_requests().init();

	/* The LED manager owns the pin; a failure is reported but not fatal. */
	if (!led_manager_start()) {
		printk("WARNING: LED indication unavailable; everything else runs\n");
	}

	/*
	 * A bus left latched by an interrupted transfer (SDA held low) is
	 * recoverable in software: nine clocks plus a STOP. Harmless on a healthy
	 * bus, and it removes the "unplug the board after every flash" step.
	 */
	const int recover_rc = i2c_recover_bus(kSensorBus);

	if (recover_rc != 0) {
		printk("WARNING: I2C bus recovery returned %d\n", recover_rc);
	}

	if (!sensor_task_start()) {
		printk("ERROR: %s not ready. Check 3V3, GND, SDA=PF0, SCL=PF1, "
		       "CSB->3V3, SDO->GND (address 0x76 = SDO low, "
		       "0x77 = SDO high).\n",
		       DEVICE_DT_GET(DT_NODELABEL(bmp280))->name);

		probe_sensor_addresses();

		/* Stay alive with a usable console; never fabricate measurements. */
		for (;;) {
			k_msleep(5000);
		}
	}

	telemetry_task_start();
	altitude_task_start();
	apogee_task_start();
	heat_task_start();
	diag_task_start();

#if defined(APP_LED_SELFTEST)
	led_selftest_start();
	printk("TEST MODE: LED SELFTEST is active, requests are scripted\n");
#endif

	printk("BMP280 ready: I2C2 SCL=PF1 SDA=PF0 addr=0x76, forced mode 8x/2x,"
	       " %d ms period\n", cfg::kSamplePeriodMs);
	printk("threads: sensor(%d) altitude(%d) apogee(%d) heat(%d) led(%d)"
	       " telemetry(%d) diag(%d); stale timeout %d ms\n",
	       cfg::kSensorPriority, cfg::kAltitudePriority, cfg::kApogeePriority,
	       cfg::kHeatPriority, cfg::kLedPriority, cfg::kTelemetryPriority,
	       cfg::kDiagPriority, cfg::kStaleTimeoutMs);

	/* All work is in the threads now; main has nothing left to do. */
	for (;;) {
		k_msleep(1000);
	}
}
