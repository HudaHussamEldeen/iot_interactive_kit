import threading
import customtkinter as ctk
import api


class LedPage(ctk.CTkFrame):
    def __init__(self, parent, app):
        super().__init__(parent, corner_radius=0, fg_color="transparent")
        self.app = app

        ctk.CTkLabel(self, text="LED Control",
                     font=ctk.CTkFont(size=22, weight="bold")).pack(pady=(32, 4))
        ctk.CTkLabel(self, text="Toggle LEDs (GPIO7, GPIO8, GPIO3)",
                     font=ctk.CTkFont(size=13), text_color="gray").pack(pady=(0, 24))

        self._labels = {}
        for led in (1, 2, 3):
            card = ctk.CTkFrame(self)
            card.pack(padx=60, pady=10, fill="x")
            card.grid_columnconfigure(0, weight=1)

            ctk.CTkLabel(card, text=f"LED {led}",
                         font=ctk.CTkFont(size=16, weight="bold")).grid(
                row=0, column=0, padx=20, pady=(16, 2), sticky="w")

            lbl = ctk.CTkLabel(card, text="state: —",
                               font=ctk.CTkFont(size=12), text_color="gray")
            lbl.grid(row=1, column=0, padx=20, pady=(0, 16), sticky="w")
            self._labels[led] = lbl

            ctk.CTkButton(card, text="ON", width=90,
                          fg_color="#f39c12", hover_color="#d68910",
                          command=lambda l=led: self._set(l, "on")).grid(
                row=0, column=1, padx=8, pady=16, rowspan=2)

            ctk.CTkButton(card, text="OFF", width=90,
                          fg_color="#7f8c8d", hover_color="#616a6b",
                          command=lambda l=led: self._set(l, "off")).grid(
                row=0, column=2, padx=8, pady=16, rowspan=2)

            ctk.CTkButton(card, text="Read", width=80,
                          command=lambda l=led: self._read(l)).grid(
                row=0, column=3, padx=(8, 20), pady=16, rowspan=2)

        self.status_var = ctk.StringVar(value="")
        ctk.CTkLabel(self, textvariable=self.status_var,
                     font=ctk.CTkFont(size=12), text_color="gray").pack(pady=8)

    def _update_label(self, led, state):
        color = "#f39c12" if state == "on" else "#7f8c8d" if state == "off" else "gray"
        self._labels[led].configure(text=f"state: {state}", text_color=color)

    def _read(self, led):
        def task():
            try:
                d = api.get_led(self.app.base_url, self.app.headers, led)
                state = d.get("data", {}).get("state", "error") if d.get("ok") else "error"
            except Exception as e:
                state = f"err: {e}"
            self.after(0, lambda: self._update_label(led, state))
            self.after(0, lambda: self.status_var.set(""))
        self.status_var.set(f"Reading LED {led}…")
        threading.Thread(target=task, daemon=True).start()

    def _set(self, led, state):
        def task():
            try:
                d = api.set_led(self.app.base_url, self.app.headers, led, state)
                result = state if d.get("ok") else "error"
            except Exception as e:
                result = f"err: {e}"
            self.after(0, lambda: self._update_label(led, result))
            self.after(0, lambda: self.status_var.set(""))
        self.status_var.set(f"Setting LED {led} {state}…")
        threading.Thread(target=task, daemon=True).start()

    def on_show(self):
        for led in (1, 2, 3):
            self._read(led)
