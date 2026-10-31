/*
 * Host-side tests for the overheat policy. No board, no Zephyr, no waiting.
 *
 * Build and run with tests/run-tests.ps1.
 */
#include "../src/heat.hpp"

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

constexpr int32_t kHot = heat::kDefaultConfig.threshold_mc + 1000;
constexpr int32_t kCool = heat::kDefaultConfig.threshold_mc - 1000;
constexpr int32_t kBand = heat::kDefaultConfig.threshold_mc - 100;

} // namespace

int main()
{
	/* 1. Cool bench: nothing happens. */
	{
		heat::Monitor monitor;
		for (int i = 0; i < 50; ++i) {
			monitor.on_sample(kCool, i * 100, i + 1);
		}
		check(monitor.state() == heat::State::cool, "cool samples stay cool");
		check(!monitor.request().active, "no request while cool");
	}

	/* 2. Crossing the threshold starts exactly one 5 s window. */
	{
		heat::Monitor monitor;
		const heat::Decision d = monitor.on_sample(kHot, 1000, 1);

		check(d.changed, "becoming hot is a change");
		check(d.request.active, "hot raises the request");
		check(d.request.start_ms == 1000, "window starts at the hot sample");
		check(d.request.deadline_ms == 6000, "window is 5 s");
	}

	/* 3. Repeated hot samples do not restart the window. */
	{
		heat::Monitor monitor;
		monitor.on_sample(kHot, 1000, 1);

		bool restarted = false;
		for (uint32_t seq = 2; seq < 30; ++seq) {
			const heat::Decision d =
				monitor.on_sample(kHot, 1000 + seq * 100, seq);
			if (d.request.start_ms != 1000 || d.request.deadline_ms != 6000) {
				restarted = true;
			}
		}

		check(!restarted, "hot samples inside the window do not restart it");
	}

	/* 4. Duplicate sequences are ignored. */
	{
		heat::Monitor monitor;
		monitor.on_sample(kHot, 1000, 7);
		const heat::Decision d = monitor.on_sample(kHot, 5000, 7);

		check(!d.changed, "a repeated sequence changes nothing");
		check(d.request.deadline_ms == 6000, "the deadline is untouched");
	}

	/* 5. Still hot at expiry renews, keeping the original start. */
	{
		heat::Monitor monitor;
		monitor.on_sample(kHot, 1000, 1);
		const heat::Decision d = monitor.on_sample(kHot, 6000, 2);

		check(d.changed, "renewing is a change");
		check(d.request.start_ms == 1000, "renewal keeps the alarm anchor");
		check(d.request.deadline_ms == 11000, "renewal extends by 5 s");
	}

	/* 5a. Renewal happens just before expiry, so the window never lapses. */
	{
		heat::Monitor monitor;
		monitor.on_sample(kHot, 1000, 1);
		const heat::Decision d = monitor.on_sample(
			kHot, 5950, 2);

		check(d.changed, "renewing early is a change");
		check(d.request.active, "the request stays active across renewal");
		check(d.request.deadline_ms == 11000,
		      "the window advances by a whole period");
	}

	/* 6. Nobody renewed at expiry, then hot arrives late: extend from now. */
	{
		heat::Monitor monitor;
		monitor.on_sample(kHot, 1000, 1);
		const heat::Decision d = monitor.on_sample(kHot, 20000, 2);

		check(d.request.deadline_ms == 25000,
		      "a late renewal starts a fresh window, no back-filling");
	}

	/* 7. Cooling inside the window clears it immediately. */
	{
		heat::Monitor monitor;
		monitor.on_sample(kHot, 1000, 1);
		const heat::Decision d = monitor.on_sample(kCool, 2000, 2);

		check(d.changed, "cooling is a change");
		check(!d.request.active, "cooling clears the request at once");
		check(monitor.state() == heat::State::cool, "state returns to cool");
	}

	/* 8. Hysteresis: a sample just under the threshold does not clear it. */
	{
		heat::Monitor monitor;
		monitor.on_sample(kHot, 1000, 1);
		const heat::Decision d = monitor.on_sample(kBand, 2000, 2);

		check(!d.changed, "the hysteresis band holds the state");
		check(d.request.active, "still alarming inside the band");
	}

	/* 8a. Still inside the hysteresis band at expiry: the alarm renews, so
	 *     "hot" and "alarm active" can never disagree. */
	{
		heat::Monitor monitor;
		monitor.on_sample(kHot, 1000, 1);
		const heat::Decision d = monitor.on_sample(kBand, 5950, 2);

		check(d.changed, "a band sample just before expiry renews");
		check(d.request.active, "the alarm stays active inside the band");
		check(d.request.deadline_ms == 11000, "the window advances by 5 s");
	}

	/* 8b. The window had already lapsed while in the band: a full new window,
	 *     not the four seconds that are left of the old grid. */
	{
		heat::Monitor monitor;
		monitor.on_sample(kHot, 1000, 1);
		const heat::Decision d = monitor.on_sample(kBand, 7000, 2);

		check(d.changed, "a band sample after expiry restarts the window");
		check(d.request.deadline_ms == 12000,
		      "a lapsed window restarts in full");
		check(monitor.state() == heat::State::hot, "state stays hot");
	}

	/* 9. Threshold equality counts as hot. */
	{
		heat::Monitor monitor;
		const heat::Decision d = monitor.on_sample(
			heat::kDefaultConfig.threshold_mc, 1000, 1);

		check(d.request.active, "temperature equal to the threshold is hot");
	}

	/* 10. Missing data never clears the alarm. */
	{
		heat::Monitor monitor;
		monitor.on_sample(kHot, 1000, 1);
		const heat::Decision d = monitor.on_missing();

		check(!d.changed, "missing data changes nothing");
		check(d.request.active, "missing data does not clear the alarm");
	}

	/* 11. Hot again after cooling starts a new window with a new anchor. */
	{
		heat::Monitor monitor;
		monitor.on_sample(kHot, 1000, 1);
		monitor.on_sample(kCool, 2000, 2);
		const heat::Decision d = monitor.on_sample(kHot, 3000, 3);

		check(d.request.active, "hot again raises the request");
		check(d.request.start_ms == 3000, "a new alarm has a new anchor");
		check(d.request.deadline_ms == 8000, "the new window is 5 s");
	}

	/* 12. Persistent heat across several windows keeps renewing. */
	{
		heat::Monitor monitor;
		monitor.on_sample(kHot, 1000, 1);
		monitor.on_sample(kHot, 6000, 2);
		const heat::Decision d = monitor.on_sample(kHot, 11000, 3);

		check(d.request.deadline_ms == 16000,
		      "each expiry renews by another 5 s");
		check(d.request.start_ms == 1000, "the anchor never moves");
	}

	/* 13. Reset clears everything for a new flight. */
	{
		heat::Monitor monitor;
		monitor.on_sample(kHot, 1000, 1);
		monitor.reset();

		check(monitor.state() == heat::State::cool, "reset returns to cool");
		check(!monitor.request().active, "reset clears the request");
	}

	std::printf("\n%s (%d failure%s)\n", failures != 0 ? "FAILED" : "PASSED",
		    failures, failures == 1 ? "" : "s");

	return failures != 0 ? 1 : 0;
}
