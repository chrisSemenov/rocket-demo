/*
 * Host-side tests for the pure altitude calculation. No board, no Zephyr.
 *
 * Build and run with tests/run-tests.ps1.
 *
 * The reference values were computed independently in 40-digit decimal
 * arithmetic from h = 44330 * (1 - (P/P0)^0.1903), not by this implementation.
 */
#include "../src/altitude.hpp"

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

void check_near(const char *label, double actual, double expected, double tolerance)
{
	const double difference = std::fabs(actual - expected);

	if (difference > tolerance) {
		std::printf("FAIL %-28s actual=%.6f expected=%.6f tol=%.6f\n", label,
			    actual, expected, tolerance);
		++failures;
	} else {
		std::printf("ok   %-28s %10.6f m\n", label, actual);
	}
}

void check_rejected(const char *label, int32_t pressure, int32_t reference)
{
	int32_t cm = 0;

	if (altitude::centimeters(pressure, reference, cm)) {
		std::printf("FAIL %-28s accepted %d Pa -> %d cm\n", label, pressure, cm);
		++failures;
	} else {
		std::printf("ok   %-28s rejected\n", label);
	}
}

void check_accepted(const char *label, int32_t pressure, int32_t reference)
{
	int32_t cm = 0;

	if (altitude::centimeters(pressure, reference, cm)) {
		std::printf("ok   %-28s accepted (%d cm)\n", label, cm);
	} else {
		std::printf("FAIL %-28s rejected a valid pressure\n", label);
		++failures;
	}
}

struct Case {
	const char *label;
	int32_t pressure;
	int32_t reference;
	double expected_m;
};

} // namespace

int main()
{
	const Case cases[] = {
		{"P == P0", 101325, 101325, 0.0},
		{"standard 1000 hPa", 100000, 101325, 110.903981},
		{"standard 950 hPa", 95000, 101325, 540.431869},
		{"standard 900 hPa", 90000, 101325, 988.672468},
		{"bench 95000 -> 94000", 94000, 95000, 89.180839},
		{"bench 95000 -> 94500", 94500, 95000, 44.494903},
		{"bench above reference", 95100, 95000, -8.876217},
	};

	for (const Case &c : cases) {
		double metres = 0.0;

		if (!altitude::meters(c.pressure, c.reference, metres)) {
			std::printf("FAIL %-28s unexpectedly rejected\n", c.label);
			++failures;
			continue;
		}

		check_near(c.label, metres, c.expected_m, 0.01);
	}

	/* Lower pressure must mean higher altitude. */
	double low = 0.0;
	double high = 0.0;
	if (altitude::meters(94500, 95000, low) && altitude::meters(94550, 95000, high) &&
	    low > high) {
		std::printf("ok   %-28s %.3f > %.3f m\n", "monotonic in pressure", low, high);
	} else {
		std::printf("FAIL %-28s ordering is wrong\n", "monotonic in pressure");
		++failures;
	}

	/* The integer output must agree with the metre calculation. */
	{
		double metres = 0.0;
		int32_t cm = 0;

		if (altitude::meters(94000, 95000, metres) &&
		    altitude::centimeters(94000, 95000, cm) &&
		    std::fabs(static_cast<double>(cm) - metres * 100.0) <= 1.0) {
			std::printf("ok   %-28s %d cm\n", "cm agrees with metres", cm);
		} else {
			std::printf("FAIL %-28s disagreement\n", "cm agrees with metres");
			++failures;
		}
	}

	check_rejected("zero pressure", 0, 101325);
	check_rejected("negative pressure", -100, 101325);
	check_rejected("below sensor range", 29999, 101325);
	check_rejected("above sensor range", 110001, 101325);
	check_rejected("zero reference", 95000, 0);
	check_rejected("reference out of range", 95000, 120000);

	check_accepted("lowest in range", 30000, 101325);
	check_accepted("highest in range", 110000, 101325);

	std::printf("\n%s (%d failure%s)\n", failures != 0 ? "FAILED" : "PASSED",
		    failures, failures == 1 ? "" : "s");

	return failures != 0 ? 1 : 0;
}
