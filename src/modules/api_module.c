#include "modules/api_module.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/util.h>

#include "device_profile.h"
#include "modules/config_module.h"
#include "modules/device_identity.h"
#include "modules/mpu6050_module.h"
#include "modules/provision_module.h"
#include "modules/servo_module.h"
#include "modules/vl6180x_module.h"

LOG_MODULE_REGISTER(api_module, LOG_LEVEL_INF);

#define HTTP_RX_BUF_SIZE 1024
#define HTTP_TX_BUF_SIZE 1536
#define HTTP_CLIENT_TIMEOUT_MS 5000
#define API_THREAD_STACK_SIZE 12288

static K_SEM_DEFINE(api_start_sem, 0, 1);
static K_MUTEX_DEFINE(api_lock);
static int server_fd = -1;
static char http_tx_buf[HTTP_TX_BUF_SIZE];

static void api_close_listener(void)
{
	int fd;

	k_mutex_lock(&api_lock, K_FOREVER);
	fd = server_fd;
	server_fd = -1;
	k_mutex_unlock(&api_lock);

	if (fd >= 0) {
		(void)zsock_close(fd);
	}
}

void api_module_restart(void)
{
	/* No-op: keep listener bound for entire provisioning session. */
}

static int http_server_open(void)
{
	struct sockaddr_in bind_addr = {
		.sin_family = AF_INET,
		.sin_port = htons(DEVICE_API_PORT),
		.sin_addr.s_addr = htonl(INADDR_ANY),
	};
	int fd;
	int reuse = 1;

	fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (fd < 0) {
		LOG_ERR("HTTP socket create failed: %d", errno);
		return -errno;
	}

	(void)zsock_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

	if (zsock_bind(fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
		LOG_ERR("HTTP bind failed: %d", errno);
		(void)zsock_close(fd);
		return -errno;
	}

	if (zsock_listen(fd, 4) < 0) {
		LOG_ERR("HTTP listen failed: %d", errno);
		(void)zsock_close(fd);
		return -errno;
	}

	LOG_INF("HTTP server ready on 0.0.0.0:%d (SoftAP %s)", DEVICE_API_PORT,
		DEVICE_PORTAL_AP_IP);
	return fd;
}

static const char *provision_mode_to_string(enum provision_mode mode)
{
	switch (mode) {
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

static int send_response(int fd, int status, const char *status_text, const char *content_type,
			 const char *body)
{
	int len;
	int sent_total = 0;

	len = snprintk(http_tx_buf, sizeof(http_tx_buf),
		       "HTTP/1.1 %d %s\r\n"
		       "Content-Type: %s\r\n"
		       "Content-Length: %u\r\n"
		       "Connection: close\r\n\r\n"
		       "%s",
		       status, status_text, content_type, (unsigned int)strlen(body), body);

	if (len < 0 || len >= sizeof(http_tx_buf)) {
		LOG_ERR("HTTP response too large: %d", len);
		return -EMSGSIZE;
	}

	while (sent_total < len) {
		struct zsock_pollfd pollfd = {
			.fd = fd,
			.events = ZSOCK_POLLOUT,
		};
		int poll_rc;
		int sent;

		poll_rc = zsock_poll(&pollfd, 1, HTTP_CLIENT_TIMEOUT_MS);
		if (poll_rc <= 0) {
			LOG_WRN("HTTP response send poll failed rc=%d errno=%d", poll_rc, errno);
			return poll_rc < 0 ? -errno : -ETIMEDOUT;
		}

		sent = zsock_send(fd, http_tx_buf + sent_total, len - sent_total,
				  ZSOCK_MSG_DONTWAIT);
		if (sent < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				continue;
			}

			LOG_WRN("HTTP response send failed: %d", errno);
			return -errno;
		}

		if (sent == 0) {
			return -EPIPE;
		}

		sent_total += sent;
		LOG_INF("HTTP sent chunk=%d total=%d/%d", sent, sent_total, len);
	}

	return sent_total;
}

static int http_content_length(const char *request)
{
	const char *header = request;

	while (header != NULL && *header != '\0') {
		const char *line_end = strstr(header, "\r\n");
		const char *value;
		int length = 0;

		if (line_end == NULL || line_end == header) {
			break;
		}

		if (strncmp(header, "Content-Length:", strlen("Content-Length:")) != 0) {
			header = line_end + 2;
			continue;
		}

		value = header + strlen("Content-Length:");
		while (*value == ' ' || *value == '\t') {
			value++;
		}

		while (value < line_end && *value >= '0' && *value <= '9') {
			length = (length * 10) + (*value - '0');
			value++;
		}

		return length;
	}

	return 0;
}

static int receive_http_request(int fd, char *request, size_t request_size)
{
	size_t used = 0;
	size_t expected = 0;

	while (used < request_size - 1U) {
		struct zsock_pollfd pollfd = {
			.fd = fd,
			.events = ZSOCK_POLLIN,
		};
		char *header_end;
		int poll_rc;
		int rx_len;

		poll_rc = zsock_poll(&pollfd, 1, HTTP_CLIENT_TIMEOUT_MS);
		if (poll_rc <= 0) {
			LOG_WRN("HTTP request receive timeout/err rc=%d errno=%d used=%u",
				poll_rc, errno, (unsigned int)used);
			return poll_rc < 0 ? -errno : -ETIMEDOUT;
		}

		rx_len = zsock_recv(fd, request + used, request_size - 1U - used,
				    ZSOCK_MSG_DONTWAIT);
		if (rx_len < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				continue;
			}

			LOG_WRN("HTTP request recv failed: %d", errno);
			return -errno;
		}

		if (rx_len == 0) {
			LOG_WRN("HTTP peer closed while receiving used=%u expected=%u",
				(unsigned int)used, (unsigned int)expected);
			return used > 0U ? (int)used : -ECONNRESET;
		}

		used += rx_len;
		request[used] = '\0';
		LOG_INF("HTTP recv chunk=%d total=%u", rx_len, (unsigned int)used);

		header_end = strstr(request, "\r\n\r\n");
		if (header_end == NULL) {
			continue;
		}

		if (expected == 0U) {
			expected = (size_t)(header_end - request) + 4U +
				   (size_t)http_content_length(request);
			LOG_INF("HTTP headers complete header_len=%u expected_total=%u",
				(unsigned int)((header_end - request) + 4),
				(unsigned int)expected);
			if (expected >= request_size) {
				return -EMSGSIZE;
			}
		}

		if (used >= expected) {
			return (int)used;
		}
	}

	return -EMSGSIZE;
}

static bool json_get_string(const char *json, const char *key, char *out, size_t out_len)
{
	char pattern[36];
	const char *start;
	const char *end;
	size_t len;

	if (json == NULL || key == NULL || out == NULL || out_len == 0U) {
		return false;
	}

	snprintk(pattern, sizeof(pattern), "\"%s\"", key);
	start = strstr(json, pattern);
	if (start == NULL) {
		return false;
	}

	start += strlen(pattern);
	while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
		start++;
	}

	if (*start != ':') {
		return false;
	}

	start++;
	while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
		start++;
	}

	if (*start != '"') {
		return false;
	}

	start++;
	end = strchr(start, '"');
	if (end == NULL) {
		return false;
	}

	len = MIN((size_t)(end - start), out_len - 1U);
	memcpy(out, start, len);
	out[len] = '\0';
	return true;
}

static bool json_get_int(const char *json, const char *key, int *out)
{
	char pattern[36];
	const char *start;
	int value = 0;
	bool negative = false;

	if (json == NULL || key == NULL || out == NULL) {
		return false;
	}

	snprintk(pattern, sizeof(pattern), "\"%s\"", key);
	start = strstr(json, pattern);
	if (start == NULL) {
		return false;
	}

	start += strlen(pattern);
	while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
		start++;
	}

	if (*start != ':') {
		return false;
	}

	start++;
	while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
		start++;
	}

	if (*start == '-') {
		negative = true;
		start++;
	}

	if (*start < '0' || *start > '9') {
		return false;
	}

	while (*start >= '0' && *start <= '9') {
		value = value * 10 + (*start - '0');
		start++;
	}

	*out = negative ? -value : value;
	return true;
}

static bool auth_header_valid(const char *request)
{
	struct kit_config config;
	char expected[KIT_API_TOKEN_MAX_LEN + 32];

	config_module_get(&config);
	if (config.api_token[0] == '\0') {
		return false;
	}

	snprintk(expected, sizeof(expected), "Authorization: Bearer %s", config.api_token);
	return strstr(request, expected) != NULL;
}

static const char *wifi_link_state_to_string(wifi_link_state_t state)
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

static int handle_status(int fd)
{
  struct kit_config config;
  const struct device_identity *identity = device_identity_get();
  struct wifi_manager_status wifi_status;
  char body[1200];

  config_module_get(&config);
  provision_module_get_wifi_status(&wifi_status);

  if (wifi_status.sta_ipv4[0] == '\0') {
    snprintk(wifi_status.sta_ipv4, sizeof(wifi_status.sta_ipv4), "0.0.0.0");
  }

  snprintk(body, sizeof(body),
       "{"
       "\"ok\":true,"
       "\"profile_version\":\"%s\","
       "\"device\":{"
       "\"id\":\"%s\","
       "\"product\":\"%s\","
       "\"sta_mac\":\"%s\","
       "\"uptime_s\":%u"
       "},"
       "\"provisioning\":{"
       "\"mode\":\"%s\","
       "\"active\":%s,"
       "\"softap_ssid\":\"%s\","
       "\"softap_ip\":\"%s\","
       "\"api_port\":%d,"
       "\"wifi_provisioned\":%s"
       "},"
       "\"wifi\":{"
       "\"link_state\":\"%s\","
       "\"last_event\":\"%s\","
       "\"sta_ssid\":\"%s\","
       "\"sta_ipv4\":\"%s\","
       "\"ip_mode\":\"%s\","
       "\"sta_ready\":%s,"
       "\"connect_attempt\":%u"
       "}"
       "}",
       DEVICE_WIFI_PROFILE_VERSION, config.device_id, DEVICE_PRODUCT_NAME,
       identity->sta_mac, (unsigned int)(k_uptime_get() / 1000),
       provision_mode_to_string(provision_module_get_mode()),
       provision_module_is_provisioning_active() ? "true" : "false",
       identity->softap_ssid, DEVICE_PORTAL_AP_IP, DEVICE_API_PORT,
       config.wifi_provisioned ? "true" : "false",
       wifi_link_state_to_string(wifi_status.link_state), wifi_status.last_event,
       wifi_status.sta_ssid, wifi_status.sta_ipv4, config.wifi_ip_mode,
       provision_module_is_sta_ready() ? "true" : "false",
       (unsigned int)provision_module_get_connect_attempt());

  return send_response(fd, 200, "OK", "application/json", body);
}

static bool validate_ipv4_string(const char *addr)
{
	struct in_addr in_addr;

	return addr != NULL && addr[0] != '\0' && net_addr_pton(AF_INET, addr, &in_addr) == 0;
}

static int handle_wifi_post(int fd, const char *request, const char *body)
{
	char ssid[KIT_WIFI_SSID_MAX_LEN];
	char psk[KIT_WIFI_PSK_MAX_LEN] = "";
	char ip_mode[KIT_WIFI_IP_MODE_MAX_LEN] = "dhcp";
	char ip_address[KIT_IP_ADDR_MAX_LEN] = "";
	char netmask[KIT_IP_ADDR_MAX_LEN] = "";
	char gateway[KIT_IP_ADDR_MAX_LEN] = "";
	wifi_sta_cfg_t cfg;
	int ret;

	if (provision_module_get_mode() == PROVISION_MODE_OPERATIONAL &&
	    !auth_header_valid(request)) {
		return send_response(fd, 401, "Unauthorized", "application/json",
				     "{\"ok\":false,\"error\":\"bearer token required\"}");
	}

	if (!json_get_string(body, "ssid", ssid, sizeof(ssid)) &&
	    !json_get_string(body, "wifi_ssid", ssid, sizeof(ssid))) {
		return send_response(fd, 400, "Bad Request", "application/json",
				     "{\"ok\":false,\"error\":\"missing ssid\"}");
	}

	(void)json_get_string(body, "psk", psk, sizeof(psk));
	(void)json_get_string(body, "wifi_psk", psk, sizeof(psk));
	(void)json_get_string(body, "ip_mode", ip_mode, sizeof(ip_mode));
	(void)json_get_string(body, "wifi_ip_mode", ip_mode, sizeof(ip_mode));
	(void)json_get_string(body, "ip_address", ip_address, sizeof(ip_address));
	(void)json_get_string(body, "netmask", netmask, sizeof(netmask));
	(void)json_get_string(body, "gateway", gateway, sizeof(gateway));

	if (strcmp(ip_mode, "static") == 0) {
		if (!validate_ipv4_string(ip_address) || !validate_ipv4_string(netmask)) {
			return send_response(fd, 400, "Bad Request", "application/json",
				     "{\"ok\":false,\"error\":\"invalid static IP or netmask\"}");
		}
		if (gateway[0] != '\0' && !validate_ipv4_string(gateway)) {
			return send_response(fd, 400, "Bad Request", "application/json",
				     "{\"ok\":false,\"error\":\"invalid gateway\"}");
		}
	}

	snprintk(cfg.ssid, sizeof(cfg.ssid), "%s", ssid);
	snprintk(cfg.psk, sizeof(cfg.psk), "%s", psk);
	cfg.ip_mode = strcmp(ip_mode, "static") == 0 ? WIFI_IP_MODE_STATIC : WIFI_IP_MODE_DHCP;
	snprintk(cfg.ip_address, sizeof(cfg.ip_address), "%s", ip_address);
	snprintk(cfg.netmask, sizeof(cfg.netmask), "%s", netmask);
	snprintk(cfg.gateway, sizeof(cfg.gateway), "%s", gateway);

	/* Total blocking timeout: all attempts × (connect timeout + retry delay) + margin */
	int32_t timeout_ms = (int32_t)PROVISION_STA_MAX_ATTEMPTS *
			     ((int32_t)PROVISION_STA_CONNECT_TIMEOUT_MS +
			      (int32_t)PROVISION_STA_RETRY_DELAY_MS) + 5000;

	LOG_INF("Attempting WiFi connect ssid=%s ip_mode=%s (blocking, timeout=%d ms)",
		ssid, ip_mode, (int)timeout_ms);

	ret = provision_module_connect_blocking(&cfg, timeout_ms);
	if (ret < 0) {
		LOG_WRN("WiFi connect failed ret=%d ssid=%s", ret, ssid);
		return send_response(fd, 200, "OK", "application/json",
				     "{\"ok\":false,\"error\":\"connection failed\","
				     "\"status\":\"failed\"}");
	}

	/* Connected — now save credentials to NVS */
	ret = config_module_save_wifi_config(&cfg);
	if (ret < 0) {
		LOG_ERR("Failed to save WiFi config: %d", ret);
		return send_response(fd, 500, "Internal Server Error", "application/json",
				     "{\"ok\":false,\"error\":\"failed to save credentials\"}");
	}

	/* Verify the save was successful */
	{
		struct kit_config saved;

		config_module_get(&saved);
		if (strncmp(saved.wifi_ssid, cfg.ssid, sizeof(saved.wifi_ssid) - 1U) != 0) {
			LOG_ERR("Credential verify failed: SSID mismatch after save");
			return send_response(fd, 500, "Internal Server Error", "application/json",
					     "{\"ok\":false,\"error\":\"credential verify failed\"}");
		}
	}

	config_module_mark_wifi_provisioned(true);

	/* Get assigned IP for the response */
	char ip[NET_IPV4_ADDR_LEN];

	if (provision_module_get_sta_ipv4(ip, sizeof(ip)) != 0 || ip[0] == '\0') {
		snprintk(ip, sizeof(ip), "0.0.0.0");
	}

	char resp[256];

	snprintk(resp, sizeof(resp),
		 "{\"ok\":true,\"status\":\"connected\","
		 "\"ssid\":\"%s\",\"ip\":\"%s\"}",
		 cfg.ssid, ip);

	ret = send_response(fd, 200, "OK", "application/json", resp);

	/* Stop SoftAP after response is flushed (AP was kept up during connect) */
	provision_module_schedule_ap_stop(1000);

	return ret;
}

static int handle_config_get(int fd)
{
	struct kit_config config;
	char body[384];

	config_module_get(&config);
	snprintk(body, sizeof(body),
		 "{\"ok\":true,\"data\":{"
		 "\"device_id\":\"%s\","
		 "\"wifi_provisioned\":%s,"
		 "\"api_token_set\":%s"
		 "}}",
		 config.device_id, config.wifi_provisioned ? "true" : "false",
		 config.api_token[0] != '\0' ? "true" : "false");

	return send_response(fd, 200, "OK", "application/json", body);
}

static int handle_config_put(int fd, const char *request, const char *body)
{
	struct kit_config config;

	if (!auth_header_valid(request)) {
		return send_response(fd, 401, "Unauthorized", "application/json",
				     "{\"ok\":false,\"error\":\"invalid bearer token\"}");
	}

	config_module_get(&config);
	(void)json_get_string(body, "device_id", config.device_id, sizeof(config.device_id));
	(void)json_get_string(body, "api_token", config.api_token, sizeof(config.api_token));

	if (config_module_save(&config) < 0) {
		return send_response(fd, 500, "Internal Server Error", "application/json",
				     "{\"ok\":false,\"error\":\"save failed\"}");
	}

	return send_response(fd, 200, "OK", "application/json", "{\"ok\":true}");
}

static int handle_provision_reset_post(int fd, const char *request)
{
	if (!auth_header_valid(request)) {
		return send_response(fd, 401, "Unauthorized", "application/json",
				     "{\"ok\":false,\"error\":\"invalid bearer token\"}");
	}

	if (provision_module_clear_wifi_and_reprovision() < 0) {
		return send_response(fd, 500, "Internal Server Error", "application/json",
				     "{\"ok\":false,\"error\":\"reset failed\"}");
	}

	return send_response(fd, 200, "OK", "application/json",
			     "{\"ok\":true,\"status\":\"provisioning\"}");
}

/* Helpers for formatting sensor_value int pairs as JSON numbers */
#define SV_SIGN(v1, v2) (((v1) < 0 || (v2) < 0) ? "-" : "")
#define SV_ABS(v)       ((int)((v) < 0 ? -(v) : (v)))

static int handle_imu_get(int fd, const char *request)
{
	struct mpu6050_reading r;
	char body[512];
	int ret;

	if (!auth_header_valid(request)) {
		return send_response(fd, 401, "Unauthorized", "application/json",
				     "{\"ok\":false,\"error\":\"invalid bearer token\"}");
	}

	ret = mpu6050_module_read(&r);
	if (ret < 0) {
		return send_response(fd, 503, "Service Unavailable", "application/json",
				     "{\"ok\":false,\"error\":\"IMU not available\"}");
	}

	snprintk(body, sizeof(body),
		 "{\"ok\":true,\"data\":{"
		 "\"accel\":{\"x\":%s%d.%06d,\"y\":%s%d.%06d,\"z\":%s%d.%06d},"
		 "\"gyro\":{\"x\":%s%d.%06d,\"y\":%s%d.%06d,\"z\":%s%d.%06d},"
		 "\"temperature\":%s%d.%06d"
		 "}}",
		 SV_SIGN(r.accel_x_1, r.accel_x_2), SV_ABS(r.accel_x_1), SV_ABS(r.accel_x_2),
		 SV_SIGN(r.accel_y_1, r.accel_y_2), SV_ABS(r.accel_y_1), SV_ABS(r.accel_y_2),
		 SV_SIGN(r.accel_z_1, r.accel_z_2), SV_ABS(r.accel_z_1), SV_ABS(r.accel_z_2),
		 SV_SIGN(r.gyro_x_1,  r.gyro_x_2),  SV_ABS(r.gyro_x_1),  SV_ABS(r.gyro_x_2),
		 SV_SIGN(r.gyro_y_1,  r.gyro_y_2),  SV_ABS(r.gyro_y_1),  SV_ABS(r.gyro_y_2),
		 SV_SIGN(r.gyro_z_1,  r.gyro_z_2),  SV_ABS(r.gyro_z_1),  SV_ABS(r.gyro_z_2),
		 SV_SIGN(r.temp_1,    r.temp_2),     SV_ABS(r.temp_1),    SV_ABS(r.temp_2));

	return send_response(fd, 200, "OK", "application/json", body);
}

static int handle_tof_get(int fd, const char *request)
{
	char body[64];
	uint8_t range_mm;
	int ret;

	if (!auth_header_valid(request)) {
		return send_response(fd, 401, "Unauthorized", "application/json",
				     "{\"ok\":false,\"error\":\"invalid bearer token\"}");
	}

	ret = vl6180x_module_read_range(&range_mm);
	if (ret < 0) {
		return send_response(fd, 503, "Service Unavailable", "application/json",
				     "{\"ok\":false,\"error\":\"ToF sensor not available\"}");
	}

	snprintk(body, sizeof(body),
		 "{\"ok\":true,\"data\":{\"range_mm\":%u}}", (unsigned int)range_mm);

	return send_response(fd, 200, "OK", "application/json", body);
}

static int handle_servo_get(int fd, const char *request)
{
	char body[64];

	if (!auth_header_valid(request)) {
		return send_response(fd, 401, "Unauthorized", "application/json",
				     "{\"ok\":false,\"error\":\"invalid bearer token\"}");
	}

	snprintk(body, sizeof(body),
		 "{\"ok\":true,\"data\":{\"angle\":%d}}", servo_module_get_angle());

	return send_response(fd, 200, "OK", "application/json", body);
}

static int handle_servo_post(int fd, const char *request, const char *body)
{
	char resp[64];
	int angle;
	int ret;

	if (!auth_header_valid(request)) {
		return send_response(fd, 401, "Unauthorized", "application/json",
				     "{\"ok\":false,\"error\":\"invalid bearer token\"}");
	}

	if (!json_get_int(body, "angle", &angle)) {
		return send_response(fd, 400, "Bad Request", "application/json",
				     "{\"ok\":false,\"error\":\"missing or invalid angle\"}");
	}

	if (angle < 0 || angle > 180) {
		return send_response(fd, 400, "Bad Request", "application/json",
				     "{\"ok\":false,\"error\":\"angle must be 0-180\"}");
	}

	ret = servo_module_set_angle(angle);
	if (ret < 0) {
		return send_response(fd, 500, "Internal Server Error", "application/json",
				     "{\"ok\":false,\"error\":\"servo set failed\"}");
	}

	snprintk(resp, sizeof(resp),
		 "{\"ok\":true,\"data\":{\"angle\":%d}}", angle);

	return send_response(fd, 200, "OK", "application/json", resp);
}

static void extract_request_parts(char *request, char **method, char **path, char **body)
{
	char *header_end;
	char *line_end;
	char *method_end;
	char *path_end;

	*method = NULL;
	*path = NULL;
	*body = request + strlen(request);

	header_end = strstr(request, "\r\n\r\n");
	if (header_end != NULL) {
		*body = header_end + 4;
	}

	line_end = strstr(request, "\r\n");
	if (line_end != NULL) {
		*line_end = '\0';
	}

	*method = request;
	method_end = strchr(request, ' ');
	if (method_end == NULL) {
		*method = NULL;
		return;
	}

	*method_end = '\0';
	*path = method_end + 1;

	path_end = strchr(*path, ' ');
	if (path_end != NULL) {
		*path_end = '\0';
	}
}

static void handle_client(int client_fd)
{
	char request[HTTP_RX_BUF_SIZE] = {0};
	char raw_request[HTTP_RX_BUF_SIZE] = {0};
	char *method = NULL;
	char *path = NULL;
	char *body = NULL;
	int rx_len;

	rx_len = receive_http_request(client_fd, request, sizeof(request));
	if (rx_len == -EMSGSIZE) {
		(void)send_response(client_fd, 413, "Payload Too Large", "application/json",
				    "{\"ok\":false,\"error\":\"request too large\"}");
		return;
	}

	if (rx_len < 0) {
		LOG_WRN("HTTP client closed or timed out before full request: %d", rx_len);
		return;
	}

	request[rx_len] = '\0';
	memcpy(raw_request, request, rx_len + 1);
	extract_request_parts(request, &method, &path, &body);

	if (method == NULL || path == NULL) {
		(void)send_response(client_fd, 400, "Bad Request", "application/json",
				    "{\"ok\":false,\"error\":\"bad request\"}");
		return;
	}

	LOG_INF("HTTP %s %s", method, path);

	if (strcmp(method, "GET") == 0 &&
	    (strcmp(path, "/api/status") == 0 || strcmp(path, "/api/v1/status") == 0)) {
		int ret = handle_status(client_fd);

		LOG_INF("HTTP %s %s complete ret=%d", method, path, ret);
	} else if (strcmp(method, "POST") == 0 &&
		   (strcmp(path, "/api/wifi") == 0 || strcmp(path, "/api/v1/wifi") == 0)) {
		int ret = handle_wifi_post(client_fd, raw_request, body);

		LOG_INF("HTTP %s %s complete ret=%d", method, path, ret);
	} else if (strcmp(method, "GET") == 0 &&
		   (strcmp(path, "/api/config") == 0 || strcmp(path, "/api/v1/config") == 0)) {
		int ret = handle_config_get(client_fd);

		LOG_INF("HTTP %s %s complete ret=%d", method, path, ret);
	} else if ((strcmp(method, "PUT") == 0 || strcmp(method, "POST") == 0) &&
		   (strcmp(path, "/api/config") == 0 || strcmp(path, "/api/v1/config") == 0)) {
		int ret = handle_config_put(client_fd, raw_request, body);

		LOG_INF("HTTP %s %s complete ret=%d", method, path, ret);
	} else if (strcmp(method, "POST") == 0 &&
		   strcmp(path, "/api/v1/provision/reset") == 0) {
		int ret = handle_provision_reset_post(client_fd, raw_request);

		LOG_INF("HTTP %s %s complete ret=%d", method, path, ret);
	} else if (strcmp(method, "GET") == 0 &&
		   strcmp(path, "/api/v1/sensors/imu") == 0) {
		int ret = handle_imu_get(client_fd, raw_request);

		LOG_INF("HTTP %s %s complete ret=%d", method, path, ret);
	} else if (strcmp(method, "GET") == 0 &&
		   strcmp(path, "/api/v1/sensors/tof") == 0) {
		int ret = handle_tof_get(client_fd, raw_request);

		LOG_INF("HTTP %s %s complete ret=%d", method, path, ret);
	} else if (strcmp(method, "GET") == 0 &&
		   strcmp(path, "/api/v1/servo") == 0) {
		int ret = handle_servo_get(client_fd, raw_request);

		LOG_INF("HTTP %s %s complete ret=%d", method, path, ret);
	} else if (strcmp(method, "POST") == 0 &&
		   strcmp(path, "/api/v1/servo") == 0) {
		int ret = handle_servo_post(client_fd, raw_request, body);

		LOG_INF("HTTP %s %s complete ret=%d", method, path, ret);
	} else {
		int ret = send_response(client_fd, 404, "Not Found", "application/json",
					"{\"ok\":false,\"error\":\"unknown route\"}");

		LOG_INF("HTTP %s %s not_found ret=%d", method, path, ret);
	}
}

int api_module_init(void)
{
	return 0;
}

static void api_thread(void)
{
	k_sem_take(&api_start_sem, K_FOREVER);
	LOG_INF("Provisioning API thread started (port %d)", DEVICE_API_PORT);

	while (true) {
		struct zsock_pollfd pollfd;
		int poll_fd;
		int poll_rc;

		k_mutex_lock(&api_lock, K_FOREVER);
		if (server_fd < 0) {
			server_fd = http_server_open();
		}
		poll_fd = server_fd;
		k_mutex_unlock(&api_lock);

		if (poll_fd < 0) {
			k_sleep(K_SECONDS(1));
			continue;
		}

		pollfd.fd = poll_fd;
		pollfd.events = ZSOCK_POLLIN;
		pollfd.revents = 0;

		poll_rc = zsock_poll(&pollfd, 1, 500);

		k_mutex_lock(&api_lock, K_FOREVER);
		if (poll_fd != server_fd) {
			k_mutex_unlock(&api_lock);
			continue;
		}
		k_mutex_unlock(&api_lock);

		if (poll_rc < 0) {
			LOG_ERR("HTTP poll failed: %d", errno);
			api_close_listener();
			k_sleep(K_MSEC(200));
			continue;
		}

		if (poll_rc == 0) {
			continue;
		}

		if (pollfd.revents & ZSOCK_POLLIN) {
			struct sockaddr_in client_addr;
			socklen_t client_len = sizeof(client_addr);
			char addr[NET_IPV4_ADDR_LEN];
			int client_fd;

			k_mutex_lock(&api_lock, K_FOREVER);
			poll_fd = server_fd;
			k_mutex_unlock(&api_lock);

			if (poll_fd < 0) {
				continue;
			}

			client_fd = zsock_accept(poll_fd, (struct sockaddr *)&client_addr,
						 &client_len);
			if (client_fd < 0) {
				LOG_WRN("HTTP accept failed: %d", errno);
				continue;
			}

			(void)net_addr_ntop(AF_INET, &client_addr.sin_addr, addr, sizeof(addr));
			LOG_INF("HTTP client connected fd=%d from %s:%u",
				client_fd, addr, ntohs(client_addr.sin_port));
			handle_client(client_fd);
			LOG_INF("HTTP client closing fd=%d", client_fd);
			(void)zsock_close(client_fd);
		}
	}
}

K_THREAD_DEFINE(api_module_tid, API_THREAD_STACK_SIZE, api_thread, NULL, NULL, NULL, 7, 0, 0);

void api_module_start(void)
{
	k_sem_give(&api_start_sem);
}
