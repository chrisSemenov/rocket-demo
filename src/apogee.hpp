#pragma once

#include <cstdint>

/*
 * Apogee detection, with no Zephyr, clock or hardware dependency: you feed it
 * timestamped altitude samples and it tells you when a deployment event starts.
 *
 * Trigger: the current altitude is at or above the configured minimum AND the
 * average vertical velocity over the preceding 750 ms is negative by at least a
 * configured margin. The event latches for the rest of the flight.
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
};

class Detector {
public:
	explicit Detector(const Config &config = kDefaultConfig);

	/* Returns an event only on the sample that first satisfies both conditions. */
	Event on_sample(const Sample &sample);

	State state() const;
	bool deployed() const;
	int32_t velocity_mm_s() const;
	int64_t coverage_ms() const;
	int samples() const;

	/* New simulated flight: clears history and the latch. */
	void reset();

private:
	void push(const Sample &sample);
	void evict_older_than(int64_t newest_ms);
	bool velocity_over_window(int64_t &out_target_ms, double &out_velocity_ms) const;

	Config config_;
	Sample samples_[kCapacity];
	int count_;
	int last_sequence_valid_;
	uint32_t last_sequence_;
	State state_;
	int32_t velocity_mm_s_;
};

} // namespace apogee
