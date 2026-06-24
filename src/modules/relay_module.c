#include "modules/relay_module.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(relay_module, LOG_LEVEL_INF);

static const struct gpio_dt_spec relays[2] = {
	GPIO_DT_SPEC_GET(DT_NODELABEL(relay1), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(relay2), gpios),
};

static bool relay_state[2] = {false, false};

static void relay_self_test(void);

int relay_module_init(void)
{
	for (int i = 0; i < 2; i++) {
		if (!gpio_is_ready_dt(&relays[i])) {
			LOG_ERR("Relay%d GPIO not ready", i + 1);
			return -ENODEV;
		}

		int ret = gpio_pin_configure_dt(&relays[i], GPIO_OUTPUT_INACTIVE);

		if (ret < 0) {
			LOG_ERR("Relay%d configure failed: %d", i + 1, ret);
			return ret;
		}
	}

	/* relay_self_test(); */
	LOG_INF("Relay module ready (relay1=GPIO12, relay2=GPIO13)");
	return 0;
}

int relay_module_set(int relay, bool on)
{
	int ret;

	if (relay < 1 || relay > 2) {
		return -EINVAL;
	}

	ret = gpio_pin_set_dt(&relays[relay - 1], on ? 1 : 0);
	if (ret == 0) {
		relay_state[relay - 1] = on;
	}
	return ret;
}

static void relay_self_test(void)
{
	LOG_INF("Relay self-test start");
	for (int i = 1; i <= 2; i++) {
		relay_module_set(i, true);
		k_msleep(200);
		relay_module_set(i, false);
		k_msleep(100);
	}
	LOG_INF("Relay self-test done");
}

int relay_module_get(int relay, bool *on)
{
	if (relay < 1 || relay > 2 || on == NULL) {
		return -EINVAL;
	}

	*on = relay_state[relay - 1];
	return 0;
}
