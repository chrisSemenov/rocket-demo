/*
 * Host-side tests for the boot pulse timing. No board, no clock: time is a
 * parameter, so the whole 320 ms sequence is stepped through instantly.
 */
#include "../src/led_pulse.hpp"

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char *label)
{
	if (condition) {
		std::printf("ok   %s\n", label);
	} else {
		std::printf("FAIL %s\n", label);
		++failures;
	}
}

} // namespace

int main()
{
	/* 1. Starts dark and does nothing until started. */
	{
		led::ReadyPulse pulse;
		check(!pulse.active(), "idle pulse is inactive");
		check(!pulse.lit(), "idle pulse is dark");
	}

	/* 2. Full sequence: on 100 ms, off 120 ms, on 100 ms, then done. */
	{
		led::ReadyPulse pulse;

		pulse.start(1000);
		pulse.advance(1000);
		check(pulse.active() && pulse.lit(), "first flash on at start");

		pulse.advance(1099);
		check(pulse.lit(), "still on at 99 ms");

		pulse.advance(1100);
		check(!pulse.lit(), "off at 100 ms");

		pulse.advance(1219);
		check(!pulse.lit(), "still off at 219 ms");

		pulse.advance(1220);
		check(pulse.lit(), "second flash on at 220 ms");

		pulse.advance(1319);
		check(pulse.lit(), "still on at 319 ms");

		pulse.advance(1320);
		check(!pulse.active(), "inactive at 320 ms");
		check(!pulse.lit(), "dark when finished");
	}

	/* 3. Cancel ends it immediately, whatever the step. */
	{
		led::ReadyPulse pulse;
		pulse.start(0);
		pulse.advance(0);
		pulse.cancel();

		check(!pulse.active(), "cancel deactivates");
		check(!pulse.lit(), "cancel turns the light off");
	}

	/* 4. A late advance does not replay missed steps. */
	{
		led::ReadyPulse pulse;
		pulse.start(0);
		pulse.advance(0);
		pulse.advance(5000);

		check(pulse.active() && !pulse.lit(), "one step per call, no catch-up");

		pulse.advance(5130);
		check(pulse.lit(), "next step still runs");

		pulse.advance(5240);
		check(!pulse.active(), "sequence completes");
	}

	std::printf("\n%s (%d failure%s)\n", failures != 0 ? "FAILED" : "PASSED",
		    failures, failures == 1 ? "" : "s");

	return failures != 0 ? 1 : 0;
}
