#include "altitude.hpp"

/*
 * picolibc's C maths header. Zephyr builds C++ with -nostdinc++ against its
 * minimal C++ library, which has no <cmath>, so the C names are the only ones
 * available here.
 */
#include <math.h>

namespace {

bool plausible(int32_t pressure_pa, int32_t reference_pa)
{
	return pressure_pa >= altitude::kMinPressurePa &&
	       pressure_pa <= altitude::kMaxPressurePa &&
	       reference_pa >= altitude::kMinPressurePa &&
	       reference_pa <= altitude::kMaxPressurePa;
}

} // namespace

namespace altitude {

bool meters(int32_t pressure_pa, int32_t reference_pa, double &out_m)
{
	if (!plausible(pressure_pa, reference_pa)) {
		return false;
	}

	/*
	 * log1p/expm1 keep their precision when P is close to P0, where a plain
	 * 1 - pow() subtracts two nearly equal numbers. On the bench the whole
	 * signal lives in that region: 1 hPa is only about 8 m.
	 */
	const double delta_ratio =
		(static_cast<double>(pressure_pa) - static_cast<double>(reference_pa)) /
		static_cast<double>(reference_pa);
	const double value = -kScaleM * expm1(kExponent * log1p(delta_ratio));

	if (!isfinite(value)) {
		return false;
	}

	out_m = value;
	return true;
}

bool centimeters(int32_t pressure_pa, int32_t reference_pa, int32_t &out_cm)
{
	double metres = 0.0;

	if (!meters(pressure_pa, reference_pa, metres)) {
		return false;
	}

	const double value = metres * 100.0;

	if (!isfinite(value) || value > 2147483647.0 || value < -2147483648.0) {
		return false;
	}

	/* Round half away from zero without depending on llround(). */
	out_cm = static_cast<int32_t>(value >= 0.0 ? value + 0.5 : value - 0.5);

	return true;
}

} // namespace altitude
