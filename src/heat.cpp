#include "heat.hpp"

namespace heat {

Monitor::Monitor(const Config &config)
	: config_(config), state_(State::cool), request_{false, 0, 0},
	  last_sequence_(0), have_sequence_(false)
{
}

Decision Monitor::on_sample(int32_t temperature_mc, int64_t timestamp_ms,
			    uint32_t sequence)
{
	Decision decision{false, request_};

	if (have_sequence_ && sequence == last_sequence_) {
		/* The thread polls faster than the sensor publishes. */
		return decision;
	}

	last_sequence_ = sequence;
	have_sequence_ = true;

	const bool hot = temperature_mc >= config_.threshold_mc;
	const bool cold = temperature_mc < config_.threshold_mc - config_.hysteresis_mc;

	if (state_ == State::cool) {
		if (hot) {
			state_ = State::hot;
			request_.active = true;
			request_.start_ms = timestamp_ms;
			request_.deadline_ms = timestamp_ms + config_.alarm_ms;
			decision.changed = true;
		}
	} else {
		if (cold) {
			state_ = State::cool;
			request_.active = false;
			decision.changed = true;
		} else if (hot && timestamp_ms + config_.renew_guard_ms >=
					   request_.deadline_ms) {
			/*
			 * Still hot and the window is about to lapse: renew before it
			 * does. Renewing exactly at expiry leaves a visible dark gap,
			 * because the LED manager expires the request on the deadline and
			 * only sees the renewal one poll later.
			 *
			 * The deadline advances by whole windows to keep the cadence; if
			 * the check is very late, it starts fresh from now instead of
			 * replaying windows that never happened.
			 */
			int64_t next = request_.deadline_ms + config_.alarm_ms;

			if (next <= timestamp_ms) {
				next = timestamp_ms + config_.alarm_ms;
			}

			request_.deadline_ms = next;
			decision.changed = true;
		}
	}

	decision.request = request_;

	return decision;
}

Decision Monitor::on_missing()
{
	/*
	 * Fail-safe: no data is not evidence of cooling. Hold the state; an active
	 * window still expires on its own deadline.
	 */
	return Decision{false, request_};
}

State Monitor::state() const
{
	return state_;
}

Request Monitor::request() const
{
	return request_;
}

void Monitor::reset()
{
	state_ = State::cool;
	request_ = Request{false, 0, 0};
	last_sequence_ = 0;
	have_sequence_ = false;
}

} // namespace heat
