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
#include "modules/rgb_module.h"
#include "modules/wifi_manager.h"

LOG_MODULE_REGISTER(provision_module, LOG_LEVEL_INF);

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

static enum provision_mode mode = PROVISION_MODE_BOOT;
static bool provisioning_active;
static bool sta_ready;
static bool sta_connect_in_progress;
static bool sta_retry_pending;
static uint32_t connect_attempt;
static wifi_sta_cfg_t pending_sta_cfg;
static wifi_ap_cfg_t ap_cfg;

/* Blocking connect synchronisation */
static K_SEM_DEFINE(connect_result_sem, 0, 1);
static bool connect_result_success;
static bool blocking_connect_active;
static bool use_pending_cfg;       /* use pending_sta_cfg instead of NVS on next attempt */
static bool persistent_reconnect;  /* retry every 60 s after operational disconnect */

static struct k_work_delayable sta_connect_work;
static struct k_work_delayable connect_timeout_work;
static struct k_work_delayable sta_reconnect_work;
static struct k_work_delayable boot_fallback_ap_work;
static struct k_work_delayable sta_start_after_response_work;
static struct k_work_delayable stop_ap_work;

static const char *mode_name(enum provision_mode m)
{
	switch (m) {
	case PROVISION_MODE_BOOT:         return "boot";
	case PROVISION_MODE_PROVISIONING: return "provisioning";
	case PROVISION_MODE_CONNECTING:   return "connecting";
	case PROVISION_MODE_OPERATIONAL:  return "operational";
	default:                          return "unknown";
	}
}

static void set_mode(enum provision_mode next)
{
	if (mode == next) {
		return;
	}
	mode = next;
	LOG_INF("Provision mode -> %s", mode_name(next));
	switch (next) {
	case PROVISION_MODE_BOOT:
		rgb_module_set_network_status(RGB_STATUS_BOOT);
		break;
	case PROVISION_MODE_PROVISIONING:
		rgb_module_set_network_status(RGB_STATUS_PROVISIONING);
		break;
	case PROVISION_MODE_CONNECTING:
		rgb_module_set_network_status(RGB_STATUS_CONNECTING);
		break;
	case PROVISION_MODE_OPERATIONAL:
		rgb_module_set_network_status(RGB_STATUS_OPERATIONAL);
		break;
	}
}

static void load_sta_cfg_from_config(wifi_sta_cfg_t *cfg)
{
	struct kit_config config;

	config_module_get(&config);
	snprintk(cfg->ssid, sizeof(cfg->ssid), "%s", config.wifi_ssid);
	snprintk(cfg->psk, sizeof(cfg->psk), "%s", config.wifi_psk);
	cfg->ip_mode = ip_mode_from_string(config.wifi_ip_mode);
	snprintk(cfg->ip_address, sizeof(cfg->ip_address), "%s", config.wifi_ip_address);
	snprintk(cfg->netmask, sizeof(cfg->netmask), "%s", config.wifi_netmask);
	snprintk(cfg->gateway, sizeof(cfg->gateway), "%s", config.wifi_gateway);
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
		if (ret == -EINPROGRESS || ret == -EALREADY) {
			LOG_INF("Retrying SoftAP start in %d ms", PROVISION_AP_RETRY_DELAY_MS);
			(void)k_work_reschedule(&boot_fallback_ap_work,
						K_MSEC(PROVISION_AP_RETRY_DELAY_MS));
		}
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

static void schedule_sta_connect_after_ms(int32_t delay_ms);

static void sta_connect_work_handler(struct k_work *work)
{
	int ret;

	ARG_UNUSED(work);

	sta_retry_pending = false;

	/* During blocking connect credentials are not yet in NVS — always use
	 * the caller-supplied pending_sta_cfg instead of loading from NVS. */
	if (!use_pending_cfg && !blocking_connect_active) {
		load_sta_cfg_from_config(&pending_sta_cfg);
	}
	use_pending_cfg = false;

	if (pending_sta_cfg.ssid[0] == '\0') {
		LOG_WRN("No STA credentials to connect");
		if (!blocking_connect_active) {
			(void)enter_provisioning_mode();
		}
		return;
	}

	connect_attempt++;
	sta_connect_in_progress = true;
	set_mode(PROVISION_MODE_CONNECTING);
	LOG_INF("STA connect attempt %u/%u ssid=%s",
		(unsigned int)connect_attempt,
		(unsigned int)PROVISION_STA_MAX_ATTEMPTS,
		pending_sta_cfg.ssid);

	ret = wifi_manager_connect_sta(&pending_sta_cfg);
	if (ret < 0) {
		LOG_ERR("STA connect request failed immediately: %d", ret);
		sta_connect_in_progress = false;
		(void)k_work_cancel_delayable(&connect_timeout_work);

		if (connect_attempt < PROVISION_STA_MAX_ATTEMPTS) {
			sta_retry_pending = true;
			schedule_sta_connect_after_ms(PROVISION_STA_RETRY_DELAY_MS);
			return;
		}

		/* All attempts done */
		if (blocking_connect_active) {
			connect_result_success = false;
			blocking_connect_active = false;
			k_sem_give(&connect_result_sem);
		} else if (persistent_reconnect) {
			connect_attempt = 0;
			k_work_reschedule(&sta_reconnect_work, K_MSEC(PROVISION_STA_RECONNECT_DELAY_MS));
		} else {
			(void)enter_provisioning_mode();
		}
	}
}

static void connect_timeout_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (sta_ready) {
		return;
	}

	LOG_WRN("STA connect timed out after %d ms (attempt %u/%u)",
		PROVISION_STA_CONNECT_TIMEOUT_MS,
		(unsigned int)connect_attempt,
		(unsigned int)PROVISION_STA_MAX_ATTEMPTS);

	sta_connect_in_progress = false;
	(void)wifi_manager_disconnect_sta();

	if (connect_attempt < PROVISION_STA_MAX_ATTEMPTS) {
		sta_retry_pending = true;
		LOG_INF("Retrying STA connect in %d ms", PROVISION_STA_RETRY_DELAY_MS);
		schedule_sta_connect_after_ms(PROVISION_STA_RETRY_DELAY_MS);
		return;
	}

	/* All attempts exhausted */
	if (blocking_connect_active) {
		LOG_WRN("Blocking connect: all %u attempts failed", (unsigned int)connect_attempt);
		/* AP is still up — return RGB to provisioning state */
		set_mode(PROVISION_MODE_PROVISIONING);
		connect_result_success = false;
		blocking_connect_active = false;
		k_sem_give(&connect_result_sem);
		return;
	}

	if (persistent_reconnect) {
		connect_attempt = 0;
		LOG_INF("Persistent reconnect: retry in %d ms", PROVISION_STA_RECONNECT_DELAY_MS);
		k_work_reschedule(&sta_reconnect_work, K_MSEC(PROVISION_STA_RECONNECT_DELAY_MS));
		return;
	}

	LOG_WRN("STA failed after %u attempts; returning to provisioning",
		(unsigned int)connect_attempt);
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

	LOG_INF("Persistent reconnect: starting new attempt set");
	connect_attempt = 0;
	schedule_sta_connect_after_ms(0);
}

static void boot_fallback_ap_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (sta_ready || provisioning_active) {
		return;
	}

	if (sta_connect_in_progress || sta_retry_pending) {
		LOG_INF("STA attempt still active; delaying fallback provisioning SoftAP");
		(void)k_work_reschedule(&boot_fallback_ap_work,
					K_MSEC(PROVISION_BOOT_FALLBACK_AP_DELAY_MS));
		return;
	}

	LOG_WRN("STA not ready; opening fallback provisioning SoftAP");
	(void)enter_provisioning_mode();
}

static void sta_start_after_response_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (provisioning_active) {
		LOG_INF("Stopping SoftAP before STA connect to free Wi-Fi resources");
		(void)stop_provisioning_mode();
		k_sleep(K_MSEC(200));
	}

	LOG_INF("Starting STA connect after API response");
	schedule_sta_connect_after_ms(0);
}

static void stop_ap_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (provisioning_active) {
		LOG_INF("Stopping SoftAP after successful blocking connect");
		(void)stop_provisioning_mode();
	}
}

static void schedule_sta_connect_after_ms(int32_t delay_ms)
{
	(void)k_work_cancel_delayable(&connect_timeout_work);
	(void)k_work_reschedule(&sta_connect_work, K_MSEC(delay_ms));
	(void)k_work_reschedule(&connect_timeout_work,
				K_MSEC(delay_ms + PROVISION_STA_CONNECT_TIMEOUT_MS));
}

static void on_sta_operational(void)
{
	char ip[NET_IPV4_ADDR_LEN];
	struct kit_config config;

	sta_ready = true;
	sta_connect_in_progress = false;
	sta_retry_pending = false;
	connect_attempt = 0;
	persistent_reconnect = false;
	(void)k_work_cancel_delayable(&connect_timeout_work);
	(void)k_work_cancel_delayable(&boot_fallback_ap_work);
	(void)k_work_cancel_delayable(&sta_reconnect_work);

	set_mode(PROVISION_MODE_OPERATIONAL);

	if (blocking_connect_active) {
		/* Signal success; caller saves credentials and stops AP */
		connect_result_success = true;
		k_sem_give(&connect_result_sem);
	} else {
		config_module_get(&config);
		config_module_mark_wifi_provisioned(true);
		LOG_INF("Wi-Fi station config confirmed in NVS");
	}

	(void)net_hostname_set(DEVICE_PORTAL_HOSTNAME, strlen(DEVICE_PORTAL_HOSTNAME));

	config_module_get(&config);
	if (provision_module_get_sta_ipv4(ip, sizeof(ip)) == 0) {
		LOG_INF("[Provision] operational ssid=%s ipv4=%s hostname=%s.local",
			config.wifi_ssid, ip, DEVICE_PORTAL_HOSTNAME);
	} else {
		LOG_INF("[Provision] operational ssid=%s (no IPv4 yet)", config.wifi_ssid);
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
			LOG_WRN("Ignoring STA connected (no connect in progress)");
			break;
		}
		if (!blocking_connect_active) {
			/* Normal boot/reconnect: stop AP to free Wi-Fi resources */
			(void)stop_provisioning_mode();
		}
		/* In blocking mode AP stays up so the HTTP connection remains alive */
		on_sta_operational();
		break;

	case WIFI_MANAGER_EVENT_STA_DISCONNECTED:
		/* Ignore SoftAP client disconnects when no STA attempt is running */
		if (provisioning_active && !sta_connect_in_progress) {
			break;
		}

		LOG_INF("[Provision] STA disconnected");
		sta_ready = false;
		sta_connect_in_progress = false;

		if (sta_retry_pending) {
			/* Already scheduled by timeout handler — don't double-schedule */
			break;
		}

		if (blocking_connect_active) {
			/* Retry is managed by connect_timeout_work; reschedule here for
			 * fast retry on immediate auth failure (no need to wait full 30 s) */
			if (connect_attempt < PROVISION_STA_MAX_ATTEMPTS) {
				(void)k_work_cancel_delayable(&connect_timeout_work);
				sta_retry_pending = true;
				LOG_INF("Blocking connect attempt %u failed; retry in %d ms",
					(unsigned int)connect_attempt,
					PROVISION_STA_RETRY_DELAY_MS);
				schedule_sta_connect_after_ms(PROVISION_STA_RETRY_DELAY_MS);
			}
			/* If all attempts done, connect_timeout_work already signalled */
			break;
		}

		if (config_module_has_wifi_credentials()) {
			if (mode == PROVISION_MODE_CONNECTING &&
			    connect_attempt < PROVISION_STA_MAX_ATTEMPTS) {
				/* Initial boot connect: retry within attempt budget */
				(void)k_work_cancel_delayable(&connect_timeout_work);
				sta_retry_pending = true;
				LOG_INF("STA failed; retry %u/%u in %d ms",
					(unsigned int)(connect_attempt + 1U),
					(unsigned int)PROVISION_STA_MAX_ATTEMPTS,
					PROVISION_STA_RETRY_DELAY_MS);
				schedule_sta_connect_after_ms(PROVISION_STA_RETRY_DELAY_MS);
			} else {
				/* Lost connection after being operational, or exceeded
				 * initial attempts: reconnect every 60 s indefinitely */
				persistent_reconnect = true;
				set_mode(PROVISION_MODE_CONNECTING);
				connect_attempt = 0;
				LOG_INF("STA lost; persistent reconnect in %d ms",
					PROVISION_STA_RECONNECT_DELAY_MS);
				(void)k_work_reschedule(&sta_reconnect_work,
							K_MSEC(PROVISION_STA_RECONNECT_DELAY_MS));
			}
		} else {
			(void)enter_provisioning_mode();
		}
		break;

	default:
		break;
	}
}

/* ---------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/

int provision_module_connect_blocking(const wifi_sta_cfg_t *cfg, int32_t timeout_ms)
{
	int ret;

	if (cfg == NULL || cfg->ssid[0] == '\0') {
		return -EINVAL;
	}

	/* Cancel any pending work */
	k_work_cancel_delayable(&sta_connect_work);
	k_work_cancel_delayable(&connect_timeout_work);
	k_work_cancel_delayable(&sta_reconnect_work);
	k_work_cancel_delayable(&boot_fallback_ap_work);
	k_work_cancel_delayable(&sta_start_after_response_work);
	k_work_cancel_delayable(&stop_ap_work);

	(void)wifi_manager_disconnect_sta();
	k_msleep(300); /* let disconnect settle */

	pending_sta_cfg = *cfg;
	use_pending_cfg = true;
	connect_attempt = 0;
	sta_ready = false;
	sta_connect_in_progress = false;
	sta_retry_pending = false;
	persistent_reconnect = false;
	connect_result_success = false;
	blocking_connect_active = true;

	k_sem_reset(&connect_result_sem);

	/* Start connect — SoftAP stays up so the HTTP connection remains alive */
	schedule_sta_connect_after_ms(0);

	/* Block the API thread until success, failure, or timeout */
	ret = k_sem_take(&connect_result_sem, K_MSEC(timeout_ms));
	blocking_connect_active = false;

	if (ret < 0) {
		/* Semaphore timed out (shouldn't happen with correctly set timeout_ms) */
		k_work_cancel_delayable(&sta_connect_work);
		k_work_cancel_delayable(&connect_timeout_work);
		(void)wifi_manager_disconnect_sta();
		LOG_WRN("Blocking connect semaphore timed out");
		return -ETIMEDOUT;
	}

	return connect_result_success ? 0 : -EIO;
}

void provision_module_schedule_ap_stop(int32_t delay_ms)
{
	k_work_reschedule(&stop_ap_work, K_MSEC(delay_ms));
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
	k_work_init_delayable(&stop_ap_work, stop_ap_work_handler);

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
	schedule_sta_connect_after_ms(0);
	return 0;
}

int provision_module_submit_wifi(const wifi_sta_cfg_t *cfg)
{
	int ret;

	if (cfg == NULL || !config_module_wifi_credentials_valid(cfg->ssid, cfg->psk)) {
		return -EINVAL;
	}

	ret = config_module_save_wifi_config(cfg);
	if (ret < 0) {
		return ret;
	}

	load_sta_cfg_from_config(&pending_sta_cfg);
	connect_attempt = 0;
	sta_ready = false;
	sta_retry_pending = false;

	(void)k_work_cancel_delayable(&sta_reconnect_work);
	(void)k_work_cancel_delayable(&boot_fallback_ap_work);
	(void)k_work_cancel_delayable(&sta_start_after_response_work);
	(void)wifi_manager_disconnect_sta();

	if (provisioning_active) {
		LOG_INF("Wi-Fi credentials saved; STA connect starts %d ms after response",
			PROVISION_STA_START_AFTER_RESPONSE_MS);
		(void)k_work_reschedule(&sta_start_after_response_work,
					K_MSEC(PROVISION_STA_START_AFTER_RESPONSE_MS));
		return 0;
	}

	schedule_sta_connect_after_ms(0);
	return 0;
}

int provision_module_clear_wifi_and_reprovision(void)
{
	(void)k_work_cancel_delayable(&sta_connect_work);
	(void)k_work_cancel_delayable(&connect_timeout_work);
	(void)k_work_cancel_delayable(&sta_reconnect_work);
	(void)k_work_cancel_delayable(&boot_fallback_ap_work);
	(void)k_work_cancel_delayable(&sta_start_after_response_work);
	(void)k_work_cancel_delayable(&stop_ap_work);

	sta_ready = false;
	sta_connect_in_progress = false;
	sta_retry_pending = false;
	persistent_reconnect = false;
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
