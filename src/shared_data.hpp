#pragma once

#include <zephyr/kernel.h>

#include <cstdint>

#include "led_logic.hpp"

/*
 * One coherent acquisition: pressure, temperature, timestamp and sequence are
 * published together, so a reader can never pair pressure from one sample with
 * temperature from the next.
 */
struct Measurement {
	int32_t pressure_pa;
	int32_t temperature_mc; /* milli-degrees Celsius */
	uint32_t sequence;      /* advances only on a successful publication */
	int64_t timestamp_ms;   /* k_uptime_get() at acquisition */
	bool valid;
	int8_t error; /* last fetch return code, 0 when valid */
};

/*
 * One altitude result, tagged with the measurement it was derived from. The
 * source timestamp and sequence are copied, never regenerated, so a consumer
 * can tell how old the pressure behind the number is.
 */
struct Altitude {
	int32_t altitude_cm;
	uint32_t source_sequence;
	int64_t source_timestamp_ms;
	int32_t reference_pa;     /* frozen baseline P0, 0 until calibrated */
	uint16_t baseline_samples; /* progress while calibrating */
	bool valid;
	bool calibrated;
};

/* Producer-side timing counters, republished as a snapshot for the console. */
struct SensorStats {
	uint32_t samples;
	uint32_t errors;
	uint32_t recoveries;
	uint32_t overruns;
	int32_t fetch_min_ms;
	int32_t fetch_avg_ms;
	int32_t fetch_max_ms;
	int32_t interval_min_ms;
	int32_t interval_avg_ms;
	int32_t interval_max_ms;
};

/*
 * Latest-value store. Every reader copies out under a short mutex; a read never
 * removes or consumes anything, so any number of consumers can observe the same
 * sequence at their own rate. Locks are never held across I2C, printing or
 * computation.
 */
class SensorDataStore {
public:
	void init();

	void publish(const Measurement &sample);
	void read(Measurement &out);

	void publish_stats(const SensorStats &stats);
	void read_stats(SensorStats &out);

	uint32_t publish_count();

private:
	struct k_mutex lock_;
	Measurement measurement_;
	SensorStats stats_;
	uint32_t published_;
	bool ready_;
};

/* Single owned instance: the sensor thread writes, everyone else only reads. */
SensorDataStore &sensor_data();

/*
 * Altitude store, same contract as SensorDataStore: the altitude thread is the
 * only writer, every other thread reads copies. It is separate from the
 * measurement store so a consumer of altitude never depends on pressure.
 */
class AltitudeStore {
public:
	void init();

	void publish(const Altitude &sample);
	void read(Altitude &out);

private:
	struct k_mutex lock_;
	Altitude altitude_;
	bool ready_;
};

AltitudeStore &altitude_data();

/*
 * The two LED requests, in one snapshot so the manager sees a coherent pair.
 * Deployment and heat have different writers (apogee, temperature monitor), but
 * each has its own publish method that takes the lock, so neither does a
 * read-modify-write across the other.
 *
 * The deployment event is latched here: the first accepted event sets the
 * deadline and later ones are ignored, so no amount of descending samples can
 * extend the indication (FLT-04, LED-06).
 */
class LedRequestStore {
public:
	void init();

	void request_deployment(int64_t event_ms, int64_t deadline_ms);
	bool request_heat(int64_t start_ms, int64_t deadline_ms);
	void clear_heat();

	/*
	 * One-shot "the system is ready" flag. The apogee thread sets it once the
	 * history is full; the LED manager takes it (read and clear) so the
	 * indication happens a single time per boot.
	 */
	void signal_ready();
	bool take_ready();

	void read(led::Request &out);
	uint32_t deployment_events();

private:
	struct k_mutex lock_;
	led::Request request_;
	uint32_t deployment_events_;
	bool ready_signalled_;
	bool ready_;
};

LedRequestStore &led_requests();
