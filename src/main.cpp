#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

static const gpio_dt_spec led =
    GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

int main(void)
{
    if (!gpio_is_ready_dt(&led)) {
        return 0;
    }

    if (gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE) < 0) {
        return 0;
    }

    while (true) {
        if (gpio_pin_toggle_dt(&led) < 0) {
            return 0;
        }

        k_msleep(500);
    }
}
