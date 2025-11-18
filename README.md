# Epicure_Task – ESP32 ↔ STM32 ↔ Python GUI Communication Demo

This project demonstrates a minimal communication pipeline between:
- **ESP32-WROOM** (MQTT to UART bridge)
- **STM32 Nucleo-F303K8** (framed UART command parser)
- **Python host GUI** (simple MQTT command sender and response viewer)

The goal is to show a reliable message flow across all three systems using a small readable codebase.

---

## Repository Contents

| File         | Description                                                          |
| ------------ | -------------------------------------------------------------------- |
| `espcom.ino` | ESP32 firmware (MQTT bridge + heartbeat + UART framing)              |
| `main.c`     | STM32 firmware (parsing framed UART messages and returning ACKs)     |
| `pins.h`     | STM32 pin configuration (easy migration across boards)               |
| `pubgui.py`  | Python script with GUI for sending commands through MQTT              |
| `LICENSE`    | MIT License                                                          |
| `README.md`  | Project overview (this file)                                         |

---

## Hardware Used

- **ESP32-WROOM DevKit**
- **STM32 Nucleo-F303K8**
- *(Optional)* On-board LED for testing command execution



---

## Dependencies

**ESP32:**
- WiFi library
- PubSubClient (MQTT)

**STM32:**
- STM32CubeIDE
- HAL UART (interrupt mode enabled)

**Python:**
- `paho-mqtt` < 2
- `tkinter` (usually included)

Install Python requirements:
```bash
pip install "paho-mqtt<2"
```

---

## How to Run the System

1. **Start an MQTT broker**
2. **Flash the ESP32**
   - Open `espcom.ino`, set WiFi credentials and broker IP, then upload.
   - ESP32 connects to WiFi, then to MQTT, and waits for commands.

3. **Flash the STM32**
   - Open project in CubeIDE.
   - Make sure `pins.h` matches your UART pins (typically PA2 TX, PA3 RX for Nucleo F303K8).
   - Flash the board.
     
4. **Run the Python GUI**
   ```bash
   python pubgui.py
   ```
   - Send LED and other test commands from your PC.

---

## Message Format

ESP32 and STM32 communicate over UART using a simple framed protocol:

```text
[0xAA][LEN][ASCII_PAYLOAD][CHECKSUM]
```

- `0xAA` – Start of frame
- `LEN` – Payload length in bytes
- `PAYLOAD` – ASCII text (examples: `ping`, `led:on`, `led:off`, `motor:120:1`)
- `CHECKSUM` – Sum of payload bytes (modulo 256)

**Examples:**
- python → MQTT → ESP:  
  `led:on`
- ESP → STM32 (UART frame):  
  `0xAA 05 6C65643A6F6E CS`
- STM32 → ESP (response):  
  `ACK:pong`, `LED:OK`, `MOTOR:OK`, `UNKNOWN`
- ESP → MQTT → Python:  
  `epicure/status` : `"LED:OK"`

---

## Demo Screenshots

Below are images showing the Python GUI interface and the serial monitor output for command exchanges:

### 1. Python GUI Interface

![Python GUI showing status, controls, and telemetry log](images/python_gui.png)

### 2. Serial Monitor Output (UART traffic)

![Serial monitor log showing command exchange](images/serial_monitor.png)

---

## System Flow

1. Python GUI publishes commands on MQTT topic **`epicure/commands`**.
2. ESP32 receives the MQTT command, wraps it in a UART frame, sends to STM32.
3. STM32 parses the frame, executes/interprets the command, and returns a framed response.
4. ESP32 publishes the response on **`epicure/status`**.
5. Python GUI displays the status and acknowledgement.

---

## STM32 Pin Configuration (`pins.h`)

All pin and peripheral configuration is centralized for easy porting.

**Example:**
```c
#define UART_HANDLE huart2
#define LED_PORT GPIOB
#define LED_PIN  GPIO_PIN_3

#define UART_TX_PORT GPIOA
#define UART_TX_PIN  GPIO_PIN_2
#define UART_RX_PORT GPIOA
#define UART_RX_PIN  GPIO_PIN_3
```
When switching boards (e.g. to STM32F407VET6), update only this file.

---

## Troubleshooting Notes

- **If the GUI shows STM:UNKNOWN**  
  Probably subscribed after ESP already published status. Press the Ping button or restart GUI.

- **If there is no UART response**  
  Double-check ESP TX2 → STM RX, ESP RX2 ← STM TX, and common ground.  
  Confirm baud rate is the same (115200 recommended).

- **If MQTT shows nothing**  
  Ensure ESP32 is on same network as broker.  
  Use:
  ```bash
  mosquitto_sub -t "#"
  ```
  to observe MQTT traffic.

---

## License

This project is released under the **MIT License**.

---
