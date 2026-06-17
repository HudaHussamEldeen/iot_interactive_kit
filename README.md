# IoT Interactive Kit

Zephyr RTOS application for an ESP32-S3 DevKit C based interactive IoT kit with production-style Wi-Fi provisioning, sensor APIs, servo control, and factory reset.

## Hardware

| Peripheral | Interface | Pin(s) |
|---|---|---|
| WS2812 RGB LED | GPIO (I2S) | GPIO48 |
| Servo motor | PWM (LEDC0) | GPIO6 |
| MPU6050 IMU | I2C0 | SDA=GPIO1, SCL=GPIO2 |
| VL6180X ToF | I2C0 | SDA=GPIO1, SCL=GPIO2, addr=0x29 |
| Buzzer | PWM | — |
| Factory reset button | GPIO0 | BOOT button (built-in) |

---

## Architecture

| Module | Role |
|---|---|
| `device_identity` | Unique device ID and SoftAP credentials from ESP32 MAC |
| `wifi_manager` | Low-level STA/SoftAP control (`net_mgmt`) |
| `provision_module` | Provisioning state machine and reconnect policy |
| `config_module` | NVS persistence via Zephyr Settings |
| `api_module` | REST API on port 8080 |
| `rgb_module` | WS2812 status LED |
| `servo_module` | LEDC PWM servo control |
| `mpu6050_module` | IMU sensor (accel, gyro, temperature) |
| `vl6180x_module` | Time-of-Flight distance sensor |
| `reset_button_module` | Factory reset via 3-second BOOT button hold |

---

## Provisioning Flow

1. Boot loads settings from NVS.
2. **No Wi-Fi credentials** → SoftAP starts automatically (`IotKit-XXXX` / password from MAC).
3. Client joins SoftAP (`192.168.4.1`) and calls `POST /api/v1/wifi`.
4. Device attempts connection — **HTTP response is held open** until all attempts complete.
5. **Connection succeeds** → credentials saved to NVS, response sent with IP, SoftAP stops.
6. **Connection fails** → error response sent, device stays in provisioning mode.
7. **Boot with saved credentials** → tries STA; if not ready in 15 s, SoftAP opens as fallback.
8. **STA disconnects while operational** → retries every 60 seconds indefinitely (no SoftAP fallback).

---

## Device Identity

- **Device ID**: `IotKit-<STA-MAC>` (e.g. `IotKit-A1B2C3D4E5F6`)
- **SoftAP SSID**: `IotKit-<AP-MAC-suffix>`
- **SoftAP password**: `IotKit<suffix>` (WPA2)

---

## Factory Reset (Hardware)

Hold the **BOOT button (GPIO0) for 3 seconds** while the device is running:
- All NVS configuration is erased (SSID, PSK, API token, device ID, IP settings)
- Device reboots into provisioning mode

---

## RGB Network Status

| Color | State | Meaning |
|---|---|---|
| Blue | Boot | Initializing |
| Magenta | Provisioning | Waiting for Wi-Fi credentials via SoftAP |
| Cyan | Connecting | Attempting STA connection |
| Green | Operational | Connected to Wi-Fi |

---

## REST API

Base URL in provisioning mode: `http://192.168.4.1:8080`  
Base URL in operational mode: `http://<device_ip>:8080`  
Port: **8080**

### Authentication

Most endpoints require a Bearer token once the device is operational.  
Default token: **`iot-kit-dev`** (change via `PUT /api/v1/config`)

```
Authorization: Bearer iot-kit-dev
```

---

### Endpoints

#### `GET /api/v1/status` — Device & Wi-Fi Status

No authentication required.

```sh
curl http://192.168.4.1:8080/api/v1/status
```

```json
{
  "ok": true,
  "profile_version": "1.0",
  "device": {
    "id": "IotKit-A1B2C3D4E5F6",
    "product": "IotKit",
    "sta_mac": "A1:B2:C3:D4:E5:F6",
    "uptime_s": 42
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
    "sta_ssid": "MyWiFi",
    "sta_ipv4": "192.168.1.42",
    "ip_mode": "dhcp",
    "sta_ready": true,
    "connect_attempt": 1
  }
}
```

---

#### `POST /api/v1/wifi` — Provision Wi-Fi

No auth in provisioning mode; Bearer token required in operational mode.

**The request blocks until the connection attempt completes** (up to ~160 s for 5 attempts).  
Credentials are only written to NVS if the connection succeeds.

```sh
curl -X POST http://192.168.4.1:8080/api/v1/wifi \
  -H 'Content-Type: application/json' \
  -d '{"ssid":"MyWiFi","psk":"mypassword"}'
```

Static IP:
```sh
curl -X POST http://192.168.4.1:8080/api/v1/wifi \
  -H 'Content-Type: application/json' \
  -d '{"ssid":"MyWiFi","psk":"mypassword","ip_mode":"static","ip_address":"192.168.1.50","netmask":"255.255.255.0","gateway":"192.168.1.1"}'
```

**Success (200)**:
```json
{ "ok": true, "status": "connected", "ssid": "MyWiFi", "ip": "192.168.1.42" }
```

**Failure (200)**:
```json
{ "ok": false, "error": "connection failed", "status": "failed" }
```

| Field | Required | Notes |
|---|---|---|
| `ssid` / `wifi_ssid` | Yes | 1–32 chars |
| `psk` / `wifi_psk` | No | 8–63 chars; omit for open networks |
| `ip_mode` / `wifi_ip_mode` | No | `dhcp` (default) or `static` |
| `ip_address` | Static only | e.g. `192.168.1.50` |
| `netmask` | Static only | e.g. `255.255.255.0` |
| `gateway` | Static optional | e.g. `192.168.1.1` |

---

#### `GET /api/v1/sensors/imu` — IMU (MPU6050)

Reads accelerometer, gyroscope, and die temperature on demand.

**Auth required.**

```sh
curl http://<device_ip>:8080/api/v1/sensors/imu \
  -H 'Authorization: Bearer iot-kit-dev'
```

```json
{
  "ok": true,
  "data": {
    "accel": { "x": 0.123456, "y": -0.045678, "z": 9.812345 },
    "gyro":  { "x": 0.001234, "y": -0.002345, "z": 0.000123 },
    "temperature": 25.123456
  }
}
```

Units: accel in **m/s²**, gyro in **rad/s**, temperature in **°C**.

---

#### `GET /api/v1/sensors/tof` — Time-of-Flight Distance (VL6180X)

Triggers a single-shot range measurement and returns the result.

**Auth required.**

```sh
curl http://<device_ip>:8080/api/v1/sensors/tof \
  -H 'Authorization: Bearer iot-kit-dev'
```

```json
{
  "ok": true,
  "data": { "range_mm": 87 }
}
```

Range in **millimeters** (0–255 mm, VL6180X hardware limit).

---

#### `GET /api/v1/servo` — Get Servo Angle

**Auth required.**

```sh
curl http://<device_ip>:8080/api/v1/servo \
  -H 'Authorization: Bearer iot-kit-dev'
```

```json
{ "ok": true, "data": { "angle": 90 } }
```

---

#### `POST /api/v1/servo` — Set Servo Angle

**Auth required.**

```sh
curl -X POST http://<device_ip>:8080/api/v1/servo \
  -H 'Authorization: Bearer iot-kit-dev' \
  -H 'Content-Type: application/json' \
  -d '{"angle":90}'
```

```json
{ "ok": true, "data": { "angle": 90 } }
```

`angle` must be 0–180 degrees.

---

#### `GET /api/v1/config` — Get Device Config

No auth required.

```sh
curl http://<device_ip>:8080/api/v1/config
```

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

#### `PUT /api/v1/config` — Update Device Config

**Auth required.**

```sh
curl -X PUT http://<device_ip>:8080/api/v1/config \
  -H 'Authorization: Bearer iot-kit-dev' \
  -H 'Content-Type: application/json' \
  -d '{"api_token":"new-secure-token"}'
```

| Field | Description |
|---|---|
| `device_id` | New device identifier |
| `api_token` | New Bearer token for future requests |

---

#### `POST /api/v1/provision/reset` — Clear Wi-Fi & Reprovision

Clears saved Wi-Fi credentials and restarts SoftAP provisioning mode.

**Auth required.**

```sh
curl -X POST http://<device_ip>:8080/api/v1/provision/reset \
  -H 'Authorization: Bearer iot-kit-dev'
```

```json
{ "ok": true, "status": "provisioning" }
```

---

### HTTP Status Codes

| Code | Meaning |
|---|---|
| 200 | Success (also used for connect failures — check `"ok"` field) |
| 400 | Bad request (missing/invalid fields) |
| 401 | Unauthorized (missing or wrong Bearer token) |
| 404 | Unknown endpoint |
| 413 | Request too large (> 1 KB) |
| 500 | Internal error (NVS save failure, etc.) |
| 503 | Sensor not available (device not ready or wiring issue) |

---

## Build & Flash

```sh
cd /home/huda-hussam/zephyrproject
source zephyr/zephyr-env.sh
west build -p always -b esp32s3_devkitc/esp32s3/procpu iot_interactive_kit
west flash -d iot_interactive_kit/build
```
