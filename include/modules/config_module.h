#ifndef MODULES_CONFIG_MODULE_H_
#define MODULES_CONFIG_MODULE_H_

#include <stdbool.h>
#include <stddef.h>

#include "modules/wifi_manager.h"

#define KIT_DEVICE_ID_MAX_LEN 32
#define KIT_WIFI_SSID_MAX_LEN 33
#define KIT_WIFI_PSK_MAX_LEN 65
#define KIT_API_TOKEN_MAX_LEN 65
#define KIT_WIFI_IP_MODE_MAX_LEN 8
#define KIT_IP_ADDR_MAX_LEN 16

struct kit_config {
	char device_id[KIT_DEVICE_ID_MAX_LEN];
	char wifi_ssid[KIT_WIFI_SSID_MAX_LEN];
	char wifi_psk[KIT_WIFI_PSK_MAX_LEN];
	char api_token[KIT_API_TOKEN_MAX_LEN];
	char wifi_ip_mode[KIT_WIFI_IP_MODE_MAX_LEN];
	char wifi_ip_address[KIT_IP_ADDR_MAX_LEN];
	char wifi_netmask[KIT_IP_ADDR_MAX_LEN];
	char wifi_gateway[KIT_IP_ADDR_MAX_LEN];
	bool wifi_provisioned;
};

int config_module_init(void);
int config_module_load(void);
int config_module_get(struct kit_config *config);
int config_module_save_device_id(const char *device_id);
int config_module_save(const struct kit_config *config);
bool config_module_has_wifi_credentials(void);
bool config_module_wifi_credentials_valid(const char *ssid, const char *psk);
int config_module_save_wifi_credentials(const char *ssid, const char *psk);
int config_module_save_wifi_config(const wifi_sta_cfg_t *cfg);
int config_module_mark_wifi_provisioned(bool provisioned);
int config_module_clear_wifi(void);

#endif /* MODULES_CONFIG_MODULE_H_ */
