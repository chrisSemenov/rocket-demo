#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/init.h>

#if !defined(CONFIG_APP_REPLAY)

namespace {

const device *const kBus = DEVICE_DT_GET(DT_BUS(DT_NODELABEL(bmp280)));

int g_recovery_rc = 0;

/*
 * A reset issued while the sensor is mid-transfer can leave it holding SDA low,
 * and an MCU reset does not clear that. Nine clocks plus a STOP fix it, but the
 * timing matters: the I2C controller must exist (so its pins can be re-applied)
 * and the BMP280 driver must not have run yet.
 *
 * That is the whole point of doing it here rather than in main(). A device whose
 * init fails is marked not-ready permanently, so recovering the bus after the
 * driver has already failed cannot bring the sensor back for this boot.
 *
 * The console is not installed yet at this priority, so the result is stored and
 * reported by main().
 */
int recover_bus_at_boot()
{
	g_recovery_rc = i2c_recover_bus(kBus);

	return 0;
}

} // namespace

/*
 * The priority has to be a literal: SYS_INIT() stringifies the token to build
 * the section name, so "CONFIG_I2C_INIT_PRIORITY + 1" would become the section
 * ".z_init_post_kernel_P_50 + 1_SUB_0" and fail the initlevel_error assert.
 * The two assertions below enforce the ordering that makes this useful.
 */
SYS_INIT(recover_bus_at_boot, POST_KERNEL, 51);

BUILD_ASSERT(51 > CONFIG_I2C_INIT_PRIORITY,
	     "bus recovery must run after the I2C controller is initialised");
BUILD_ASSERT(51 < CONFIG_SENSOR_INIT_PRIORITY,
	     "bus recovery must run before the BMP280 driver initialises");

int bus_recovery_status()
{
	return g_recovery_rc;
}

#else /* CONFIG_APP_REPLAY */

/* Replay never touches the bus, so there is nothing to recover and no result. */
int bus_recovery_status()
{
	return 0;
}

#endif /* CONFIG_APP_REPLAY */
