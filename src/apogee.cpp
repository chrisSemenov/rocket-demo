#include "apogee.hpp"

/* Zephyr's minimal C++ library has no <cstring>; the C header is available. */
#include <string.h>

namespace {

int32_t to_mm_s(double velocity_ms)
{
	const double scaled = velocity_ms * 1000.0;

	/* Round half away from zero without depending on llround(). */
	return static_cast<int32_t>(scaled >= 0.0 ? scaled + 0.5 : scaled - 0.5);
}

} // namespace

namespace apogee {

Detector::Detector(const Config &config)
	: config_(config), count_(0), last_sequence_valid_(0), last_sequence_(0),
	  state_(State::filling), velocity_mm_s_(0), slope_mm_s_(0),
	  peak_cm_(INT32_MIN), apogee_cm_(INT32_MIN), apogee_ms_(0), drop_cm_(0)
{
}

void Detector::push(const Sample &sample)
{
	if (count_ == kCapacity) {
		/* Drop the oldest; the window is time-bounded anyway. */
		memmove(&samples_[0], &samples_[1],
			     sizeof(Sample) * (kCapacity - 1));
		--count_;
	}

	samples_[count_++] = sample;
}

void Detector::evict_older_than(int64_t newest_ms)
{
	int keep_from = 0;

	while (keep_from < count_ &&
	       newest_ms - samples_[keep_from].timestamp_ms > config_.history_ms) {
		++keep_from;
	}

	if (keep_from == 0) {
		return;
	}

	count_ -= keep_from;
	memmove(&samples_[0], &samples_[keep_from], sizeof(Sample) * count_);
}

bool Detector::interpolate_altitude(int64_t at_ms, double &out_cm) const
{
	if (count_ < 2) {
		return false;
	}

	int i = 0;

	while (i < count_ && samples_[i].timestamp_ms < at_ms) {
		++i;
	}

	if (i == 0) {
		/* at_ms is at or before the oldest sample: exact only on equality. */
		if (samples_[0].timestamp_ms == at_ms) {
			out_cm = static_cast<double>(samples_[0].altitude_cm);
			return true;
		}

		return false;
	}

	if (i >= count_) {
		/* at_ms is after the newest sample: there is nothing to interpolate. */
		return false;
	}

	const Sample &before = samples_[i - 1];
	const Sample &after = samples_[i];
	const int64_t span = after.timestamp_ms - before.timestamp_ms;

	if (span <= 0) {
		out_cm = static_cast<double>(after.altitude_cm);
		return true;
	}

	const double fraction =
		static_cast<double>(at_ms - before.timestamp_ms) /
		static_cast<double>(span);

	out_cm = static_cast<double>(before.altitude_cm) +
		 fraction * (static_cast<double>(after.altitude_cm) -
			     static_cast<double>(before.altitude_cm));

	return true;
}

int32_t Detector::median_recent(int n, int64_t &out_timestamp_ms) const
{
	if (n > count_) {
		n = count_;
	}

	if (n <= 0) {
		out_timestamp_ms = 0;
		return 0;
	}

	const int first = count_ - n;

	/*
	 * Selection sort over at most three values: no allocation and no copy of
	 * the sample array. Ties keep their original order, so the middle value is
	 * stable.
	 */
	int order[3] = {0, 1, 2};

	for (int i = 0; i < n; ++i) {
		for (int j = i + 1; j < n; ++j) {
			if (samples_[first + order[j]].altitude_cm <
			    samples_[first + order[i]].altitude_cm) {
				const int swap = order[i];

				order[i] = order[j];
				order[j] = swap;
			}
		}
	}

	/*
	 * The filtered value is the middle altitude, but its time is the middle
	 * sample's time: a median filter is centred on the middle of its window,
	 * so the estimate for that instant is only available once the newest
	 * sample has arrived.
	 */
	out_timestamp_ms = samples_[first + n / 2].timestamp_ms;

	return samples_[first + order[n / 2]].altitude_cm;
}

bool Detector::average_velocity(int64_t &out_target_ms,
				double &out_velocity_ms) const
{
	if (count_ < 2 || config_.window_ms <= 0) {
		return false;
	}

	const Sample &newest = samples_[count_ - 1];
	const int64_t start_ts = newest.timestamp_ms - config_.window_ms;

	if (start_ts < samples_[0].timestamp_ms) {
		/* History does not reach back a full window yet. */
		return false;
	}

	double start_cm = 0.0;

	if (!interpolate_altitude(start_ts, start_cm)) {
		return false;
	}

	const double seconds = static_cast<double>(config_.window_ms) / 1000.0;

	if (!(seconds > 0.0)) {
		return false;
	}

	out_velocity_ms =
		(static_cast<double>(newest.altitude_cm) - start_cm) / 100.0 /
		seconds;
	out_target_ms = start_ts;

	return true;
}

bool Detector::slope_over_window(int64_t &out_target_ms,
				 double &out_slope_ms) const
{
	if (count_ < 2) {
		return false;
	}

	const Sample &newest = samples_[count_ - 1];
	const int64_t start_ts = newest.timestamp_ms - config_.window_ms;

	if (samples_[0].timestamp_ms > start_ts) {
		return false;
	}

	int first = 0;

	while (first < count_ && samples_[first].timestamp_ms < start_ts) {
		++first;
	}

	const int n = count_ - first;

	if (n < 2) {
		return false;
	}

	const double t_ref = static_cast<double>(newest.timestamp_ms) / 1000.0;
	double sum_x = 0.0;
	double sum_y = 0.0;
	double sum_xx = 0.0;
	double sum_xy = 0.0;

	for (int i = first; i < count_; ++i) {
		const double x =
			static_cast<double>(samples_[i].timestamp_ms) / 1000.0 - t_ref;
		const double y = static_cast<double>(samples_[i].altitude_cm) / 100.0;

		sum_x += x;
		sum_y += y;
		sum_xx += x * x;
		sum_xy += x * y;
	}

	const double denominator =
		static_cast<double>(n) * sum_xx - sum_x * sum_x;

	if (!(denominator > 0.0)) {
		/* Degenerate timestamps: refuse rather than divide by zero. */
		return false;
	}

	out_slope_ms = (static_cast<double>(n) * sum_xy - sum_x * sum_y) /
		       denominator;
	out_target_ms = start_ts;

	return true;
}

Event Detector::on_sample(const Sample &sample)
{
	Event event{};

	if (state_ == State::deployed) {
		/* One deployment per flight: nothing can extend or repeat it. */
		return event;
	}

	if (last_sequence_valid_ && sample.sequence == last_sequence_) {
		/* The consumer polls at its own rate; the same sample is not new data. */
		return event;
	}

	if (count_ > 0) {
		const int64_t newest = samples_[count_ - 1].timestamp_ms;

		if (sample.timestamp_ms <= newest) {
			/* Out-of-order or repeated timestamp: reject rather than reorder. */
			return event;
		}

		if (sample.timestamp_ms - newest > config_.max_gap_ms) {
			/*
			 * A hole this long cannot support a defensible 750 ms estimate:
			 * start the window again from this sample.
			 */
			count_ = 0;
			state_ = State::filling;
			peak_cm_ = INT32_MIN;
		}
	}

	push(sample);
	last_sequence_ = sample.sequence;
	last_sequence_valid_ = 1;

	evict_older_than(sample.timestamp_ms);

	const Sample &newest = samples_[count_ - 1];

	int64_t target = 0;
	double velocity_ms = 0.0;

	if (!average_velocity(target, velocity_ms)) {
		state_ = State::filling;
		return event;
	}

	velocity_mm_s_ = to_mm_s(velocity_ms);

	double slope_ms = 0.0;
	const bool slope_valid = slope_over_window(target, slope_ms);

	if (slope_valid) {
		slope_mm_s_ = to_mm_s(slope_ms);
	}

	if (state_ == State::filling) {
		state_ = State::ready;
	}

	/*
	 * Peak estimate. Each sample produces one filtered altitude: the median
	 * of itself and its two neighbours, stamped with the middle sample's time.
	 * Taking the maximum of that series means an isolated spike cannot become
	 * the peak, and a genuine peak is reported within about one sample of
	 * curvature (1.2 cm at 20 Hz) rather than one sample of climb: on a
	 * monotone climb the median of three is the middle sample's own value.
	 *
	 * Two references, deliberately different:
	 *   peak_cm_   - the drop confirmation. A history gap rebuilds it, because
	 *                a fault must not be able to manufacture a deployment.
	 *   apogee_cm_ - the flight result. It survives a gap and is reset only by
	 *                reset(), so an outage cannot erase the measured peak.
	 *
	 * Fewer than three samples after a gap means the filter window would
	 * straddle the hole, so nothing is estimated until it is valid again.
	 */
	if (count_ >= 3) {
		int64_t filtered_ms = 0;
		const int32_t filtered_cm = median_recent(3, filtered_ms);

		if (filtered_cm > peak_cm_) {
			peak_cm_ = filtered_cm;
		}

		if (filtered_cm > apogee_cm_) {
			apogee_cm_ = filtered_cm;
			apogee_ms_ = filtered_ms;
		}
	}

	int32_t drop_cm = 0;

	if (peak_cm_ != INT32_MIN) {
		drop_cm = peak_cm_ - newest.altitude_cm;
	}

	const bool high_enough = newest.altitude_cm >= config_.min_altitude_cm;
	const bool dropped = peak_cm_ != INT32_MIN &&
			     drop_cm >= config_.min_drop_cm;

	/*
	 * Reported drop, always from the estimated peak, so the log and the
	 * frozen event describe the same flight result even across a gap.
	 */
	drop_cm_ = apogee_cm_ == INT32_MIN ? 0 : apogee_cm_ - newest.altitude_cm;

	/*
	 * Strictly negative, and at least the configured margin below zero. The
	 * strictness matters when the margin is configured as 0: "v <= 0" would
	 * treat a flat trace as descending.
	 */
	const bool descending = velocity_mm_s_ < 0 &&
				velocity_mm_s_ <= -config_.min_descent_mm_s;

	/*
	 * Second opinion over the same window. A single bad sample at either edge
	 * of the average moves it by sample_error / 0.75 s, which is easily more
	 * than the margin; the fit averages all ~15 samples and only agrees when
	 * the window is a steady descent rather than a kink.
	 */
	const bool slope_descending = slope_valid && slope_mm_s_ < 0 &&
				      slope_mm_s_ <= -config_.min_descent_mm_s;

	if (high_enough && dropped && descending && slope_descending) {
		state_ = State::deployed;

		event.deployed = true;
		event.event_ms = newest.timestamp_ms;
		event.deadline_ms = newest.timestamp_ms + kDeploymentIndicationMs;
		event.apogee_cm = apogee_cm_;
		event.apogee_ms = apogee_ms_;
		event.drop_cm = drop_cm_;
	}

	return event;
}

State Detector::state() const
{
	return state_;
}

bool Detector::deployed() const
{
	return state_ == State::deployed;
}

int32_t Detector::velocity_mm_s() const
{
	return velocity_mm_s_;
}

int32_t Detector::slope_mm_s() const
{
	return slope_mm_s_;
}

int64_t Detector::coverage_ms() const
{
	if (count_ < 2) {
		return 0;
	}

	return samples_[count_ - 1].timestamp_ms - samples_[0].timestamp_ms;
}

int Detector::samples() const
{
	return count_;
}

bool Detector::have_apogee() const
{
	return apogee_cm_ != INT32_MIN;
}

int32_t Detector::apogee_cm() const
{
	return apogee_cm_;
}

int64_t Detector::apogee_ms() const
{
	return apogee_ms_;
}

int32_t Detector::drop_cm() const
{
	return drop_cm_;
}

void Detector::reset()
{
	count_ = 0;
	peak_cm_ = INT32_MIN;
	apogee_cm_ = INT32_MIN;
	apogee_ms_ = 0;
	drop_cm_ = 0;
	last_sequence_valid_ = 0;
	last_sequence_ = 0;
	state_ = State::filling;
	velocity_mm_s_ = 0;
	slope_mm_s_ = 0;
}

} // namespace apogee
