#pragma once

#include <cstdint>

/*
 * Overheat policy, with no Zephyr or hardware dependency.
 *
 * Policy (documented in docs/decisions.md):
 *   - hot at or above the threshold;
 *   - a heat window starts when the state becomes hot, lasting 5 s;
 *   - while still hot at or after expiry, the window is renewed for another
 *     5 s, keeping the original start so the LED blink phase does not jump;
 *   - cooling clears the alarm immediately, below the threshold minus the
 *     hysteresis band;
 *   - missing data never counts as cool: the state is left unchanged.
 */
namespace heat {

struct Config {
	int32_t threshold_mc;
	int32_t hysteresis_mc;
	int64_t alarm_ms;
	int64_t renew_guard_ms;
};

enum class State { cool, hot };

struct Request {
	bool active;
	int64_t start_ms;
	int64_t deadline_ms;
};

struct Decision {
	bool changed;
	Request request;
};

constexpr Config kDefaultConfig{
	30000, /* 30.0 C: reachable with a finger, above a still bench */
	200,   /* 0.2 C of hysteresis so noise does not chatter at the threshold */
	5000,
	60,    /* renew this early, so the window never lapses while hot */
};

class Monitor {
public:
	explicit Monitor(const Config &config = kDefaultConfig);

	/* A fresh temperature sample. Repeats of the same sequence are ignored. */
	Decision on_sample(int32_t temperature_mc, int64_t timestamp_ms,
			   uint32_t sequence);

	/* No usable sample: hold the current state, never clear it. */
	Decision on_missing();

	State state() const;
	Request request() const;
	void reset();

private:
	Config config_;
	State state_;
	Request request_;
	uint32_t last_sequence_;
	bool have_sequence_;
};

} // namespace heat
