/*
 * Host-side tests for the re-arm button debounce. No board, no Zephyr: the
 * detector is fed a level and a timestamp, exactly as the thread polls it every
 * 50 ms.
 *
 * The first rule these tests pin: a pin that reads low from reset is not a
 * press. The board produced exactly that phantom at 0.3 s after reset, before
 * anyone touched it.
 *
 * Build and run with tests/run-tests.ps1.
 */
#include "../src/rearm.hpp"

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

/* Two released polls, as a 20 Hz loop would produce before a press. */
void settle_released(rearm::Button &b, int64_t from_ms)
{
	b.update(false, from_ms);
	b.update(false, from_ms + 50);
}

} // namespace

int main()
{
	/* 1. A bounce shorter than the debounce interval is not a press. */
	{
		rearm::Button b(50);
		int events = 0;

		settle_released(b, 900);
		events += b.update(true, 1000) ? 1 : 0;
		events += b.update(false, 1010) ? 1 : 0;
		events += b.update(true, 1020) ? 1 : 0;
		events += b.update(false, 1030) ? 1 : 0;

		check(events == 0, "a bounce shorter than the debounce is ignored");
	}

	/* 2. A held press reports exactly once, after the interval. */
	{
		rearm::Button b(50);
		int events = 0;

		settle_released(b, 900);
		events += b.update(true, 1000) ? 1 : 0;
		check(events == 0, "a press is not reported before the interval");
		check(!b.update(true, 1040), "still inside the interval");

		events += b.update(true, 1050) ? 1 : 0;
		check(events == 1, "a stable press reports once");

		events += b.update(true, 1100) ? 1 : 0;
		events += b.update(true, 2000) ? 1 : 0;
		check(events == 1, "holding the button does not repeat");
	}

	/* 3. Release re-arms the detector for the next press. */
	{
		rearm::Button b(50);
		int events = 0;

		settle_released(b, 900);
		b.update(true, 1000);
		events += b.update(true, 1050) ? 1 : 0;
		check(events == 1, "first press reported");

		b.update(false, 1100);
		check(!b.update(false, 1150), "release alone reports nothing");

		b.update(true, 1200);
		events += b.update(true, 1250) ? 1 : 0;
		check(events == 2, "a second press is reported");
	}

	/* 4. A pin that reads low from reset is not a press. */
	{
		rearm::Button b(50);
		int events = 0;

		for (int i = 0; i < 10; ++i) {
			events += b.update(true, 1000 + i * 50) ? 1 : 0;
		}

		check(events == 0, "a pin held low from reset is not a press");

		b.update(false, 1500);
		b.update(false, 1550);

		b.update(true, 1600);
		events += b.update(true, 1650) ? 1 : 0;
		check(events == 1, "the first real press still reports");
	}

	/* 5. reset() drops any pending press and the arming state. */
	{
		rearm::Button b(50);

		settle_released(b, 900);
		b.update(true, 1000);
		b.reset();

		check(!b.update(true, 1050), "reset clears a pending press");
		check(!b.update(true, 1100), "and leaves the detector disarmed");

		settle_released(b, 1150);
		b.update(true, 1250);
		check(b.update(true, 1300), "the detector works again after reset");
	}

	/* 6. A zero debounce still reports a clean edge once. */
	{
		rearm::Button b(0);
		int events = 0;

		b.update(false, 990);
		events += b.update(true, 1000) ? 1 : 0;
		events += b.update(true, 1000) ? 1 : 0;
		check(events == 1, "zero debounce reports one press");
	}

	std::printf("\n%s (%d failure%s)\n", failures != 0 ? "FAILED" : "PASSED",
		    failures, failures == 1 ? "" : "s");

	return failures != 0 ? 1 : 0;
}
