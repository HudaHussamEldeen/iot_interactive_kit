#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "modules/api_module.h"
#include "modules/device_identity.h"
#include "modules/config_module.h"
#include "modules/provision_module.h"
#include "modules/rgb_module.h"
#include "modules/buzzer_module.h"
#include "modules/analog_module.h"
#include "modules/gpio_inputs_module.h"
#include "modules/relay_module.h"
#include "modules/dht22_module.h"
#include "modules/mpu6050_module.h"
#include "modules/reset_button_module.h"
#include "modules/vl6180x_module.h"
#include "modules/servo_module.h"
#include "modules/led_module.h"
#include "modules/motor_module.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define RGB_PATTERN_DELAY K_MSEC(700)

int main(void)
{
	int ret;

	LOG_INF("IoT Interactive Kit starting");

	/* ── 1. Persistent config (NVS reads must finish before WiFi starts) ── */
	ret = config_module_init();
	if (ret < 0) {
		LOG_ERR("Config init failed: %d", ret);
	}

	ret = config_module_load();
	if (ret < 0) {
		LOG_WRN("Config load failed: %d", ret);
	}

	/* ── 2. RGB first — provision module uses it for status LEDs ── */
	ret = rgb_module_init();
	if (ret < 0) {
		LOG_ERR("RGB init failed: %d", ret);
		return 0;
	}

	/* ── 3. WiFi / AP — start before any other peripheral ── */
	ret = device_identity_init(NULL);
	if (ret < 0) {
		LOG_ERR("Device identity init failed: %d", ret);
	}

	ret = provision_module_init();
	if (ret < 0) {
		LOG_ERR("Provision init failed: %d", ret);
	} else {
		ret = provision_module_start();
		if (ret < 0) {
			LOG_ERR("Provision start failed: %d", ret);
		}
	}

	/* ── 4. Remaining peripherals (after WiFi is already starting) ── */
	ret = buzzer_module_init();
	if (ret < 0) {
		LOG_WRN("Buzzer init failed: %d", ret);
	}

	ret = reset_button_module_init();
	if (ret < 0) {
		LOG_WRN("Reset button init failed: %d", ret);
	}

	ret = relay_module_init();
	if (ret < 0) {
		LOG_WRN("Relay module init failed: %d", ret);
	}

	ret = gpio_inputs_module_init();
	if (ret < 0) {
		LOG_WRN("GPIO inputs init failed: %d", ret);
	}

	ret = analog_module_init();
	if (ret < 0) {
		LOG_WRN("Analog module init failed: %d", ret);
	}

	ret = dht22_module_init();
	if (ret < 0) {
		LOG_WRN("DHT22 init failed: %d", ret);
	}

	ret = mpu6050_module_init();
	if (ret < 0) {
		LOG_WRN("MPU6050 init failed: %d", ret);
	}

	ret = vl6180x_module_init();
	if (ret < 0) {
		LOG_WRN("VL6180X init failed: %d", ret);
	}

	ret = servo_module_init();
	if (ret < 0) {
		LOG_WRN("Servo init failed: %d", ret);
	}

	ret = led_module_init();
	if (ret < 0) {
		LOG_WRN("LED module init failed: %d", ret);
	}

	/* Motor last — PWM noise at boot interferes with WiFi startup */
	ret = motor_module_init();
	if (ret < 0) {
		LOG_WRN("Motor module init failed: %d", ret);
	}

	ret = api_module_init();
	if (ret < 0) {
		LOG_ERR("API init failed: %d", ret);
	} else {
		api_module_start();
	}

	while (1) {
		rgb_module_run_pattern(RGB_PATTERN_DELAY);
	}
}
