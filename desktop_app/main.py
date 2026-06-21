import customtkinter as ctk
from pages.provision import ProvisionPage
from pages.sensors import SensorsPage
from pages.servo import ServoPage
from pages.buzzer import BuzzerPage

ctk.set_appearance_mode("dark")
ctk.set_default_color_theme("blue")


class App(ctk.CTk):
    def __init__(self):
        super().__init__()
        self.title("IoT Interactive Kit")
        self.geometry("800x600")
        self.resizable(False, False)

        # ── Sidebar ──────────────────────────────────────────────────────────
        self.sidebar = ctk.CTkFrame(self, width=180, corner_radius=0)
        self.sidebar.pack(side="left", fill="y")
        self.sidebar.pack_propagate(False)

        ctk.CTkLabel(self.sidebar, text="IoT Kit", font=ctk.CTkFont(size=20, weight="bold")).pack(pady=(24, 4))

        self.device_ip_var = ctk.StringVar(value="192.168.4.1")
        ctk.CTkLabel(self.sidebar, text="Device IP", font=ctk.CTkFont(size=11)).pack(pady=(12, 0))
        self.ip_entry = ctk.CTkEntry(self.sidebar, textvariable=self.device_ip_var, width=150)
        self.ip_entry.pack(pady=(2, 16))

        self.token_var = ctk.StringVar(value="iot-kit-dev")
        ctk.CTkLabel(self.sidebar, text="Bearer Token", font=ctk.CTkFont(size=11)).pack()
        ctk.CTkEntry(self.sidebar, textvariable=self.token_var, width=150).pack(pady=(2, 24))

        nav_items = [
            ("📡  Provision",  "provision"),
            ("📊  Sensors",    "sensors"),
            ("⚙️  Servo",      "servo"),
            ("🔔  Buzzer",     "buzzer"),
        ]
        self.nav_buttons = {}
        for label, name in nav_items:
            btn = ctk.CTkButton(
                self.sidebar, text=label, width=150, anchor="w",
                command=lambda n=name: self.show_page(n),
                fg_color="transparent", hover_color=("gray70", "gray30"),
            )
            btn.pack(pady=4, padx=12)
            self.nav_buttons[name] = btn

        # Status bar at bottom of sidebar
        self.status_var = ctk.StringVar(value="")
        ctk.CTkLabel(self.sidebar, textvariable=self.status_var, font=ctk.CTkFont(size=10),
                     wraplength=160, justify="center").pack(side="bottom", pady=12, padx=8)

        # ── Main content area ─────────────────────────────────────────────────
        self.content = ctk.CTkFrame(self, corner_radius=0, fg_color=("gray90", "gray15"))
        self.content.pack(side="left", fill="both", expand=True)

        self.pages = {
            "provision": ProvisionPage(self.content, self),
            "sensors":   SensorsPage(self.content, self),
            "servo":     ServoPage(self.content, self),
            "buzzer":    BuzzerPage(self.content, self),
        }

        self.show_page("provision")

    def show_page(self, name):
        for page in self.pages.values():
            page.pack_forget()
        self.pages[name].pack(fill="both", expand=True)

        for n, btn in self.nav_buttons.items():
            btn.configure(fg_color=("gray75", "gray25") if n == name else "transparent")

        if hasattr(self.pages[name], "on_show"):
            self.pages[name].on_show()

    def set_status(self, msg, color="gray"):
        self.status_var.set(msg)

    @property
    def base_url(self):
        return f"http://{self.device_ip_var.get()}:8080"

    @property
    def headers(self):
        return {
            "Authorization": f"Bearer {self.token_var.get()}",
            "Content-Type":  "application/json",
        }


if __name__ == "__main__":
    App().mainloop()
