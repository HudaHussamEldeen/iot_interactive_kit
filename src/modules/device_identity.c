#include "modules/device_identity.h"

#include <stdio.h>
#include <string.h>

#include <esp_mac.h>
#include <zephyr/logging/log.h>

#include "device_profile.h"

LOG_MODULE_REGISTER(device_identity, LOG_LEVEL_INF);

static struct device_identity identity;

static void format_mac_string(const uint8_t *mac, char *out, size_t out_len)
{
	snprintk(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X",
		 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

int device_identity_init(struct device_identity *out)
{
	uint8_t sta_mac[6];
	uint8_t ap_mac[6];
	int ret;

	memset(&identity, 0, sizeof(identity));

	ret = esp_read_mac(sta_mac, ESP_MAC_WIFI_STA);
	if (ret != 0) {
		LOG_WRN("STA MAC read failed (%d), using fallback identity", ret);
		snprintk(identity.device_id, sizeof(identity.device_id), "%s-000000",
			 DEVICE_PRODUCT_NAME);
		snprintk(identity.softap_ssid, sizeof(identity.softap_ssid), "%s-Setup",
			 DEVICE_PRODUCT_NAME);
		snprintk(identity.softap_psk, sizeof(identity.softap_psk), "IotKit0000");
		strcpy(identity.sta_mac, "00:00:00:00:00:00");
		strcpy(identity.ap_mac, "00:00:00:00:00:00");
	} else {
		(void)esp_read_mac(ap_mac, ESP_MAC_WIFI_SOFTAP);

		snprintk(identity.device_id, sizeof(identity.device_id),
			 "%s-%02X%02X%02X%02X%02X%02X", DEVICE_PRODUCT_NAME, sta_mac[0],
			 sta_mac[1], sta_mac[2], sta_mac[3], sta_mac[4], sta_mac[5]);

		snprintk(identity.softap_ssid, sizeof(identity.softap_ssid), "%s-%02X%02X",
			 DEVICE_PRODUCT_NAME, ap_mac[4], ap_mac[5]);

		snprintk(identity.softap_psk, sizeof(identity.softap_psk), "IotKit%02X%02X",
			 ap_mac[4], ap_mac[5]);

		format_mac_string(sta_mac, identity.sta_mac, sizeof(identity.sta_mac));
		format_mac_string(ap_mac, identity.ap_mac, sizeof(identity.ap_mac));
	}

	LOG_INF("Device identity: id=%s softap=%s sta_mac=%s",
		identity.device_id, identity.softap_ssid, identity.sta_mac);

	if (out != NULL) {
		*out = identity;
	}

	return 0;
}

const struct device_identity *device_identity_get(void)
{
	return &identity;
}
