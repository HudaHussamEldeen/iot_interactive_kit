import requests


TIMEOUT = 10  # seconds for normal calls
WIFI_TIMEOUT = 180  # seconds — blocking connect can take up to ~165 s


def get_status(base_url, headers):
    r = requests.get(f"{base_url}/api/v1/status", timeout=TIMEOUT)
    return r.json()


def post_wifi(base_url, headers, ssid, psk, ip_mode="dhcp",
              ip_address="", netmask="", gateway=""):
    payload = {"ssid": ssid, "psk": psk, "ip_mode": ip_mode}
    if ip_mode == "static":
        payload.update({"ip_address": ip_address, "netmask": netmask, "gateway": gateway})
    r = requests.post(f"{base_url}/api/v1/wifi", json=payload,
                      headers=headers, timeout=WIFI_TIMEOUT)
    return r.json()


def provision_reset(base_url, headers):
    r = requests.post(f"{base_url}/api/v1/provision/reset",
                      headers=headers, timeout=TIMEOUT)
    return r.json()


def get_imu(base_url, headers):
    r = requests.get(f"{base_url}/api/v1/sensors/imu",
                     headers=headers, timeout=TIMEOUT)
    return r.json()


def get_tof(base_url, headers):
    r = requests.get(f"{base_url}/api/v1/sensors/tof",
                     headers=headers, timeout=TIMEOUT)
    return r.json()


def get_servo(base_url, headers):
    r = requests.get(f"{base_url}/api/v1/servo",
                     headers=headers, timeout=TIMEOUT)
    return r.json()


def set_servo(base_url, headers, angle):
    r = requests.post(f"{base_url}/api/v1/servo", json={"angle": angle},
                      headers=headers, timeout=TIMEOUT)
    return r.json()


def buzzer_on(base_url, headers, freq):
    r = requests.post(f"{base_url}/api/v1/buzzer",
                      json={"action": "on", "freq": freq},
                      headers=headers, timeout=TIMEOUT)
    return r.json()


def buzzer_off(base_url, headers):
    r = requests.post(f"{base_url}/api/v1/buzzer",
                      json={"action": "off"},
                      headers=headers, timeout=TIMEOUT)
    return r.json()


def buzzer_beep(base_url, headers, freq, duration_ms):
    r = requests.post(f"{base_url}/api/v1/buzzer",
                      json={"action": "beep", "freq": freq, "duration_ms": duration_ms},
                      headers=headers, timeout=TIMEOUT + duration_ms / 1000)
    return r.json()
