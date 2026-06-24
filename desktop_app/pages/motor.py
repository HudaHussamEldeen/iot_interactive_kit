import threading
import customtkinter as ctk
import api


class MotorPage(ctk.CTkFrame):
    def __init__(self, parent, app):
        super().__init__(parent, corner_radius=0, fg_color="transparent")
        self.app = app

        ctk.CTkLabel(self, text="Motor Control",
                     font=ctk.CTkFont(size=22, weight="bold")).pack(pady=(32, 4))
        ctk.CTkLabel(self, text="DC motor via LEDC (GPIO15, GPIO16)",
                     font=ctk.CTkFont(size=13), text_color="gray").pack(pady=(0, 24))

        card = ctk.CTkFrame(self)
        card.pack(padx=60, pady=10, fill="x")

        # Speed
        ctk.CTkLabel(card, text="Speed (0–100%)",
                     font=ctk.CTkFont(size=14, weight="bold")).pack(anchor="w", padx=20, pady=(16, 4))

        self.speed_var = ctk.IntVar(value=0)
        speed_row = ctk.CTkFrame(card, fg_color="transparent")
        speed_row.pack(fill="x", padx=20, pady=(0, 8))

        self.speed_slider = ctk.CTkSlider(speed_row, from_=0, to=100, number_of_steps=100,
                                          variable=self.speed_var,
                                          command=lambda v: self.speed_label.configure(
                                              text=f"{int(v)}%"))
        self.speed_slider.pack(side="left", fill="x", expand=True)
        self.speed_label = ctk.CTkLabel(speed_row, text="0%", width=48)
        self.speed_label.pack(side="left", padx=(8, 0))

        # Direction
        ctk.CTkLabel(card, text="Direction",
                     font=ctk.CTkFont(size=14, weight="bold")).pack(anchor="w", padx=20, pady=(8, 4))

        self.direction_var = ctk.StringVar(value="forward")
        dir_row = ctk.CTkFrame(card, fg_color="transparent")
        dir_row.pack(padx=20, pady=(0, 16))

        ctk.CTkRadioButton(dir_row, text="Forward", variable=self.direction_var,
                           value="forward").pack(side="left", padx=16)
        ctk.CTkRadioButton(dir_row, text="Backward", variable=self.direction_var,
                           value="backward").pack(side="left", padx=16)

        # Buttons row
        btn_row = ctk.CTkFrame(card, fg_color="transparent")
        btn_row.pack(pady=(0, 20))

        ctk.CTkButton(btn_row, text="Apply", width=110, command=self._apply).pack(side="left", padx=8)
        ctk.CTkButton(btn_row, text="Stop", width=110,
                      fg_color="#c0392b", hover_color="#96281b",
                      command=self._stop).pack(side="left", padx=8)
        ctk.CTkButton(btn_row, text="Read State", width=110,
                      command=self._read).pack(side="left", padx=8)

        # State display
        self.state_var = ctk.StringVar(value="")
        ctk.CTkLabel(card, textvariable=self.state_var,
                     font=ctk.CTkFont(family="Courier", size=12),
                     text_color="gray").pack(padx=20, pady=(0, 16))

        self.status_var = ctk.StringVar(value="")
        ctk.CTkLabel(self, textvariable=self.status_var,
                     font=ctk.CTkFont(size=12), text_color="gray").pack(pady=8)

    def _read(self):
        def task():
            try:
                d = api.get_motor(self.app.base_url, self.app.headers)
                if d.get("ok"):
                    data = d["data"]
                    text = f"speed: {data['speed']}%  direction: {data['direction']}"
                else:
                    text = f"error: {d.get('error', 'unknown')}"
            except Exception as e:
                text = f"err: {e}"
            self.after(0, lambda: self.state_var.set(text))
            self.after(0, lambda: self.status_var.set(""))
        self.status_var.set("Reading motor state…")
        threading.Thread(target=task, daemon=True).start()

    def _apply(self):
        speed = self.speed_var.get()
        direction = self.direction_var.get()

        def task():
            try:
                d = api.set_motor(self.app.base_url, self.app.headers, speed, direction)
                text = f"speed: {speed}%  direction: {direction}" if d.get("ok") else f"error: {d.get('error')}"
            except Exception as e:
                text = f"err: {e}"
            self.after(0, lambda: self.state_var.set(text))
            self.after(0, lambda: self.status_var.set(""))
        self.status_var.set(f"Setting motor {speed}% {direction}…")
        threading.Thread(target=task, daemon=True).start()

    def _stop(self):
        self.speed_var.set(0)
        self.speed_label.configure(text="0%")

        def task():
            try:
                d = api.set_motor(self.app.base_url, self.app.headers, 0, "forward")
                text = "stopped" if d.get("ok") else f"error: {d.get('error')}"
            except Exception as e:
                text = f"err: {e}"
            self.after(0, lambda: self.state_var.set(text))
            self.after(0, lambda: self.status_var.set(""))
        self.status_var.set("Stopping motor…")
        threading.Thread(target=task, daemon=True).start()

    def on_show(self):
        self._read()
