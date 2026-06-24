import threading
import customtkinter as ctk
import api


class ProvisionPage(ctk.CTkFrame):
    def __init__(self, parent, app):
        super().__init__(parent, corner_radius=0, fg_color="transparent")
        self.app = app

        ctk.CTkLabel(self, text="Wi-Fi Provisioning",
                     font=ctk.CTkFont(size=22, weight="bold")).pack(pady=(32, 4))
        ctk.CTkLabel(self, text="Configure the device's Wi-Fi connection",
                     font=ctk.CTkFont(size=13), text_color="gray").pack(pady=(0, 24))

        form = ctk.CTkFrame(self, width=420)
        form.pack(padx=40, pady=0, fill="x")

        # SSID
        ctk.CTkLabel(form, text="SSID", anchor="w").grid(row=0, column=0, sticky="w", padx=16, pady=(16, 2))
        self.ssid_var = ctk.StringVar()
        ctk.CTkEntry(form, textvariable=self.ssid_var, placeholder_text="Network name", width=280).grid(
            row=1, column=0, padx=16, pady=(0, 8), sticky="w")

        # PSK
        ctk.CTkLabel(form, text="Password", anchor="w").grid(row=2, column=0, sticky="w", padx=16, pady=(4, 2))
        self.psk_var = ctk.StringVar()
        ctk.CTkEntry(form, textvariable=self.psk_var, placeholder_text="Leave empty for open network",
                     show="*", width=280).grid(row=3, column=0, padx=16, pady=(0, 8), sticky="w")

        # IP mode
        ctk.CTkLabel(form, text="IP Mode", anchor="w").grid(row=4, column=0, sticky="w", padx=16, pady=(4, 2))
        self.ip_mode_var = ctk.StringVar(value="dhcp")
        seg = ctk.CTkSegmentedButton(form, values=["dhcp", "static"],
                                     variable=self.ip_mode_var, command=self._on_ip_mode)
        seg.grid(row=5, column=0, padx=16, pady=(0, 8), sticky="w")

        # Static IP fields (hidden by default)
        self.static_frame = ctk.CTkFrame(form, fg_color="transparent")
        self.static_frame.grid(row=6, column=0, padx=16, sticky="w")
        self.static_frame.grid_remove()

        for i, (label, attr) in enumerate([
            ("IP Address", "ip_var"),
            ("Netmask",    "netmask_var"),
            ("Gateway",    "gw_var"),
        ]):
            ctk.CTkLabel(self.static_frame, text=label, anchor="w").grid(
                row=i*2, column=0, sticky="w", pady=(4, 0))
            var = ctk.StringVar()
            setattr(self, attr, var)
            ctk.CTkEntry(self.static_frame, textvariable=var, width=240).grid(
                row=i*2+1, column=0, sticky="w", pady=(0, 4))

        # Buttons
        btn_frame = ctk.CTkFrame(self, fg_color="transparent")
        btn_frame.pack(pady=20)

        self.connect_btn = ctk.CTkButton(btn_frame, text="Connect", width=140,
                                         command=self._connect)
        self.connect_btn.grid(row=0, column=0, padx=8)

        ctk.CTkButton(btn_frame, text="Factory Reset", width=140,
                      fg_color="#c0392b", hover_color="#96281b",
                      command=self._factory_reset).grid(row=0, column=1, padx=8)

        # Status
        self.status_var = ctk.StringVar(value="")
        self.status_label = ctk.CTkLabel(self, textvariable=self.status_var,
                                         font=ctk.CTkFont(size=13), wraplength=480)
        self.status_label.pack(pady=8)

        # No-connection banner (hidden by default)
        self.no_conn_frame = ctk.CTkFrame(self, fg_color="#2c1f1f", corner_radius=8)
        ctk.CTkLabel(self.no_conn_frame, text="Not connected to device",
                     font=ctk.CTkFont(size=14, weight="bold"),
                     text_color="#e74c3c").pack(pady=(12, 2))
        self.no_conn_hint = ctk.CTkLabel(
            self.no_conn_frame,
            text="",
            font=ctk.CTkFont(size=12), text_color="#aaaaaa", wraplength=400)
        self.no_conn_hint.pack(pady=(0, 12), padx=16)

        # Device info box
        self.info_box = ctk.CTkTextbox(self, height=120, state="disabled", font=ctk.CTkFont(size=12))
        self.info_box.pack(padx=40, pady=(0, 16), fill="x")

        self._poll_job = None
        self._fetch_status()

    def _on_ip_mode(self, value):
        if value == "static":
            self.static_frame.grid()
        else:
            self.static_frame.grid_remove()

    def _set_status(self, msg, color="white"):
        self.status_var.set(msg)
        self.status_label.configure(text_color=color)

    def _show_no_conn_banner(self, target_ip):
        hint = (f"Connect your computer to the device's Wi-Fi network,\n"
                f"or set the correct Device IP in the sidebar.\n"
                f"Current target: {target_ip}")
        self.no_conn_hint.configure(text=hint)
        self.no_conn_frame.pack(padx=40, pady=(0, 8), fill="x", before=self.info_box)

    def _hide_no_conn_banner(self):
        self.no_conn_frame.pack_forget()

    def _fetch_status(self):
        target_ip = self.app.device_ip_var.get()
        def task():
            try:
                data = api.get_status(self.app.base_url, self.app.headers)
                dev = data.get("device", {})
                wifi = data.get("wifi", {})
                prov = data.get("provisioning", {})
                sta_ip = wifi.get("sta_ipv4", "")
                mode = prov.get("mode", "")
                info = (
                    f"Device ID : {dev.get('id', '?')}\n"
                    f"Mode      : {mode}\n"
                    f"Link      : {wifi.get('link_state', '?')}\n"
                    f"SSID      : {wifi.get('sta_ssid', '?') or '—'}\n"
                    f"IP        : {sta_ip or '—'}\n"
                    f"Uptime    : {dev.get('uptime_s', '?')} s"
                )
                self.after(0, self._hide_no_conn_banner)
                self.after(0, lambda: self._update_info(info))
                # Auto-switch sidebar IP to station IP once device is operational
                if mode == "operational" and sta_ip and sta_ip != self.app.device_ip_var.get():
                    self.after(0, lambda ip=sta_ip: self.app.device_ip_var.set(ip))
                    self.after(0, lambda ip=sta_ip: self._set_status(
                        f"Device operational — switched to {ip}", "#2ecc71"))
            except Exception:
                self.after(0, lambda ip=target_ip: self._show_no_conn_banner(ip))
                self.after(0, lambda: self._set_status("", "white"))
        threading.Thread(target=task, daemon=True).start()

    def _update_info(self, text):
        self.info_box.configure(state="normal")
        self.info_box.delete("1.0", "end")
        self.info_box.insert("end", text)
        self.info_box.configure(state="disabled")

    def _connect(self):
        ssid = self.ssid_var.get().strip()
        if not ssid:
            self._set_status("SSID is required.", "#e74c3c")
            return

        self.connect_btn.configure(state="disabled")
        self._set_status("Connecting… (this may take up to 2 minutes)", "#f39c12")

        def task():
            try:
                result = api.post_wifi(
                    self.app.base_url, self.app.headers,
                    ssid=ssid,
                    psk=self.psk_var.get(),
                    ip_mode=self.ip_mode_var.get(),
                    ip_address=getattr(self, "ip_var", ctk.StringVar()).get(),
                    netmask=getattr(self, "netmask_var", ctk.StringVar()).get(),
                    gateway=getattr(self, "gw_var", ctk.StringVar()).get(),
                )
                if result.get("ok"):
                    ip = result.get("ip", "?")
                    msg = f"Connected!  IP: {ip}"
                    self.after(0, lambda: self._set_status(msg, "#2ecc71"))
                    if ip and ip != "?":
                        self.after(500, lambda i=ip: self.app.device_ip_var.set(i))
                    self.after(0, self._fetch_status)
                else:
                    err = result.get("error", "unknown error")
                    self.after(0, lambda: self._set_status(f"Failed: {err}", "#e74c3c"))
            except Exception as e:
                self.after(0, lambda err=e: self._set_status(f"Error: {err}", "#e74c3c"))
            finally:
                self.after(0, lambda: self.connect_btn.configure(state="normal"))

        threading.Thread(target=task, daemon=True).start()

    def _factory_reset(self):
        def task():
            try:
                api.provision_reset(self.app.base_url, self.app.headers)
                self.after(0, lambda: self._set_status("Factory reset done. Device in provisioning mode.", "#f39c12"))
            except Exception as e:
                self.after(0, lambda err=e: self._set_status(f"Reset error: {err}", "#e74c3c"))
        threading.Thread(target=task, daemon=True).start()

    def _start_poll(self):
        self._stop_poll()
        self._fetch_status()
        self._poll_job = self.after(20000, self._start_poll)

    def _stop_poll(self):
        if self._poll_job is not None:
            self.after_cancel(self._poll_job)
            self._poll_job = None

    def on_show(self):
        self._start_poll()

    def on_hide(self):
        self._stop_poll()
