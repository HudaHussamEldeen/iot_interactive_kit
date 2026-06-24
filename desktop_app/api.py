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


# ── Relay ─────────────────────────────────────────────────────────────────────

def get_relay(base_url, headers, relay):
    r = requests.get(f"{base_url}/api/v1/relay/{relay}", headers=headers, timeout=TIMEOUT)
    return r.json()


def set_relay(base_url, headers, relay, state):
    r = requests.post(f"{base_url}/api/v1/relay/{relay}", json={"state": state},
                      headers=headers, timeout=TIMEOUT)
    return r.json()


# ── LED ───────────────────────────────────────────────────────────────────────

def get_led(base_url, headers, led):
    r = requests.get(f"{base_url}/api/v1/led/{led}", headers=headers, timeout=TIMEOUT)
    return r.json()


def set_led(base_url, headers, led, state):
    r = requests.post(f"{base_url}/api/v1/led/{led}", json={"state": state},
                      headers=headers, timeout=TIMEOUT)
    return r.json()


# ── Motor ─────────────────────────────────────────────────────────────────────

def get_motor(base_url, headers):
    r = requests.get(f"{base_url}/api/v1/motor", headers=headers, timeout=TIMEOUT)
    return r.json()


def set_motor(base_url, headers, speed, direction="forward"):
    r = requests.post(f"{base_url}/api/v1/motor",
                      json={"speed": speed, "direction": direction},
                      headers=headers, timeout=TIMEOUT)
    return r.json()


# ── GPIO / Analog / DHT ───────────────────────────────────────────────────────

def get_magnetic(base_url, headers):
    r = requests.get(f"{base_url}/api/v1/sensors/magnetic", headers=headers, timeout=TIMEOUT)
    return r.json()


def get_button(base_url, headers):
    r = requests.get(f"{base_url}/api/v1/sensors/button", headers=headers, timeout=TIMEOUT)
    return r.json()


def get_ldr(base_url, headers):
    r = requests.get(f"{base_url}/api/v1/sensors/ldr", headers=headers, timeout=TIMEOUT)
    return r.json()


def get_water(base_url, headers):
    r = requests.get(f"{base_url}/api/v1/sensors/water", headers=headers, timeout=TIMEOUT)
    return r.json()


def get_dht(base_url, headers):
    r = requests.get(f"{base_url}/api/v1/sensors/dht", headers=headers, timeout=TIMEOUT)
    return r.json()
