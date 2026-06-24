#include "modules/gpio_inputs_module.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(gpio_inputs_module, LOG_LEVEL_INF);

static const struct gpio_dt_spec magnetic_sw =
	GPIO_DT_SPEC_GET(DT_NODELABEL(magnetic_sw), gpios);

static const struct gpio_dt_spec user_btn =
	GPIO_DT_SPEC_GET(DT_NODELABEL(user_btn), gpios);

static void gpio_inputs_self_test(void);

int gpio_inputs_module_init(void)
{
	int ret;

	if (!gpio_is_ready_dt(&magnetic_sw)) {
		LOG_ERR("Magnetic switch GPIO40 not ready");
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&user_btn)) {
		LOG_ERR("Button GPIO41 not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&magnetic_sw, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Magnetic switch configure failed: %d", ret);
		return ret;
	}

	ret = gpio_pin_configure_dt(&user_btn, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Button configure failed: %d", ret);
		return ret;
	}

	/* gpio_inputs_self_test(); */
	LOG_INF("GPIO inputs ready (magnetic=GPIO40, button=GPIO41)");
	return 0;
}

static void gpio_inputs_self_test(void)
{
	bool closed, pressed;

	LOG_INF("GPIO inputs self-test start");
	gpio_inputs_module_read_magnetic(&closed);
	gpio_inputs_module_read_button(&pressed);
	LOG_INF("GPIO inputs: magnetic=%s button=%s",
		closed ? "closed" : "open", pressed ? "pressed" : "released");
}

int gpio_inputs_module_read_magnetic(bool *closed)
{
	int val;

	if (closed == NULL) {
		return -EINVAL;
	}

	val = gpio_pin_get_dt(&magnetic_sw);
	if (val < 0) {
		return val;
	}

	*closed = (val == 1);
	return 0;
}

int gpio_inputs_module_read_button(bool *pressed)
{
	int val;

	if (pressed == NULL) {
		return -EINVAL;
	}

	val = gpio_pin_get_dt(&user_btn);
	if (val < 0) {
		return val;
	}

	*pressed = (val == 1);
	return 0;
}
