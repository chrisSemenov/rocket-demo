/*
 * Host-side tests for LED arbitration. No board, no Zephyr, no waiting: time is
 * a parameter, so 10- and 5-second windows are stepped through instantly.
 *
 * Build and run with tests/run-tests.ps1.
 */
#include "../src/led_logic.hpp"

#include <cstdio>

namespace {

int failures = 0;

const char *name(led::Output output)
{
	switch (output) {
	case led::Output::off:
		return "off";
	case led::Output::on:
		return "on";
	case led::Output::blink_on:
		return "blink_on";
	case led::Output::blink_off:
		return "blink_off";
	}

	return "?";
}

void check(const char *label, int64_t now, const led::Request &request,
	   led::Output expected)
{
	const led::Output actual = led::decide(now, request);

	if (actual != expected) {
		std::printf("FAIL %-40s t=%6lld got %-10s want %-10s\n", label,
			    static_cast<long long>(now), name(actual), name(expected));
		++failures;
	} else {
		std::printf("ok   %-40s t=%6lld %-10s\n", label,
			    static_cast<long long>(now), name(actual));
	}
}

led::Request heat_only(int64_t start, int64_t deadline)
{
	led::Request r{};
	r.heat_active = true;
	r.heat_start_ms = start;
	r.heat_deadline_ms = deadline;
	return r;
}

led::Request deployment_only(int64_t deadline)
{
	led::Request r{};
	r.deployment_active = true;
	r.deployment_deadline_ms = deadline;
	return r;
}

led::Request both(int64_t deployment_deadline, int64_t heat_start,
		  int64_t heat_deadline)
{
	led::Request r = deployment_only(deployment_deadline);
	r.heat_active = true;
	r.heat_start_ms = heat_start;
	r.heat_deadline_ms = heat_deadline;
	return r;
}

} // namespace

int main()
{
	const led::Request idle{};

	/* 1. Nothing requested. */
	check("idle", 0, idle, led::Output::off);
	check("idle, later", 100000, idle, led::Output::off);

	/* 2. Deployment only: steady on for exactly 10 s. */
	{
		const led::Request r = deployment_only(led::kDeploymentIndicationMs);

		check("deployment at t=0", 0, r, led::Output::on);
		check("deployment mid-window", 5000, r, led::Output::on);
		check("deployment last ms", 9999, r, led::Output::on);
		check("deployment at deadline (expired)", 10000, r, led::Output::off);
		check("deployment after deadline", 10001, r, led::Output::off);
	}

	/* 3. Heat only: 250 / 250 half periods, anchored to the alarm start. */
	{
		const led::Request r = heat_only(0, led::kHeatAlarmMs);

		check("blink first half", 0, r, led::Output::blink_on);
		check("blink first half end", 249, r, led::Output::blink_on);
		check("blink second half", 250, r, led::Output::blink_off);
		check("blink second half end", 499, r, led::Output::blink_off);
		check("blink second cycle", 500, r, led::Output::blink_on);
		check("blink mid-alarm", 2250, r, led::Output::blink_off);
		check("blink last full cycle", 4500, r, led::Output::blink_on);
		check("blink before alarm end", 4750, r, led::Output::blink_off);
	}

	/* 4. Heat at an arbitrary phase: still anchored, not restarted. */
	{
		const led::Request r = heat_only(1000, 6000);

		check("anchored blink at start", 1000, r, led::Output::blink_on);
		check("anchored blink off-phase", 1250, r, led::Output::blink_off);
		check("anchored blink next on", 1500, r, led::Output::blink_on);
		check("heat starts late", 999, r, led::Output::off);
	}

	/* 5. Heat beats deployment while both are active. */
	{
		const led::Request r = both(10000, 2000, 7000);

		check("deployment before heat", 1000, r, led::Output::on);
		check("heat starts, overrides", 2000, r, led::Output::blink_on);
		check("heat overrides mid-cycle", 2250, r, led::Output::blink_off);
		check("heat still overrides", 6000, r, led::Output::blink_on);
	}

	/* 6. Cooling before the deployment deadline resumes steady on, with the
	 *    original deadline untouched. */
	{
		const led::Request r = deployment_only(10000);

		check("cooled at 7 s -> deployment resumes", 7000, r, led::Output::on);
		check("still on just before deadline", 9999, r, led::Output::on);
	}

	/* 7. Heat expires on its own, then deployment resumes if still valid. */
	{
		const led::Request r = both(10000, 2000, 7000);

		check("heat deadline reached, deployment resumes", 7000, r,
		      led::Output::on);
		check("after heat, still deployed", 8000, r, led::Output::on);
	}

	/* 8. Cooling after the deployment deadline must not restore it. */
	{
		const led::Request r = both(10000, 8000, 13000);

		check("overlap before deployment expiry", 9000, r,
		      led::Output::blink_on);
		check("overlap at deployment expiry", 10000, r,
		      led::Output::blink_on);
		check("heat past old deadline still blinks", 11000, r,
		      led::Output::blink_on);
		check("heat past old deadline, next phase", 11250, r,
		      led::Output::blink_off);
	}

	/* 9. Heat request already expired falls through to deployment. */
	{
		const led::Request r = both(10000, 1000, 2000);

		check("stale heat ignored", 5000, r, led::Output::on);
		check("stale heat, deployment expired", 10000, r, led::Output::off);
	}

	/* 10. Boundary: heat deadline instant is expired, not active. */
	{
		const led::Request r = both(10000, 1000, 5000);

		check("heat at deadline-1", 4999, r, led::Output::blink_off);
		check("heat at deadline", 5000, r, led::Output::on);
	}

	std::printf("\n%s (%d failure%s)\n", failures != 0 ? "FAILED" : "PASSED",
		    failures, failures == 1 ? "" : "s");

	return failures != 0 ? 1 : 0;
}
