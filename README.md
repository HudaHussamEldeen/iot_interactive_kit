# IoT Interactive Kit

Zephyr application for an ESP32-S3 based interactive IoT kit with production-style Wi-Fi provisioning.

## Architecture

| Module | Role |
|--------|------|
| `device_identity` | Unique device ID and SoftAP credentials from ESP32 MAC |
| `wifi_manager` | Low-level STA/SoftAP control (`net_mgmt`) |
| `provision_module` | Provisioning state machine and reconnect policy |
| `config_module` | NVS persistence via Zephyr Settings |
| `api_module` | REST provisioning API on port 8080 |
| `rgb_module` | WS2812 status/demo LED |

## Provisioning flow

1. Boot loads settings from NVS.
2. **No Wi-Fi credentials** → SoftAP starts automatically (`IotKit-XXXX` / `IotKitYYYY` from MAC).
3. Mobile app joins SoftAP (`192.168.4.1`) and calls the REST API.
4. `POST /api/v1/wifi` stores the station config, sends the HTTP response, then stops SoftAP before connecting STA.
5. **STA success** → operational mode (SoftAP off, hostname `iot-kit.local`).
6. **STA failure (30 s timeout)** → returns to provisioning mode (SoftAP on again).
7. Boot with saved credentials tries STA first; if STA is not ready within 15 s, SoftAP opens as fallback.

## Device identity

- **Device ID**: `IotKit-<STA-MAC>` (e.g. `IotKit-A1B2C3D4E5F6`)
- **SoftAP SSID**: `IotKit-<AP-MAC-suffix>`
- **SoftAP password**: `IotKit<suffix>` (WPA2)

## RGB Network Status Indicator

The WS2812 RGB LED provides real-time visual feedback of the device's network status:

| Color | Status | Meaning |
|-------|--------|---------|
| **Blue** | Boot | Device is initializing |
| **Magenta** | Provisioning | Waiting for Wi-Fi credentials via SoftAP |
| **Cyan** | Connecting | Attempting to connect to configured Wi-Fi network |
| **Green** | Operational | Successfully connected to Wi-Fi network |
| **Red** | Error | Network error state |

The LED continuously reflects the current provisioning state, allowing users to understand device status at a glance.

## REST API

The device exposes a REST API on port `8080` for provisioning and status management. During provisioning mode (SoftAP active), the API is available at `http://192.168.4.1:8080`. Once provisioned and connected to a Wi-Fi network, use the device's STA IP address (e.g., `http://<device_ip>:8080`).

### Authentication

- **Provisioning Mode**: No authentication required
- **Operational Mode** (post-provisioning): Bearer token authentication required for config and reset endpoints

Default bearer token: `iot-kit-dev`

Legacy paths `/api/status`, `/api/wifi`, and `/api/config` remain supported alongside v1 paths.

---

### API Endpoints

#### 1. **GET /api/v1/status** — Device and Wi-Fi Status
Returns comprehensive device and provisioning state information.

**Authentication**: None  
**HTTP Status**: 200 OK

**Example Request**:
```sh
curl http://192.168.4.1:8080/api/v1/status
```

**Example Response** (Provisioning Mode):
```json
{
  "ok": true,
  "profile_version": "1.0",
  "device": {
    "id": "IotKit-A1B2C3D4E5F6",
    "product": "IoT Interactive Kit",
    "sta_mac": "A1:B2:C3:D4:E5:F6",
    "uptime_s": 42
  },
  "provisioning": {
    "mode": "provisioning",
    "active": true,
    "softap_ssid": "IotKit-D4E5F6",
    "softap_ip": "192.168.4.1",
    "api_port": 8080,
    "wifi_provisioned": false
  },
  "wifi": {
    "link_state": "disconnected",
    "last_event": "disconnected",
    "sta_ssid": "",
    "sta_ipv4": "0.0.0.0",
    "ip_mode": "dhcp",
    "sta_ready": false,
    "connect_attempt": 0
  }
}
```

**Example Response** (Operational Mode):
```json
{
  "ok": true,
  "profile_version": "1.0",
  "device": {
    "id": "IotKit-A1B2C3D4E5F6",
    "product": "IoT Interactive Kit",
    "sta_mac": "A1:B2:C3:D4:E5:F6",
    "uptime_s": 300
  },
  "provisioning": {
    "mode": "operational",
    "active": false,
    "softap_ssid": "IotKit-D4E5F6",
    "softap_ip": "192.168.4.1",
    "api_port": 8080,
    "wifi_provisioned": true
  },
  "wifi": {
    "link_state": "connected",
    "last_event": "connected",
    "sta_ssid": "YOUR_WIFI",
    "sta_ipv4": "<device_ip>",
    "ip_mode": "dhcp",
    "sta_ready": true,
    "connect_attempt": 1
  }
}
```

---

#### 2. **POST /api/v1/wifi** — Provision Wi-Fi Network
Submits Wi-Fi credentials (SSID and password) and initiates connection attempt. The device returns a 202 response and begins connecting asynchronously.

**Authentication**: None in provisioning mode; Bearer token required in operational mode  
**HTTP Status**: 202 Accepted | 400 Bad Request | 401 Unauthorized

**Request Schema**:

| Field | Required | Description |
|-------|----------|-------------|
| `ssid` or `wifi_ssid` | Yes | Target Wi-Fi network name (1-32 characters) |
| `psk` or `wifi_psk` | No | WPA/WPA2 password; empty for open network (8-63 characters when set) |
| `ip_mode` or `wifi_ip_mode` | No | `dhcp` (default) or `static` |
| `ip_address` | Static only | Static station IPv4 address (e.g., `192.168.1.50`) |
| `netmask` | Static only | Static station netmask (e.g., `255.255.255.0`) |
| `gateway` | Optional static | Static gateway (e.g., `192.168.1.1`) |

**Example Requests**:

DHCP Mode (Default):
```sh
curl -X POST http://192.168.4.1:8080/api/v1/wifi \
  -H 'Content-Type: application/json' \
  -d '{"ssid":"YOUR_WIFI","psk":"YOUR_PASSWORD"}'
```

DHCP Mode (Explicit):
```sh
curl -X POST http://192.168.4.1:8080/api/v1/wifi \
  -H 'Content-Type: application/json' \
  -d '{"ssid":"YOUR_WIFI","psk":"YOUR_PASSWORD","ip_mode":"dhcp"}'
```

Static IP Mode:
```sh
curl -X POST http://192.168.4.1:8080/api/v1/wifi \
  -H 'Content-Type: application/json' \
  -d '{"ssid":"YOUR_WIFI","psk":"YOUR_PASSWORD","ip_mode":"static","ip_address":"192.168.1.50","netmask":"255.255.255.0","gateway":"192.168.1.1"}'
```

With Bearer Token (Operational Mode):
```sh
curl -X POST http://<device_ip>:8080/api/v1/wifi \
  -H 'Authorization: Bearer iot-kit-dev' \
  -H 'Content-Type: application/json' \
  -d '{"ssid":"YOUR_WIFI","psk":"YOUR_PASSWORD"}'
```

**Success Response** (202 Accepted):
```json
{
  "ok": true,
  "status": "connecting",
  "next": "operational_on_success",
  "status_url": "/api/v1/status"
}
```

**Error Responses**:
```json
{
  "ok": false,
  "error": "missing ssid"
}
```
```json
{
  "ok": false,
  "error": "invalid static IP or netmask"
}
```
```json
{
  "ok": false,
  "error": "bearer token required"
}
```

---

#### 3. **GET /api/v1/config** — Get Device Configuration
Returns non-sensitive device configuration (device ID, provisioning state, and whether API token is set).

**Authentication**: None  
**HTTP Status**: 200 OK

**Example Request**:
```sh
curl http://192.168.4.1:8080/api/v1/config
```

**Example Response**:
```json
{
  "ok": true,
  "data": {
    "device_id": "IotKit-A1B2C3D4E5F6",
    "wifi_provisioned": true,
    "api_token_set": true
  }
}
```

---

#### 4. **PUT or POST /api/v1/config** — Update Device Configuration
Updates device ID and/or API Bearer token. Requires Bearer token authentication. (Accepts PUT or POST)

**Authentication**: Bearer token required  
**HTTP Status**: 200 OK | 401 Unauthorized | 500 Internal Server Error

**Request Schema**:

| Field | Description |
|-------|-------------|
| `device_id` | New device identifier (optional) |
| `api_token` | New Bearer token for subsequent authenticated requests (optional) |

**Example Request**:
```sh
curl -X PUT http://<device_ip>:8080/api/v1/config \
  -H 'Authorization: Bearer iot-kit-dev' \
  -H 'Content-Type: application/json' \
  -d '{"api_token":"new-secure-token-12345"}'
```

**Success Response** (200 OK):
```json
{
  "ok": true
}
```

**Error Response** (401 Unauthorized):
```json
{
  "ok": false,
  "error": "invalid bearer token"
}
```

---

#### 5. **POST /api/v1/provision/reset** — Factory Wi-Fi Reset
Clears saved Wi-Fi credentials and returns device to provisioning mode. SoftAP will restart automatically. Requires Bearer token authentication.

**Authentication**: Bearer token required  
**HTTP Status**: 200 OK | 401 Unauthorized | 500 Internal Server Error

**Example Request**:
```sh
curl -X POST http://<device_ip>:8080/api/v1/provision/reset \
  -H 'Authorization: Bearer iot-kit-dev'
```

**Success Response** (200 OK):
```json
{
  "ok": true,
  "status": "provisioning"
}
```

**Error Response** (401 Unauthorized):
```json
{
  "ok": false,
  "error": "invalid bearer token"
}
```

---

### API Status Codes

| Code | Meaning |
|------|---------|
| 200 | Request successful |
| 202 | Request accepted, processing asynchronously (Wi-Fi provisioning) |
| 400 | Bad request (invalid JSON, missing required fields, validation error) |
| 401 | Unauthorized (invalid or missing Bearer token when required) |
| 404 | Not found (unknown endpoint) |
| 413 | Payload too large (request exceeds 1 KB) |
| 500 | Internal server error (NVS save failure, provisioning error) |

### Connection Flow Notes

1. **Provisioning Attempt**: After `POST /api/v1/wifi`, the device attempts to connect to the specified network with a 30-second timeout.
2. **Success**: If connection succeeds within 30 seconds, SoftAP shuts down and device enters operational mode.
3. **Failure**: If connection fails after 30 seconds, the device automatically returns to provisioning mode (SoftAP on again).
4. **Fallback**: On boot with saved credentials, the device tries STA connection first. If not ready within 15 seconds, SoftAP opens as a fallback.

### Response Status Codes and Links

The `link_state` field in the status response indicates the Wi-Fi connection state:
- `disconnected` — No active connection
- `connecting` — Connection in progress
- `connected_no_ip` — Associated but no IP address assigned
- `connected` — Fully operational (associated + IP assigned)

Some Wi-Fi driver events may arrive after the provisioning state machine has moved on. Logs starting with `Ignoring unexpected SoftAP ...` or `Ignoring SoftAP client ... while AP inactive` indicate the event was discarded. `STA disconnected status=-1` means the station connection attempt failed before an IP address was assigned.

## Build

```sh
cd /home/huda-hussam/zephyrproject
source zephyr/zephyr-env.sh
west build -p always -b esp32s3_devkitc/esp32s3/procpu iot_interactive_kit
west flash -d iot_interactive_kit/build
```
# iot_interactive_kit
