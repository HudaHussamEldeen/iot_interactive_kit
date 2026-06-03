#ifndef MODULES_RGB_MODULE_H_
#define MODULES_RGB_MODULE_H_

#include <zephyr/kernel.h>

typedef enum {
	RGB_STATUS_BOOT,        /* Blue: Device booting */
	RGB_STATUS_PROVISIONING, /* Magenta: Waiting for provisioning */
	RGB_STATUS_CONNECTING,   /* Cyan: Attempting Wi-Fi connection */
	RGB_STATUS_OPERATIONAL,  /* Green: Connected and operational */
	RGB_STATUS_ERROR,        /* Red: Error state */
} rgb_network_status_t;

int rgb_module_init(void);
void rgb_module_run_pattern(k_timeout_t delay);
void rgb_module_set_network_status(rgb_network_status_t status);
void rgb_module_set_color(uint8_t r, uint8_t g, uint8_t b);

#endif /* MODULES_RGB_MODULE_H_ */
