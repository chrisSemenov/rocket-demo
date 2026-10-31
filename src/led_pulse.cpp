#include "led_pulse.hpp"

namespace led {

void ReadyPulse::start(int64_t now)
{
	active_ = true;
	step_ = 0;
	next_at_ = now;
	lit_ = false;
}

void ReadyPulse::cancel()
{
	active_ = false;
	lit_ = false;
}

void ReadyPulse::advance(int64_t now)
{
	if (!active_ || now < next_at_) {
		return;
	}

	switch (step_) {
	case 0:
		lit_ = true;
		next_at_ = now + kReadyOnMs;
		++step_;
		break;
	case 1:
		lit_ = false;
		next_at_ = now + kReadyGapMs;
		++step_;
		break;
	case 2:
		lit_ = true;
		next_at_ = now + kReadyOnMs;
		++step_;
		break;
	default:
		lit_ = false;
		active_ = false;
		break;
	}
}

bool ReadyPulse::active() const
{
	return active_;
}

bool ReadyPulse::lit() const
{
	return lit_;
}

} // namespace led
