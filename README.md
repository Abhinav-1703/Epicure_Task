# Epicure_Task — ESP32 ↔ STM32 Communication + Python Host GUI

This project demonstrates a simple communication pipeline between an **ESP32**, an **STM32**, and a **Python host GUI**.  
The ESP32 acts as a bridge, the STM32 parses commands via a framed UART protocol, and the Python GUI sends commands and displays responses.

This repository contains only the minimal files required to understand and test the flow.

---

## 📦 Repository Layout

```
espcom.ino      → ESP32 MQTT ↔ UART bridge
main.c          → STM32 framed UART parser + basic command handling
pins.h          → Centralized STM32 pin definitions
pubgui.py       → Minimal Python GUI / MQTT command sender
LICENSE         → MIT License
README.md       → This file
```

---

## 🔧 Dependencies

### ESP32
- Arduino + ESP32 Core
- WiFi + PubSubClient (MQTT)

### STM32
- STM32CubeIDE (any board; sample uses USART2)
- HAL UART (interrupt RX)

### Python (`pubgui.py`)
- paho-mqtt<2
- tkinter (usually included)

Install:

```bash
pip install "paho-mqtt<2"
```

---

## 🚀 How to Run

**1) Run MQTT broker (local)**

```bash
docker run -it -p 1883:1883 eclipse-mosquitto
```
Or install Mosquitto locally.

**2) Flash ESP32 (`espcom.ino`)**
- Set WiFi SSID/PASS
- Set broker IP
- Upload to ESP32

**3) Flash STM32**
- Open CubeIDE
- Build & flash `main.c`
- Ensure UART wiring:
  - ESP TX → STM RX
  - ESP RX ← STM TX
  - Common GND

**4) Run Python GUI**

```bash
python pubgui.py
```
Choose LED or motor commands and send them to the ESP → STM.

---

## 📡 Message Format (Framed UART)

Communication between ESP32 ↔ STM32 uses a compact framed protocol:

```
[0xAA][LEN][PAYLOAD ASCII][CHECKSUM]
```
- `0xAA` = frame start
- `LEN` = number of bytes in payload
- `PAYLOAD` = ASCII text (e.g., `ping`, `led:on`, `motor:100:1`)
- `CHECKSUM` = sum(payload bytes) & 0xFF

**Example payloads**

| Command             | Meaning                    |
|---------------------|---------------------------|
| ping                | ESP checks if STM32 is alive |
| led:on / led:off    | Control onboard LED       |
| motor:<steps>:<dir> | Example parse-only command|

**STM32 Responses**

| Response            | Meaning                    |
|---------------------|---------------------------|
| ACK:pong            | Valid reply to ping       |
| LED:OK / LED:ERR    | LED command status        |
| MOTOR:OK            | Parsed motor command      |
| UNKNOWN             | Invalid command           |

---

## 🔄 System Flow Overview

1) **Python → MQTT**  
    The GUI publishes commands to the MQTT topic `epicure/commands`.

2) **ESP32 → STM32**  
    ESP32 receives MQTT commands → wraps them in a UART frame → sends to STM32.

3) **STM32 → ESP32**  
    STM32 parses the frame, validates it, executes the command, and returns a framed response.

4) **ESP32 → MQTT**  
    ESP32 publishes STM32’s responses back to `epicure/status`.

5) **Python GUI**  
    The GUI shows acknowledgements such as:
    - STM:ONLINE
    - ACK:pong
    - LED:OK
    - MOTOR:OK

---

## 🗂 Pin Configuration (STM32)

Pins are centralized in:

`pins.h`

Modify this file when changing boards (e.g., from Nucleo F303K8 → STM32F407VET6).  
No changes needed in `main.c`.

---

## 📜 License

This project is released under the MIT License.

---

## ✔ Purpose of This Repository

This project demonstrates:

- A clean framed UART protocol
- Heartbeat-based STM32 detection
- Reliable ESP32–STM32 communication
- A simple Python MQTT GUI
- Minimal, portable STM32 command parsing

It is intentionally lightweight to allow easy testing and porting.
