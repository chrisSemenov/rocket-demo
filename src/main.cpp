#include "app_config.hpp"
#include "altitude_task.hpp"
#include "apogee_task.hpp"
#include "bus_recovery.hpp"

#include "heat_task.hpp"
#include "led_manager.hpp"
#include "sensor_task.hpp"
#include "shared_data.hpp"
#include "telemetry_task.hpp"

#if defined(CONFIG_APP_REPLAY)
#include "replay.hpp"
#endif

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
 *
 * Replay mode never touches the bus, so this is compiled out there.
 */
#if !defined(CONFIG_APP_REPLAY)
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
#endif /* !CONFIG_APP_REPLAY */

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

#if defined(CONFIG_APP_REPLAY)
	/*
	 * Replay mode: no sensor thread, no bus recovery. Exactly one publisher.
	 */
	printk("TEST MODE: REPLAY %s (live sensor not used)\n",
	       replay_trace_name());
	replay_task_start();
#else
	/*
	 * A latched bus is cleared by bus_recovery.cpp, which runs as a POST_KERNEL
	 * init step: after the I2C controller exists and, crucially, before the
	 * BMP280 driver initialises. Doing it here would be too late, because a
	 * device whose init failed stays not-ready for the rest of the boot.
	 *
	 * It runs before the console exists, so its result is reported now. A
	 * non-zero value means the bus was latched and the recovery failed.
	 */
	const int boot_recovery = bus_recovery_status();

	if (boot_recovery == 0) {
		printk("i2c: boot recovery ok (ran before the sensor driver)\n");
	} else {
		printk("WARNING: I2C boot recovery failed (%d)\n", boot_recovery);
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
#endif

	telemetry_task_start();
	altitude_task_start();
	apogee_task_start();
	heat_task_start();

#if defined(APP_LED_SELFTEST)
	led_selftest_start();
	printk("TEST MODE: LED SELFTEST is active, requests are scripted\n");
#endif

#if defined(CONFIG_APP_REPLAY)
	printk("input: REPLAY %s, %d ms cadence (live sensor not used)\n",
	       replay_trace_name(), cfg::kSamplePeriodMs);
	printk("threads: replay(%d) altitude(%d) apogee(%d) heat(%d) led(%d)"
	       " telemetry(%d); stale timeout %d ms\n",
	       cfg::kReplayPriority, cfg::kAltitudePriority,
	       cfg::kApogeePriority, cfg::kHeatPriority, cfg::kLedPriority,
	       cfg::kTelemetryPriority, cfg::kStaleTimeoutMs);
#else
	printk("input: LIVE BMP280, I2C2 SCL=PF1 SDA=PF0 addr=0x76, forced mode"
	       " 8x/2x, %d ms period\n", cfg::kSamplePeriodMs);
	printk("threads: sensor(%d) altitude(%d) apogee(%d) heat(%d) led(%d)"
	       " telemetry(%d); stale timeout %d ms\n",
	       cfg::kSensorPriority, cfg::kAltitudePriority, cfg::kApogeePriority,
	       cfg::kHeatPriority, cfg::kLedPriority, cfg::kTelemetryPriority,
	       cfg::kStaleTimeoutMs);
#endif

	/* All work is in the threads now; main has nothing left to do. */
	for (;;) {
		k_msleep(1000);
	}
}
