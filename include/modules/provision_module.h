#ifndef MODULES_PROVISION_MODULE_H_
#define MODULES_PROVISION_MODULE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "modules/wifi_manager.h"

enum provision_mode {
	PROVISION_MODE_BOOT = 0,
	PROVISION_MODE_PROVISIONING,
	PROVISION_MODE_CONNECTING,
	PROVISION_MODE_OPERATIONAL,
};

int provision_module_init(void);
int provision_module_start(void);
int provision_module_submit_wifi(const char *ssid, const char *psk);
int provision_module_clear_wifi_and_reprovision(void);
enum provision_mode provision_module_get_mode(void);
bool provision_module_is_provisioning_active(void);
bool provision_module_is_sta_ready(void);
uint32_t provision_module_get_connect_attempt(void);
int provision_module_get_sta_ipv4(char *buffer, size_t buffer_len);
void provision_module_get_wifi_status(struct wifi_manager_status *status);

#endif /* MODULES_PROVISION_MODULE_H_ */
