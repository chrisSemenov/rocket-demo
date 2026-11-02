#include "rearm.hpp"

namespace rearm {

Button::Button(int64_t debounce_ms)
	: debounce_ms_(debounce_ms > 0 ? debounce_ms : 0), pending_(false),
	  pending_since_ms_(0), stable_(false), reported_(false),
	  saw_release_(false)
{
}

bool Button::update(bool raw_active, int64_t now_ms)
{
	/*
	 * Seeing the released level is what arms the detector. A pin held low
	 * from reset therefore never produces a press until it is actually
	 * released once.
	 */
	if (!raw_active) {
		saw_release_ = true;
	}

	/*
	 * A change starts a new stability window. Bounces reset it, so only a
	 * level that survives the whole interval is accepted.
	 */
	if (raw_active != pending_) {
		pending_ = raw_active;
		pending_since_ms_ = now_ms;
	}

	if (pending_ == stable_ || now_ms - pending_since_ms_ < debounce_ms_) {
		return false;
	}

	stable_ = pending_;

	if (!stable_) {
		/* Released: the next press may report again. */
		reported_ = false;
		return false;
	}

	if (!saw_release_) {
		return false;
	}

	if (reported_) {
		/* Held down: one press, one report. */
		return false;
	}

	reported_ = true;
	return true;
}

void Button::reset()
{
	pending_ = false;
	pending_since_ms_ = 0;
	stable_ = false;
	reported_ = false;
	saw_release_ = false;
}

} // namespace rearm
