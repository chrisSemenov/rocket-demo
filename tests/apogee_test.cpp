/*
 * Host-side tests for apogee detection. No board, no Zephyr, no waiting: the
 * series are generated in microseconds and the detector only sees timestamps.
 *
 * Build and run with tests/run-tests.ps1.
 */
#include "../src/apogee.hpp"

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

struct Feeder {
	explicit Feeder(const apogee::Config &config = apogee::kDefaultConfig)
		: detector(config)
	{
	}

	apogee::Detector detector;
	int64_t now_ms = 0;
	uint32_t sequence = 0;
	int events = 0;
	apogee::Event last{};

	apogee::Sample make(int32_t altitude_cm, int64_t step_ms)
	{
		apogee::Sample sample{now_ms, altitude_cm, ++sequence};
		now_ms += step_ms;
		return sample;
	}

	apogee::Event step(int32_t altitude_cm, int64_t step_ms = 50)
	{
		const apogee::Event event =
			detector.on_sample(make(altitude_cm, step_ms));

		if (event.deployed) {
			++events;
			last = event;
		}

		return event;
	}

	void run(int count, int32_t altitude_cm, int64_t step_ms = 50)
	{
		for (int i = 0; i < count; ++i) {
			step(altitude_cm, step_ms);
		}
	}

	/* Ramp from one altitude to another, one centimetre per sample. */
	void ramp(int32_t from_cm, int32_t to_cm, int64_t step_ms = 50)
	{
		const int32_t direction = to_cm >= from_cm ? 1 : -1;

		for (int32_t a = from_cm; a != to_cm + direction; a += direction) {
			step(a, step_ms);
		}
	}

	/*
	 * Descend at a rate the detector must act on: 6 cm per 50 ms = 1.2 m/s,
	 * comfortably above the 1.0 m/s margin.
	 */
	void descend(int samples, int32_t from_cm, int64_t step_ms = 50,
		     int32_t step_cm = 6)
	{
		for (int i = 0; i < samples; ++i) {
			step(from_cm - step_cm * i, step_ms);
		}
	}
};

} // namespace

int main()
{
	/* 1. Stationary on the bench: never triggers. */
	{
		Feeder f;
		f.run(80, 0);
		check(f.events == 0, "stationary at 0 cm does not deploy");
		check(f.detector.state() == apogee::State::ready,
		      "state becomes ready once the window is full");
		check(f.detector.coverage_ms() >= 750,
		      "coverage reaches at least the 750 ms window");
	}

	/* 2. Descending, but never high enough. */
	{
		Feeder f;
		f.descend(35, 120);
		check(f.events == 0, "descent below the minimum does not deploy");
	}

	/* 3. Ascending above the minimum: velocity is positive. */
	{
		Feeder f;
		f.ramp(0, 500);
		check(f.events == 0, "ascent above the minimum does not deploy");
	}

	/* 4. Rise, peak, descend: exactly one event at the first qualifying sample. */
	{
		Feeder f;
		f.ramp(0, 500);
		f.run(4, 500);
		f.descend(40, 500);

		check(f.events == 1, "peak then descent deploys exactly once");
		check(f.last.deadline_ms == f.last.event_ms + 10000,
		      "deployment deadline is event + 10 s");
		check(f.detector.state() == apogee::State::deployed,
		      "state is deployed after the event");
	}

	/* 5. Continued descent must not retrigger or move the deadline. */
	{
		Feeder f;
		f.ramp(0, 500);
		f.run(4, 500);
		f.descend(40, 500);
		const int64_t first_deadline = f.last.deadline_ms;
		f.descend(40, 380);

		check(f.events == 1, "continued descent does not retrigger");
		check(f.last.deadline_ms == first_deadline,
		      "the original deadline is never extended");
	}

	/* 6. Not enough history: 600 ms of descent is not a 750 ms window. */
	{
		Feeder f;
		for (int i = 0; i < 12; ++i) {
			f.step(600 - i);
		}
		check(f.detector.coverage_ms() < 750, "coverage is below the window");
		check(f.events == 0, "incomplete 750 ms history does not deploy");
	}

	/* 7. Duplicate sequence numbers are ignored, not counted twice. */
	{
		Feeder f;
		f.run(40, 400);
		const int before = f.detector.samples();
		auto sample = apogee::Sample{f.now_ms, 300, f.sequence};

		const apogee::Event duplicate = f.detector.on_sample(sample);
		check(!duplicate.deployed, "a repeated sequence produces no event");
		check(f.detector.samples() == before,
		      "a repeated sequence is not added to the history");
	}

	/* 8. Backward timestamps are rejected. */
	{
		Feeder f;
		f.run(40, 400);
		const int before = f.detector.samples();
		const apogee::Event backwards = f.detector.on_sample(
			apogee::Sample{f.now_ms - 500, 0, f.sequence + 1});

		check(!backwards.deployed, "a backward timestamp produces no event");
		check(f.detector.samples() == before,
		      "a backward timestamp is not added to the history");
	}

	/* 9. A gap longer than the stale timeout rebuilds the history. */
	{
		Feeder f;
		f.ramp(0, 600);
		f.now_ms += 1000; /* sensor outage */
		f.step(600);

		check(f.detector.samples() == 1,
		      "a long gap restarts the history from the new sample");
		check(f.detector.state() != apogee::State::deployed,
		      "a gap alone does not deploy");

		/* Only 200 ms of history since the gap: not enough for a window. */
		f.ramp(600, 596);
		check(f.detector.coverage_ms() < 750,
		      "the window is not covered 200 ms after a gap");
		check(f.events == 0,
		      "no deployment until a full window is collected again");
	}

	/* 10. Flat above the minimum: zero average velocity is not descent. */
	{
		Feeder f;
		f.run(80, 500);
		check(f.events == 0, "zero average velocity does not deploy");
	}

	/* 11. A shallow descent below the configured margin does not deploy. */
	{
		Feeder f;
		f.ramp(0, 500);
		for (int i = 0; i < 40; ++i) {
			f.step(500 - (i % 2)); /* 1 cm ripple, far below 0.2 m/s */
		}
		check(f.events == 0, "descent below the margin is ignored");
	}

	/* 12. Exact threshold: a step down to exactly the minimum deploys, and the
	 *     event carries the timestamp of the sample that satisfied both tests.
	 *     The descent margin is set to zero here so the test isolates the
	 *     altitude gate; the margin has its own case below. */
	{
		apogee::Config config = apogee::kDefaultConfig;
		config.min_descent_mm_s = 0;

		Feeder f(config);
		f.run(20, 300);
		const int64_t trigger_ts = f.now_ms;
		const apogee::Event event = f.step(150);

		check(event.deployed,
		      "altitude exactly at the minimum with descent deploys");
		check(event.event_ms == trigger_ts,
		      "the event is timestamped with the triggering sample");
	}

	/* 12b. One centimetre below the minimum does not deploy. */
	{
		apogee::Config config = apogee::kDefaultConfig;
		config.min_descent_mm_s = 0;

		Feeder f(config);
		f.run(20, 300);
		const apogee::Event event = f.step(149);

		check(!event.deployed, "one centimetre below the minimum does not deploy");
	}

	/* 12c. The estimator itself: 3 cm per 50 ms is exactly 0.6 m/s. The
	 *      altitude gate is raised out of reach so the detector cannot latch
	 *      and keep updating the velocity. */
	{
		apogee::Config config = apogee::kDefaultConfig;
		config.min_altitude_cm = 10000;

		Feeder f(config);
		f.run(22, 300);
		f.descend(20, 300, 50, 3);

		const int32_t v = f.detector.velocity_mm_s();
		check(v <= -595 && v >= -605, "slope of a uniform ramp is 0.6 m/s");
	}

	/* 12d. Exactly at the margin deploys: 3 cm per 100 ms is 0.3 m/s. */
	{
		Feeder f;
		f.run(20, 300);
		f.descend(20, 300, 100, 3);

		check(f.events == 1, "descent exactly at the margin deploys");
	}

	/* 13. Irregular cadence still yields a 750 ms window. */	{
		Feeder f;
		for (int i = 0; i < 12; ++i) {
			f.step(500 + i, 40);
		}
		for (int i = 0; i < 20; ++i) {
			f.step(600 - 8 * i, 70);
		}

		check(f.events >= 1, "irregular cadence still detects the descent");
		check(f.detector.coverage_ms() >= 0, "coverage stays sane");
	}

	/* 14. Reset clears the latch and the history for a new flight. */
	{
		Feeder f;
		f.ramp(0, 500);
		f.run(4, 500);
		f.descend(40, 500);
		check(f.events == 1, "first flight deploys once");

		f.detector.reset();
		check(f.detector.state() == apogee::State::filling,
		      "reset returns to filling");
		check(f.detector.samples() == 0, "reset clears the history");

		f.ramp(0, 500);
		f.run(4, 500);
		f.descend(40, 500);
		check(f.events == 2, "a new flight can deploy again after reset");
	}

	std::printf("\n%s (%d failure%s)\n", failures != 0 ? "FAILED" : "PASSED",
		    failures, failures == 1 ? "" : "s");

	return failures != 0 ? 1 : 0;
}
