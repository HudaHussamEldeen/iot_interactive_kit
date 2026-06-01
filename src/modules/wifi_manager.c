#include "modules/wifi_manager.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/dhcpv4_server.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>

#include "device_profile.h"

LOG_MODULE_REGISTER(wifi_manager, LOG_LEVEL_INF);

#define WIFI_MANAGER_EVENT_MASK                                              \
	(NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT |  \
	 NET_EVENT_WIFI_AP_ENABLE_RESULT | NET_EVENT_WIFI_AP_DISABLE_RESULT | \
	 NET_EVENT_WIFI_AP_STA_CONNECTED | NET_EVENT_WIFI_AP_STA_DISCONNECTED)

static struct net_if *ap_iface;
static struct net_if *sta_iface;
static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback ipv4_cb;

static wifi_manager_cb_t app_cb;
static void *app_cb_user_data;
static wifi_sta_cfg_t current_sta_cfg;
static bool ap_active;
static bool ap_start_requested;
static bool ap_stop_requested;
static bool ap_ipv4_configured;
static bool ap_dhcp_running;
static bool ap_client_connected;
static bool sta_ready_notified;
static bool sta_link_connected;
static bool sta_connect_requested;
static struct wifi_manager_status link_status;

static int wifi_manager_setup_ap_ipv4(void);
static int wifi_manager_start_ap_dhcp(void);

static void format_mac_string(const uint8_t *mac, char *out, size_t out_len)
{
	if (mac == NULL || out == NULL || out_len == 0U) {
		return;
	}

	snprintk(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X",
		 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static const char *link_state_name(wifi_link_state_t state)
{
	switch (state) {
	case WIFI_LINK_STATE_CONNECTING:
		return "connecting";
	case WIFI_LINK_STATE_CONNECTED_NO_IP:
		return "connected_no_ip";
	case WIFI_LINK_STATE_CONNECTED:
		return "connected";
	case WIFI_LINK_STATE_DISCONNECTED:
	default:
		return "disconnected";
	}
}

static void wifi_manager_set_last_event(const char *event)
{
	snprintk(link_status.last_event, sizeof(link_status.last_event), "%s", event);
}

static void wifi_manager_set_link_state(wifi_link_state_t state)
{
	if (link_status.link_state == state) {
		return;
	}

	link_status.link_state = state;
	LOG_INF("[WiFi] link_state=%s event=%s ssid=%s ip=%s",
		link_state_name(state), link_status.last_event, link_status.sta_ssid,
		link_status.sta_ipv4[0] != '\0' ? link_status.sta_ipv4 : "none");
}

static void wifi_manager_notify_sta_ready(const char *reason)
{
	char ip[NET_IPV4_ADDR_LEN];

	if (sta_ready_notified) {
		return;
	}

	if (!sta_connect_requested) {
		LOG_DBG("Ignoring STA IPv4 (no connect requested)");
		return;
	}

	if (wifi_manager_get_sta_ipv4(ip, sizeof(ip)) != 0 || ip[0] == '\0') {
		return;
	}

	sta_ready_notified = true;
	snprintk(link_status.sta_ipv4, sizeof(link_status.sta_ipv4), "%s", ip);
	wifi_manager_set_last_event(reason != NULL ? reason : "ipv4_assigned");
	wifi_manager_set_link_state(WIFI_LINK_STATE_CONNECTED);

	LOG_INF("[WiFi] STA assigned IPv4 %s (%s)", ip, reason ? reason : "ready");

	if (app_cb != NULL) {
		app_cb(WIFI_MANAGER_EVENT_STA_CONNECTED, app_cb_user_data);
	}
}

static void wifi_manager_clear_sta_ipv4_state(void)
{
	struct net_in_addr *existing_addr;

	if (sta_iface == NULL) {
		return;
	}

	(void)net_dhcpv4_stop(sta_iface);

	existing_addr = net_if_ipv4_get_global_addr(sta_iface, NET_ADDR_PREFERRED);
	if (existing_addr != NULL) {
		(void)net_if_ipv4_addr_rm(sta_iface, existing_addr);
	}
}

static void wifi_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			       struct net_if *iface)
{
	char mac_string[sizeof("XX:XX:XX:XX:XX:XX")];

	switch (mgmt_event) {
	case NET_EVENT_WIFI_CONNECT_RESULT: {
		const struct wifi_status *status = cb->info;
		int st = status != NULL ? status->status : -EINVAL;

		if (!sta_connect_requested) {
			LOG_DBG("Ignoring STA connect result (not requested)");
			break;
		}

		if (st != 0) {
			LOG_WRN("[WiFi] STA connect failed status=%d ssid=%s", st,
				link_status.sta_ssid);
			sta_ready_notified = false;
			sta_link_connected = false;
			link_status.sta_ipv4[0] = '\0';
			wifi_manager_set_last_event("connect_failed");
			wifi_manager_set_link_state(WIFI_LINK_STATE_DISCONNECTED);
			if (app_cb != NULL) {
				app_cb(WIFI_MANAGER_EVENT_STA_DISCONNECTED, app_cb_user_data);
			}
			break;
		}

		LOG_INF("[WiFi] STA connected (link up), waiting for IPv4 ssid=%s",
			link_status.sta_ssid);
		sta_link_connected = true;
		wifi_manager_set_last_event("link_up");
		wifi_manager_set_link_state(WIFI_LINK_STATE_CONNECTED_NO_IP);
		net_dhcpv4_start(sta_iface);
		LOG_INF("[WiFi] STA DHCP client started iface=%p", sta_iface);
		break;
	}
	case NET_EVENT_WIFI_DISCONNECT_RESULT:
	{
		const struct wifi_status *status = cb->info;
		int st = status != NULL ? status->status : 0;

		if (!sta_connect_requested && !sta_link_connected) {
			break;
		}

		LOG_INF("[WiFi] STA disconnected status=%d iface=%p ssid=%s last_ip=%s",
			st, iface, link_status.sta_ssid,
			link_status.sta_ipv4[0] != '\0' ? link_status.sta_ipv4 : "none");
		sta_ready_notified = false;
		sta_link_connected = false;
		sta_connect_requested = false;
		link_status.sta_ipv4[0] = '\0';
		wifi_manager_clear_sta_ipv4_state();
		wifi_manager_set_last_event("disconnected");
		wifi_manager_set_link_state(WIFI_LINK_STATE_DISCONNECTED);
		if (app_cb != NULL) {
			app_cb(WIFI_MANAGER_EVENT_STA_DISCONNECTED, app_cb_user_data);
		}
		break;
	}
	case NET_EVENT_WIFI_AP_ENABLE_RESULT:
	{
		const struct wifi_status *status = cb->info;
		int st = status != NULL ? status->status : 0;

		if (!ap_start_requested) {
			LOG_WRN("[WiFi] Ignoring unexpected SoftAP start event status=%d iface=%p",
				st, iface);
			break;
		}

		ap_start_requested = false;
		ap_stop_requested = false;
		ap_active = true;
		LOG_INF("[WiFi] SoftAP started status=%d iface=%p", st, iface);
		if (wifi_manager_start_ap_dhcp() != 0) {
			LOG_ERR("SoftAP DHCP start failed");
		}
		if (app_cb != NULL) {
			app_cb(WIFI_MANAGER_EVENT_AP_STARTED, app_cb_user_data);
		}
		break;
	}
	case NET_EVENT_WIFI_AP_DISABLE_RESULT:
	{
		const struct wifi_status *status = cb->info;
		int st = status != NULL ? status->status : 0;

		if (!ap_stop_requested && !ap_active) {
			LOG_WRN("[WiFi] Ignoring unexpected SoftAP stop event status=%d iface=%p",
				st, iface);
			break;
		}

		ap_stop_requested = false;
		ap_start_requested = false;
		ap_active = false;
		ap_dhcp_running = false;
		LOG_INF("[WiFi] SoftAP stopped status=%d iface=%p", st, iface);
		if (app_cb != NULL) {
			app_cb(WIFI_MANAGER_EVENT_AP_STOPPED, app_cb_user_data);
		}
		break;
	}
	case NET_EVENT_WIFI_AP_STA_CONNECTED:
	{
		const struct wifi_ap_sta_info *sta_info = cb->info;

		if (!ap_active) {
			LOG_WRN("[WiFi] Ignoring SoftAP client connected while AP inactive");
			break;
		}

		ap_client_connected = true;
		if (sta_info != NULL && sta_info->mac_length > 0U) {
			format_mac_string(sta_info->mac, mac_string, sizeof(mac_string));
			LOG_INF("[WiFi] SoftAP client connected mac=%s link_mode=%d twt=%d",
				mac_string, sta_info->link_mode, sta_info->twt_capable);
		} else {
			LOG_INF("[WiFi] SoftAP client connected (no station info)");
		}
		if (app_cb != NULL) {
			app_cb(WIFI_MANAGER_EVENT_AP_CLIENT_CONNECTED, app_cb_user_data);
		}
		break;
	}
	case NET_EVENT_WIFI_AP_STA_DISCONNECTED:
	{
		const struct wifi_ap_sta_info *sta_info = cb->info;

		if (!ap_active) {
			LOG_WRN("[WiFi] Ignoring SoftAP client disconnected while AP inactive");
			break;
		}

		ap_client_connected = false;
		if (sta_info != NULL && sta_info->mac_length > 0U) {
			format_mac_string(sta_info->mac, mac_string, sizeof(mac_string));
			LOG_INF("[WiFi] SoftAP client disconnected mac=%s link_mode=%d twt=%d",
				mac_string, sta_info->link_mode, sta_info->twt_capable);
		} else {
			LOG_INF("[WiFi] SoftAP client disconnected (no station info)");
		}
		if (app_cb != NULL) {
			app_cb(WIFI_MANAGER_EVENT_AP_CLIENT_DISCONNECTED, app_cb_user_data);
		}
		break;
	}
	default:
		break;
	}
}

static void ipv4_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			       struct net_if *iface)
{
	ARG_UNUSED(cb);

	if (sta_iface == NULL || iface != sta_iface || !sta_link_connected) {
		return;
	}

	switch (mgmt_event) {
	case NET_EVENT_IPV4_DHCP_BOUND:
		wifi_manager_notify_sta_ready("DHCP bound");
		break;
	case NET_EVENT_IPV4_ADDR_ADD:
		wifi_manager_notify_sta_ready("IPv4 address added");
		break;
	default:
		break;
	}
}

static int wifi_manager_setup_ap_ipv4(void)
{
	struct net_in_addr addr;
	struct net_in_addr netmask_addr;

	if (ap_iface == NULL) {
		return -ENODEV;
	}

	if (ap_ipv4_configured) {
		return 0;
	}

	if (net_addr_pton(AF_INET, DEVICE_PORTAL_AP_IP, &addr) != 0 ||
	    net_addr_pton(AF_INET, DEVICE_PORTAL_NETMASK, &netmask_addr) != 0) {
		return -EINVAL;
	}

	(void)net_if_up(ap_iface);
	net_if_ipv4_set_gw(ap_iface, &addr);

	if (net_if_ipv4_addr_add(ap_iface, &addr, NET_ADDR_MANUAL, 0) == NULL) {
		return -EIO;
	}

	if (!net_if_ipv4_set_netmask_by_addr(ap_iface, &addr, &netmask_addr)) {
		return -EIO;
	}

	ap_ipv4_configured = true;
	LOG_INF("SoftAP IPv4 %s", DEVICE_PORTAL_AP_IP);
	return 0;
}

static int wifi_manager_start_ap_dhcp(void)
{
	struct net_in_addr addr;
	char pool_start[NET_IPV4_ADDR_LEN];

	if (ap_iface == NULL || ap_dhcp_running) {
		return ap_iface == NULL ? -ENODEV : 0;
	}

	if (net_addr_pton(AF_INET, DEVICE_PORTAL_AP_IP, &addr) != 0) {
		return -EINVAL;
	}

	addr.s4_addr[3] += 10;

	if (net_dhcpv4_server_start(ap_iface, &addr) != 0) {
		return -EIO;
	}

	ap_dhcp_running = true;
	(void)net_addr_ntop(AF_INET, &addr, pool_start, sizeof(pool_start));
	LOG_INF("SoftAP DHCP server started pool_start=%s", pool_start);
	return 0;
}

int wifi_manager_init(wifi_manager_cb_t cb, void *user_data)
{
	app_cb = cb;
	app_cb_user_data = user_data;

	ap_iface = net_if_get_wifi_sap();
	sta_iface = net_if_get_wifi_sta();

	if (sta_iface == NULL) {
		sta_iface = net_if_get_default();
	}

	if (ap_iface == NULL) {
		ap_iface = sta_iface;
	}

	if (sta_iface == NULL) {
		return -ENODEV;
	}

	net_mgmt_init_event_callback(&wifi_cb, wifi_event_handler, WIFI_MANAGER_EVENT_MASK);
	net_mgmt_add_event_callback(&wifi_cb);
	net_mgmt_init_event_callback(&ipv4_cb, ipv4_event_handler,
				     NET_EVENT_IPV4_DHCP_BOUND | NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&ipv4_cb);

	memset(&link_status, 0, sizeof(link_status));
	link_status.link_state = WIFI_LINK_STATE_DISCONNECTED;
	snprintk(link_status.last_event, sizeof(link_status.last_event), "init");

	LOG_INF("wifi_manager ready sta=%p ap=%p", sta_iface, ap_iface);
	return 0;
}

int wifi_manager_start_ap(const wifi_ap_cfg_t *cfg)
{
	struct wifi_connect_req_params ap = {0};
	int ret;

	if (cfg == NULL || ap_iface == NULL) {
		return -EINVAL;
	}

	ret = wifi_manager_setup_ap_ipv4();
	if (ret < 0) {
		return ret;
	}

	ap.ssid = (const uint8_t *)cfg->ssid;
	ap.ssid_length = strlen(cfg->ssid);
	ap.channel = cfg->channel ? cfg->channel : WIFI_CHANNEL_ANY;
	ap.band = WIFI_FREQ_BAND_2_4_GHZ;

	if (cfg->open || cfg->psk[0] == '\0') {
		ap.security = WIFI_SECURITY_TYPE_NONE;
	} else {
		ap.security = WIFI_SECURITY_TYPE_PSK;
		ap.psk = (const uint8_t *)cfg->psk;
		ap.psk_length = strlen(cfg->psk);
	}

	LOG_INF("Starting SoftAP ssid=%s channel=%u band=2.4GHz security=%s iface=%p",
		cfg->ssid, ap.channel,
		ap.security == WIFI_SECURITY_TYPE_NONE ? "open" : "psk", ap_iface);
	ap_start_requested = true;
	ap_stop_requested = false;
	return net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, ap_iface, &ap, sizeof(ap));
}

int wifi_manager_stop_ap(void)
{
	if (ap_iface == NULL) {
		return -ENODEV;
	}

	LOG_WRN("Stopping SoftAP (SSID will disappear from scan)");
	(void)net_dhcpv4_server_stop(ap_iface);
	ap_dhcp_running = false;
	ap_client_connected = false;
	ap_stop_requested = true;
	ap_start_requested = false;

	(void)net_mgmt(NET_REQUEST_WIFI_AP_DISABLE, ap_iface, NULL, 0);
	(void)net_if_down(ap_iface);

	return 0;
}

int wifi_manager_connect_sta(const wifi_sta_cfg_t *cfg)
{
	struct wifi_connect_req_params sta = {0};
	size_t psk_len;
	int ret;

	if (cfg == NULL || sta_iface == NULL) {
		return -EINVAL;
	}

	current_sta_cfg = *cfg;
	sta_connect_requested = true;
	sta_ready_notified = false;
	sta_link_connected = false;
	link_status.sta_ipv4[0] = '\0';
	snprintk(link_status.sta_ssid, sizeof(link_status.sta_ssid), "%s", cfg->ssid);
	wifi_manager_set_last_event("connecting");
	wifi_manager_set_link_state(WIFI_LINK_STATE_CONNECTING);
	wifi_manager_clear_sta_ipv4_state();

	ret = net_if_up(sta_iface);
	if ((ret < 0) && (ret != -EALREADY)) {
		LOG_WRN("STA net_if_up failed: %d", ret);
	}

	sta.ssid = (const uint8_t *)cfg->ssid;
	sta.ssid_length = strlen(cfg->ssid);
	psk_len = strlen(cfg->psk);
	sta.psk = (const uint8_t *)cfg->psk;
	sta.psk_length = psk_len;
	sta.security = psk_len > 0U ? WIFI_SECURITY_TYPE_PSK : WIFI_SECURITY_TYPE_NONE;
	sta.channel = WIFI_CHANNEL_ANY;
	sta.band = WIFI_FREQ_BAND_2_4_GHZ;

	LOG_INF("[WiFi] STA connecting ssid=%s", cfg->ssid);
	return net_mgmt(NET_REQUEST_WIFI_CONNECT, sta_iface, &sta, sizeof(sta));
}

int wifi_manager_disconnect_sta(void)
{
	if (sta_iface == NULL) {
		return -ENODEV;
	}

	LOG_INF("[WiFi] STA disconnect requested ssid=%s", link_status.sta_ssid);
	sta_connect_requested = false;
	sta_ready_notified = false;
	sta_link_connected = false;
	link_status.sta_ipv4[0] = '\0';
	wifi_manager_clear_sta_ipv4_state();
	return net_mgmt(NET_REQUEST_WIFI_DISCONNECT, sta_iface, NULL, 0);
}

int wifi_manager_get_sta_ipv4(char *buf, size_t buf_len)
{
	struct net_in_addr *addr;

	if (buf == NULL || buf_len == 0U || sta_iface == NULL) {
		return -EINVAL;
	}

	buf[0] = '\0';
	addr = net_if_ipv4_get_global_addr(sta_iface, NET_ADDR_PREFERRED);
	if (addr == NULL) {
		return -ENOENT;
	}

	return net_addr_ntop(AF_INET, addr, buf, buf_len) ? 0 : -EINVAL;
}

bool wifi_manager_is_ap_active(void)
{
	return ap_active;
}

bool wifi_manager_is_sta_link_up(void)
{
	return sta_link_connected;
}

void wifi_manager_get_status(struct wifi_manager_status *status)
{
	if (status == NULL) {
		return;
	}

	*status = link_status;

	if (link_status.link_state >= WIFI_LINK_STATE_CONNECTED_NO_IP) {
		char ip[NET_IPV4_ADDR_LEN];

		if (wifi_manager_get_sta_ipv4(ip, sizeof(ip)) == 0 && ip[0] != '\0') {
			snprintk(status->sta_ipv4, sizeof(status->sta_ipv4), "%s", ip);
		}
	}
}
