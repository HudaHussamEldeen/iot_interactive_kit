#include "modules/rgb_module.h"

#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#define STRIP_NODE DT_ALIAS(led_strip)

#if !DT_NODE_HAS_PROP(STRIP_NODE, chain_length)
#error "No LED strip found. Add a led-strip alias with chain-length in devicetree."
#endif

#define STRIP_NUM_PIXELS DT_PROP(STRIP_NODE, chain_length)
#define RGB(_r, _g, _b) { .r = (_r), .g = (_g), .b = (_b) }

static const struct led_rgb colors[] = {
	RGB(32, 0, 0),
	RGB(0, 32, 0),
	RGB(0, 0, 32),
	RGB(32, 32, 0),
	RGB(0, 32, 32),
	RGB(32, 0, 32),
	RGB(32, 32, 32),
	RGB(0, 0, 0),
};

static struct led_rgb pixels[STRIP_NUM_PIXELS];
static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);
static size_t color_index;

int rgb_module_init(void)
{
	if (!device_is_ready(strip)) {
		printk("RGB module: LED strip device is not ready\n");
		return -ENODEV;
	}

	printk("RGB module: ready\n");
	return 0;
}

void rgb_module_run_pattern(k_timeout_t delay)
{
	int ret;

	for (size_t i = 0; i < ARRAY_SIZE(pixels); i++) {
		pixels[i] = colors[color_index];
	}

	ret = led_strip_update_rgb(strip, pixels, STRIP_NUM_PIXELS);
	if (ret < 0) {
		printk("RGB module: update failed: %d\n", ret);
		return;
	}

	color_index = (color_index + 1) % ARRAY_SIZE(colors);
	k_sleep(delay);
}
