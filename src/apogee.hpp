#pragma once

#include <cstdint>

/*
 * Apogee detection, with no Zephyr, clock or hardware dependency: you feed it
 * timestamped altitude samples and it tells you when a deployment event starts.
 *
 * Trigger: the current altitude is at or above the configured minimum AND the
 * average vertical velocity over the preceding 750 ms is negative by at least a
 * configured margin, AND the least-squares slope over the same window is also
 * at least that margin below zero.
 *
 * That average is the textbook one, (h(now) - h(now - 750 ms)) / 0.750 s, with
 * the start height linearly interpolated between the two samples that bracket
 * the window edge. It is the quantity the assignment names, but it depends on
 * exactly two points, so one bad sample at either edge moves it by
 * sample_error / 0.75 s -- easily more than the margin.
 *
 * The slope is the second opinion. For a quadratic (constant acceleration) the
 * two are mathematically identical, so requiring both costs nothing on a real
 * descent; they only disagree when the window is not a steady fall, which is
 * exactly the case a bench lift produces. The assignment's "indicating
 * constant descent" is that linearity check, not a separate rule.
 *
 * Peak estimate (the drop confirmation and the reported flight result):
 * every new sample produces one filtered altitude, the median of that sample
 * and its two neighbours, and the median is associated with the timestamp of
 * the *middle* of the three (so it is available 50 ms after that time at
 * 20 Hz, not 150 ms). The peak is the maximum of those filtered values. An
 * isolated spike cannot raise it, because the median ignores the extreme of
 * the three. A genuine peak is not preserved exactly: if it falls on a sample,
 * the two neighbours are about 1.2 cm lower at 20 Hz, and the median reports
 * that lower value. The error is one sample of curvature, not one sample of
 * climb: during a monotone climb the median of three is the middle sample's
 * own altitude, so the same timestamp and value are recorded, only one sample
 * later.
 *
 * The peak feeds two different things and they are not the same obligation:
 *   - the drop confirmation needs a reference that a fault cannot manufacture,
 *     so a history gap rebuilds it from fresh samples;
 *   - the reported flight result is a measurement, so it survives a gap and is
 *     reset only by reset() (a new simulated flight).
 */
namespace apogee {

enum class State { filling, ready, deployed };

struct Config {
	int32_t min_altitude_cm;
	int32_t min_descent_mm_s; /* how far below zero the average must be */
	int64_t window_ms;        /* 750 */
	int64_t history_ms;       /* how long a sample may stay in the buffer */
	int64_t max_gap_ms;       /* a longer hole invalidates the window */
	int32_t min_drop_cm;      /* how far below the peak before deploying */
};

struct Sample {
	int64_t timestamp_ms;
	int32_t altitude_cm;
	uint32_t sequence;
};

struct Event {
	bool deployed;
	int64_t event_ms;    /* timestamp of the sample that satisfied both conditions */
	int64_t deadline_ms; /* event + 10 s, set once */

	/*
	 * Frozen when the decision is taken, so a later sample cannot move the
	 * flight result. drop_cm is the estimated peak minus the altitude at the
	 * decision; event_ms - apogee_ms is the decision delay from the peak.
	 */
	int32_t apogee_cm;
	int64_t apogee_ms;
	int32_t drop_cm;
};

constexpr int kCapacity = 32;
constexpr int64_t kDeploymentIndicationMs = 10000;

constexpr Config kDefaultConfig{
	150,   /* 1.50 m: about twice the observed peak-to-peak noise */
	300,   /* 0.30 m/s: a noise guard, not a performance threshold */
	750,
	2000,
	250,
	25,    /* 25 cm below the peak: a lift has zero drop by definition */
};

class Detector {
public:
	explicit Detector(const Config &config = kDefaultConfig);

	/* Returns an event only on the sample that first satisfies both conditions. */
	Event on_sample(const Sample &sample);

	State state() const;
	bool deployed() const;

	/* The value the trigger uses: window endpoint average. */
	int32_t velocity_mm_s() const;

	/* Diagnostic only: least-squares slope over the same window. */
	int32_t slope_mm_s() const;

	int64_t coverage_ms() const;
	int samples() const;

	/*
	 * Peak altitude estimate so far (median-of-three, middle-sample time).
	 * Before the decision this is "the highest estimate yet"; after it, the
	 * frozen flight result, because on_sample() stops updating once latched.
	 */
	bool have_apogee() const;
	int32_t apogee_cm() const;
	int64_t apogee_ms() const;

	/* apogee_cm() - the newest altitude, in cm; 0 before there is an estimate. */
	int32_t drop_cm() const;

	/* New simulated flight: clears history, the latch and the peak record. */
	void reset();

private:
	void push(const Sample &sample);
	void evict_older_than(int64_t newest_ms);

	/* Linear interpolation of the stored altitude at an arbitrary time. */
	bool interpolate_altitude(int64_t at_ms, double &out_cm) const;

	/*
	 * Median of the newest n altitudes (n <= 3, and only when n samples are
	 * present), associated with the timestamp of the middle sample of those n.
	 */
	int32_t median_recent(int n, int64_t &out_timestamp_ms) const;

	/* (h(now) - h(now - window)) / window, as documented in the README. */
	bool average_velocity(int64_t &out_target_ms, double &out_velocity_ms) const;

	/* Least-squares slope over the same window; diagnostic, not the trigger. */
	bool slope_over_window(int64_t &out_target_ms, double &out_slope_ms) const;

	Config config_;
	Sample samples_[kCapacity];
	int count_;
	int last_sequence_valid_;
	uint32_t last_sequence_;
	State state_;
	int32_t velocity_mm_s_;
	int32_t slope_mm_s_;

	/* Drop confirmation reference: median peak since the last history gap. */
	int32_t peak_cm_;

	/* Peak altitude estimate for the flight: survives gaps, reset by reset(). */
	int32_t apogee_cm_;
	int64_t apogee_ms_;
	int32_t drop_cm_;
};

} // namespace apogee
