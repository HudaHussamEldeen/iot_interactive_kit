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

/* Demo pattern colors */
static const struct led_rgb demo_colors[] = {
	RGB(32, 0, 0),
	RGB(0, 32, 0),
	RGB(0, 0, 32),
	RGB(32, 32, 0),
	RGB(0, 32, 32),
	RGB(32, 0, 32),
	RGB(32, 32, 32),
	RGB(0, 0, 0),
};

/* Network status colors */
static const struct led_rgb status_colors[] = {
	[RGB_STATUS_BOOT]        = RGB(0, 0, 48),      /* Blue: Booting */
	[RGB_STATUS_PROVISIONING] = RGB(48, 0, 48),    /* Magenta: Provisioning */
	[RGB_STATUS_CONNECTING]   = RGB(0, 48, 48),    /* Cyan: Connecting */
	[RGB_STATUS_OPERATIONAL]  = RGB(0, 48, 0),     /* Green: Operational */
	[RGB_STATUS_ERROR]        = RGB(48, 0, 0),     /* Red: Error */
};

static struct led_rgb pixels[STRIP_NUM_PIXELS];
static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);
static size_t color_index;
static rgb_network_status_t current_status = RGB_STATUS_BOOT;
static bool status_mode_enabled = true;  /* true = status colors, false = demo pattern */

int rgb_module_init(void)
{
	if (!device_is_ready(strip)) {
		printk("RGB module: LED strip device is not ready\n");
		return -ENODEV;
	}

	printk("RGB module: ready\n");
	return 0;
}

void rgb_module_set_color(uint8_t r, uint8_t g, uint8_t b)
{
	int ret;
	struct led_rgb color = {.r = r, .g = g, .b = b};

	for (size_t i = 0; i < ARRAY_SIZE(pixels); i++) {
		pixels[i] = color;
	}

	ret = led_strip_update_rgb(strip, pixels, STRIP_NUM_PIXELS);
	if (ret < 0) {
		printk("RGB module: update failed: %d\n", ret);
	}
}

void rgb_module_set_network_status(rgb_network_status_t status)
{
	if (status >= ARRAY_SIZE(status_colors)) {
		printk("RGB module: invalid status %d\n", status);
		return;
	}

	current_status = status;
	status_mode_enabled = true;

	struct led_rgb color = status_colors[status];
	rgb_module_set_color(color.r, color.g, color.b);

	const char *status_name[] = {
		[RGB_STATUS_BOOT]        = "boot",
		[RGB_STATUS_PROVISIONING] = "provisioning",
		[RGB_STATUS_CONNECTING]   = "connecting",
		[RGB_STATUS_OPERATIONAL]  = "operational",
		[RGB_STATUS_ERROR]        = "error",
	};

	if (status < ARRAY_SIZE(status_name)) {
		printk("RGB module: network status -> %s\n", status_name[status]);
	}
}

void rgb_module_run_pattern(k_timeout_t delay)
{
	int ret;

	if (status_mode_enabled) {
		/* In status mode, keep the current status color */
		k_sleep(delay);
		return;
	}

	/* Demo mode: cycle through colors */
	for (size_t i = 0; i < ARRAY_SIZE(pixels); i++) {
		pixels[i] = demo_colors[color_index];
	}

	ret = led_strip_update_rgb(strip, pixels, STRIP_NUM_PIXELS);
	if (ret < 0) {
		printk("RGB module: update failed: %d\n", ret);
		return;
	}

	color_index = (color_index + 1) % ARRAY_SIZE(demo_colors);
	k_sleep(delay);
}
