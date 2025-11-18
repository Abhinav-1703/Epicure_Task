#!/usr/bin/env python3
"""
Epicure Robotics - Control Station (Complete)
Single-file Tkinter GUI + Paho-MQTT client.

Usage:
    python epicure_gui.py

Requirements:
    pip install paho-mqtt
"""

import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox
import paho.mqtt.client as mqtt
import threading
import time

# --- CONFIGURATION ---
# Put your broker IP here (or 'localhost' if broker runs locally)
BROKER = "xxx.xxx.xxx.xxx"
PORT = 1883

TOPIC_CMD = "epicure/commands"
TOPIC_STATUS = "epicure/status"
TOPIC_LOGS = "epicure/logs"

COLORS = {
    "bg_main": "#1E1E1E",
    "bg_panel": "#2D2D30",
    "fg_text": "#E0E0E0",
    "primary": "#007ACC",
    "primary_fg": "#FFFFFF",
    "danger": "#D32F2F",
    "success": "#4CAF50",
    "warning": "#FFC107",
    "log_bg": "#121212",
    "log_fg": "#00FF00"
}

# --- MQTT CLIENT WRAPPER ---
class MqttClient:
    def __init__(self, broker, port, app):
        self.broker = broker
        self.port = port
        self.app = app  # reference to GUI app for safe UI updates
        # create a unique client id
        self.client = mqtt.Client(client_id="epicure_host_gui_" + str(int(time.time())))
        # bind callbacks to wrapper functions that schedule GUI updates via app.after
        self.client.on_connect = self._on_connect
        self.client.on_disconnect = self._on_disconnect
        self.client.on_message = self._on_message
        self._started = False

    def start(self):
        try:
            # Use connect_async + loop_start for non-blocking behavior
            self.client.connect_async(self.broker, self.port)
            self.client.loop_start()
            self._started = True
            return None
        except Exception as e:
            return str(e)

    def publish(self, message):
        """Publish command to TOPIC_CMD. Returns True on accepted attempt, False otherwise."""
        try:
            if self.client.is_connected():
                # publish with qos=1 for better reliability
                result = self.client.publish(TOPIC_CMD, payload=message, qos=1)
                # result is an MQTTMessageInfo object; rc == 0 indicates queued
                if getattr(result, "rc", 1) == 0:
                    return True
                else:
                    return False
            else:
                return False
        except Exception as e:
            # optionally log
            self.app.safe_log(f"Publish Exception: {e}")
            return False

    def stop(self):
        try:
            if self._started:
                self.client.loop_stop()
            # always try to disconnect
            self.client.disconnect()
        except Exception:
            pass

    # ---------- Internal MQTT callbacks ----------
    def _on_connect(self, client, userdata, flags, rc):
        # schedule on main thread
        self.app.after(0, lambda: self.app._mqtt_on_connect(rc))

    def _on_disconnect(self, client, userdata, rc):
        self.app.after(0, lambda: self.app._mqtt_on_disconnect(rc))

    def _on_message(self, client, userdata, msg):
        # schedule handling on main thread (safe for tkinter)
        topic = msg.topic
        try:
            payload = msg.payload.decode(errors='replace')
        except Exception:
            payload = str(msg.payload)
        self.app.after(0, lambda: self.app._mqtt_on_message(topic, payload))


# --- MAIN GUI APP ---
class EpicureApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Epicure Robotics | Control Station")
        self.geometry("520x700")
        self.configure(bg=COLORS["bg_main"])

        # Styles
        style = ttk.Style(self)
        try:
            style.theme_use('clam')
        except Exception:
            pass
        style.configure("TFrame", background=COLORS["bg_main"])
        style.configure("TLabel", background=COLORS["bg_main"], foreground=COLORS["fg_text"], font=('Segoe UI', 10))
        style.configure("TLabelframe", background=COLORS["bg_panel"], bordercolor=COLORS["primary"])
        style.configure("TLabelframe.Label", background=COLORS["bg_panel"], foreground=COLORS["primary"], font=('Segoe UI', 10, 'bold'))
        style.configure("TButton", font=('Segoe UI', 10, 'bold'))
        style.configure("Danger.TButton", foreground=COLORS["primary_fg"])
        style.map("Danger.TButton", foreground=[('active', COLORS["primary_fg"])])

        self.create_widgets()

        # MQTT client
        self.mqtt = MqttClient(BROKER, PORT, self)

        # show startup log
        self.safe_log(f"System Initialized. Connecting to Broker {BROKER}:{PORT}...")
        err = self.mqtt.start()
        if err:
            self.safe_log(f"MQTT Start Error: {err}")
            self.lbl_broker.config(text=f"Broker: Error", fg=COLORS["danger"])
        # else - wait for on_connect

        # Clean shutdown
        self.protocol("WM_DELETE_WINDOW", self.on_close)

    def create_widgets(self):
        # Header panel
        header_frame = tk.Frame(self, bg=COLORS["bg_panel"], height=56)
        header_frame.pack(fill='x', padx=10, pady=10)

        self.lbl_broker = tk.Label(header_frame, text="Broker: Connecting...", bg=COLORS["bg_panel"], fg=COLORS["warning"], font=('Segoe UI', 10, 'bold'))
        self.lbl_broker.pack(side='left', padx=12, pady=12)

        self.lbl_stm = tk.Label(header_frame, text="STM32: UNKNOWN", bg=COLORS["bg_panel"], fg=COLORS["danger"], font=('Segoe UI', 10, 'bold'))
        self.lbl_stm.pack(side='right', padx=12, pady=12)

        # Spacer
        ttk.Frame(self, height=8).pack()

        # LED Control
        led_frame = ttk.LabelFrame(self, text=" LED Control ", padding=12)
        led_frame.pack(fill='x', padx=14, pady=6)

        frm_led_buttons = tk.Frame(led_frame, bg=COLORS["bg_panel"])
        frm_led_buttons.pack(fill='x')
        ttk.Button(frm_led_buttons, text="ON", command=lambda: self.send_cmd("led:on")).pack(side='left', fill='x', expand=True, padx=4)
        ttk.Button(frm_led_buttons, text="OFF", command=lambda: self.send_cmd("led:off")).pack(side='left', fill='x', expand=True, padx=4)

        # Motor Control
        motor_frame = ttk.LabelFrame(self, text=" Motor Control ", padding=12)
        motor_frame.pack(fill='x', padx=14, pady=8)

        tk.Label(motor_frame, text="Target Steps:", bg=COLORS["bg_panel"], fg="#AAA").pack(anchor='w')
        self.ent_steps = ttk.Entry(motor_frame)
        self.ent_steps.insert(0, "100")
        self.ent_steps.pack(fill='x', pady=(2, 8))

        self.dir_var = tk.StringVar(value="1")
        frm_radio = tk.Frame(motor_frame, bg=COLORS["bg_panel"])
        frm_radio.pack(fill='x', pady=(0, 6))
        ttk.Radiobutton(frm_radio, text="Clockwise", variable=self.dir_var, value="1").pack(side='left', padx=4)
        ttk.Radiobutton(frm_radio, text="Counter-CW", variable=self.dir_var, value="0").pack(side='left', padx=20)

        ttk.Button(motor_frame, text="GO", command=self.send_motor).pack(fill='x')

        # Telemetry / Logs
        log_frame = ttk.LabelFrame(self, text=" Telemetry ", padding=10)
        log_frame.pack(fill='both', expand=True, padx=14, pady=14)

        self.log_box = scrolledtext.ScrolledText(log_frame, height=18, state='disabled', bg=COLORS["log_bg"], fg=COLORS["log_fg"], font=('Consolas', 10))
        self.log_box.pack(fill='both', expand=True)

        # Small footer controls
        footer = tk.Frame(self, bg=COLORS["bg_main"])
        footer.pack(fill='x', padx=14, pady=(0,14))
        ttk.Button(footer, text="Clear Logs", command=self.clear_logs).pack(side='left')
        ttk.Button(footer, text="Ping STM", command=lambda: self.send_cmd("ping")).pack(side='right')

    # --- Safe UI helpers (all safe to call from any thread) ---
    def safe_log(self, msg):
        """Thread-safe way to append a log line to the log_box."""
        def _append():
            self.log_box.config(state='normal')
            ts = time.strftime("%H:%M:%S")
            self.log_box.insert('end', f"[{ts}] {msg}\n")
            self.log_box.see('end')
            self.log_box.config(state='disabled')
        # schedule on mainloop
        self.after(0, _append)

    def clear_logs(self):
        self.log_box.config(state='normal')
        self.log_box.delete('1.0', 'end')
        self.log_box.config(state='disabled')

    # --- Publish / Commands ---
    def send_cmd(self, cmd):
        self.safe_log(f"TX: {cmd}")
        ok = self.mqtt.publish(cmd)
        if not ok:
            self.safe_log("Error: MQTT Not Connected (publish failed)")

    def send_motor(self):
        s = self.ent_steps.get().strip()
        d = self.dir_var.get()
        if s.isdigit() and int(s) >= 0:
            self.send_cmd(f"motor:{s}:{d}")
        else:
            self.safe_log("Error: Invalid Steps (must be non-negative integer)")

    # --- MQTT callbacks scheduled on main thread ---
    def _mqtt_on_connect(self, rc):
        if rc == 0:
            self.lbl_broker.config(text="Broker: Connected", fg=COLORS["success"])
            self.safe_log("MQTT Connected. Subscribing to topics...")
            # subscribe to status and logs
            try:
                self.mqtt.client.subscribe(TOPIC_STATUS)
                self.mqtt.client.subscribe(TOPIC_LOGS)
            except Exception as e:
                self.safe_log(f"Subscribe error: {e}")
        else:
            self.lbl_broker.config(text=f"Broker: Failed (rc={rc})", fg=COLORS["danger"])
            self.safe_log(f"MQTT Connection failed with rc={rc}")

    def _mqtt_on_disconnect(self, rc):
        self.lbl_broker.config(text="Broker: Disconnected", fg=COLORS["danger"])
        self.lbl_stm.config(text="STM32: UNKNOWN", fg=COLORS["danger"])
        self.safe_log(f"MQTT Disconnected (rc={rc})")

    def _mqtt_on_message(self, topic, payload):
        # topic and payload are simple strings; decide action
        self.safe_log(f"RX [{topic}]: {payload}")
        if topic == TOPIC_STATUS:
            # look for STM status tokens
            if "STM:ONLINE" in payload or "STM:ONLINE" == payload:
                self.lbl_stm.config(text="STM32: ONLINE", fg=COLORS["success"])
                self.safe_log("System Alert: STM32 Bridge Active")
            elif "STM:OFFLINE" in payload or "STM:OFFLINE" == payload:
                self.lbl_stm.config(text="STM32: OFFLINE", fg=COLORS["danger"])
                self.safe_log("System Alert: STM32 Bridge Lost")
            else:
                # other status messages (e.g., LED:OK, MOTOR:OK)
                # keep STM label unchanged but log the message
                pass
        elif topic == TOPIC_LOGS:
            # display logs from STM forwarded via this topic
            # payload may already include useful text
            self.safe_log(f"STM_LOG: {payload}")

    # Clean shutdown
    def on_close(self):
        if messagebox.askokcancel("Quit", "Exit Epicure Control Station?"):
            try:
                self.mqtt.stop()
            except Exception:
                pass
            self.destroy()


if __name__ == "__main__":
    app = EpicureApp()
    app.mainloop()
