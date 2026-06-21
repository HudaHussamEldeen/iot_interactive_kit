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
                     font=ctk.CTkFont(size=22, weight="bold")).pack(pady=(32, 4))
        ctk.CTkLabel(self, text="On-demand readings from IMU and Time-of-Flight sensor",
                     font=ctk.CTkFont(size=13), text_color="gray").pack(pady=(0, 20))

        # Controls row
        ctrl = ctk.CTkFrame(self, fg_color="transparent")
        ctrl.pack(pady=(0, 12))

        ctk.CTkButton(ctrl, text="Read Once", width=120, command=self._read_once).grid(row=0, column=0, padx=8)
        self.auto_btn = ctk.CTkButton(ctrl, text="Auto Refresh: OFF", width=160,
                                      command=self._toggle_auto)
        self.auto_btn.grid(row=0, column=1, padx=8)

        # IMU card
        imu_card = ctk.CTkFrame(self)
        imu_card.pack(padx=40, pady=6, fill="x")
        ctk.CTkLabel(imu_card, text="IMU — MPU6050",
                     font=ctk.CTkFont(size=15, weight="bold")).pack(anchor="w", padx=16, pady=(12, 4))

        self.imu_box = ctk.CTkTextbox(imu_card, height=160, state="disabled",
                                      font=ctk.CTkFont(family="Courier", size=12))
        self.imu_box.pack(padx=16, pady=(0, 12), fill="x")

        # ToF card
        tof_card = ctk.CTkFrame(self)
        tof_card.pack(padx=40, pady=6, fill="x")
        ctk.CTkLabel(tof_card, text="Distance — VL6180X (ToF)",
                     font=ctk.CTkFont(size=15, weight="bold")).pack(anchor="w", padx=16, pady=(12, 4))

        self.tof_box = ctk.CTkTextbox(tof_card, height=60, state="disabled",
                                      font=ctk.CTkFont(family="Courier", size=12))
        self.tof_box.pack(padx=16, pady=(0, 12), fill="x")

        # Status
        self.status_var = ctk.StringVar(value="")
        ctk.CTkLabel(self, textvariable=self.status_var, font=ctk.CTkFont(size=12),
                     text_color="gray").pack(pady=4)

    def _write_box(self, box, text):
        box.configure(state="normal")
        box.delete("1.0", "end")
        box.insert("end", text)
        box.configure(state="disabled")

    def _read_imu(self):
        try:
            d = api.get_imu(self.app.base_url, self.app.headers)
            if not d.get("ok"):
                return f"Error: {d.get('error', 'unknown')}"
            data = d["data"]
            a, g = data["accel"], data["gyro"]
            return (
                f"Accel (m/s²)  x={a['x']:+.4f}  y={a['y']:+.4f}  z={a['z']:+.4f}\n"
                f"Gyro  (rad/s) x={g['x']:+.6f}  y={g['y']:+.6f}  z={g['z']:+.6f}\n"
                f"Temp  (°C)    {data['temperature']:.2f}"
            )
        except Exception as e:
            return f"Error: {e}"  # same thread, no lambda — safe

    def _read_tof(self):
        try:
            d = api.get_tof(self.app.base_url, self.app.headers)
            if not d.get("ok"):
                return f"Error: {d.get('error', 'unknown')}"
            return f"Range: {d['data']['range_mm']} mm"
        except Exception as e:
            return f"Error: {e}"  # same thread, no lambda — safe

    def _read_once(self):
        self.status_var.set("Reading…")
        def task():
            imu_text = self._read_imu()
            tof_text = self._read_tof()
            self.after(0, lambda: self._write_box(self.imu_box, imu_text))
            self.after(0, lambda: self._write_box(self.tof_box, tof_text))
            self.after(0, lambda: self.status_var.set(""))
        threading.Thread(target=task, daemon=True).start()

    def _toggle_auto(self):
        self._auto_refresh = not self._auto_refresh
        if self._auto_refresh:
            self.auto_btn.configure(text="Auto Refresh: ON",
                                    fg_color="#27ae60", hover_color="#1e8449")
            self._schedule_refresh()
        else:
            self.auto_btn.configure(text="Auto Refresh: OFF",
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
