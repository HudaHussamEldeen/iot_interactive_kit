#include "modules/config_module.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

LOG_MODULE_REGISTER(config_module, LOG_LEVEL_INF);

static struct kit_config current_config = {
	.device_id = "iot-kit-esp32s3",
	.wifi_ssid = "",
	.wifi_psk = "",
	.api_token = "iot-kit-dev",
	.wifi_ip_mode = "dhcp",
	.wifi_ip_address = "",
	.wifi_netmask = "",
	.wifi_gateway = "",
	.wifi_provisioned = false,
};

static K_MUTEX_DEFINE(config_lock);

static wifi_ip_mode_t ip_mode_from_string(const char *mode)
{
	if (mode == NULL) {
		return WIFI_IP_MODE_DHCP;
	}

	if (strcmp(mode, "static") == 0) {
		return WIFI_IP_MODE_STATIC;
	}

	return WIFI_IP_MODE_DHCP;
}

static const char *ip_mode_to_string(wifi_ip_mode_t mode)
{
	return mode == WIFI_IP_MODE_STATIC ? "static" : "dhcp";
}

static int settings_set_cb(const char *key, size_t len_rd, settings_read_cb read_cb,
			   void *cb_arg)
{
	char *target = NULL;
	size_t target_size = 0;
	int rc = -ENOENT;

	if (settings_name_steq(key, "device_id", NULL)) {
		target = current_config.device_id;
		target_size = sizeof(current_config.device_id);
	} else if (settings_name_steq(key, "wifi_ssid", NULL)) {
		target = current_config.wifi_ssid;
		target_size = sizeof(current_config.wifi_ssid);
	} else if (settings_name_steq(key, "wifi_psk", NULL)) {
		target = current_config.wifi_psk;
		target_size = sizeof(current_config.wifi_psk);
	} else if (settings_name_steq(key, "api_token", NULL)) {
		target = current_config.api_token;
		target_size = sizeof(current_config.api_token);
	} else if (settings_name_steq(key, "wifi_ip_mode", NULL)) {
		target = current_config.wifi_ip_mode;
		target_size = sizeof(current_config.wifi_ip_mode);
	} else if (settings_name_steq(key, "wifi_ip_address", NULL)) {
		target = current_config.wifi_ip_address;
		target_size = sizeof(current_config.wifi_ip_address);
	} else if (settings_name_steq(key, "wifi_netmask", NULL)) {
		target = current_config.wifi_netmask;
		target_size = sizeof(current_config.wifi_netmask);
	} else if (settings_name_steq(key, "wifi_gateway", NULL)) {
		target = current_config.wifi_gateway;
		target_size = sizeof(current_config.wifi_gateway);
	}

	if (target != NULL) {
		rc = read_cb(cb_arg, target, target_size);
	}

	if ((rc >= 0) && (len_rd > 0U)) {
		return 0;
	}

	return rc;
}

static struct settings_handler kit_settings = {
	.name = "iot_kit",
	.h_set = settings_set_cb,
};

static int save_string(const char *key, const char *value)
{
	char path[48];

	snprintk(path, sizeof(path), "iot_kit/%s", key);
	return settings_save_one(path, value, strlen(value) + 1U);
}

int config_module_init(void)
{
	int ret;

	ret = settings_subsys_init();
	if ((ret < 0) && (ret != -EALREADY)) {
		LOG_ERR("settings init failed: %d", ret);
		return ret;
	}

	ret = settings_register(&kit_settings);
	if (ret < 0) {
		LOG_ERR("settings register failed: %d", ret);
	}

	return ret;
}

int config_module_load(void)
{
	int ret;

	ret = settings_load_subtree("iot_kit");
	if (ret < 0) {
		LOG_ERR("settings load failed: %d", ret);
		return ret;
	}

	k_mutex_lock(&config_lock, K_FOREVER);
	current_config.wifi_provisioned = current_config.wifi_ssid[0] != '\0';
	k_mutex_unlock(&config_lock);

	LOG_INF("--- NVS config loaded ---");
	LOG_INF("  device_id    : %s", current_config.device_id);
	LOG_INF("  wifi_ssid    : %s",
		current_config.wifi_ssid[0] != '\0' ? current_config.wifi_ssid : "(not set)");
	LOG_INF("  wifi_psk     : %s",
		current_config.wifi_psk[0] != '\0' ? current_config.wifi_psk : "(not set)");
	LOG_INF("  api_token    : %s",
		current_config.api_token[0] != '\0' ? current_config.api_token : "(not set)");
	LOG_INF("  ip_mode      : %s", current_config.wifi_ip_mode);
	LOG_INF("  ip_address   : %s",
		current_config.wifi_ip_address[0] != '\0' ? current_config.wifi_ip_address
							   : "(not set)");
	LOG_INF("  netmask      : %s",
		current_config.wifi_netmask[0] != '\0' ? current_config.wifi_netmask
						       : "(not set)");
	LOG_INF("  gateway      : %s",
		current_config.wifi_gateway[0] != '\0' ? current_config.wifi_gateway
						       : "(not set)");
	LOG_INF("  provisioned  : %s", current_config.wifi_provisioned ? "yes" : "no");
	LOG_INF("-------------------------");

	return 0;
}

int config_module_get(struct kit_config *config)
{
	if (config == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&config_lock, K_FOREVER);
	*config = current_config;
	k_mutex_unlock(&config_lock);

	return 0;
}

int config_module_save_device_id(const char *device_id)
{
	int ret;

	if (device_id == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&config_lock, K_FOREVER);
	snprintk(current_config.device_id, sizeof(current_config.device_id), "%s",
		 device_id);
	ret = save_string("device_id", current_config.device_id);
	k_mutex_unlock(&config_lock);

	if (ret < 0) {
		LOG_ERR("device_id save failed: %d", ret);
	}

	return ret;
}

int config_module_save(const struct kit_config *config)
{
	int ret = 0;

	if (config == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&config_lock, K_FOREVER);
	current_config = *config;
	ret = save_string("device_id", current_config.device_id);
	if (ret == 0) {
		ret = save_string("wifi_ssid", current_config.wifi_ssid);
	}
	if (ret == 0) {
		ret = save_string("wifi_psk", current_config.wifi_psk);
	}
	if (ret == 0) {
		ret = save_string("api_token", current_config.api_token);
	}
	if (ret == 0) {
		ret = save_string("wifi_ip_mode", current_config.wifi_ip_mode);
	}
	if (ret == 0) {
		ret = save_string("wifi_ip_address", current_config.wifi_ip_address);
	}
	if (ret == 0) {
		ret = save_string("wifi_netmask", current_config.wifi_netmask);
	}
	if (ret == 0) {
		ret = save_string("wifi_gateway", current_config.wifi_gateway);
	}
	k_mutex_unlock(&config_lock);

	if (ret < 0) {
		LOG_ERR("config save failed: %d", ret);
	}

	return ret;
}

int config_module_save_wifi_config(const wifi_sta_cfg_t *cfg)
{
	struct kit_config config;

	if (cfg == NULL) {
		return -EINVAL;
	}

	config_module_get(&config);
	snprintk(config.wifi_ssid, sizeof(config.wifi_ssid), "%s", cfg->ssid);
	snprintk(config.wifi_psk, sizeof(config.wifi_psk), "%s", cfg->psk);
	snprintk(config.wifi_ip_mode, sizeof(config.wifi_ip_mode), "%s",
		 ip_mode_to_string(cfg->ip_mode));
	snprintk(config.wifi_ip_address, sizeof(config.wifi_ip_address), "%s",
		 cfg->ip_address);
	snprintk(config.wifi_netmask, sizeof(config.wifi_netmask), "%s",
		 cfg->netmask);
	snprintk(config.wifi_gateway, sizeof(config.wifi_gateway), "%s",
		 cfg->gateway);
	config.wifi_provisioned = false;

	return config_module_save(&config);
}

bool config_module_has_wifi_credentials(void)
{
	bool has_credentials;

	k_mutex_lock(&config_lock, K_FOREVER);
	has_credentials = current_config.wifi_ssid[0] != '\0';
	k_mutex_unlock(&config_lock);

	return has_credentials;
}

bool config_module_wifi_credentials_valid(const char *ssid, const char *psk)
{
	size_t ssid_len;
	size_t psk_len;

	if (ssid == NULL || psk == NULL) {
		return false;
	}

	ssid_len = strlen(ssid);
	psk_len = strlen(psk);

	if (ssid_len == 0U || ssid_len > 32U) {
		return false;
	}

	if (psk_len != 0U && (psk_len < 8U || psk_len > 63U)) {
		return false;
	}

	return true;
}

int config_module_save_wifi_credentials(const char *ssid, const char *psk)
{
	struct kit_config config;
	int ret;

	if (!config_module_wifi_credentials_valid(ssid, psk)) {
		return -EINVAL;
	}

	config_module_get(&config);
	snprintk(config.wifi_ssid, sizeof(config.wifi_ssid), "%s", ssid);
	snprintk(config.wifi_psk, sizeof(config.wifi_psk), "%s", psk);
	snprintk(config.wifi_ip_mode, sizeof(config.wifi_ip_mode), "%s", "dhcp");
	config.wifi_ip_address[0] = '\0';
	config.wifi_netmask[0] = '\0';
	config.wifi_gateway[0] = '\0';
	config.wifi_provisioned = false;

	ret = config_module_save(&config);
	if (ret == 0) {
		k_mutex_lock(&config_lock, K_FOREVER);
		current_config.wifi_provisioned = false;
		k_mutex_unlock(&config_lock);
	}

	return ret;
}

int config_module_mark_wifi_provisioned(bool provisioned)
{
	k_mutex_lock(&config_lock, K_FOREVER);
	current_config.wifi_provisioned = provisioned;
	k_mutex_unlock(&config_lock);

	return 0;
}

int config_module_factory_reset(void)
{
	static const char *const keys[] = {
		"iot_kit/device_id",
		"iot_kit/wifi_ssid",
		"iot_kit/wifi_psk",
		"iot_kit/api_token",
		"iot_kit/wifi_ip_mode",
		"iot_kit/wifi_ip_address",
		"iot_kit/wifi_netmask",
		"iot_kit/wifi_gateway",
	};

	LOG_WRN("Factory reset: erasing all NVS config");

	k_mutex_lock(&config_lock, K_FOREVER);

	for (int i = 0; i < ARRAY_SIZE(keys); i++) {
		(void)settings_delete(keys[i]);
	}

	struct kit_config defaults = {
		.device_id      = "iot-kit-esp32s3",
		.wifi_ssid      = "",
		.wifi_psk       = "",
		.api_token      = "iot-kit-dev",
		.wifi_ip_mode   = "dhcp",
		.wifi_ip_address = "",
		.wifi_netmask   = "",
		.wifi_gateway   = "",
		.wifi_provisioned = false,
	};
	current_config = defaults;

	k_mutex_unlock(&config_lock);

	LOG_WRN("Factory reset: NVS cleared");
	return 0;
}

int config_module_clear_wifi(void)
{
	struct kit_config config;

	config_module_get(&config);
	config.wifi_ssid[0] = '\0';
	config.wifi_psk[0] = '\0';
	config.wifi_provisioned = false;
	return config_module_save(&config);
}
