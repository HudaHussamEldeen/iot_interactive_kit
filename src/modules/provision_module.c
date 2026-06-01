#include "modules/provision_module.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/hostname.h>
#include <zephyr/net/net_ip.h>

#include "device_profile.h"
#include "modules/api_module.h"
#include "modules/config_module.h"
#include "modules/device_identity.h"
#include "modules/wifi_manager.h"

LOG_MODULE_REGISTER(provision_module, LOG_LEVEL_INF);

static enum provision_mode mode = PROVISION_MODE_BOOT;
static bool provisioning_active;
static bool sta_ready;
static bool sta_connect_in_progress;
static uint32_t connect_attempt;
static wifi_sta_cfg_t pending_sta_cfg;
static wifi_ap_cfg_t ap_cfg;

static struct k_work_delayable sta_connect_work;
static struct k_work_delayable connect_timeout_work;
static struct k_work_delayable sta_reconnect_work;
static struct k_work_delayable boot_fallback_ap_work;
static struct k_work_delayable sta_start_after_response_work;

static const char *mode_name(enum provision_mode m)
{
	switch (m) {
	case PROVISION_MODE_BOOT:
		return "boot";
	case PROVISION_MODE_PROVISIONING:
		return "provisioning";
	case PROVISION_MODE_CONNECTING:
		return "connecting";
	case PROVISION_MODE_OPERATIONAL:
		return "operational";
	default:
		return "unknown";
	}
}

static void set_mode(enum provision_mode next)
{
	if (mode == next) {
		return;
	}

	mode = next;
	LOG_INF("Provision mode -> %s", mode_name(next));
}

static void load_sta_cfg_from_config(wifi_sta_cfg_t *cfg)
{
	struct kit_config config;

	config_module_get(&config);
	snprintk(cfg->ssid, sizeof(cfg->ssid), "%s", config.wifi_ssid);
	snprintk(cfg->psk, sizeof(cfg->psk), "%s", config.wifi_psk);
}

static void configure_softap_from_identity(void)
{
	const struct device_identity *id = device_identity_get();

	snprintk(ap_cfg.ssid, sizeof(ap_cfg.ssid), "%s", id->softap_ssid);
	snprintk(ap_cfg.psk, sizeof(ap_cfg.psk), "%s", id->softap_psk);
	ap_cfg.channel = 1;
	ap_cfg.open = false;
}

static int enter_provisioning_mode(void)
{
	int ret;
	bool keep_sta_connecting = sta_connect_in_progress && !sta_ready;

	if (provisioning_active) {
		return 0;
	}

	if (!keep_sta_connecting) {
		(void)k_work_cancel_delayable(&connect_timeout_work);
		sta_connect_in_progress = false;
		sta_ready = false;
	}

	(void)k_work_cancel_delayable(&boot_fallback_ap_work);

	configure_softap_from_identity();
	ret = wifi_manager_start_ap(&ap_cfg);
	if (ret < 0) {
		LOG_ERR("SoftAP start failed: %d", ret);
		return ret;
	}

	provisioning_active = true;
	if (!keep_sta_connecting) {
		set_mode(PROVISION_MODE_PROVISIONING);
	}

	LOG_INF("Provisioning active: join %s at %s", ap_cfg.ssid, DEVICE_PORTAL_AP_IP);
	return 0;
}

static int stop_provisioning_mode(void)
{
	if (!provisioning_active) {
		return 0;
	}

	(void)wifi_manager_stop_ap();
	provisioning_active = false;
	return 0;
}

static void schedule_sta_connect(void);

static void sta_connect_work_handler(struct k_work *work)
{
	int ret;

	ARG_UNUSED(work);

	if (provisioning_active) {
		LOG_INF("Starting STA connect while provisioning SoftAP remains active");
	}

	load_sta_cfg_from_config(&pending_sta_cfg);
	if (pending_sta_cfg.ssid[0] == '\0') {
		LOG_WRN("No STA credentials to connect");
		(void)enter_provisioning_mode();
		return;
	}

	connect_attempt++;
	sta_connect_in_progress = true;
	set_mode(PROVISION_MODE_CONNECTING);

	ret = wifi_manager_connect_sta(&pending_sta_cfg);
	if (ret < 0) {
		LOG_ERR("STA connect request failed: %d", ret);
		sta_connect_in_progress = false;
		(void)k_work_cancel_delayable(&connect_timeout_work);
		(void)enter_provisioning_mode();
	}
}

static void connect_timeout_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (sta_ready) {
		return;
	}

	LOG_WRN("STA connect timed out after %d ms", PROVISION_STA_CONNECT_TIMEOUT_MS);
	sta_connect_in_progress = false;
	(void)wifi_manager_disconnect_sta();
	(void)enter_provisioning_mode();
}

static void sta_reconnect_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (sta_ready || provisioning_active || sta_connect_in_progress) {
		return;
	}

	if (!config_module_has_wifi_credentials()) {
		(void)enter_provisioning_mode();
		return;
	}

	LOG_INF("Scheduling STA reconnect");
	(void)k_work_schedule(&sta_connect_work, K_NO_WAIT);
}

static void boot_fallback_ap_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (sta_ready || provisioning_active) {
		return;
	}

	LOG_WRN("STA not ready; opening fallback provisioning SoftAP");
	(void)enter_provisioning_mode();
}

static void sta_start_after_response_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	LOG_INF("Starting STA connect after API response; keeping SoftAP available");
	schedule_sta_connect();
}

static void schedule_sta_connect(void)
{
	(void)k_work_cancel_delayable(&connect_timeout_work);
	(void)k_work_reschedule(&sta_connect_work, K_NO_WAIT);
	(void)k_work_reschedule(&connect_timeout_work,
				K_MSEC(PROVISION_STA_CONNECT_TIMEOUT_MS));
}

static void on_sta_operational(void)
{
	struct kit_config config;

	sta_ready = true;
	sta_connect_in_progress = false;
	connect_attempt = 0;
	(void)k_work_cancel_delayable(&connect_timeout_work);
	(void)k_work_cancel_delayable(&boot_fallback_ap_work);
	(void)k_work_cancel_delayable(&sta_reconnect_work);

	set_mode(PROVISION_MODE_OPERATIONAL);

	config_module_get(&config);
	config_module_mark_wifi_provisioned(true);
	if (config_module_save_wifi_credentials(config.wifi_ssid, config.wifi_psk) == 0) {
		LOG_INF("Wi-Fi credentials confirmed in NVS");
	}

	(void)net_hostname_set(DEVICE_PORTAL_HOSTNAME, strlen(DEVICE_PORTAL_HOSTNAME));

	{
		char ip[NET_IPV4_ADDR_LEN];

		if (provision_module_get_sta_ipv4(ip, sizeof(ip)) == 0) {
			LOG_INF("[Provision] operational ssid=%s ipv4=%s hostname=%s.local",
				config.wifi_ssid, ip, DEVICE_PORTAL_HOSTNAME);
		} else {
			LOG_INF("[Provision] operational ssid=%s (no IPv4 yet)",
				config.wifi_ssid);
		}
	}
}

static void wifi_manager_event_cb(wifi_manager_event_t event, void *user_data)
{
	ARG_UNUSED(user_data);

	switch (event) {
	case WIFI_MANAGER_EVENT_AP_STARTED:
		provisioning_active = true;
		if (!sta_connect_in_progress && mode != PROVISION_MODE_CONNECTING) {
			set_mode(PROVISION_MODE_PROVISIONING);
		}
		break;
	case WIFI_MANAGER_EVENT_AP_STOPPED:
		provisioning_active = false;
		break;
	case WIFI_MANAGER_EVENT_AP_CLIENT_CONNECTED:
		LOG_INF("[Provision] setup client joined SoftAP — use http://%s:%d",
			DEVICE_PORTAL_AP_IP, DEVICE_API_PORT);
		break;
	case WIFI_MANAGER_EVENT_STA_CONNECTED:
		if (!sta_connect_in_progress) {
			LOG_WRN("Ignoring STA connected (SoftAP provisioning active)");
			break;
		}
		(void)stop_provisioning_mode();
		on_sta_operational();
		break;
	case WIFI_MANAGER_EVENT_STA_DISCONNECTED:
		if (provisioning_active) {
			break;
		}

		LOG_INF("[Provision] STA disconnected");
		sta_ready = false;
		sta_connect_in_progress = false;

		if (config_module_has_wifi_credentials()) {
			if (mode == PROVISION_MODE_OPERATIONAL) {
				set_mode(PROVISION_MODE_CONNECTING);
				LOG_INF("STA lost; reconnect in %d ms",
					PROVISION_STA_RECONNECT_DELAY_MS);
				(void)k_work_reschedule(&sta_reconnect_work,
							K_MSEC(PROVISION_STA_RECONNECT_DELAY_MS));
			} else {
				LOG_WRN("STA connect failed; returning to provisioning");
				(void)k_work_cancel_delayable(&connect_timeout_work);
				(void)enter_provisioning_mode();
			}
		} else {
			(void)enter_provisioning_mode();
		}
		break;
	default:
		break;
	}
}

int provision_module_init(void)
{
	struct device_identity id;
	struct kit_config config;

	k_work_init_delayable(&sta_connect_work, sta_connect_work_handler);
	k_work_init_delayable(&connect_timeout_work, connect_timeout_work_handler);
	k_work_init_delayable(&sta_reconnect_work, sta_reconnect_work_handler);
	k_work_init_delayable(&boot_fallback_ap_work, boot_fallback_ap_work_handler);
	k_work_init_delayable(&sta_start_after_response_work,
			      sta_start_after_response_work_handler);

	(void)device_identity_init(&id);

	if (wifi_manager_init(wifi_manager_event_cb, NULL) < 0) {
		return -ENODEV;
	}

	config_module_get(&config);
	if (config.device_id[0] == '\0' ||
	    strcmp(config.device_id, "iot-kit-esp32s3") == 0) {
		(void)config_module_save_device_id(id.device_id);
	}

	return 0;
}

int provision_module_start(void)
{
	if (!config_module_has_wifi_credentials()) {
		LOG_INF("No stored Wi-Fi credentials; starting provisioning mode");
		return enter_provisioning_mode();
	}

	LOG_INF("Stored Wi-Fi credentials found; connecting STA");
	set_mode(PROVISION_MODE_CONNECTING);
	(void)k_work_reschedule(&boot_fallback_ap_work,
				K_MSEC(PROVISION_BOOT_FALLBACK_AP_DELAY_MS));
	schedule_sta_connect();
	return 0;
}

int provision_module_submit_wifi(const char *ssid, const char *psk)
{
	int ret;

	if (!config_module_wifi_credentials_valid(ssid, psk)) {
		return -EINVAL;
	}

	ret = config_module_save_wifi_credentials(ssid, psk);
	if (ret < 0) {
		return ret;
	}

	load_sta_cfg_from_config(&pending_sta_cfg);
	connect_attempt = 0;
	sta_ready = false;

	(void)k_work_cancel_delayable(&sta_reconnect_work);
	(void)k_work_cancel_delayable(&boot_fallback_ap_work);
	(void)k_work_cancel_delayable(&sta_start_after_response_work);
	(void)wifi_manager_disconnect_sta();

	if (provisioning_active) {
		LOG_INF("Wi-Fi credentials saved; STA connect starts %d ms after response, SoftAP stays active",
			PROVISION_STA_START_AFTER_RESPONSE_MS);
		(void)k_work_reschedule(&sta_start_after_response_work,
					K_MSEC(PROVISION_STA_START_AFTER_RESPONSE_MS));
		return 0;
	}

	schedule_sta_connect();
	return 0;
}

int provision_module_clear_wifi_and_reprovision(void)
{
	(void)k_work_cancel_delayable(&sta_connect_work);
	(void)k_work_cancel_delayable(&connect_timeout_work);
	(void)k_work_cancel_delayable(&sta_reconnect_work);
	(void)k_work_cancel_delayable(&boot_fallback_ap_work);
	(void)k_work_cancel_delayable(&sta_start_after_response_work);

	sta_ready = false;
	sta_connect_in_progress = false;
	(void)wifi_manager_disconnect_sta();
	(void)config_module_clear_wifi();

	return enter_provisioning_mode();
}

enum provision_mode provision_module_get_mode(void)
{
	return mode;
}

bool provision_module_is_provisioning_active(void)
{
	return provisioning_active;
}

bool provision_module_is_sta_ready(void)
{
	return sta_ready;
}

uint32_t provision_module_get_connect_attempt(void)
{
	return connect_attempt;
}

int provision_module_get_sta_ipv4(char *buffer, size_t buffer_len)
{
	return wifi_manager_get_sta_ipv4(buffer, buffer_len);
}

void provision_module_get_wifi_status(struct wifi_manager_status *status)
{
	wifi_manager_get_status(status);
}
