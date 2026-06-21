import threading
import customtkinter as ctk
import api


class ServoPage(ctk.CTkFrame):
    def __init__(self, parent, app):
        super().__init__(parent, corner_radius=0, fg_color="transparent")
        self.app = app

        ctk.CTkLabel(self, text="Servo Control",
                     font=ctk.CTkFont(size=22, weight="bold")).pack(pady=(32, 4))
        ctk.CTkLabel(self, text="Set servo angle 0–180°",
                     font=ctk.CTkFont(size=13), text_color="gray").pack(pady=(0, 24))

        card = ctk.CTkFrame(self, width=420)
        card.pack(padx=40, pady=0)

        # Current angle readout
        self.angle_readout = ctk.CTkLabel(card, text="—°",
                                          font=ctk.CTkFont(size=48, weight="bold"))
        self.angle_readout.pack(pady=(24, 8))

        # Slider
        self.slider_var = ctk.DoubleVar(value=90)
        self.slider = ctk.CTkSlider(card, from_=0, to=180, number_of_steps=180,
                                    variable=self.slider_var, width=320,
                                    command=self._on_slider)
        self.slider.pack(padx=24, pady=8)

        tick_row = ctk.CTkFrame(card, fg_color="transparent")
        tick_row.pack(padx=24, fill="x")
        for val in [0, 45, 90, 135, 180]:
            ctk.CTkLabel(tick_row, text=str(val), font=ctk.CTkFont(size=11),
                         text_color="gray").pack(side="left", expand=True)

        # Preset buttons
        preset_frame = ctk.CTkFrame(card, fg_color="transparent")
        preset_frame.pack(pady=16)
        for angle in [0, 45, 90, 135, 180]:
            ctk.CTkButton(preset_frame, text=f"{angle}°", width=60,
                          command=lambda a=angle: self._set_angle(a)).pack(side="left", padx=4)

        # Set button
        self.set_btn = ctk.CTkButton(card, text="Set Angle", width=140,
                                     command=self._send)
        self.set_btn.pack(pady=(8, 8))

        # Get current
        ctk.CTkButton(card, text="Read Current", width=140,
                      fg_color="transparent", border_width=1,
                      command=self._read).pack(pady=(0, 20))

        # Status
        self.status_var = ctk.StringVar(value="")
        ctk.CTkLabel(self, textvariable=self.status_var,
                     font=ctk.CTkFont(size=13)).pack(pady=4)

    def _on_slider(self, value):
        self.angle_readout.configure(text=f"{int(value)}°")

    def _set_angle(self, angle):
        self.slider_var.set(angle)
        self.angle_readout.configure(text=f"{angle}°")
        self._send()

    def _send(self):
        angle = int(self.slider_var.get())
        self.set_btn.configure(state="disabled")
        self.status_var.set(f"Setting to {angle}°…")

        def task():
            try:
                result = api.set_servo(self.app.base_url, self.app.headers, angle)
                if result.get("ok"):
                    actual = result.get("data", {}).get("angle", angle)
                    self.after(0, lambda: self.status_var.set(f"Set to {actual}°"))
                else:
                    self.after(0, lambda: self.status_var.set(f"Error: {result.get('error', 'failed')}"))
            except Exception as e:
                self.after(0, lambda err=e: self.status_var.set(f"Error: {err}"))
            finally:
                self.after(0, lambda: self.set_btn.configure(state="normal"))

        threading.Thread(target=task, daemon=True).start()

    def _read(self):
        self.status_var.set("Reading…")
        def task():
            try:
                result = api.get_servo(self.app.base_url, self.app.headers)
                if result.get("ok"):
                    angle = result.get("data", {}).get("angle", 0)
                    self.after(0, lambda: self.slider_var.set(angle))
                    self.after(0, lambda: self.angle_readout.configure(text=f"{angle}°"))
                    self.after(0, lambda: self.status_var.set(f"Current: {angle}°"))
                else:
                    self.after(0, lambda: self.status_var.set(f"Error: {result.get('error', 'failed')}"))
            except Exception as e:
                self.after(0, lambda err=e: self.status_var.set(f"Error: {err}"))
        threading.Thread(target=task, daemon=True).start()

    def on_show(self):
        self._read()
