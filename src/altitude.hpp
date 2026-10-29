#pragma once

#include <cstdint>

/*
 * Barometric altitude from the standard-atmosphere approximation:
 *
 *   h = 44330 * (1 - (P / P0) ^ 0.1903)   metres
 *
 * Pure calculation: no Zephyr headers, no hardware, no state. It is kept
 * separate from the thread so it can be tested on a workstation without a
 * board (tests/altitude_test.cpp).
 */
namespace altitude {

constexpr double kScaleM = 44330.0;
constexpr double kExponent = 0.1903;

/* BMP280 datasheet operating range, in pascals. */
constexpr int32_t kMinPressurePa = 30000;
constexpr int32_t kMaxPressurePa = 110000;

/*
 * Inputs outside the sensor's physical range are rejected rather than
 * converted, so a bus fault cannot become a plausible-looking altitude.
 * The result is rounded to the nearest centimetre.
 */
bool centimeters(int32_t pressure_pa, int32_t reference_pa, int32_t &out_cm);

/* Same calculation in metres, for the tests and the README examples. */
bool meters(int32_t pressure_pa, int32_t reference_pa, double &out_m);

} // namespace altitude
