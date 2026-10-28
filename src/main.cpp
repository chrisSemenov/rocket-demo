#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

namespace {

/*
 * The descent test averages vertical velocity over the preceding 750 ms.
 * 750 is exactly 15 periods of 50 ms, so the window endpoint is always a
 * stored sample and the average needs no interpolation.
 *
 * 8x pressure / 2x temperature take 24.8 ms to convert, leaving about 24 ms
 * of slack per period. Forced mode makes that exactly one conversion per
 * fetch. See "Sampling design" in README.md for why 16x was rejected.
 */
constexpr int kSamplePeriodMs = 50;

/* 20 samples per second would flood the 2048-byte RTT buffer; log 2 Hz. */
constexpr int kLogEveryNSamples = 10;

/* Liveness heartbeat: one toggle per second, until the LED manager takes over. */
constexpr int kHeartbeatEveryNSamples = 20;

/* Report measured timing so the cadence is verified rather than assumed. */
constexpr int kStatsEveryNSamples = 100;

const gpio_dt_spec kLed = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
const device *const kBmp280 = DEVICE_DT_GET(DT_NODELABEL(bmp280));

/* The Zephyr driver reports pressure in kPa (val1 + val2 * 1e-6). */
long pressure_pa(const sensor_value &pressure)
{
	return static_cast<long>(pressure.val1) * 1000L +
	       pressure.val2 / 1000L;
}

/* The Zephyr driver reports temperature in degrees C (val1 + val2 * 1e-6). */
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

/* Min/avg/max accumulator for the timing evidence. */
struct Timing {
	long long min = -1;
	long long max = 0;
	long long sum = 0;
	unsigned int count = 0;

	void add(long long value)
	{
		if (min < 0 || value < min) {
			min = value;
		}

		if (value > max) {
			max = value;
		}

		sum += value;
		++count;
	}

	long long average() const
	{
		return count != 0 ? sum / count : 0;
	}

	void reset()
	{
		min = -1;
		max = 0;
		sum = 0;
		count = 0;
	}
};

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
	       "forced mode 8x/2x, %d ms period\n", kSamplePeriodMs);

	Timing fetch_time;
	Timing sample_interval;

	int64_t next = k_uptime_get();
	int64_t last_sample = next;
	unsigned int sequence = 0;
	unsigned int errors = 0;
	unsigned int overruns = 0;
	unsigned int since_stats = 0;

	for (;;) {
		const int64_t fetch_start = k_uptime_get();
		const int fetch_rc = sensor_sample_fetch(kBmp280);
		const long long fetch_ms = k_uptime_get() - fetch_start;

		sensor_value pressure{};
		sensor_value temperature{};

		const bool read_ok =
			fetch_rc == 0 &&
			sensor_channel_get(kBmp280, SENSOR_CHAN_PRESS, &pressure) == 0 &&
			sensor_channel_get(kBmp280, SENSOR_CHAN_AMBIENT_TEMP,
					   &temperature) == 0;

		if (!read_ok) {
			if (++errors == 1) {
				printk("ERROR: BMP280 fetch failed (%d); retrying\n",
				       fetch_rc);
			}
		} else {
			const int64_t sample_time = k_uptime_get();
			const long long interval_ms = sample_time - last_sample;

			last_sample = sample_time;
			fetch_time.add(fetch_ms);
			sample_interval.add(interval_ms);

			if (sequence % kLogEveryNSamples == 0) {
				const long pa = pressure_pa(pressure);
				const long centi_c = temperature_centi_c(temperature);
				const bool below_zero = centi_c < 0;
				const long abs_centi_c = below_zero ? -centi_c : centi_c;

				printk("[%7lld.%02lld s] seq=%u  P=%ld Pa  T=%s%ld.%02ld C"
				       "  fetch=%lld ms  dt=%lld ms\n",
				       static_cast<long long>(sample_time / 1000),
				       static_cast<long long>((sample_time % 1000) / 10),
				       sequence, pa, below_zero ? "-" : "",
				       abs_centi_c / 100, abs_centi_c % 100, fetch_ms,
				       interval_ms);
			}

			if (sequence % kHeartbeatEveryNSamples == 0) {
				(void)gpio_pin_toggle_dt(&kLed);
			}

			++sequence;
		}

		if (++since_stats >= kStatsEveryNSamples) {
			printk("stats: %u samples  fetch %lld/%lld/%lld ms "
			       "(min/avg/max)  dt %lld/%lld/%lld ms  "
			       "overruns=%u  errors=%u\n",
			       fetch_time.count, fetch_time.min, fetch_time.average(),
			       fetch_time.max, sample_interval.min,
			       sample_interval.average(), sample_interval.max,
			       overruns, errors);

			since_stats = 0;
			fetch_time.reset();
			sample_interval.reset();
		}

		/*
		 * Absolute deadlines: sleep until the next slot rather than
		 * "work + 50 ms", which would silently stretch the period.
		 */
		next += kSamplePeriodMs;

		const int64_t now = k_uptime_get();

		if (now > next) {
			/* Late: count it and re-anchor instead of bursting to catch up. */
			++overruns;
			next = now + kSamplePeriodMs;
		} else {
			k_sleep(K_TIMEOUT_ABS_MS(next));
		}
	}
}
