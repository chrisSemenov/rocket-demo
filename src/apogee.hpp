#pragma once

#include <cstdint>

/*
 * Apogee detection, with no Zephyr, clock or hardware dependency: you feed it
 * timestamped altitude samples and it tells you when a deployment event starts.
 *
 * Trigger: the current altitude is at or above the configured minimum AND the
 * average vertical velocity over the preceding 750 ms is negative by at least a
 * configured margin.
 *
 * That average is the textbook one, (h(now) - h(now - 750 ms)) / 0.750 s, with
 * the start height linearly interpolated between the two samples that bracket
 * the window edge. A least-squares slope over the same window is also computed
 * and exposed as a diagnostic, but it is not what arms the trigger: the two
 * disagree in sign whenever the path is not close to a straight line.
 *
 * A negative *average* only means the endpoint is lower than 750 ms ago; it does
 * not mean every intermediate sample descended, and it does not mean apogee was
 * the exact peak. This is what FLT-03 asks for, no more.
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

	/* New simulated flight: clears history and the latch. */
	void reset();

private:
	void push(const Sample &sample);
	void evict_older_than(int64_t newest_ms);

	/* Linear interpolation of the stored altitude at an arbitrary time. */
	bool interpolate_altitude(int64_t at_ms, double &out_cm) const;

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
	int32_t peak_cm_;
};

} // namespace apogee
