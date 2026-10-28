#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

namespace {

constexpr int kSamplePeriodMs = 1000;

const gpio_dt_spec kLed = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
const device *const kBmp280 = DEVICE_DT_GET(DT_NODELABEL(bmp280));

/* The Zephyr driver reports pressure in kPa (val1 + val2 * 1e-6). */
long pressure_pa(const sensor_value &pressure)
{
	return static_cast<long>(pressure.val1) * 1000L +
	       pressure.val2 / 1000L;
}

/* The driver reports temperature in degrees C (val1 + val2 * 1e-6). */
long temperature_centi_c(const sensor_value &temperature)
{
	return static_cast<long>(temperature.val1) * 100L +
	       temperature.val2 / 10000L;
}

void configure_led()
{
	if (!gpio_is_ready_dt(&kLed)) {
		printk("ERROR: LED device not ready\n");
		return;
	}

	if (gpio_pin_configure_dt(&kLed, GPIO_OUTPUT_INACTIVE) < 0) {
		printk("ERROR: LED configuration failed\n");
	}
}

} // namespace

int main(void)
{
	configure_led();

	if (!device_is_ready(kBmp280)) {
		printk("ERROR: %s not ready. Check 3V3, GND, SDA=PF0, "
		       "SCL=PF1, CSB->3V3, SDO->GND (address 0x76).\n",
		       kBmp280->name);

		for (;;) {
			k_msleep(5000);
		}
	}

	printk("BMP280 ready: I2C2 SCL=PF1 SDA=PF0 addr=0x76, "
	       "sampling every %d ms\n", kSamplePeriodMs);

	unsigned int sequence = 0;

	for (;;) {
		if (sensor_sample_fetch(kBmp280) < 0) {
			printk("ERROR: sensor fetch failed\n");
			k_msleep(kSamplePeriodMs);
			continue;
		}

		sensor_value pressure{};
		sensor_value temperature{};

		if (sensor_channel_get(kBmp280, SENSOR_CHAN_PRESS, &pressure) < 0 ||
		    sensor_channel_get(kBmp280, SENSOR_CHAN_AMBIENT_TEMP,
				       &temperature) < 0) {
			printk("ERROR: sensor channel read failed\n");
			k_msleep(kSamplePeriodMs);
			continue;
		}

		const long pa = pressure_pa(pressure);
		const long centi_c = temperature_centi_c(temperature);
		const bool below_zero = centi_c < 0;
		const long abs_centi_c = below_zero ? -centi_c : centi_c;

		printk("[%6u s] seq=%u  P=%ld Pa  T=%s%ld.%02ld C\n",
		       static_cast<unsigned int>(k_uptime_get() / 1000), sequence,
		       pa, below_zero ? "-" : "", abs_centi_c / 100,
		       abs_centi_c % 100);

		++sequence;

		/* Heartbeat: proves the firmware is still running. */
		(void)gpio_pin_toggle_dt(&kLed);

		k_msleep(kSamplePeriodMs);
	}
}
