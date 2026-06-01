#ifndef MODULES_WIFI_MANAGER_H_
#define MODULES_WIFI_MANAGER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WIFI_MANAGER_SSID_MAX_LEN 32
#define WIFI_MANAGER_PSK_MAX_LEN 64

typedef enum {
	WIFI_MANAGER_EVENT_STA_CONNECTED,
	WIFI_MANAGER_EVENT_STA_DISCONNECTED,
	WIFI_MANAGER_EVENT_AP_STARTED,
	WIFI_MANAGER_EVENT_AP_STOPPED,
	WIFI_MANAGER_EVENT_AP_CLIENT_CONNECTED,
	WIFI_MANAGER_EVENT_AP_CLIENT_DISCONNECTED,
} wifi_manager_event_t;

typedef struct {
	char ssid[WIFI_MANAGER_SSID_MAX_LEN + 1];
	char psk[WIFI_MANAGER_PSK_MAX_LEN + 1];
	uint8_t channel;
	bool open;
} wifi_ap_cfg_t;

typedef struct {
	char ssid[WIFI_MANAGER_SSID_MAX_LEN + 1];
	char psk[WIFI_MANAGER_PSK_MAX_LEN + 1];
} wifi_sta_cfg_t;

typedef void (*wifi_manager_cb_t)(wifi_manager_event_t event, void *user_data);

typedef enum {
	WIFI_LINK_STATE_DISCONNECTED = 0,
	WIFI_LINK_STATE_CONNECTING,
	WIFI_LINK_STATE_CONNECTED_NO_IP,
	WIFI_LINK_STATE_CONNECTED,
} wifi_link_state_t;

struct wifi_manager_status {
	wifi_link_state_t link_state;
	char sta_ssid[WIFI_MANAGER_SSID_MAX_LEN + 1];
	char sta_ipv4[16];
	char last_event[24];
};

int wifi_manager_init(wifi_manager_cb_t cb, void *user_data);
int wifi_manager_start_ap(const wifi_ap_cfg_t *cfg);
int wifi_manager_stop_ap(void);
int wifi_manager_connect_sta(const wifi_sta_cfg_t *cfg);
int wifi_manager_disconnect_sta(void);
int wifi_manager_get_sta_ipv4(char *buf, size_t buf_len);
bool wifi_manager_is_ap_active(void);
bool wifi_manager_is_sta_link_up(void);
void wifi_manager_get_status(struct wifi_manager_status *status);

#endif /* MODULES_WIFI_MANAGER_H_ */
