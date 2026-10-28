#include "shared_data.hpp"

namespace {

/*
 * Deliberately a plain static object with no constructor: Zephyr does not need
 * to run C++ static initialisers for it, and the mutex is brought up by
 * init() from main() before any thread starts.
 */
SensorDataStore g_store;

} // namespace

SensorDataStore &sensor_data()
{
	return g_store;
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
