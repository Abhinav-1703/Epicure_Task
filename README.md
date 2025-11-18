# Epicure_Task — ESP32 ↔ STM32 Bridge + Python Host Tools

**Clean, recovered project** providing a framed UART protocol and full host tooling:

- **ESP32**: MQTT ↔ UART bridge (heartbeat + forwarding)  
- **STM32**: framed UART parser (ACK/pong, LED, motor parsing — motor actuation optional)  
- **Host**: Python Tkinter GUI + serial test helpers

> This README is written to be drop-in-ready. Follow the **Quick Start** to get running fast.

---

## Table of contents

1. [Repository layout](#repository-layout)  
2. [Quick start](#quick-start)  
3. [Dependencies](#dependencies)  
4. [Configuration](#configuration)  
5. [How the framed protocol works](#how-the-framed-protocol-works)  
6. [STM32 — where to put `pins.h` & notes](#stm32---where-to-put-pinsh--notes)  
7. [Porting hints (F303K8 → STM32F407VET6)](#porting-hints-f303k8--stm32f407vet6)  
8. [Common workflows & useful commands](#common-workflows--useful-commands)  
9. [Troubleshooting (quick wins)](#troubleshooting-quick-wins)  
10. [Development checklist & recommendations](#development-checklist--recommendations)  
11. [License](#license)  

---

## Repository layout

```
project-root/
├─ pubs.py                  # quick publisher / test script
├─ stmespcom/               # serial helpers (send_ping.py, read_frame.py)
├─ espclient/               # Python GUI (espclient/gui.py)
├─ esp/                     # ESP32 sketch (esp_bridge.ino)
├─ Core/                    # STM32 CubeIDE project (optional)
│  ├─ Inc/
│  │   └─ pins.h
│  └─ Src/
│      └─ main.c
├─ README.md
├─ requirements.txt
└─ .gitignore
```

> Keep `venv/` out of the repo (add to `.gitignore`).

---

## Quick start

### 1) Create & activate a Python virtual env

**Windows PowerShell**
```powershell
python -m venv venv
venv\Scripts\Activate.ps1
```

**macOS / Linux**
```bash
python3 -m venv venv
source venv/bin/activate
```

### 2) Install Python deps (we use `paho-mqtt < 2`)

```bash
pip install "paho-mqtt<2" pyserial
# or from file:
# pip install -r requirements.txt
```

### 3) Run an MQTT broker (local testing)

```bash
docker run -it -p 1883:1883 eclipse-mosquitto
```

### 4) Configure & run the GUI

* Edit `espclient/gui.py` top constants:

  ```py
  BROKER = "localhost"
  PORT = 1883
  ```
* Run:

  ```bash
  python espclient/gui.py
  ```

### 5) Test serial/frame with helpers

* Send a framed `ping` using `stmespcom/send_ping.py` (or `pubs.py`):

  ```bash
  python stmespcom/send_ping.py --port /dev/ttyUSB0 --payload ping
  ```
* Inspect frames with `stmespcom/read_frame.py`.

(If you want, I can generate these helper scripts next.)

---

## Dependencies

Add to `requirements.txt`:

```
paho-mqtt<2
pyserial>=3.5
```

Install:

```bash
pip install -r requirements.txt
```

> `paho-mqtt<2` is recommended for this repo because the GUI was written expecting older paho semantics (connect_async / loop_start behavior).

---

## How the framed protocol works

**Frame layout**:

```
[0xAA][len:1][payload:len bytes][checksum:1]
```

* `payload` is ASCII text (examples: `ping:123`, `ACK:pong:123`, `LED:OK`, `MOTOR:OK`)
* `checksum` = `sum(payload bytes) & 0xFF`
* Example Python maker:

```py
def make_frame(payload: str) -> bytes:
    p = payload.encode('ascii')
    ln = len(p)
    cs = sum(p) & 0xFF
    return bytes([0xAA, ln]) + p + bytes([cs])
```

**Recommended payload patterns**

* Heartbeat: `ping:<nonce>` → `ACK:pong:<nonce>`
* LED commands: `led:on`, `led:off` → `LED:OK` or `LED:ERR`
* Motor commands: `motor:<steps>:<dir>` → parsing ACK `MOTOR:OK` (actuation optional in firmware)

---

## STM32 — where to put `pins.h` & notes

Place `pins.h` at `Core/Inc/pins.h`. Make business logic reference these macros instead of raw pins so porting is one-file change.

Example `pins.h`:

```c
#ifndef PINS_H
#define PINS_H

#define UART_HANDLE huart2    // CubeMX-generated UART handle
#define LED_PORT GPIOB
#define LED_PIN  GPIO_PIN_3

// Pins for UART TX/RX (adjust for your board)
#define UART_TX_PORT GPIOA
#define UART_TX_PIN  GPIO_PIN_2
#define UART_RX_PORT GPIOA
#define UART_RX_PIN  GPIO_PIN_3

#endif
```

**CubeMX tips**

* Keep `MX_USART2_UART_Init`, `MX_GPIO_Init`, and `SystemClock_Config` from CubeMX.
* If you change the UART peripheral (e.g., to USART1), update `pins.h` and CubeMX settings.

---

## Porting hints: F303K8 → STM32F407VET6

1. Open `.ioc` in CubeIDE and change the MCU to `STM32F407VET6`.
2. Reconfigure clock (F407 commonly uses HSE+PLL).
3. Reassign UART pins (USART2 often PA2/PA3).
4. Regenerate code, merge `pins.h` and your logic files.
5. Re-check timers and DMA mapping (APB clocks and AF mappings differ).

**Pitfalls to check**

* Timer prescalers & clock tree differences
* Alternate function mappings for USART/TIM/DMA
* Peripheral availability & package pins on F407 (VET6 package has many pins)

---

## Common workflows & useful commands

* Create venv & install:

  ```bash
  python3 -m venv venv
  source venv/bin/activate
  pip install "paho-mqtt<2" pyserial
  ```
* Start Mosquitto (Docker):

  ```bash
  docker run -it -p 1883:1883 eclipse-mosquitto
  ```
* Commit & push:

  ```bash
  git add .
  git commit -m "Initial commit"
  git push origin main
  ```

---

## Troubleshooting — quick wins

### GUI shows `STM: UNKNOWN`

* Likely the GUI subscribed **after** ESP published `STM:ONLINE` (non-retained). Fixes:

  * Press GUI **Ping** (sends `ping`) to force a reply.
  * Add an automatic `ping` on GUI connect (implemented in `gui.py` recommended).
  * Best: make ESP publish `STM:ONLINE` with `retained=true` and set a retained LWT `STM:OFFLINE`.

### GUI shows `STM:ONLINE` incorrectly (false positive)

* Use pending-expectation logic: only mark STM online when the reply matches expected tokens (e.g., `ACK:pong`, `LED:OK`, `MOTOR:OK`).

### Checksum/frame mismatches

* Ensure **no** `\r\n` or trailing whitespace in payload (these change checksum).
* Use `read_frame.py` to dump raw bytes and confirm `[0xAA,len,payload,cs]`.

### UART wiring & baud

* Wiring: ESP TX → STM RX, ESP RX ← STM TX, common GND.
* Baud: ensure both sides use the same speed (115200 recommended).

---

## Development checklist & recommendations

Short-term

* [ ] Centralize pin macros in `Core/Inc/pins.h`.
* [ ] Make ESP publish `STM:ONLINE` retained; set LWT `STM:OFFLINE`.
* [ ] Keep GUI pending-response logic for all commands.
* [ ] Add `stmespcom/send_ping.py` and `stmespcom/read_frame.py`.

Long-term

* [ ] Add unit tests for Python logic (mock MQTT).
* [ ] Add CI (GitHub Actions) for tests and linting.
* [ ] Add TLS for MQTT in production deployments.
* [ ] Add wiring diagram and screenshots to `docs/`.

---

## License

This project is recommended to use the **MIT License**. Add a `LICENSE` file with the standard MIT text and your name/year.

---
