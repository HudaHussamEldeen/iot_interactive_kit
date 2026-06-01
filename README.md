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
4. `POST /api/v1/wifi` stores credentials, stops SoftAP, and connects STA.
5. **STA success** → operational mode (SoftAP off, hostname `iot-kit.local`).
6. **STA failure (30 s timeout)** → returns to provisioning mode (SoftAP on again).
7. Boot with saved credentials tries STA first; if STA is not ready within 15 s, SoftAP opens as fallback.

## Device identity

- **Device ID**: `IotKit-<STA-MAC>` (e.g. `IotKit-A1B2C3D4E5F6`)
- **SoftAP SSID**: `IotKit-<AP-MAC-suffix>`
- **SoftAP password**: `IotKit<suffix>` (WPA2)

## REST API

```sh
# Status (SoftAP or LAN)
curl http://192.168.4.1:8080/api/v1/status

# Provision Wi-Fi (no auth while in provisioning mode)
curl -X POST http://192.168.4.1:8080/api/v1/wifi \
  -H 'Content-Type: application/json' \
  -d '{"ssid":"YOUR_WIFI","psk":"YOUR_PASSWORD"}'

# Device config (no Wi-Fi secrets in response)
curl http://192.168.4.1:8080/api/v1/config

# Update API token (Bearer required)
curl -X PUT http://192.168.4.1:8080/api/v1/config \
  -H 'Authorization: Bearer iot-kit-dev' \
  -H 'Content-Type: application/json' \
  -d '{"api_token":"new-token"}'

# Factory Wi-Fi reset (Bearer required)
curl -X POST http://192.168.4.1:8080/api/v1/provision/reset \
  -H 'Authorization: Bearer iot-kit-dev'
```

Legacy paths `/api/status`, `/api/wifi`, and `/api/config` remain supported.

## Build

```sh
cd /home/huda-hussam/zephyrproject
source zephyr/zephyr-env.sh
west build -p always -b esp32s3_devkitc/esp32s3/procpu iot_interactive_kit
west flash -d iot_interactive_kit/build
```
