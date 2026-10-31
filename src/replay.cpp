#include "replay.hpp"

#include "app_config.hpp"
#include "shared_data.hpp"
#include "traces_data.hpp"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <string.h>

namespace {

K_THREAD_STACK_DEFINE(replay_stack, cfg::kReplayStackBytes);
struct k_thread replay_thread;

/* One trace per build, selected in Kconfig. */
#if defined(CONFIG_APP_TRACE_AT04A)
constexpr const char *kTraceName = "at04a";
#elif defined(CONFIG_APP_TRACE_AT04B)
constexpr const char *kTraceName = "at04b";
#elif defined(CONFIG_APP_TRACE_AT08)
constexpr const char *kTraceName = "at08";
#elif defined(CONFIG_APP_TRACE_AT09)
constexpr const char *kTraceName = "at09";
#elif defined(CONFIG_APP_TRACE_AT07A)
constexpr const char *kTraceName = "at07a";
#elif defined(CONFIG_APP_TRACE_AT07B)
constexpr const char *kTraceName = "at07b";
#else
constexpr const char *kTraceName = "at05";
#endif

const Trace *find_trace(const char *name)
{
	for (unsigned int i = 0; i < kTraceCount; ++i) {
		if (strcmp(kTraces[i].name, name) == 0) {
			return &kTraces[i];
		}
	}

	return nullptr;
}

void replay_entry(void *, void *, void *)
{
	const Trace *trace = find_trace(kTraceName);

	if (trace == nullptr) {
		printk("REPLAY TEST: trace '%s' not found\n", kTraceName);
		return;
	}

	printk("REPLAY TEST: %s - %s (%u samples, %u ms)\n", trace->name,
	       trace->note, trace->count,
	       trace->count > 0 ? trace->samples[trace->count - 1].time_ms : 0);

	int64_t next = k_uptime_get();
	uint32_t sequence = 0;

	for (unsigned int i = 0; i < trace->count; ++i) {
		const TraceSample &sample = trace->samples[i];

		Measurement measurement{};

		/*
		 * The timestamp is the replay moment, not the trace time: consumers
		 * measure staleness and the 750 ms window against k_uptime_get().
		 * The cadence comes from the trace itself, held in real time so the
		 * 10 s indication and the 250 ms blink are genuine.
		 */
		measurement.timestamp_ms = k_uptime_get();
		measurement.pressure_pa = sample.pressure_pa;
		measurement.temperature_mc =
			static_cast<int32_t>(sample.temperature_c_x100) * 10;
		measurement.sequence = ++sequence;
		measurement.valid = true;
		measurement.error = 0;

		sensor_data().publish(measurement);

		const int64_t step =
			(i + 1 < trace->count)
				? static_cast<int64_t>(trace->samples[i + 1].time_ms) -
					  static_cast<int64_t>(sample.time_ms)
				: static_cast<int64_t>(cfg::kSamplePeriodMs);

		next += step > 0 ? step : cfg::kSamplePeriodMs;

		if (k_uptime_get() > next) {
			next = k_uptime_get() + (step > 0 ? step : cfg::kSamplePeriodMs);
		} else {
			k_sleep(K_TIMEOUT_ABS_MS(next));
		}
	}

	/*
	 * The trace is over: publish an invalid snapshot rather than holding the
	 * last value, so consumers cannot treat the end of a replay as live data.
	 */
	Measurement end{};

	end.timestamp_ms = k_uptime_get();
	end.sequence = sequence;
	end.valid = false;

	sensor_data().publish(end);

	printk("REPLAY TEST: done, %u samples published\n", sequence);
}

} // namespace

const char *replay_trace_name()
{
	return kTraceName;
}

void replay_task_start()
{
	k_thread_create(&replay_thread, replay_stack,
			K_THREAD_STACK_SIZEOF(replay_stack), replay_entry,
			nullptr, nullptr, nullptr, cfg::kReplayPriority, 0,
			K_NO_WAIT);
	(void)k_thread_name_set(&replay_thread, "replay");
}
