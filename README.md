# IoT Interactive Kit

Zephyr RTOS application for an ESP32-S3 DevKit C based interactive IoT kit with production-style Wi-Fi provisioning, sensor APIs, servo control, and factory reset.

## Hardware

| Peripheral | Interface | Pin(s) |
|---|---|---|
| WS2812 RGB LED | GPIO (I2S) | GPIO48 |
| Servo motor | PWM (LEDC0) | GPIO6 |
| MPU6050 IMU | I2C0 | SDA=GPIO1, SCL=GPIO2 |
| VL6180X ToF | I2C0 | SDA=GPIO1, SCL=GPIO2, addr=0x29 |
| DHT22 temp/humidity | GPIO (1-wire) | GPIO21 |
| Magnetic switch | GPIO input | GPIO40 |
| User button | GPIO input (active low) | GPIO41 |
| Relay 1 | GPIO output | GPIO12 |
| Relay 2 | GPIO output | GPIO13 |
| LDR (light) | ADC1 CH8 | GPIO9 |
| Water level | ADC1 CH9 | GPIO10 |
| Buzzer | PWM (LEDC0) | GPIO11 |
| LED 1 | GPIO output | GPIO7 |
| LED 2 | GPIO output | GPIO8 |
| LED 3 | GPIO output | GPIO3 |
| DC motor (IN1/IN2) | PWM (LEDC0 CH3/CH4) | GPIO15, GPIO16 |
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
| `relay_module` | Relay 1 & 2 GPIO outputs |
| `gpio_inputs_module` | Magnetic switch + button GPIO inputs |
| `analog_module` | LDR + water level via ADC1 |
| `dht22_module` | DHT22 temperature & humidity |
| `rgb_module` | WS2812 status LED |
| `servo_module` | LEDC PWM servo control |
| `mpu6050_module` | IMU sensor (accel, gyro, temperature) |
| `vl6180x_module` | Time-of-Flight distance sensor |
| `led_module` | 3× GPIO LED outputs (GPIO7/8/3) |
| `motor_module` | DC motor dual-PWM control (LEDC CH3/CH4) |
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

#### `POST /api/v1/buzzer` — Buzzer Control

**Auth required.**

| Action | Description |
|---|---|
| `on` | Turn buzzer on continuously at given frequency |
| `off` | Turn buzzer off |
| `beep` | Beep for a fixed duration (blocks until done) |

**Turn on:**
```sh
curl -X POST http://<device_ip>:8080/api/v1/buzzer \
  -H 'Authorization: Bearer iot-kit-dev' \
  -H 'Content-Type: application/json' \
  -d '{"action":"on","freq":2000}'
```

**Turn off:**
```sh
curl -X POST http://<device_ip>:8080/api/v1/buzzer \
  -H 'Authorization: Bearer iot-kit-dev' \
  -H 'Content-Type: application/json' \
  -d '{"action":"off"}'
```

**Beep:**
```sh
curl -X POST http://<device_ip>:8080/api/v1/buzzer \
  -H 'Authorization: Bearer iot-kit-dev' \
  -H 'Content-Type: application/json' \
  -d '{"action":"beep","freq":1000,"duration_ms":300}'
```

```json
{ "ok": true }
```

| Field | Required for | Range |
|---|---|---|
| `action` | always | `on`, `off`, `beep` |
| `freq` | `on`, `beep` | 100–20000 Hz |
| `duration_ms` | `beep` | 1–5000 ms |

---

#### `GET /api/v1/relay/{n}` — Get Relay State

**Auth required.** `n` = 1 or 2.

```sh
curl http://<device_ip>:8080/api/v1/relay/1 \
  -H 'Authorization: Bearer iot-kit-dev'
```

```json
{ "ok": true, "data": { "relay": 1, "state": "off" } }
```

---

#### `POST /api/v1/relay/{n}` — Set Relay State

**Auth required.** `n` = 1 or 2.

```sh
curl -X POST http://<device_ip>:8080/api/v1/relay/1 \
  -H 'Authorization: Bearer iot-kit-dev' \
  -H 'Content-Type: application/json' \
  -d '{"state":"on"}'
```

```json
{ "ok": true, "data": { "relay": 1, "state": "on" } }
```

`state` must be `"on"` or `"off"`.

---

#### `GET /api/v1/sensors/magnetic` — Magnetic Switch (GPIO40)

**Auth required.**

```sh
curl http://<device_ip>:8080/api/v1/sensors/magnetic \
  -H 'Authorization: Bearer iot-kit-dev'
```

```json
{ "ok": true, "data": { "closed": true } }
```

`closed: true` = magnet detected, `false` = open.

---

#### `GET /api/v1/sensors/button` — User Button (GPIO41)

**Auth required.**

```sh
curl http://<device_ip>:8080/api/v1/sensors/button \
  -H 'Authorization: Bearer iot-kit-dev'
```

```json
{ "ok": true, "data": { "pressed": true } }
```

---

#### `GET /api/v1/sensors/ldr` — Light Level (LDR)

Reads light intensity from the LDR on GPIO9 (ADC1 CH8).

**Auth required.**

```sh
curl http://<device_ip>:8080/api/v1/sensors/ldr \
  -H 'Authorization: Bearer iot-kit-dev'
```

```json
{
  "ok": true,
  "data": { "raw": 2560, "percent": 62 }
}
```

| Field | Description |
|---|---|
| `raw` | 12-bit ADC value (0–4095) |
| `percent` | Light level 0–100 % (0 = dark, 100 = bright) |

---

#### `GET /api/v1/sensors/water` — Water Level

Reads water level from the analog sensor on GPIO10 (ADC1 CH9).

**Auth required.**

```sh
curl http://<device_ip>:8080/api/v1/sensors/water \
  -H 'Authorization: Bearer iot-kit-dev'
```

```json
{
  "ok": true,
  "data": { "raw": 1024, "percent": 25 }
}
```

| Field | Description |
|---|---|
| `raw` | 12-bit ADC value (0–4095) |
| `percent` | Water level 0–100 % (0 = dry, 100 = full) |

---

#### `GET /api/v1/sensors/dht` — Temperature & Humidity (DHT22)

Reads temperature and humidity from the DHT22 on GPIO4.

**Auth required.**

```sh
curl http://<device_ip>:8080/api/v1/sensors/dht \
  -H 'Authorization: Bearer iot-kit-dev'
```

```json
{
  "ok": true,
  "data": { "temperature": 25.000000, "humidity": 60.000000 }
}
```

Units: temperature in **°C**, humidity in **%RH**.

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

#### `GET /api/v1/led/{n}` — Get LED State

**Auth required.** `n` = 1 (GPIO7), 2 (GPIO8), or 3 (GPIO3).

```sh
curl http://<device_ip>:8080/api/v1/led/1 \
  -H 'Authorization: Bearer iot-kit-dev'
```

```json
{ "ok": true, "data": { "led": 1, "state": "off" } }
```

---

#### `POST /api/v1/led/{n}` — Set LED State

**Auth required.** `n` = 1, 2, or 3.

```sh
curl -X POST http://<device_ip>:8080/api/v1/led/1 \
  -H 'Authorization: Bearer iot-kit-dev' \
  -H 'Content-Type: application/json' \
  -d '{"state":"on"}'
```

```json
{ "ok": true, "data": { "led": 1, "state": "on" } }
```

`state` must be `"on"` or `"off"`.

---

#### `GET /api/v1/motor` — Get Motor State

**Auth required.**

```sh
curl http://<device_ip>:8080/api/v1/motor \
  -H 'Authorization: Bearer iot-kit-dev'
```

```json
{ "ok": true, "data": { "speed": 75, "direction": "forward" } }
```

---

#### `POST /api/v1/motor` — Set Motor Speed & Direction

**Auth required.**

| Field | Required | Notes |
|---|---|---|
| `speed` | Yes | 0–100 % (0 = coast) |
| `direction` | No | `forward` (default) or `backward` |

**Run forward at 60 %:**
```sh
curl -X POST http://<device_ip>:8080/api/v1/motor \
  -H 'Authorization: Bearer iot-kit-dev' \
  -H 'Content-Type: application/json' \
  -d '{"speed":60,"direction":"forward"}'
```

```json
{ "ok": true, "data": { "speed": 60, "direction": "forward" } }
```

**Coast (stop):**
```sh
curl -X POST http://<device_ip>:8080/api/v1/motor \
  -H 'Authorization: Bearer iot-kit-dev' \
  -H 'Content-Type: application/json' \
  -d '{"speed":0}'
```

```json
{ "ok": true, "data": { "speed": 0, "direction": "coast" } }
```

H-bridge wiring: IN1=GPIO15 (LEDC CH3), IN2=GPIO16 (LEDC CH4). Both are 20 kHz PWM.  
Forward: IN1=duty, IN2=0. Backward: IN1=0, IN2=duty. Coast: both 0.

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
