# Black Void — ESP32 Wireless Reconnaissance Platform

> A portable, handheld wireless research platform built on the ESP32, with a 128×64 OLED display, push-button navigation, an NRF24L01+ radio, and SD card logging.

---

## Features

| Module              | Capability                                                          |
| ------------------- | -------------------------------------------------------------------- |
| **Wi-Fi Scanner**    | Scans nearby access points; logs SSID, BSSID, channel, RSSI, encryption |
| **BLE Scanner**      | Discovers advertising BLE devices and nearby beacons                 |
| **RF Monitor**       | Passive 2.4 GHz activity observation via NRF24L01+                    |
| **MAC Randomization**| Rotates the device's own Wi-Fi MAC to reduce persistent fingerprints  |
| **SD Card Logging**  | Writes scan sessions to onboard storage for later review              |
| **Spectrum Visualization** | On-screen representation of nearby RF activity                 |
| **Status Screen**    | Live radio state, storage usage, and device health                    |

---

## Hardware

### Bill of Materials

| Component           | Notes                       |
| -------------------- | ---------------------------- |
| ESP32 dev board       | Any standard 38-pin module   |
| SSD1306 OLED 128×64   | I²C, connected on pins 21/22 |
| NRF24L01+ module      | SPI, shares bus with SD card |
| MicroSD card module   | Shares SPI bus with NRF24    |
| Push buttons (x4)     | UP / DOWN / SELECT / BACK    |
| MicroSD card          | FAT32 formatted              |

### Pin Map

```
OLED SDA   → GPIO 21
OLED SCL   → GPIO 22

SD  CS     → GPIO 15
SD  SCK    → GPIO 18
SD  MISO   → GPIO 19
SD  MOSI   → GPIO 23

NRF24 CE   → GPIO 4
NRF24 CSN  → GPIO 5

BTN UP     → GPIO 32
BTN DOWN   → GPIO 33
BTN SELECT → GPIO 25
BTN BACK   → GPIO 26
```

> **Note:** The NRF24L01+ and SD card share the same SPI bus. The SD chip-select is held HIGH during NRF24 operations to prevent bus contention.

---

## Software Dependencies

Install via the Arduino Library Manager or PlatformIO:

```
U8g2           — OLED display driver
NimBLE-Arduino — Lightweight BLE stack
RF24           — NRF24L01+ driver
SD (built-in)  — SD card filesystem
FS (built-in)  — ESP32 filesystem abstraction
WiFi (built-in)— ESP32 Wi-Fi stack
```

---

## Building & Flashing

1. Open the project in Arduino IDE or PlatformIO.
2. Select your ESP32 board and serial port.
3. Install all dependencies listed above.
4. Flash at 115200 baud.

---

## SD Card Structure

The firmware creates the following directory tree on first boot:

```
/blackvoid/
├── wifi/     — Wi-Fi scan results
├── ble/      — BLE scan results
├── rf/       — RF monitor logs
└── system/   — Device and session metadata
```

Log files are timestamped using device uptime (`HHhMMmSSs` format).

---

## Navigation

```
[ UP ]    / [ DOWN ]  — scroll menu or results
[ SELECT ]            — confirm / enter submenu / start action
[ BACK ]              — return to previous menu / stop active scan
```

During an active scan, press `BACK` to stop and display results.

---

## Menu Structure

```
Main Menu
├── 1. Wi-Fi Scan
│   ├── Scan Networks
│   └── Save Last Scan
├── 2. BLE Scan
│   ├── Scan Devices
│   └── Save Scan
├── 3. RF Monitor
│   ├── Spectrum View
│   └── Save Log
├── 4. Device Info
│   ├── MAC Status
│   └── Radio Status
├── 5. SD Card
│   ├── Browse Files
│   ├── Card Info
│   └── Delete File
└── 6. Status
```

---

## Boot Sequence

On power-up, the device initializes peripherals in order:

1. OLED display
2. NRF24L01+ radio
3. SD card + directory tree
4. MAC randomization
5. Wi-Fi (enabled only when a scan is requested)

Radios are kept off when idle. This reduces unnecessary broadcasts and
extends battery life — it does not make the device undetectable. Any
active transmission, from Black Void or any other device, remains
observable to anyone listening on the same spectrum.

---

## Design Philosophy

Black Void is built around a few fixed constraints:

- **No cloud dependency.** Everything runs on-device. Nothing requires
  a server, an account, or a subscription.
- **No proprietary ecosystem.** Firmware and hardware are fully open.
- **Reduced fingerprint, not anonymity.** MAC randomization and radio
  power management lower how easily the device can be tracked across
  sessions. They do not hide the fact that it is transmitting.
- **Affordable by default.** Every part on the bill of materials is
  common and inexpensive. The capability comes from the firmware.

---

## Legal Notice

This project is intended for **authorized security research, educational use, and testing on networks and devices you own or have explicit permission to test.**

Unauthorized interception of wireless communications or unauthorized access to computer networks may violate local laws, including (but not limited to) the Computer Fraud and Abuse Act (US), the Computer Misuse Act (UK), and equivalent legislation in other jurisdictions.

Use responsibly. The authors assume no liability for misuse.

---

## License

MIT — see `LICENSE` for details.
