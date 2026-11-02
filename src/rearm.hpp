#pragma once

#include <cstdint>

/*
 * Debounced momentary-button detector, with no Zephyr, GPIO or clock
 * dependency: it is a pure function of (raw level, timestamp). That is what
 * makes it testable without hardware (tests/rearm_test.cpp).
 *
 * A press is reported once, after the pressed level has been stable for the
 * debounce interval. Holding the button does not repeat it; releasing re-arms
 * the detector for the next press.
 *
 * A press is only accepted after the released level has been observed at least
 * once. Without that rule a pin that reads low at reset -- floating before its
 * pull-up takes effect, or wired to something that holds it low -- reports a
 * phantom press on every boot. On this board the phantom was observed at 0.3 s
 * after reset.
 *
 * Why debounce at all: a mechanical button bounces for a few milliseconds, so a
 * naive edge read can produce two or three "presses" from one finger.
 */
namespace rearm {

class Button {
public:
	explicit Button(int64_t debounce_ms = 50);

	/*
	 * raw_active is true while the button reads as pressed, already corrected
	 * for the devicetree polarity (GPIO_ACTIVE_LOW). Returns true exactly once
	 * per press.
	 */
	bool update(bool raw_active, int64_t now_ms);

	void reset();

private:
	int64_t debounce_ms_;
	bool pending_;
	int64_t pending_since_ms_;
	bool stable_;
	bool reported_;
	bool saw_release_;
};

} // namespace rearm
