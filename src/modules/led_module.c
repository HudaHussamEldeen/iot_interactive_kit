#include "modules/led_module.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(led_module, LOG_LEVEL_INF);

static const struct gpio_dt_spec leds[LED_COUNT] = {
	GPIO_DT_SPEC_GET(DT_NODELABEL(led1), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(led2), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(led3), gpios),
};

static bool led_state[LED_COUNT] = {false, false, false};

static void led_self_test(void);

int led_module_init(void)
{
	for (int i = 0; i < LED_COUNT; i++) {
		if (!gpio_is_ready_dt(&leds[i])) {
			LOG_ERR("LED%d GPIO not ready", i + 1);
			return -ENODEV;
		}

		int ret = gpio_pin_configure_dt(&leds[i], GPIO_OUTPUT_INACTIVE);

		if (ret < 0) {
			LOG_ERR("LED%d configure failed: %d", i + 1, ret);
			return ret;
		}
	}

	/* led_self_test(); */
	LOG_INF("LED module ready — GPIO7, GPIO8, GPIO3");
	return 0;
}

static void led_self_test(void)
{
	LOG_INF("LED self-test start");
	for (int i = 1; i <= LED_COUNT; i++) {
		led_module_set(i, true);
		k_msleep(200);
		led_module_set(i, false);
		k_msleep(100);
	}
	LOG_INF("LED self-test done");
}

int led_module_set(int led, bool on)
{
	int ret;

	if (led < 1 || led > LED_COUNT) {
		return -EINVAL;
	}

	ret = gpio_pin_set_dt(&leds[led - 1], on ? 1 : 0);
	if (ret == 0) {
		led_state[led - 1] = on;
	}
	return ret;
}

int led_module_get(int led, bool *on)
{
	if (led < 1 || led > LED_COUNT || on == NULL) {
		return -EINVAL;
	}

	*on = led_state[led - 1];
	return 0;
}
