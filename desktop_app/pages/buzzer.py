import threading
import customtkinter as ctk
import api


class BuzzerPage(ctk.CTkFrame):
    def __init__(self, parent, app):
        super().__init__(parent, corner_radius=0, fg_color="transparent")
        self.app = app

        ctk.CTkLabel(self, text="Buzzer",
                     font=ctk.CTkFont(size=22, weight="bold")).pack(pady=(32, 4))
        ctk.CTkLabel(self, text="Control the piezo buzzer",
                     font=ctk.CTkFont(size=13), text_color="gray").pack(pady=(0, 20))

        # Shared freq control
        freq_row = ctk.CTkFrame(self, fg_color="transparent")
        freq_row.pack(pady=(0, 16))
        ctk.CTkLabel(freq_row, text="Frequency (Hz):", font=ctk.CTkFont(size=13)).pack(side="left", padx=(0, 8))
        self.freq_var = ctk.IntVar(value=1000)
        ctk.CTkEntry(freq_row, textvariable=self.freq_var, width=90).pack(side="left", padx=(0, 12))
        ctk.CTkLabel(freq_row, text="100–20000", font=ctk.CTkFont(size=11),
                     text_color="gray").pack(side="left")

        # Frequency presets
        preset_row = ctk.CTkFrame(self, fg_color="transparent")
        preset_row.pack(pady=(0, 16))
        ctk.CTkLabel(preset_row, text="Presets:", font=ctk.CTkFont(size=12)).pack(side="left", padx=(0, 8))
        for label, hz in [("Low", 440), ("Mid", 1000), ("High", 2500), ("Alert", 4000)]:
            ctk.CTkButton(preset_row, text=f"{label} ({hz}Hz)", width=110,
                          fg_color="transparent", border_width=1,
                          command=lambda f=hz: self.freq_var.set(f)).pack(side="left", padx=4)

        # On / Off
        on_off = ctk.CTkFrame(self, fg_color="transparent")
        on_off.pack(pady=(0, 12))
        self.on_btn = ctk.CTkButton(on_off, text="Turn ON", width=140,
                                    fg_color="#27ae60", hover_color="#1e8449",
                                    command=self._turn_on)
        self.on_btn.grid(row=0, column=0, padx=8)
        self.off_btn = ctk.CTkButton(on_off, text="Turn OFF", width=140,
                                     fg_color="#c0392b", hover_color="#96281b",
                                     command=self._turn_off)
        self.off_btn.grid(row=0, column=1, padx=8)

        # Beep section
        beep_card = ctk.CTkFrame(self)
        beep_card.pack(padx=40, pady=8, fill="x")
        ctk.CTkLabel(beep_card, text="Beep (single tone)",
                     font=ctk.CTkFont(size=15, weight="bold")).pack(anchor="w", padx=16, pady=(12, 6))

        dur_row = ctk.CTkFrame(beep_card, fg_color="transparent")
        dur_row.pack(anchor="w", padx=16, pady=(0, 8))
        ctk.CTkLabel(dur_row, text="Duration (ms):", font=ctk.CTkFont(size=13)).pack(side="left", padx=(0, 8))
        self.dur_var = ctk.IntVar(value=300)
        ctk.CTkEntry(dur_row, textvariable=self.dur_var, width=80).pack(side="left", padx=(0, 12))
        ctk.CTkLabel(dur_row, text="1–5000", font=ctk.CTkFont(size=11),
                     text_color="gray").pack(side="left")

        # Duration slider
        self.dur_slider = ctk.CTkSlider(beep_card, from_=50, to=2000, number_of_steps=199,
                                        width=320, command=lambda v: self.dur_var.set(int(v)))
        self.dur_slider.set(300)
        self.dur_slider.pack(padx=16, pady=(0, 4))

        self.beep_btn = ctk.CTkButton(beep_card, text="Beep!", width=140,
                                      command=self._beep)
        self.beep_btn.pack(pady=(4, 16))

        # Quick beep patterns
        pat_row = ctk.CTkFrame(self, fg_color="transparent")
        pat_row.pack(pady=(4, 0))
        ctk.CTkLabel(pat_row, text="Patterns:", font=ctk.CTkFont(size=12)).pack(side="left", padx=(0, 8))
        for label, f, d in [("Short", 1000, 100), ("Long", 1000, 800), ("Alert ×3", 2500, 150)]:
            ctk.CTkButton(pat_row, text=label, width=90,
                          fg_color="transparent", border_width=1,
                          command=lambda ff=f, dd=d, lb=label: self._pattern(lb, ff, dd)).pack(side="left", padx=4)

        # Status
        self.status_var = ctk.StringVar(value="")
        self.status_label = ctk.CTkLabel(self, textvariable=self.status_var,
                                         font=ctk.CTkFont(size=13))
        self.status_label.pack(pady=12)

    def _set_status(self, msg, color="white"):
        self.status_var.set(msg)
        self.status_label.configure(text_color=color)

    def _validate_freq(self):
        try:
            f = int(self.freq_var.get())
            if not 100 <= f <= 20000:
                self._set_status("Frequency must be 100–20000 Hz.", "#e74c3c")
                return None
            return f
        except (ValueError, ctk.tkinter.TclError):
            self._set_status("Invalid frequency.", "#e74c3c")
            return None

    def _turn_on(self):
        freq = self._validate_freq()
        if freq is None:
            return
        self._set_status(f"Turning on at {freq} Hz…", "#f39c12")
        def task():
            try:
                api.buzzer_on(self.app.base_url, self.app.headers, freq)
                self.after(0, lambda: self._set_status(f"ON at {freq} Hz", "#27ae60"))
            except Exception as e:
                self.after(0, lambda err=e: self._set_status(f"Error: {err}", "#e74c3c"))
        threading.Thread(target=task, daemon=True).start()

    def _turn_off(self):
        self._set_status("Turning off…", "#f39c12")
        def task():
            try:
                api.buzzer_off(self.app.base_url, self.app.headers)
                self.after(0, lambda: self._set_status("OFF", "gray"))
            except Exception as e:
                self.after(0, lambda err=e: self._set_status(f"Error: {err}", "#e74c3c"))
        threading.Thread(target=task, daemon=True).start()

    def _beep(self):
        freq = self._validate_freq()
        if freq is None:
            return
        try:
            dur = int(self.dur_var.get())
            if not 1 <= dur <= 5000:
                self._set_status("Duration must be 1–5000 ms.", "#e74c3c")
                return
        except (ValueError, ctk.tkinter.TclError):
            self._set_status("Invalid duration.", "#e74c3c")
            return

        self.beep_btn.configure(state="disabled")
        self._set_status(f"Beeping {dur} ms at {freq} Hz…", "#f39c12")
        def task():
            try:
                api.buzzer_beep(self.app.base_url, self.app.headers, freq, dur)
                self.after(0, lambda: self._set_status("Done", "#27ae60"))
            except Exception as e:
                self.after(0, lambda err=e: self._set_status(f"Error: {err}", "#e74c3c"))
            finally:
                self.after(0, lambda: self.beep_btn.configure(state="normal"))
        threading.Thread(target=task, daemon=True).start()

    def _pattern(self, label, freq, dur_ms):
        repeats = 3 if "×3" in label else 1
        self._set_status(f"Pattern: {label}…", "#f39c12")
        def task():
            try:
                for _ in range(repeats):
                    api.buzzer_beep(self.app.base_url, self.app.headers, freq, dur_ms)
                self.after(0, lambda: self._set_status("Done", "#27ae60"))
            except Exception as e:
                self.after(0, lambda err=e: self._set_status(f"Error: {err}", "#e74c3c"))
        threading.Thread(target=task, daemon=True).start()
