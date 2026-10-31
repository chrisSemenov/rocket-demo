#pragma once

#include <cstdint>

/*
 * Two-flash boot indication: 100 ms on, 120 ms off, 100 ms on, then off.
 * Pure timing, no GPIO and no clock, so it can be tested with fake time.
 */
namespace led {

constexpr int64_t kReadyOnMs = 100;
constexpr int64_t kReadyGapMs = 120;

class ReadyPulse {
public:
	void start(int64_t now);
	void cancel();
	void advance(int64_t now);

	bool active() const;
	bool lit() const;

private:
	bool active_ = false;
	unsigned int step_ = 0;
	int64_t next_at_ = 0;
	bool lit_ = false;
};

} // namespace led
