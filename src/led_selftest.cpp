#include "led_selftest.hpp"

#include "app_config.hpp"
#include "shared_data.hpp"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

namespace {

K_THREAD_STACK_DEFINE(selftest_stack, cfg::kLedStackBytes);
struct k_thread selftest_thread;

/*
 * Scripted timeline, relative to thread start. It covers every branch of the
 * arbitration policy and the latch, and finishes in about 30 s:
 *
 *   2 s   deployment accepted            -> steady on until 12 s
 *   4 s   heat starts                    -> blink overrides deployment
 *   9 s   heat cleared early             -> steady on again, same deadline
 *  12 s   deployment expires             -> off
 *  14 s   heat only                      -> blink with no deployment
 *  17 s   cooled early                   -> off (nothing to fall back to)
 *  20 s   heat again                     -> blink
 *  25 s   cooled at the window's end     -> off
 *  26 s   second deployment request      -> must be ignored (latched)
 */
struct Step {
	int64_t at_ms;
	const char *what;
	int kind;
};

constexpr int kDeploy = 0;
constexpr int kHeat = 1;
constexpr int kCool = 2;

const Step kScript[] = {
	{2000, "deployment requested, 10 s window", kDeploy},
	{4000, "heat starts (overrides deployment)", kHeat},
	{9000, "cooled early (deployment resumes)", kCool},
	{14000, "heat only, no deployment", kHeat},
	{17000, "cooled early (nothing to resume)", kCool},
	{20000, "heat again", kHeat},
	{25000, "cooled at the end of the window", kCool},
	{26000, "second deployment request (must be ignored)", kDeploy},
};

void selftest_entry(void *, void *, void *)
{
	const int64_t start = k_uptime_get();

	printk("SELFTEST: LED request sequence, %u steps, ~30 s\n",
	       static_cast<unsigned int>(sizeof(kScript) / sizeof(kScript[0])));

	for (const Step &step : kScript) {
		const int64_t wait = (start + step.at_ms) - k_uptime_get();

		if (wait > 0) {
			k_msleep(static_cast<int32_t>(wait));
		}

		const int64_t now = k_uptime_get();

		switch (step.kind) {
		case kDeploy: {
			const bool first =
				led_requests().deployment_events() == 0;

			led_requests().request_deployment(
				now, now + led::kDeploymentIndicationMs);

			printk("SELFTEST: [%lld ms] %s%s\n",
			       static_cast<long long>(step.at_ms), step.what,
			       first ? "" : " -- latch check");
			break;
		}
		case kHeat:
			(void)led_requests().request_heat(
				now, now + led::kHeatAlarmMs);
			printk("SELFTEST: [%lld ms] %s\n",
			       static_cast<long long>(step.at_ms), step.what);
			break;
		case kCool:
			led_requests().clear_heat();
			printk("SELFTEST: [%lld ms] %s\n",
			       static_cast<long long>(step.at_ms), step.what);
			break;
		}
	}

	k_msleep(2000);

	printk("SELFTEST: done, deployment events=%u (expected 1)\n",
	       led_requests().deployment_events());

	for (;;) {
		k_msleep(10000);
	}
}

} // namespace

void led_selftest_start()
{
	k_thread_create(&selftest_thread, selftest_stack,
			K_THREAD_STACK_SIZEOF(selftest_stack), selftest_entry,
			nullptr, nullptr, nullptr, cfg::kSelftestPriority, 0,
			K_NO_WAIT);
	(void)k_thread_name_set(&selftest_thread, "selftest");
}
