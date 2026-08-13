import tkinter as tk
import json
import os
from pycaw.pycaw import (
    AudioUtilities,
    ISimpleAudioVolume,
    IAudioEndpointVolume,
    IAudioMeterInformation
)
from comtypes import CLSCTX_ALL
from ctypes import cast, POINTER

CONFIG_FILE = "config.json"

def load_config():
    if os.path.exists(CONFIG_FILE):
        with open(CONFIG_FILE, "r") as f:
            return json.load(f)
    return {"orientation": "vertical"}

def save_config(config):
    with open(CONFIG_FILE, "w") as f:
        json.dump(config, f)

class VolumeMixerApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Volume Mixer")
        self.root.configure(bg="#1e1e1e")
        self.font = ("Segoe UI", 9)

        # Config
        self.config = load_config()
        self.vertical = self.config.get("orientation", "vertical") == "vertical"
        self.peak_bars = []

        # GUI
        self.create_controls()
        self.refresh()
        self.update_peak_levels()

    def create_controls(self):
        for widget in self.root.winfo_children():
            widget.destroy()

        # Orientation button
        btn_text = "Passa a Orizzontale" if self.vertical else "Passa a Verticale"
        toggle_btn = tk.Button(
            self.root, text=btn_text, command=self.toggle_orientation,
            bg="#444", fg="white", font=self.font, relief='flat'
        )
        toggle_btn.pack(pady=5)

        # Global volume
        self.sys_frame = tk.Frame(self.root, bg="#1e1e1e")
        self.sys_frame.pack(padx=10, pady=5, anchor='w')

        tk.Label(self.sys_frame, text="🔊 Sistema", fg="#ffffff", bg="#1e1e1e", font=self.font).pack(side='left')

        orientation = 'vertical' if self.vertical else 'horizontal'
        length = 100 if self.vertical else 200

        self.sys_slider = tk.Scale(
            self.sys_frame, from_=0, to=100, orient=orientation, length=length,
            troughcolor="#3a3a3a", bg="#1e1e1e", fg="#ffffff", highlightthickness=0,
            sliderrelief='flat', font=self.font
        )
        self.sys_slider.pack(side='left', padx=10)
        self.sys_slider.bind("<ButtonRelease-1>", self.set_system_volume)

        # App sliders container
        self.sliders_frame = tk.Frame(self.root, bg="#1e1e1e")
        self.sliders_frame.pack(padx=10, pady=10, anchor='w')

        # Refresh button
        tk.Button(
            self.root, text="🔄 Refresh", command=self.refresh,
            bg="#333333", fg="#ffffff", font=self.font, relief='flat'
        ).pack(pady=5)

    def toggle_orientation(self):
        self.vertical = not self.vertical
        self.config["orientation"] = "vertical" if self.vertical else "horizontal"
        save_config(self.config)
        self.create_controls()
        self.refresh()

    def set_system_volume(self, event):
        volume = self.sys_slider.get() / 100.0
        self.sys_vol.SetMasterVolumeLevelScalar(volume, None)

    def on_app_slider_release(self, volume_obj, slider):
        def inner(event):
            volume = slider.get() / 100.0
            volume_obj.SetMasterVolume(volume, None)
        return inner

    def refresh(self):
        for widget in self.sliders_frame.winfo_children():
            widget.destroy()
        self.sliders = []
        self.peak_bars = []

        # Master volume
        speakers = AudioUtilities.GetSpeakers()
        endpoint = speakers.Activate(IAudioEndpointVolume._iid_, CLSCTX_ALL, None)
        self.sys_vol = cast(endpoint, POINTER(IAudioEndpointVolume))
        current = self.sys_vol.GetMasterVolumeLevelScalar()
        self.sys_slider.set(int(current * 100))

        orientation = 'vertical' if self.vertical else 'horizontal'
        length = 100 if self.vertical else 200

        for session in AudioUtilities.GetAllSessions():
            name = session.Process.name() if session.Process else "System Sounds"
            vol = session._ctl.QueryInterface(ISimpleAudioVolume)
            meter = session._ctl.QueryInterface(IAudioMeterInformation)
            current = vol.GetMasterVolume()

            frame = tk.Frame(self.sliders_frame, bg="#1e1e1e")
            frame.pack(padx=5, pady=5, side='left' if self.vertical else 'top', anchor='n')

            tk.Label(frame, text=name[:20], fg="#cccccc", bg="#1e1e1e", font=self.font).pack()

            slider = tk.Scale(
                frame, from_=0, to=100, orient=orientation, length=length,
                troughcolor="#3a3a3a", bg="#1e1e1e", fg="#ffffff", highlightthickness=0,
                sliderrelief='flat', font=self.font
            )
            slider.set(int(current * 100))
            slider.pack()
            slider.bind("<ButtonRelease-1>", self.on_app_slider_release(vol, slider))

            bar = tk.Canvas(
                frame,
                width=10 if self.vertical else length,
                height=length if self.vertical else 10,
                bg="#1e1e1e", highlightthickness=0
            )
            bar.pack(pady=2)

            self.peak_bars.append((bar, meter))

    def update_peak_levels(self):
        for bar, meter in self.peak_bars:
            try:
                peak = meter.GetPeakValue()
            except:
                peak = 0.0

            bar.delete("peak")

            if self.vertical:
                height = bar.winfo_height()
                fill = int(height * (1 - peak))
                bar.create_rectangle(0, fill, 10, height, fill="#00ff00", tags="peak")
            else:
                width = bar.winfo_width()
                fill = int(width * peak)
                bar.create_rectangle(0, 0, fill, 10, fill="#00ff00", tags="peak")

        self.root.after(100, self.update_peak_levels)

if __name__ == "__main__":
    root = tk.Tk()
    VolumeMixerApp(root)
    root.mainloop()
