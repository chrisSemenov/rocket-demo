/*
 * Sanity checks on the generated replay traces. No board: it only verifies that
 * the data the firmware embeds is well formed, so a bad regeneration is caught
 * on a PC rather than on the bench.
 */
#include "../src/traces_data.hpp"

#include <cmath>
#include <cstring>
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

/* Inverse of the documented altitude model, at the bench pressure. */
double altitude_m(int32_t pressure_pa, double p0)
{
	const double ratio = static_cast<double>(pressure_pa) / p0;
	return 44330.0 * (1.0 - std::pow(ratio, 0.1903));
}

} // namespace

int main()
{
	check(kTraceCount == 7, "seven traces are embedded");

	for (unsigned int t = 0; t < kTraceCount; ++t) {
		const Trace &trace = kTraces[t];

		bool monotonic = true;
		bool in_range = true;

		for (unsigned int i = 0; i < trace.count; ++i) {
			const TraceSample &sample = trace.samples[i];

			if (i > 0 && sample.time_ms <= trace.samples[i - 1].time_ms) {
				monotonic = false;
			}

			if (sample.pressure_pa < 30000 ||
			    sample.pressure_pa > 110000 ||
			    sample.temperature_c_x100 < -4000 ||
			    sample.temperature_c_x100 > 8500) {
				in_range = false;
			}
		}

		char label[96];

		std::snprintf(label, sizeof(label), "%s: timestamps increase",
			      trace.name);
		check(monotonic, label);

		std::snprintf(label, sizeof(label), "%s: samples are in range",
			      trace.name);
		check(in_range, label);
	}

	/* The first sample of every trace is the stationary baseline at P0. */
	for (unsigned int t = 0; t < kTraceCount; ++t) {
		const Trace &trace = kTraces[t];

		check(trace.count > 40, "trace starts with a 2 s baseline");

		const double p0 = static_cast<double>(trace.samples[0].pressure_pa);
		const double height = altitude_m(trace.samples[0].pressure_pa, p0);

		if (std::fabs(height) > 0.01) {
			std::printf("FAIL %s: baseline is not the reference\n",
				    trace.name);
			++failures;
		}
	}

	/* at05 must actually rise above the configured minimum (1.5 m). */
	{
		const Trace *at05 = nullptr;

		for (unsigned int t = 0; t < kTraceCount; ++t) {
			if (std::strcmp(kTraces[t].name, "at05") == 0) {
				at05 = &kTraces[t];
			}
		}

		if (at05 == nullptr) {
			std::printf("FAIL at05 is missing\n");
			++failures;
		} else {
			const double p0 = static_cast<double>(at05->samples[0].pressure_pa);
			double peak = 0.0;

			for (unsigned int i = 0; i < at05->count; ++i) {
				const double h =
					altitude_m(at05->samples[i].pressure_pa, p0);

				if (h > peak) {
					peak = h;
				}
			}

			check(peak > 5.0, "at05 reaches about 6 m");
		}
	}

	std::printf("\n%s (%d failure%s)\n", failures != 0 ? "FAILED" : "PASSED",
		    failures, failures == 1 ? "" : "s");

	return failures != 0 ? 1 : 0;
}
