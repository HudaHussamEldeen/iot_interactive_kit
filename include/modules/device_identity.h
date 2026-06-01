#ifndef MODULES_DEVICE_IDENTITY_H_
#define MODULES_DEVICE_IDENTITY_H_

#include <stddef.h>

#define DEVICE_IDENTITY_DEVICE_ID_MAX 32
#define DEVICE_IDENTITY_SOFTAP_SSID_MAX 33
#define DEVICE_IDENTITY_SOFTAP_PSK_MAX 65
#define DEVICE_IDENTITY_MAC_STR_MAX 18

struct device_identity {
	char device_id[DEVICE_IDENTITY_DEVICE_ID_MAX];
	char softap_ssid[DEVICE_IDENTITY_SOFTAP_SSID_MAX];
	char softap_psk[DEVICE_IDENTITY_SOFTAP_PSK_MAX];
	char sta_mac[DEVICE_IDENTITY_MAC_STR_MAX];
	char ap_mac[DEVICE_IDENTITY_MAC_STR_MAX];
};

int device_identity_init(struct device_identity *identity);
const struct device_identity *device_identity_get(void);

#endif /* MODULES_DEVICE_IDENTITY_H_ */
