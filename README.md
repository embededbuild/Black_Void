# Black Void — ESP32 Wireless Reconnaissance Platform

> A portable, handheld wireless research platform built on the ESP32, with a 128×64 OLED display, push-button navigation, an NRF24L01+ radio, and SD card logging.

---

## Features

| Module              | Capability                                                              |
| -------------------- | -------------------------------------------------------------------------- |
| **Wi-Fi Scanner**    | Scans nearby access points; logs SSID, BSSID, channel, RSSI, and encryption |
| **Wi-Fi Connect**    | On-device password entry and association to a chosen network               |
| **LAN Device Discovery** | Enumerates hosts on the connected subnet via ARP and probes common TCP ports |
| **BLE Scanner**      | Discovers advertising BLE devices; flags known tracker beacon signatures (Apple, Tile, Samsung) |
| **RF Spectrum Analyzer** | Sweeps all 125 NRF24 channels and reports packet activity per channel   |
| **RF Packet Capture**| Records raw 2.4 GHz packets across the NRF24 channel range                 |
| **RF Packet Replay** | Re-transmits a captured packet set back out on its original channels       |
| **Probe Request Sniffer** | Passively captures 802.11 probe requests (client MAC + requested SSID) while channel-hopping |
| **Device Profiles**  | Generates and stores locally-administered MAC/IP identity profiles to SD   |
| **MAC Randomization**| Rotates the device's own Wi-Fi MAC address on boot and before scans/connections |
| **SD Card Logging & Browser** | Persists all scan/capture output; on-device file browser, card info, delete |
| **Status Screen**    | Live radio state, capture counts, and free heap                            |

---

## Hardware

### Bill of Materials

| Component           | Notes                       |
| -------------------- | ---------------------------- |
| ESP32 dev board       | Any standard 38-pin module   |
| SSD1306 OLED 128×64   | I²C, hardware I2C bus        |
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

> **Note:** The NRF24L01+ and SD card share the same SPI bus (VSPI). Button pin modes are re-asserted after any WiFi/esp_wifi call, since the radio driver transiently releases the GPIO mux on those pins.

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
lwIP (built-in)— ARP table access for LAN device discovery
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
/tank/
├── wifi/    — Wi-Fi scans and discovered LAN devices
├── ble/     — BLE scan results
├── rf24/    — RF24 spectrum, capture, and replay logs
├── probes/  — Captured 802.11 probe requests
└── spoof/   — Stored MAC/IP identity profiles
```

> The `/tank/` path and internal BLE device name (`"Tank"`) are carried over from
> an earlier codebase and haven't been renamed in this firmware yet. Update
> `initSD()` and the `NimBLEDevice::init(...)` call if you want on-disk and
> over-the-air naming to match Black Void.

Log files are timestamped using device uptime (`HHhMMmSSs` format).

---

## Navigation

```
[ UP ]    / [ DOWN ]  — scroll menu or results
[ SELECT ]            — confirm / enter submenu / save result / start action
[ BACK ]              — return to previous menu / stop active scan
```

On the password-entry screen: `UP`/`DOWN` cycle the character wheel, a short
`SELECT` press appends the highlighted character, a long `SELECT` press (~600ms)
connects, a short `BACK` press deletes a character, and a long `BACK` press
cancels back to the network list.

---

## Menu Structure

```
Main Menu
├── 1. WiFi Scan
│   ├── Scan Networks       → select network → connect (or enter password)
│   ├── Device Discovery    → ARP + port scan of connected subnet
│   ├── Save Devices
│   └── Disconnect
├── 2. BLE Scan
│   ├── Scan Devices
│   ├── Find Trackers
│   └── Save Scan
├── 3. RF24 Tools
│   ├── Spectrum Analyze
│   ├── Capture Packets
│   ├── Replay Packets
│   └── Save Capture
├── 4. Probe Sniff
│   ├── Start Sniffing
│   ├── Stop Sniffing
│   └── Save Probes
├── 5. Device Spoof
│   ├── Create Profile
│   ├── Activate Profile
│   └── Delete Profile
├── 6. SD Card
│   ├── Browse Files
│   ├── Card Info
│   └── Delete File
└── 7. Status
```

---

## Boot Sequence

On power-up, the device initializes peripherals in order, showing progress on the OLED:

1. BLE stack
2. NRF24L01+ radio
3. SD card + directory tree
4. Wi-Fi MAC randomization (radio is left off afterward)
5. Stored device-spoof profiles, loaded from SD

Wi-Fi is only brought up when a scan, connect, or discovery action is
requested. Radios kept off when idle reduce unnecessary broadcasts and extend
battery life — this does not make the device undetectable. Any active
transmission, from Black Void or any other device, remains observable to
anyone listening on the same spectrum.

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

Several features on this device — including LAN device/port discovery, RF packet capture and replay, and 802.11 probe request sniffing — can be misused against networks or devices you do not own or lack permission to test. Depending on your jurisdiction, unauthorized use of these features may violate laws such as the Computer Fraud and Abuse Act (US), the Computer Misuse Act (UK), or equivalent local legislation, and may also implicate wireless-interception or radio-equipment regulations.

Use responsibly. The authors assume no liability for misuse.

---

## License

MIT — see `LICENSE` for details.
