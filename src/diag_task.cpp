#include "diag_task.hpp"

#include "app_config.hpp"
#include "shared_data.hpp"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

namespace {

K_THREAD_STACK_DEFINE(diag_stack, cfg::kDiagStackBytes);
struct k_thread diag_thread;

void diag_entry(void *, void *, void *)
{
	for (;;) {
		Measurement sample{};
		sensor_data().read(sample);

		const int64_t now = k_uptime_get();
		const int64_t age = sample.valid ? now - sample.timestamp_ms : -1;

		printk("diag:  seq=%u  age=%lld ms  valid=%d  published=%u\n",
		       sample.sequence, static_cast<long long>(age),
		       sample.valid ? 1 : 0, sensor_data().publish_count());

		k_msleep(cfg::kDiagPeriodMs);
	}
}

} // namespace

void diag_task_start()
{
	k_thread_create(&diag_thread, diag_stack,
			K_THREAD_STACK_SIZEOF(diag_stack), diag_entry, nullptr,
			nullptr, nullptr, cfg::kDiagPriority, 0, K_NO_WAIT);
	(void)k_thread_name_set(&diag_thread, "diag");
}
