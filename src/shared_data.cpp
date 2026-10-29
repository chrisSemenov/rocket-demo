#include "shared_data.hpp"

namespace {

/*
 * Deliberately a plain static object with no constructor: Zephyr does not need
 * to run C++ static initialisers for it, and the mutex is brought up by
 * init() from main() before any thread starts.
 */
SensorDataStore g_store;
AltitudeStore g_altitude_store;
LedRequestStore g_led_requests;

} // namespace

SensorDataStore &sensor_data()
{
	return g_store;
}

AltitudeStore &altitude_data()
{
	return g_altitude_store;
}

LedRequestStore &led_requests()
{
	return g_led_requests;
}

void SensorDataStore::init()
{
	k_mutex_init(&lock_);

	measurement_ = Measurement{0, 0, 0, 0, false, 0};
	stats_ = SensorStats{};
	published_ = 0;
	ready_ = true;
}

void SensorDataStore::publish(const Measurement &sample)
{
	k_mutex_lock(&lock_, K_FOREVER);
	measurement_ = sample;

	if (sample.valid) {
		published_ = sample.sequence;
	}

	k_mutex_unlock(&lock_);
}

void SensorDataStore::read(Measurement &out)
{
	k_mutex_lock(&lock_, K_FOREVER);
	out = measurement_;
	const bool ready = ready_;
	k_mutex_unlock(&lock_);

	if (!ready) {
		out = Measurement{0, 0, 0, 0, false, 0};
	}
}

void SensorDataStore::publish_stats(const SensorStats &stats)
{
	k_mutex_lock(&lock_, K_FOREVER);
	stats_ = stats;
	k_mutex_unlock(&lock_);
}

void SensorDataStore::read_stats(SensorStats &out)
{
	k_mutex_lock(&lock_, K_FOREVER);
	out = stats_;
	k_mutex_unlock(&lock_);
}

uint32_t SensorDataStore::publish_count()
{
	k_mutex_lock(&lock_, K_FOREVER);
	const uint32_t count = published_;
	k_mutex_unlock(&lock_);

	return count;
}

void AltitudeStore::init()
{
	k_mutex_init(&lock_);

	altitude_ = Altitude{0, 0, 0, 0, 0, false, false};
	ready_ = true;
}

void AltitudeStore::publish(const Altitude &sample)
{
	k_mutex_lock(&lock_, K_FOREVER);
	altitude_ = sample;
	k_mutex_unlock(&lock_);
}

void AltitudeStore::read(Altitude &out)
{
	k_mutex_lock(&lock_, K_FOREVER);
	out = altitude_;
	const bool ready = ready_;
	k_mutex_unlock(&lock_);

	if (!ready) {
		out = Altitude{0, 0, 0, 0, 0, false, false};
	}
}

void LedRequestStore::init()
{
	k_mutex_init(&lock_);

	request_ = led::Request{false, 0, false, 0, 0};
	deployment_events_ = 0;
	ready_ = true;
}

void LedRequestStore::request_deployment(int64_t event_ms, int64_t deadline_ms)
{
	k_mutex_lock(&lock_, K_FOREVER);

	if (!request_.deployment_active) {
		request_.deployment_active = true;
		request_.deployment_deadline_ms = deadline_ms;
		++deployment_events_;
	}

	k_mutex_unlock(&lock_);

	/* event_ms is logged by the caller; the deadline is what arbitrates. */
	(void)event_ms;
}

bool LedRequestStore::request_heat(int64_t start_ms, int64_t deadline_ms)
{
	bool accepted = false;

	k_mutex_lock(&lock_, K_FOREVER);

	/*
	 * Renewing an existing window keeps its original anchor: the blink phase
	 * must not jump because the alarm was extended.
	 */
	if (!request_.heat_active) {
		request_.heat_active = true;
		request_.heat_start_ms = start_ms;
		accepted = true;
	}

	request_.heat_deadline_ms = deadline_ms;
	k_mutex_unlock(&lock_);

	return accepted;
}

void LedRequestStore::clear_heat()
{
	k_mutex_lock(&lock_, K_FOREVER);
	request_.heat_active = false;
	k_mutex_unlock(&lock_);
}

void LedRequestStore::read(led::Request &out)
{
	k_mutex_lock(&lock_, K_FOREVER);
	out = request_;
	const bool ready = ready_;
	k_mutex_unlock(&lock_);

	if (!ready) {
		out = led::Request{false, 0, false, 0, 0};
	}
}

uint32_t LedRequestStore::deployment_events()
{
	k_mutex_lock(&lock_, K_FOREVER);
	const uint32_t count = deployment_events_;
	k_mutex_unlock(&lock_);

	return count;
}
