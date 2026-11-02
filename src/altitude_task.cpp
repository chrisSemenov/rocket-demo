#include "altitude_task.hpp"

#include "altitude.hpp"
#include "app_config.hpp"
#include "shared_data.hpp"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

namespace {

K_THREAD_STACK_DEFINE(altitude_stack, cfg::kAltitudeStackBytes);
struct k_thread altitude_thread;

/*
 * Baseline calibration window. It stores time rather than a sample count, so a
 * missed sample cannot silently shorten the window, and it keeps min/max so a
 * reading taken while the sensor was moving restarts calibration.
 */
class BaselineWindow {
public:
	void add(int32_t pressure_pa, int64_t timestamp_ms)
	{
		pressure_[next_] = pressure_pa;
		timestamp_[next_] = timestamp_ms;
		next_ = (next_ + 1) % cfg::kBaselineMaxSamples;

		if (count_ < cfg::kBaselineMaxSamples) {
			++count_;
		}
	}

	/* Discard samples that fell out of the window, measured from the newest. */
	void evict(int64_t newest_ms)
	{
		while (count_ > 0) {
			const unsigned int oldest = index(0);

			if (newest_ms - timestamp_[oldest] <= cfg::kBaselineWindowMs) {
				break;
			}

			--count_;
		}
	}

	void reset()
	{
		count_ = 0;
		next_ = 0;
	}

	unsigned int count() const
	{
		return count_;
	}

	int64_t coverage_ms() const
	{
		if (count_ < 2) {
			return 0;
		}

		return timestamp_[index(count_ - 1)] - timestamp_[index(0)];
	}

	bool stats(int32_t &out_mean, int32_t &out_spread) const
	{
		if (count_ == 0) {
			return false;
		}

		int32_t lowest = INT32_MAX;
		int32_t highest = INT32_MIN;
		int64_t sum = 0;

		for (unsigned int i = 0; i < count_; ++i) {
			const int32_t value = pressure_[index(i)];

			if (value < lowest) {
				lowest = value;
			}

			if (value > highest) {
				highest = value;
			}

			sum += value;
		}

		out_mean = static_cast<int32_t>(sum / static_cast<int64_t>(count_));
		out_spread = highest - lowest;

		return true;
	}

private:
	/* 0 is the oldest retained sample, count() - 1 the newest. */
	unsigned int index(unsigned int offset_from_oldest) const
	{
		const unsigned int oldest =
			(next_ + cfg::kBaselineMaxSamples - count_) %
			cfg::kBaselineMaxSamples;

		return (oldest + offset_from_oldest) % cfg::kBaselineMaxSamples;
	}

	int32_t pressure_[cfg::kBaselineMaxSamples]{};
	int64_t timestamp_[cfg::kBaselineMaxSamples]{};
	unsigned int count_ = 0;
	unsigned int next_ = 0;
};

void publish_invalid(const Measurement &measurement, int32_t reference_pa,
		     bool calibrated, unsigned int baseline_samples)
{
	Altitude out{};

	out.source_sequence = measurement.sequence;
	out.source_timestamp_ms = measurement.timestamp_ms;
	out.reference_pa = reference_pa;
	out.baseline_samples = static_cast<uint16_t>(baseline_samples);
	out.valid = false;
	out.calibrated = calibrated;

	altitude_data().publish(out);
}

void altitude_entry(void *, void *, void *)
{
	BaselineWindow window;

	bool calibrated = false;
	bool have_sequence = false;
	uint32_t last_sequence = 0;
	int32_t reference_pa = 0;
	int64_t last_restart_log = -1000000;

	int64_t next = k_uptime_get();

	for (;;) {
		/*
		 * Re-arm: the operator asked for a new simulated flight. The
		 * baseline is this thread's state, so it is thrown away here and
		 * rebuilt from the next 1.5 s of samples. Until then altitude is
		 * published invalid, which inhibits flight decisions upstream.
		 */
		if (altitude_data().take_recalibration()) {
			window.reset();
			calibrated = false;
			reference_pa = 0;

			printk("alt:  recalibration requested; baseline cleared\n");
		}

		Measurement measurement{};
		sensor_data().read(measurement);

		const int64_t now = k_uptime_get();
		const int64_t age = now - measurement.timestamp_ms;

		const bool fresh = measurement.valid && age <= cfg::kStaleTimeoutMs;
		const bool is_new = !have_sequence || measurement.sequence != last_sequence;

		if (!fresh) {
			/*
			 * While calibrating, a gap breaks the "stationary" claim, so the
			 * window restarts. Once P0 is frozen it is kept: a missed sample
			 * must not force a recalibration that blacks out altitude for
			 * another two seconds, and must not move the reference mid-flight.
			 * Only the published altitude goes invalid.
			 */
			if (!calibrated) {
				window.reset();
			}

			publish_invalid(measurement, reference_pa, calibrated,
					calibrated ? 0 : window.count());
		} else if (is_new) {
			have_sequence = true;
			last_sequence = measurement.sequence;

			const bool plausible =
				measurement.pressure_pa >= altitude::kMinPressurePa &&
				measurement.pressure_pa <= altitude::kMaxPressurePa;

			if (!calibrated) {
				if (plausible) {
					window.add(measurement.pressure_pa,
						   measurement.timestamp_ms);
					window.evict(measurement.timestamp_ms);
				}

				int32_t mean = 0;
				int32_t spread = 0;

				if (window.count() >= cfg::kBaselineMinSamples &&
				    window.coverage_ms() >= cfg::kBaselineMinCoverageMs &&
				    window.stats(mean, spread)) {
					if (spread <= cfg::kBaselineMaxSpreadPa) {
						calibrated = true;
						reference_pa = mean;

						printk("alt:  baseline frozen P0=%d Pa after"
						       " %u samples (spread %d Pa)\n",
						       reference_pa, window.count(),
						       spread);
					} else {
						/*
						 * The sensor moved during calibration. Start
						 * again from this sample so the movement is not
						 * remembered as a valid baseline.
						 */
						window.reset();
						window.add(measurement.pressure_pa,
							   measurement.timestamp_ms);
						window.evict(measurement.timestamp_ms);

						if (now - last_restart_log >= 1000) {
							printk("alt:  baseline restarted"
							       " (spread %d Pa > %d)\n",
							       spread,
							       cfg::kBaselineMaxSpreadPa);
							last_restart_log = now;
						}
					}
				}

				if (!calibrated) {
					publish_invalid(measurement, 0, false,
							window.count());
				}
			}

			if (calibrated) {
				Altitude out{};

				out.source_sequence = measurement.sequence;
				out.source_timestamp_ms = measurement.timestamp_ms;
				out.reference_pa = reference_pa;
				out.baseline_samples =
					static_cast<uint16_t>(window.count());
				out.calibrated = true;

				out.valid = altitude::centimeters(measurement.pressure_pa,
								  reference_pa,
								  out.altitude_cm);

				altitude_data().publish(out);
			}
		}

		/*
		 * Absolute deadline, like the sensor loop: a slow reread must not
		 * stretch the period. A missed slot is skipped rather than caught up.
		 */
		next += cfg::kAltitudePeriodMs;

		if (k_uptime_get() > next) {
			next = k_uptime_get() + cfg::kAltitudePeriodMs;
		}

		k_sleep(K_TIMEOUT_ABS_MS(next));
	}
}

} // namespace

void altitude_task_start()
{
	k_thread_create(&altitude_thread, altitude_stack,
			K_THREAD_STACK_SIZEOF(altitude_stack), altitude_entry,
			nullptr, nullptr, nullptr, cfg::kAltitudePriority, 0,
			K_NO_WAIT);
	(void)k_thread_name_set(&altitude_thread, "altitude");
}
