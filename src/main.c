#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "modules/api_module.h"
#include "modules/config_module.h"
#include "modules/provision_module.h"
#include "modules/rgb_module.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define RGB_PATTERN_DELAY K_MSEC(700)

int main(void)
{
	int ret;

	LOG_INF("IoT Interactive Kit starting");

	ret = config_module_init();
	if (ret < 0) {
		LOG_ERR("Config init failed: %d", ret);
	}

	ret = config_module_load();
	if (ret < 0) {
		LOG_WRN("Config load failed: %d", ret);
	}

	ret = rgb_module_init();
	if (ret < 0) {
		LOG_ERR("RGB init failed: %d", ret);
		return 0;
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
