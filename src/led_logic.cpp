#include "led_logic.hpp"

namespace led {

bool in_window(int64_t now_ms, int64_t start_ms, int64_t deadline_ms)
{
	return now_ms >= start_ms && now_ms < deadline_ms;
}

Output decide(int64_t now_ms, const Request &request)
{
	if (request.heat_active &&
	    in_window(now_ms, request.heat_start_ms, request.heat_deadline_ms)) {
		/*
		 * 250 ms on, 250 ms off, anchored to the start of the alarm so the
		 * phase does not depend on when this function happens to be called.
		 */
		const int64_t elapsed = now_ms - request.heat_start_ms;
		const bool on = ((elapsed / kBlinkHalfPeriodMs) % 2) == 0;

		return on ? Output::blink_on : Output::blink_off;
	}

	if (request.deployment_active &&
	    now_ms < request.deployment_deadline_ms) {
		return Output::on;
	}

	return Output::off;
}

} // namespace led
