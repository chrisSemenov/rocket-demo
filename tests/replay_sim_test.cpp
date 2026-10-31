/*
 * Host simulation of the replay acceptance path.
 *
 * This is NOT on-target evidence. It runs the same pure units the firmware runs
 * (altitude, apogee detector, heat policy, LED arbitration) over the same trace
 * data, with the trace's own timestamps, so the expected outcome of each replay
 * scenario is known before the board is flashed. Anything that disagrees here
 * would disagree on the board too, but agreement here is not proof that the
 * threads, timing or GPIO behave.
 *
 * Build and run with tests/run-tests.ps1.
 */
#include "../src/altitude.hpp"
#include "../src/apogee.hpp"
#include "../src/heat.hpp"
#include "../src/led_logic.hpp"
#include "../src/traces_data.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

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

struct Snapshot {
	int64_t time_ms;
	led::Request request;
};

/* One simulated flight through the same units the firmware uses. */
struct Simulation {
	const Trace *trace = nullptr;
	int deployments = 0;
	int64_t deploy_ms = 0;
	int64_t deadline_ms = 0;
	std::vector<Snapshot> snapshots;

	/* The request as the LED manager would see it at time t. */
	led::Request request_at(int64_t t) const
	{
		led::Request request{};

		for (const Snapshot &snapshot : snapshots) {
			if (snapshot.time_ms > t) {
				break;
			}

			request = snapshot.request;
		}

		return request;
	}

	led::Output output_at(int64_t t) const
	{
		return led::decide(t, request_at(t));
	}

	/* blink_on and blink_off both count as "blinking". */
	bool blinking_at(int64_t t) const
	{
		const led::Output out = output_at(t);

		return out == led::Output::blink_on || out == led::Output::blink_off;
	}
};

const Trace *find_trace(const char *name)
{
	for (unsigned int i = 0; i < kTraceCount; ++i) {
		if (std::strcmp(kTraces[i].name, name) == 0) {
			return &kTraces[i];
		}
	}

	return nullptr;
}

Simulation run(const char *name, apogee::Config config = apogee::kDefaultConfig,
	       heat::Config heat_config = heat::kDefaultConfig)
{
	Simulation sim;
	sim.trace = find_trace(name);

	if (sim.trace == nullptr) {
		std::printf("FAIL trace %s is missing\n", name);
		++failures;
		return sim;
	}

	const Trace &trace = *sim.trace;

	/* P0 as the firmware freezes it: mean of the stationary 2 s window. */
	int64_t sum = 0;
	unsigned int count = 0;

	for (unsigned int i = 0; i < trace.count; ++i) {
		if (trace.samples[i].time_ms > 2000) {
			break;
		}

		sum += trace.samples[i].pressure_pa;
		++count;
	}

	const int32_t p0 = static_cast<int32_t>(sum / static_cast<int64_t>(count));

	apogee::Detector detector(config);
	heat::Monitor monitor(heat_config);

	led::Request request{};
	bool deployed = false;

	for (unsigned int i = 0; i < trace.count; ++i) {
		const TraceSample &sample = trace.samples[i];
		int32_t altitude_cm = 0;

		if (!altitude::centimeters(sample.pressure_pa, p0, altitude_cm)) {
			std::printf("FAIL %s: pressure %d Pa was rejected\n", name,
				    sample.pressure_pa);
			++failures;
			return sim;
		}

		const apogee::Event event = detector.on_sample(
			{static_cast<int64_t>(sample.time_ms), altitude_cm, i + 1});

		if (event.deployed && !deployed) {
			deployed = true;
			++sim.deployments;
			sim.deploy_ms = event.event_ms;
			sim.deadline_ms = event.deadline_ms;

			request.deployment_active = true;
			request.deployment_deadline_ms = event.deadline_ms;
		}

		const heat::Decision decision = monitor.on_sample(
			static_cast<int32_t>(sample.temperature_c_x100) * 10,
			static_cast<int64_t>(sample.time_ms), i + 1);

		/*
		 * heat_task() publishes on change and on cooling, so the store keeps
		 * the last published state. Mirroring that here is what makes the
		 * snapshot meaningful.
		 */
		if (decision.changed) {
			request.heat_active = decision.request.active;
			request.heat_start_ms = decision.request.start_ms;
			request.heat_deadline_ms = decision.request.deadline_ms;
		}

		sim.snapshots.push_back(
			{static_cast<int64_t>(sample.time_ms), request});
	}

	return sim;
}

const char *name_of(led::Output output)
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

void check_output(const Simulation &sim, int64_t t, led::Output expected,
		  const char *label)
{
	const led::Output actual = sim.output_at(t);

	if (actual != expected) {
		std::printf("FAIL %-52s t=%6lld got %-9s want %s\n", label,
			    static_cast<long long>(t), name_of(actual),
			    name_of(expected));
		++failures;
	} else {
		std::printf("ok   %-52s t=%6lld %s\n", label,
			    static_cast<long long>(t), name_of(actual));
	}
}

void check_blinking(const Simulation &sim, int64_t t, const char *label)
{
	if (!sim.blinking_at(t)) {
		std::printf("FAIL %-52s t=%6lld got %s\n", label,
			    static_cast<long long>(t),
			    name_of(sim.output_at(t)));
		++failures;
	} else {
		std::printf("ok   %-52s t=%6lld %s\n", label,
			    static_cast<long long>(t), name_of(sim.output_at(t)));
	}
}

} // namespace

int main()
{
	/* AT-04a: descending, never above the minimum. */
	{
		const Simulation sim = run("at04a");

		check(sim.deployments == 0, "at04a: no deployment below the minimum");
		check_output(sim, 8000, led::Output::off, "at04a: LED stays off");
	}

	/* AT-04b: above the minimum but climbing, then flat. */
	{
		const Simulation sim = run("at04b");

		check(sim.deployments == 0, "at04b: no deployment while not descending");
		check_output(sim, 9000, led::Output::off, "at04b: LED stays off");
	}

	/* AT-05 / AT-06: one deployment, 10 s, 19 s of continued descent. */
	{
		const Simulation sim = run("at05");

		check(sim.deployments == 1, "at05: exactly one deployment");
		check(sim.deploy_ms > 5000 && sim.deploy_ms < 6500,
		      "at05: deployment follows the peak, not the ascent");
		check(sim.deadline_ms == sim.deploy_ms + 10000,
		      "at05: deadline is event + 10 s");

		check_output(sim, sim.deploy_ms + 1000, led::Output::on,
			     "at05: steady ON after deployment");
		check_output(sim, sim.deadline_ms - 10, led::Output::on,
			     "at05: still ON one tick before the deadline");
		check_output(sim, sim.deadline_ms, led::Output::off,
			     "at05: OFF at the deadline instant");
		check_output(sim, 24000, led::Output::off,
			     "at06: 18 s of further descent does not retrigger");
	}

	/* AT-05b: the literal rule, no margin and no drop confirmation. */
	{
		apogee::Config literal = apogee::kDefaultConfig;

		literal.min_descent_mm_s = 0;
		literal.min_drop_cm = 0;

		const Simulation sim = run("at05", literal);

		check(sim.deployments == 1,
		      "at05b: literal thresholds still deploy once");
		check(sim.deploy_ms <= 6000,
		      "at05b: literal thresholds fire no later than the defaults");
	}

	/* AT-08: heat inside the deployment window, cooling before the deadline. */
	{
		const Simulation sim = run("at08");

		check(sim.deployments == 1, "at08: one deployment");
		check_output(sim, 7000, led::Output::on,
			     "at08: steady ON before the alarm");
		check_blinking(sim, 9000, "at08: blink overrides deployment");
		check_output(sim, 14000, led::Output::on,
			     "at08: steady resumes after cooling");
		check_output(sim, sim.deadline_ms - 10, led::Output::on,
			     "at08: deployment keeps its original deadline");
		check_output(sim, sim.deadline_ms, led::Output::off,
			     "at08: OFF at the original deadline");
	}

	/* AT-09: the alarm spans the deployment deadline, then clears. */
	{
		const Simulation sim = run("at09");

		check(sim.deployments == 1, "at09: one deployment");
		check_output(sim, 10000, led::Output::on,
			     "at09: steady ON before the alarm");
		check_blinking(sim, 13000, "at09: blink overrides deployment");
		check_blinking(sim, sim.deadline_ms,
			       "at09: blink spans the deadline");
		check_output(sim, 17500, led::Output::off,
			     "at09: cooling after the deadline gives OFF, not ON");
	}

	/* AT-07a: heat only, one complete window. */
	{
		const Simulation sim = run("at07a");

		check(sim.deployments == 0, "at07a: no deployment involved");
		check_blinking(sim, 4000, "at07a: blink starts on the hot sample");
		check_output(sim, 8990, led::Output::blink_off,
			     "at07a: still blinking just before 9.0 s");
		check_output(sim, 9000, led::Output::off,
			     "at07a: off at the end of the 5 s window");
	}

	/* AT-07b: heat beyond the first window, then early cooling. */
	{
		const Simulation sim = run("at07b");

		check(sim.deployments == 0, "at07b: no deployment involved");
		check_blinking(sim, 8900, "at07b: blinking before the first renewal");
		check_blinking(sim, 9100, "at07b: no gap across the renewal");
		check_blinking(sim, 13000, "at07b: still blinking near the second renewal");
		check_output(sim, 14000, led::Output::off,
			     "at07b: cooling clears the alarm immediately");
	}

	/* AT-11: a second simulated flight deploys again, exactly once. */
	{
		const Simulation first = run("at05");
		const Simulation second = run("at05");

		check(first.deployments == 1 && second.deployments == 1,
		      "at11: one deployment per simulated flight");
	}

	std::printf("\n%s (%d failure%s)\n", failures != 0 ? "FAILED" : "PASSED",
		    failures, failures == 1 ? "" : "s");

	return failures != 0 ? 1 : 0;
}
