## Epicure_Task – ESP32 ↔ STM32 ↔ Python GUI Communication Demo

This project demonstrates a minimal communication pipeline between:

* ESP32-WROOM (MQTT to UART bridge)
* STM32 Nucleo-F303K8 (framed UART command parser)
* Python host GUI (simple MQTT command sender and response viewer)

The goal is to show a reliable message flow across all three systems using a small readable codebase.

---

## Repository Contents

espcom.ino      – ESP32 firmware (MQTT bridge + heartbeat + UART framing)
main.c          – STM32 firmware (parsing framed UART messages and returning acknowledgements)
pins.h          – STM32 pin configuration (one place to change when using a different board)
pubgui.py       – Python script with a small GUI for sending commands through MQTT
LICENSE         – MIT License
README.md       – This file

---

## Hardware Used

ESP32-WROOM DevKit
STM32 Nucleo-F303K8
(Optional) On-board LED for testing command execution

If you want to include images in this README:

* Create an 'assets' folder in the repository
* Add your images there (example: assets/esp32.jpg, assets/f303k8.jpg)
* Then link them using:  ![Description](assets/filename.jpg)

---

## Dependencies

ESP32:
WiFi library
PubSubClient (MQTT)

STM32:
STM32CubeIDE
HAL UART with interrupt mode enabled

Python:
paho-mqtt < 2
tkinter (usually included)

Install Python requirements:
pip install "paho-mqtt<2"

---

## How to Run the System

1. Start an MQTT broker
   Easiest method using Docker:
   docker run -it -p 1883:1883 eclipse-mosquitto

2. Flash the ESP32
   Open espcom.ino, set WiFi credentials and broker IP, then upload.
   ESP32 connects to WiFi, then to MQTT, and waits for commands.

3. Flash the STM32
   Open the project in CubeIDE.
   Make sure pins.h matches your UART pins (typically PA2 TX, PA3 RX for Nucleo F303K8).
   Flash the board.

4. Run the Python GUI
   python pubgui.py
   This will let you send LED and other test commands from your PC.

---

## Message Format

The ESP32 and STM32 communicate over UART using a simple framed protocol:

```
[0xAA][LEN][ASCII_PAYLOAD][CHECKSUM]
```

0xAA       – Start of frame
LEN        – Payload length in bytes
PAYLOAD    – ASCII text such as:
ping
led:on
led:off
motor:120:1
CHECKSUM   – Sum of payload bytes (modulo 256)

Examples:

python → MQTT → ESP
led:on

ESP → STM32 (UART frame)
0xAA 05 6C65643A6F6E CS

STM32 → ESP (response)
ACK:pong
LED:OK
MOTOR:OK
UNKNOWN

ESP → MQTT → Python
epicure/status : "LED:OK"

---

## System Flow

Python GUI publishes commands on MQTT topic "epicure/commands".
ESP32 receives the MQTT command, checks it, wraps it in a UART frame, and sends it to the STM32.
STM32 parses the frame, executes or interprets the command, and returns a framed response.
ESP32 publishes this response to the topic "epicure/status".
The Python GUI displays the status and parsed acknowledgement.

---

## STM32 Pin Configuration (pins.h)

All pin and peripheral configuration is centralized here so the code can be ported easily.

Example:

#define UART_HANDLE huart2
#define LED_PORT GPIOB
#define LED_PIN  GPIO_PIN_3

#define UART_TX_PORT GPIOA
#define UART_TX_PIN  GPIO_PIN_2
#define UART_RX_PORT GPIOA
#define UART_RX_PIN  GPIO_PIN_3

When switching boards (for example to STM32F407VET6), update only this file.

---

## Troubleshooting Notes

If the GUI shows STM:UNKNOWN
Probably subscribed after the ESP already published status.
Press the Ping button or restart the GUI.

If there is no UART response
Check ESP TX → STM RX, ESP RX ← STM TX, and a common ground.
Verify both sides use the same baud rate (115200 recommended).

If MQTT shows nothing
Ensure ESP32 is connected to the same network as the broker.
Use:   mosquitto_sub -t "#"   to observe traffic.

---

## License

This project is released under the MIT License.

---
