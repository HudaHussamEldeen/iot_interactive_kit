import threading
import customtkinter as ctk
import api


class RelayPage(ctk.CTkFrame):
    def __init__(self, parent, app):
        super().__init__(parent, corner_radius=0, fg_color="transparent")
        self.app = app

        ctk.CTkLabel(self, text="Relay Control",
                     font=ctk.CTkFont(size=22, weight="bold")).pack(pady=(32, 4))
        ctk.CTkLabel(self, text="Toggle relay outputs (GPIO12, GPIO13)",
                     font=ctk.CTkFont(size=13), text_color="gray").pack(pady=(0, 24))

        self._labels = {}
        for relay in (1, 2):
            card = ctk.CTkFrame(self)
            card.pack(padx=60, pady=10, fill="x")
            card.grid_columnconfigure(0, weight=1)

            ctk.CTkLabel(card, text=f"Relay {relay}",
                         font=ctk.CTkFont(size=16, weight="bold")).grid(
                row=0, column=0, padx=20, pady=(16, 2), sticky="w")

            lbl = ctk.CTkLabel(card, text="state: —",
                               font=ctk.CTkFont(size=12), text_color="gray")
            lbl.grid(row=1, column=0, padx=20, pady=(0, 16), sticky="w")
            self._labels[relay] = lbl

            ctk.CTkButton(card, text="ON", width=90,
                          fg_color="#27ae60", hover_color="#1e8449",
                          command=lambda r=relay: self._set(r, "on")).grid(
                row=0, column=1, padx=8, pady=16, rowspan=2)

            ctk.CTkButton(card, text="OFF", width=90,
                          fg_color="#c0392b", hover_color="#96281b",
                          command=lambda r=relay: self._set(r, "off")).grid(
                row=0, column=2, padx=8, pady=16, rowspan=2)

            ctk.CTkButton(card, text="Read", width=80,
                          command=lambda r=relay: self._read(r)).grid(
                row=0, column=3, padx=(8, 20), pady=16, rowspan=2)

        self.status_var = ctk.StringVar(value="")
        ctk.CTkLabel(self, textvariable=self.status_var,
                     font=ctk.CTkFont(size=12), text_color="gray").pack(pady=8)

    def _update_label(self, relay, state):
        color = "#27ae60" if state == "on" else "#c0392b" if state == "off" else "gray"
        self._labels[relay].configure(text=f"state: {state}", text_color=color)

    def _read(self, relay):
        def task():
            try:
                d = api.get_relay(self.app.base_url, self.app.headers, relay)
                state = d.get("data", {}).get("state", "error") if d.get("ok") else "error"
            except Exception as e:
                state = f"err: {e}"
            self.after(0, lambda: self._update_label(relay, state))
            self.after(0, lambda: self.status_var.set(""))
        self.status_var.set(f"Reading relay {relay}…")
        threading.Thread(target=task, daemon=True).start()

    def _set(self, relay, state):
        def task():
            try:
                d = api.set_relay(self.app.base_url, self.app.headers, relay, state)
                result = state if d.get("ok") else "error"
            except Exception as e:
                result = f"err: {e}"
            self.after(0, lambda: self._update_label(relay, result))
            self.after(0, lambda: self.status_var.set(""))
        self.status_var.set(f"Setting relay {relay} {state}…")
        threading.Thread(target=task, daemon=True).start()

    def on_show(self):
        for relay in (1, 2):
            self._read(relay)
