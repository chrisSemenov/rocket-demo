#pragma once

#include <cstdint>

/*
 * LED arbitration, with no Zephyr, GPIO or clock dependency: a pure function of
 * "now" and the current requests. That is what makes the priority rules
 * testable without hardware (tests/led_logic_test.cpp).
 *
 * Priority, highest first:
 *   1. an active heat alarm          -> 500 ms blink
 *   2. an unexpired deployment       -> steady on
 *   3. otherwise                     -> off
 *
 * Every deadline is absolute and owned by the requester. This code never sets,
 * extends or clears one; it only reads them.
 */
namespace led {

enum class Output { off, on, blink_on, blink_off };

struct Request {
	bool deployment_active;
	int64_t deployment_deadline_ms;

	bool heat_active;
	int64_t heat_start_ms;
	int64_t heat_deadline_ms;
};

/* Indication policy, shared by the producers and the tests. */
constexpr int64_t kDeploymentIndicationMs = 10000;
constexpr int64_t kHeatAlarmMs = 5000;
constexpr int64_t kBlinkHalfPeriodMs = 250;

/*
 * A window is active while start <= now < deadline: the deadline instant
 * itself is already expired. Documented and tested at the boundary.
 */
bool in_window(int64_t now_ms, int64_t start_ms, int64_t deadline_ms);

Output decide(int64_t now_ms, const Request &request);

} // namespace led
