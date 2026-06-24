import threading
import customtkinter as ctk
import api


class SensorsPage(ctk.CTkFrame):
    def __init__(self, parent, app):
        super().__init__(parent, corner_radius=0, fg_color="transparent")
        self.app = app
        self._auto_refresh = False
        self._refresh_job = None

        ctk.CTkLabel(self, text="Sensors",
                     font=ctk.CTkFont(size=22, weight="bold")).pack(pady=(24, 4))

        # Controls row
        ctrl = ctk.CTkFrame(self, fg_color="transparent")
        ctrl.pack(pady=(0, 8))
        ctk.CTkButton(ctrl, text="Read All", width=120, command=self._read_once).grid(row=0, column=0, padx=8)
        self.auto_btn = ctk.CTkButton(ctrl, text="Auto: OFF", width=140, command=self._toggle_auto)
        self.auto_btn.grid(row=0, column=1, padx=8)

        # Scrollable area
        scroll = ctk.CTkScrollableFrame(self, fg_color="transparent")
        scroll.pack(fill="both", expand=True, padx=16, pady=4)

        # ── IMU ──────────────────────────────────────────────────────────────
        imu_card = ctk.CTkFrame(scroll)
        imu_card.pack(padx=24, pady=6, fill="x")
        ctk.CTkLabel(imu_card, text="IMU — MPU6050",
                     font=ctk.CTkFont(size=14, weight="bold")).pack(anchor="w", padx=16, pady=(10, 2))
        self.imu_box = ctk.CTkTextbox(imu_card, height=90, state="disabled",
                                      font=ctk.CTkFont(family="Courier", size=11))
        self.imu_box.pack(padx=16, pady=(0, 10), fill="x")

        # ── ToF ──────────────────────────────────────────────────────────────
        tof_card = ctk.CTkFrame(scroll)
        tof_card.pack(padx=24, pady=6, fill="x")
        ctk.CTkLabel(tof_card, text="Distance — VL6180X (ToF)",
                     font=ctk.CTkFont(size=14, weight="bold")).pack(anchor="w", padx=16, pady=(10, 2))
        self.tof_box = ctk.CTkTextbox(tof_card, height=40, state="disabled",
                                      font=ctk.CTkFont(family="Courier", size=11))
        self.tof_box.pack(padx=16, pady=(0, 10), fill="x")

        # ── DHT22 ────────────────────────────────────────────────────────────
        dht_card = ctk.CTkFrame(scroll)
        dht_card.pack(padx=24, pady=6, fill="x")
        ctk.CTkLabel(dht_card, text="Temperature & Humidity — DHT22 (GPIO21)",
                     font=ctk.CTkFont(size=14, weight="bold")).pack(anchor="w", padx=16, pady=(10, 2))
        self.dht_box = ctk.CTkTextbox(dht_card, height=50, state="disabled",
                                      font=ctk.CTkFont(family="Courier", size=11))
        self.dht_box.pack(padx=16, pady=(0, 10), fill="x")

        # ── Analog ───────────────────────────────────────────────────────────
        analog_card = ctk.CTkFrame(scroll)
        analog_card.pack(padx=24, pady=6, fill="x")
        ctk.CTkLabel(analog_card, text="Analog — LDR (GPIO9) & Water Level (GPIO10)",
                     font=ctk.CTkFont(size=14, weight="bold")).pack(anchor="w", padx=16, pady=(10, 2))
        self.analog_box = ctk.CTkTextbox(analog_card, height=50, state="disabled",
                                         font=ctk.CTkFont(family="Courier", size=11))
        self.analog_box.pack(padx=16, pady=(0, 10), fill="x")

        # ── GPIO Inputs ──────────────────────────────────────────────────────
        gpio_card = ctk.CTkFrame(scroll)
        gpio_card.pack(padx=24, pady=6, fill="x")
        ctk.CTkLabel(gpio_card, text="GPIO Inputs — Magnetic Switch (GPIO40) & Button (GPIO41)",
                     font=ctk.CTkFont(size=14, weight="bold")).pack(anchor="w", padx=16, pady=(10, 2))
        self.gpio_box = ctk.CTkTextbox(gpio_card, height=50, state="disabled",
                                       font=ctk.CTkFont(family="Courier", size=11))
        self.gpio_box.pack(padx=16, pady=(0, 10), fill="x")

        # Status
        self.status_var = ctk.StringVar(value="")
        ctk.CTkLabel(self, textvariable=self.status_var,
                     font=ctk.CTkFont(size=11), text_color="gray").pack(pady=4)

    def _write(self, box, text):
        box.configure(state="normal")
        box.delete("1.0", "end")
        box.insert("end", text)
        box.configure(state="disabled")

    def _read_once(self):
        self.status_var.set("Reading…")

        def task():
            # IMU
            try:
                d = api.get_imu(self.app.base_url, self.app.headers)
                if d.get("ok"):
                    a, g = d["data"]["accel"], d["data"]["gyro"]
                    imu = (f"Accel (m/s²)  x={a['x']:+.4f}  y={a['y']:+.4f}  z={a['z']:+.4f}\n"
                           f"Gyro  (rad/s) x={g['x']:+.6f}  y={g['y']:+.6f}  z={g['z']:+.6f}\n"
                           f"Temp  (°C)    {d['data']['temperature']:.2f}")
                else:
                    imu = f"Error: {d.get('error', 'unknown')}"
            except Exception as e:
                imu = f"Error: {e}"

            # ToF
            try:
                d = api.get_tof(self.app.base_url, self.app.headers)
                tof = f"Range: {d['data']['range_mm']} mm" if d.get("ok") else f"Error: {d.get('error')}"
            except Exception as e:
                tof = f"Error: {e}"

            # DHT22
            try:
                d = api.get_dht(self.app.base_url, self.app.headers)
                if d.get("ok"):
                    dht = (f"Temperature: {d['data']['temperature']:.2f} °C\n"
                           f"Humidity:    {d['data']['humidity']:.2f} %")
                else:
                    dht = f"Error: {d.get('error', 'unknown')}"
            except Exception as e:
                dht = f"Error: {e}"

            # LDR + Water
            try:
                ldr_d = api.get_ldr(self.app.base_url, self.app.headers)
                water_d = api.get_water(self.app.base_url, self.app.headers)
                ldr = (f"LDR raw={ldr_d['data']['raw']}  {ldr_d['data']['percent']}%"
                       if ldr_d.get("ok") else f"LDR Error: {ldr_d.get('error')}")
                water = (f"Water raw={water_d['data']['raw']}  {water_d['data']['percent']}%"
                         if water_d.get("ok") else f"Water Error: {water_d.get('error')}")
                analog = f"{ldr}\n{water}"
            except Exception as e:
                analog = f"Error: {e}"

            # Magnetic + Button
            try:
                mag_d = api.get_magnetic(self.app.base_url, self.app.headers)
                btn_d = api.get_button(self.app.base_url, self.app.headers)
                mag = ("Magnetic: CLOSED" if mag_d["data"]["closed"] else "Magnetic: OPEN") \
                    if mag_d.get("ok") else f"Magnetic Error: {mag_d.get('error')}"
                btn = ("Button: PRESSED" if btn_d["data"]["pressed"] else "Button: RELEASED") \
                    if btn_d.get("ok") else f"Button Error: {btn_d.get('error')}"
                gpio = f"{mag}\n{btn}"
            except Exception as e:
                gpio = f"Error: {e}"

            self.after(0, lambda: self._write(self.imu_box, imu))
            self.after(0, lambda: self._write(self.tof_box, tof))
            self.after(0, lambda: self._write(self.dht_box, dht))
            self.after(0, lambda: self._write(self.analog_box, analog))
            self.after(0, lambda: self._write(self.gpio_box, gpio))
            self.after(0, lambda: self.status_var.set(""))

        threading.Thread(target=task, daemon=True).start()

    def _toggle_auto(self):
        self._auto_refresh = not self._auto_refresh
        if self._auto_refresh:
            self.auto_btn.configure(text="Auto: ON",
                                    fg_color="#27ae60", hover_color="#1e8449")
            self._schedule_refresh()
        else:
            self.auto_btn.configure(text="Auto: OFF",
                                    fg_color=("#3B8ED0", "#1F6AA5"),
                                    hover_color=("#36719F", "#144870"))
            if self._refresh_job:
                self.after_cancel(self._refresh_job)
                self._refresh_job = None

    def _schedule_refresh(self):
        if not self._auto_refresh:
            return
        self._read_once()
        self._refresh_job = self.after(5000, self._schedule_refresh)

    def on_show(self):
        pass
