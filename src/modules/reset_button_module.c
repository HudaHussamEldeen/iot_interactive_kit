#include "modules/reset_button_module.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

#include "modules/config_module.h"

LOG_MODULE_REGISTER(reset_button, LOG_LEVEL_INF);

#define HOLD_DURATION_MS 3000

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static struct gpio_callback button_cb;

static void reset_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(reset_work, reset_work_handler);

static void reset_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	LOG_WRN("Factory reset triggered — erasing NVS...");
	config_module_factory_reset();
	k_msleep(200);
	LOG_WRN("Rebooting...");
	k_msleep(100);
	sys_reboot(SYS_REBOOT_COLD);
}

static void button_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	int pressed = !gpio_pin_get_dt(&button); /* active low */

	if (pressed) {
		LOG_INF("Reset button pressed — hold %d s for factory reset", HOLD_DURATION_MS / 1000);
		k_work_reschedule(&reset_work, K_MSEC(HOLD_DURATION_MS));
	} else {
		k_work_cancel_delayable(&reset_work);
		LOG_INF("Reset button released");
	}
}

int reset_button_module_init(void)
{
	int ret;

	if (!gpio_is_ready_dt(&button)) {
		LOG_ERR("Reset button GPIO not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Failed to configure reset button pin: %d", ret);
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_BOTH);
	if (ret < 0) {
		LOG_ERR("Failed to configure reset button interrupt: %d", ret);
		return ret;
	}

	gpio_init_callback(&button_cb, button_isr, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb);

	LOG_INF("Reset button ready (GPIO%d) — hold %d s to factory reset",
		button.pin, HOLD_DURATION_MS / 1000);
	return 0;
}
