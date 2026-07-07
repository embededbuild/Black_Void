#include <Wire.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <RF24.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <esp_wifi.h>
#include <lwip/etharp.h>
#include <lwip/netif.h>
#include <lwip/tcpip.h>

// ================================================================
// HARDWARE PINS
// ================================================================
#define SD_CS    15
#define SD_SCK   18
#define SD_MISO  19
#define SD_MOSI  23
#define NRF24_CE 4
#define NRF24_CSN 5
#define OLED_SDA 21
#define OLED_SCL 22
#define BTN_UP   32
#define BTN_DOWN 33
#define BTN_SELECT 25
#define BTN_BACK 26

// ================================================================
// GLOBALS
// ================================================================
char currentRandomMAC[18] = "";

unsigned long nrMarqueeLastStep = 0;
int nrMarqueeOffset = 0;
#define MARQUEE_STEP_MS 300 //how fast it be scrollin
#define MARQUEE_MAX_CHARS 19 // how many chars fit on screen at once

bool sdReady = false;
bool nrfReady = false;
bool bleReady = false;
bool wifiConnected = false;
bool probeSniffing = false;
unsigned long lastBtnTime = 0;
#define DEBOUNCE_MS 150

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);
SPIClass vspi(VSPI);
RF24 radio(NRF24_CE, NRF24_CSN);
NimBLEScan* pBLEScan = nullptr;

// ================================================================
// RF24 CAPTURE
// ================================================================
#define MAX_PACKETS 200
#define NUM_CHANNELS 125

struct RFPacket {
  uint8_t data[32];
  uint8_t len;
  uint8_t channel;
  uint32_t time;
};

RFPacket rfPackets[MAX_PACKETS];
int rfPacketCount = 0;
int channelActivity[NUM_CHANNELS];
int totalPackets = 0;
int activeChannels = 0;

// ================================================================
// BLE DEVICES
// ================================================================
#define MAX_BLE_DEVICES 30
struct BLEDeviceInfo {
  String addr;
  String name;
  int rssi;
  bool isTracker;
  String trackerType;
};
BLEDeviceInfo bleDevices[MAX_BLE_DEVICES];
int bleDeviceCount = 0;

// ================================================================
// WIFI NETWORKS
// ================================================================
#define MAX_WIFI_NETWORKS 50
struct WiFiNetwork {
  String ssid;
  String bssid;
  int rssi;
  int channel;
  bool open;
  uint8_t encryption;
  String secLabel;
};
WiFiNetwork wifiNetworks[MAX_WIFI_NETWORKS];
int wifiNetworkCount = 0;

// ================================================================
// WIFI CONNECT + DEVICE DISCOVERY  (Corntenna functionality)
// ================================================================
int nrNetIndex = 0;     // highlighted network while scrolling scan list
int nrNetScroll = 0;
String nrChosenSSID = "";
bool nrChosenOpen = false;

// --- password entry ---
#define PW_MAX_LEN 63
char pwBuffer[PW_MAX_LEN + 1] = "";
int pwLen = 0;
int pwCharIdx = 0;   // index into CHARSET for the currently highlighted char

const char* CHARSET =
  " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
  "!@#$%^&*()-_+=.,?/:;<>|\{}~[]";
const int CHARSET_LEN = strlen(CHARSET);

unsigned long selectHeldSince = 0;
unsigned long backHeldSince   = 0;
bool selectLongFired = false;
bool backLongFired   = false;
#define LONG_PRESS_MS 600

// --- discovered devices ---
#define MAX_NR_DEVICES 64
struct NRDevice {
  IPAddress ip;
  char mac[18];
  char ports[32];
  char vendor[16];
};
NRDevice nrDevices[MAX_NR_DEVICES];
int nrDeviceCount = 0;
int nrDevScroll = 0;

const uint32_t nrScanPorts[] = {21, 22, 23, 80, 443, 445, 554, 8080, 8443, 9100};
const int nrNumScanPorts = 10;

struct OUIEntry {
  uint8_t oui[3];
  const char* vendor;
  const char* type;
};

const OUIEntry ouiTable[] = {
  //apple
  {{0xB8, 0x3E, 0x59}, "Apple",      "Apple Device"},
  {{0x40, 0xB0, 0x76}, "Apple",      "Apple Device"},
  {{0xA4, 0xC3, 0xF0}, "Apple",      "Apple Device"},
  {{0x8C, 0x85, 0x90}, "Apple",      "Apple Device"},
  {{0xF0, 0x18, 0x98}, "Apple",      "Apple Device"},
  {{0x00, 0x17, 0xF2}, "Apple",      "Apple Device"},
  {{0xAC, 0xDE, 0x48}, "Apple",      "Apple Device"},
  {{0x54, 0x26, 0x96}, "Apple",      "Apple Device"},
  //samsung
  {{0xD4, 0xE2, 0x2F}, "Samsung",    "Samsung Device"},
  {{0x8C, 0x77, 0x12}, "Samsung",    "Samsung Device"},
  {{0xA0, 0x07, 0x98}, "Samsung",    "Samsung Device"},
  {{0x30, 0x07, 0x4D}, "Samsung",    "Samsung Device"},
  {{0xCC, 0x07, 0xAB}, "Samsung",    "Samsung Device"},
  //TP-Link
  {{0xF0, 0x09, 0x0D}, "TP-Link",    "Router"},
  {{0x50, 0xC7, 0xBF}, "TP-Link",    "Router"},
  {{0xC4, 0xE9, 0x84}, "TP-Link",    "Router"},
  {{0x98, 0xDA, 0xC4}, "TP-Link",    "Router"},
  //Netgear
  {{0xA0, 0x04, 0x60}, "Netgear",    "Router"},
  {{0x6C, 0x40, 0x08}, "Netgear",    "Router"},
  {{0x9C, 0x3D, 0xCF}, "Netgear",    "Router"},
  //Cisco
  {{0x00, 0x1A, 0xA1}, "Cisco",      "Switch/Router"},
  {{0x00, 0x09, 0x97}, "Cisco",      "Switch/Router"},
  {{0xF8, 0x7B, 0x20}, "Cisco",      "Switch/Router"},
  //google
  {{0xF4, 0xF5, 0xDB}, "Google",     "Google Device"},
  {{0x3C, 0x28, 0x6D}, "Google",     "Google Device"},
  {{0xA4, 0x77, 0x33}, "Google",     "Chromecast"},
  //amazon
  {{0xFC, 0x65, 0xDE}, "Amazon",     "Echo/FireTV"},
  {{0x44, 0x65, 0x0D}, "Amazon",     "Echo/FireTV"},
  {{0xA0, 0x02, 0xDC}, "Amazon",     "Echo/FireTV"},
  //Raspberry Pi
  {{0xB8, 0x27, 0xEB}, "RPi Found.", "Raspberry Pi"},
  {{0xDC, 0xA6, 0x32}, "RPi Found.", "Raspberry Pi"},
  {{0xE4, 0x5F, 0x01}, "RPi Found.", "Raspberry Pi"},
  // Espressif (ESP32/ESP8266)
  {{0x24, 0x6F, 0x28}, "Espressif",  "ESP Device"},
  {{0x30, 0xAE, 0xA4}, "Espressif",  "ESP Device"},
  {{0xA0, 0x20, 0xA6}, "Espressif",  "ESP Device"},
  // Nvidia (Shield)
  {{0x00, 0x04, 0x4B}, "Nvidia",     "Shield/GPU"},
  // Sony
  {{0x28, 0xFD, 0xEB}, "Sony",       "Sony Device"},
  {{0xAC, 0x9B, 0x0A}, "Sony",       "PlayStation"},
  {{0x70, 0x9E, 0x29}, "Sony",       "PlayStation"},
  // Microsoft
  {{0x00, 0x50, 0xF2}, "Microsoft",  "Windows PC"},
  {{0x28, 0x18, 0x78}, "Microsoft",  "Xbox"},
  {{0x98, 0x5F, 0xD3}, "Microsoft",  "Xbox"},
  // Ubiquiti
  {{0x24, 0xA4, 0x3C}, "Ubiquiti",   "AP/Router"},
  {{0x78, 0x8A, 0x20}, "Ubiquiti",   "AP/Router"},
  // Synology
  {{0x00, 0x11, 0x32}, "Synology",   "NAS"},
  // Canon
  {{0x00, 0x1E, 0x8F}, "Canon",      "Printer"},
  {{0xA4, 0x83, 0xE7}, "Canon",      "Printer"},
  // HP
  {{0x3C, 0xD9, 0x2B}, "HP",         "Printer/PC"},
  {{0xA0, 0xB3, 0xCC}, "HP",         "Printer/PC"},
};
const int ouiTableSize = sizeof(ouiTable) / sizeof(ouiTable[0]);

// ================================================================
// PROBE REQUESTS
// ================================================================
#define MAX_PROBE_REQUESTS 300
struct ProbeRequest {
  String mac;
  String ssid;
  int rssi;
  int channel;
  uint32_t time;
};
ProbeRequest probeRequests[MAX_PROBE_REQUESTS];
int probeRequestCount = 0;

// ================================================================
// DEVICE SPOOFING
// ================================================================
#define MAX_SPOOF_PROFILES 20
struct SpoofProfile {
  String name;
  String macAddress;
  String ipAddress;
  bool active;
};
SpoofProfile spoofProfiles[MAX_SPOOF_PROFILES];
int spoofProfileCount = 0;
int spoofSelectedIndex = 0;

// ================================================================
// SD FILE BROWSER
// ================================================================
#define MAX_SD_FILES 32
String sdFiles[MAX_SD_FILES];
int sdFileCount = 0;
int sdFileSel = 0;
String selectedFile = "";

// ================================================================
// MENU SYSTEM
// ================================================================
enum MenuLevel {
  MENU_MAIN = 0,
  MENU_WIFI = 1,
  MENU_BLE_SCAN = 2,
  MENU_RF24 = 3,
  MENU_PROBE = 4,
  MENU_SPOOF = 5,
  MENU_SD = 6,
  MENU_RESULT = 7,
  MENU_SD_BROWSE = 8,
  MENU_SPOOF_LIST = 9,
  MENU_WIFI_SCANLIST,   // list of scanned SSIDs, pick one to connect
  MENU_WIFI_PWENTRY,    // scroll-wheel password entry screen
  MENU_WIFI_DEVLIST     // discovered devices on the connected network
};

MenuLevel currentMenu = MENU_MAIN;
int menuIndex = 0;
int menuScroll = 0;
String resultTitle = "";

// ================================================================
// RESULT CONTEXT  <-- NEW: tracks what scan populated the result
// ================================================================
MenuLevel resultOriginMenu = MENU_MAIN;

enum ResultContext { CTX_NONE, CTX_BLE, CTX_RF24, CTX_PROBE };
ResultContext resultContext = CTX_NONE;

// Result storage
#define MAX_RESULT_LINES 64
String resultLines[MAX_RESULT_LINES];
int resultLineCount = 0;

// Menu items
const char* mainItems[] = {
  "1. WiFi Scan",
  "2. BLE Scan",
  "3. RF24 Tools",
  "4. Probe Sniff",
  "5. Device Spoof",
  "6. SD Card",
  "7. Status"
};
const int mainCount = 7;

const char* wifiItems[] = {
  "Scan Networks",
  "Device Discovery",
  "Save Devices",
  "Disconnect",
  "< Back"
};
const int wifiCount = 5;

const char* bleItems[] = {
  "Scan Devices",
  "Find Trackers",
  "Save Scan",
  "< Back"
};
const int bleCount = 4;

const char* rf24Items[] = {
  "Spectrum Analyze",
  "Capture Packets",
  "Replay Packets",
  "Save Capture",
  "< Back"
};
const int rf24Count = 5;

const char* probeItems[] = {
  "Start Sniffing",
  "Stop Sniffing",
  "Save Probes",
  "< Back"
};
const int probeCount = 4;

const char* spoofItems[] = {
  "Create Profile",
  "Activate Profile",
  "Delete Profile",
  "< Back"
};
const int spoofCount = 4;

const char* sdItems[] = {
  "Browse Files",
  "Card Info",
  "Delete File",
  "< Back"
};
const int sdCount = 4;

// ================================================================
// BUTTON HELPERS
// ================================================================
bool btnPressed(int pin) {
  if (millis() - lastBtnTime < DEBOUNCE_MS) return false;
  if (digitalRead(pin) == LOW) {
    lastBtnTime = millis();
    return true;
  }
  return false;
}

// ================================================================
// DISPLAY FUNCTIONS
// ================================================================
void oledClear() { u8g2.clearBuffer(); }
void oledSend()  { u8g2.sendBuffer(); }

void showMessage(const char* line1, const char* line2, const char* line3) {
  oledClear();
  u8g2.setFont(u8g2_font_6x10_tf);
  if (strlen(line1)) u8g2.drawStr(2, 20, line1);
  if (strlen(line2)) u8g2.drawStr(2, 34, line2);
  if (strlen(line3)) u8g2.drawStr(2, 48, line3);
  oledSend();
}

void showBigMessage(const char* msg) {
  oledClear();
  u8g2.setFont(u8g2_font_7x14B_tf);
  int w = u8g2.getStrWidth(msg);
  u8g2.drawStr((128 - w) / 2, 38, msg);
  oledSend();
}

void drawMenuList(const char* title, const char** items, int count) {
  oledClear();
  u8g2.setFont(u8g2_font_6x10_tf);
  int maxVisible = 4;
  int startIdx = 0;
  if (menuIndex >= maxVisible) startIdx = menuIndex - maxVisible + 1;

  for (int i = 0; i < maxVisible && (startIdx + i) < count; i++) {
    int itemIdx = startIdx + i;
    int y = 14 + (i * 14);
    if (itemIdx == menuIndex) {
      u8g2.drawBox(0, y - 10, 128, 13);
      u8g2.setDrawColor(0);
    }
    u8g2.drawStr(4, y, items[itemIdx]);
    u8g2.setDrawColor(1);
  }
  if (startIdx > 0) u8g2.drawTriangle(120, 3, 124, 3, 122, 0);
  if (startIdx + maxVisible < count) u8g2.drawTriangle(120, 61, 124, 61, 122, 64);
  oledSend();
}

void drawSDBrowser() {
  oledClear();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawBox(0, 0, 128, 12);
  u8g2.setDrawColor(0);
  String hdr = "FILES(" + String(sdFileCount) + ")";
  u8g2.drawStr(2, 10, hdr.c_str());
  u8g2.setDrawColor(1);

  if (sdFileCount == 0) {
    u8g2.drawStr(4, 36, "No files found");
    oledSend();
    return;
  }

  int maxVisible = 4;
  int startIdx = 0;
  if (sdFileSel >= maxVisible) startIdx = sdFileSel - maxVisible + 1;

  for (int i = 0; i < maxVisible && (startIdx + i) < sdFileCount; i++) {
    int idx = startIdx + i;
    int y = 24 + (i * 12);
    if (idx == sdFileSel) {
      u8g2.drawBox(0, y - 9, 128, 11);
      u8g2.setDrawColor(0);
    }
    String fn = sdFiles[idx];
    int lastSlash = fn.lastIndexOf('/');
    if (lastSlash >= 0) fn = fn.substring(lastSlash + 1);
    if (fn.length() > 20) fn = fn.substring(0, 19) + "~";
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(4, y, fn.c_str());
    u8g2.setDrawColor(1);
  }
  oledSend();
}

void drawSpoofList() {
  oledClear();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawBox(0, 0, 128, 12);
  u8g2.setDrawColor(0);
  u8g2.drawStr(2, 10, "SPOOF PROFILES");
  u8g2.setDrawColor(1);

  if (spoofProfileCount == 0) {
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(4, 36, "No profiles found");
    oledSend();
    return;
  }

  int maxVisible = 4;
  int startIdx = 0;
  if (spoofSelectedIndex >= maxVisible) startIdx = spoofSelectedIndex - maxVisible + 1;

  for (int i = 0; i < maxVisible && (startIdx + i) < spoofProfileCount; i++) {
    int idx = startIdx + i;
    int y = 24 + (i * 12);
    if (idx == spoofSelectedIndex) {
      u8g2.drawBox(0, y - 9, 128, 11);
      u8g2.setDrawColor(0);
    }
    String display = spoofProfiles[idx].name;
    if (display.length() > 16) display = display.substring(0, 15) + "~";
    u8g2.drawStr(4, y, display.c_str());
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(4, y + 8, spoofProfiles[idx].macAddress.c_str());
    if (spoofProfiles[idx].active) u8g2.drawStr(85, y + 8, "ACTIVE");
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.setDrawColor(1);
  }
  oledSend();
}

// ================================================================
// drawResult 
// ================================================================
void drawResult() {
  oledClear();

  // Header bar (uses resultTitle set by clearResults())
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawBox(0, 0, 128, 12);
  u8g2.setDrawColor(0);
  String hdr = resultTitle;
  if (hdr.length() > 20) hdr = hdr.substring(0, 20);
  u8g2.drawStr(2, 10, hdr.c_str());
  u8g2.setDrawColor(1);

  u8g2.setFont(u8g2_font_5x7_tf);

  int maxLines = 6;
  int lineH   = 7;
  int startY  = 20;

  for (int i = 0; i < maxLines && (menuScroll + i) < resultLineCount; i++) {
    u8g2.drawStr(2, startY + (i * lineH), resultLines[menuScroll + i].c_str());
  }

  // Scrollbar (fits within the content area, y=14..56)
  if (resultLineCount > maxLines) {
    int barH = 42 * maxLines / resultLineCount;
    int barY = 14 + 42 * menuScroll / resultLineCount;
    u8g2.drawFrame(124, 14, 4, 42);
    u8g2.drawBox(124, barY, 4, max(barH, 2));
  }

  // Hint bar
  u8g2.drawLine(0, 56, 123, 56);
  if (resultContext != CTX_NONE) {
    u8g2.drawStr(2, 63, "SEL=Save  BACK=Menu");
  } else {
    u8g2.drawStr(2, 63, "BACK=Menu");
  }

  if (resultLineCount == 0) {
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(10, 35, "No results found");
  }
  oledSend();
}

void drawMenu() {
  switch (currentMenu) {
    case MENU_MAIN:       drawMenuList("TANK", mainItems, mainCount); break;
    case MENU_WIFI:       drawMenuList("WIFI", wifiItems, wifiCount); break;
    case MENU_BLE_SCAN:   drawMenuList("BLE SCAN", bleItems, bleCount); break;
    case MENU_RF24:       drawMenuList("RF24", rf24Items, rf24Count); break;
    case MENU_PROBE:      drawMenuList("PROBE SNIFF", probeItems, probeCount); break;
    case MENU_SPOOF:      drawMenuList("DEVICE SPOOF", spoofItems, spoofCount); break;
    case MENU_SPOOF_LIST: drawSpoofList(); break;
    case MENU_SD:         drawMenuList("SD CARD", sdItems, sdCount); break;
    case MENU_SD_BROWSE:  drawSDBrowser(); break;
    case MENU_RESULT:     drawResult(); break;
    case MENU_WIFI_SCANLIST: drawWifiScanList(); break;
    case MENU_WIFI_PWENTRY:  drawPasswordEntry(); break;
    case MENU_WIFI_DEVLIST:  drawWifiDeviceList(); break;
  }
}

void clearResults(String title) {
  resultLineCount = 0;
  menuScroll = 0;
  resultTitle = title;
  for (int i = 0; i < MAX_RESULT_LINES; i++) resultLines[i] = "";
}

void addResult(String line) {
  if (resultLineCount >= MAX_RESULT_LINES) return;
  if (line.length() > 24) line = line.substring(0, 23) + "~";
  resultLines[resultLineCount++] = line;
}

// ================================================================
// TIMESTAMP
// ================================================================
String getTimestamp() {
  unsigned long s = millis() / 1000;
  char buf[20];
  snprintf(buf, sizeof(buf), "%02luh%02lum%02lus", s / 3600, (s % 3600) / 60, s % 60);
  return String(buf);
}

// ================================================================
// BOOT SEQUENCE — "Black_Void" title over a live progress bar while
// BLE / RF24 / SD / WiFi are brought up in turn
// ================================================================
void drawProgress(const char* label, int percent);   // forward-declare

void drawBootProgress(const char* label, int percent) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x14B_tf);
  int tw = u8g2.getStrWidth("Black_Void");
  u8g2.drawStr((128 - tw) / 2, 14, "Black_Void");

  u8g2.setFont(u8g2_font_6x10_tf);
  int w = u8g2.getStrWidth(label);
  u8g2.drawStr((128 - w) / 2, 30, label);

  u8g2.drawFrame(4, 40, 120, 10);
  int fillW = (int)(116 * percent / 100.0);
  if (fillW > 0) u8g2.drawBox(6, 42, fillW, 6);

  char pct[8];
  snprintf(pct, sizeof(pct), "%d%%", percent);
  u8g2.setFont(u8g2_font_5x7_tf);
  int pw = u8g2.getStrWidth(pct);
  u8g2.drawStr((128 - pw) / 2, 60, pct);
  oledSend();
}

void drawSplash() {
  drawBootProgress("Booting...", 0);
  delay(300);

  drawBootProgress("Init BLE...", 20);
  NimBLEDevice::init("Tank");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  pBLEScan = nullptr;
  bleReady = true;
  drawBootProgress(bleReady ? "BLE OK" : "BLE Failed", 20);
  delay(250);

  drawBootProgress("Init RF24...", 40);
  vspi.begin(SD_SCK, SD_MISO, SD_MOSI, NRF24_CSN);
  if (radio.begin(&vspi)) {
    radio.setPALevel(RF24_PA_MAX);
    radio.setDataRate(RF24_250KBPS);
    radio.setChannel(76);
    radio.setAutoAck(false);
    radio.setRetries(0, 0);
    radio.stopListening();
    nrfReady = true;
  }
  drawBootProgress(nrfReady ? "RF24 OK" : "RF24 Failed", 40);
  delay(250);

  drawBootProgress("Init SD Card...", 60);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  initSD();
  drawBootProgress(sdReady ? "SD OK" : "SD Failed", 60);
  delay(250);

  drawBootProgress("Init WiFi...", 80);
  WiFi.mode(WIFI_STA);
  delay(150);
  randomizeWiFiMAC();
  bool wifiHwOk = (WiFi.getMode() == WIFI_STA);
  WiFi.mode(WIFI_OFF);
  WiFi.disconnect(true);
  wifiConnected = false;
  reassertButtonPins();
  drawBootProgress(wifiHwOk ? "WiFi OK" : "WiFi Failed", 80);
  delay(250);

  drawBootProgress("Load Profiles...", 90);
  loadSpoofProfiles();
  drawBootProgress("Ready", 100);
  delay(500);

  oledClear();
  oledSend();
}

// ================================================================
// PROGRESS BAR
// ================================================================
void drawProgress(const char* label, int percent) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  int w = u8g2.getStrWidth(label);
  u8g2.drawStr((128 - w) / 2, 28, label);
  u8g2.drawFrame(4, 38, 120, 10);
  int fillW = (int)(116 * percent / 100.0);
  if (fillW > 0) u8g2.drawBox(6, 40, fillW, 6);
  char pct[8];
  snprintf(pct, sizeof(pct), "%d%%", percent);
  u8g2.setFont(u8g2_font_5x7_tf);
  int pw = u8g2.getStrWidth(pct);
  u8g2.drawStr((128 - pw) / 2, 58, pct);
  oledSend();
}

// ================================================================
// SD CARD
// ================================================================
void initSD() {
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  vspi.begin(SD_SCK, SD_MISO, SD_MOSI, NRF24_CSN);
  if (!SD.begin(SD_CS, vspi, 4000000)) { sdReady = false; return; }
  sdReady = true;
  if (!SD.exists("/tank"))         SD.mkdir("/tank");
  if (!SD.exists("/tank/wifi"))    SD.mkdir("/tank/wifi");
  if (!SD.exists("/tank/ble"))     SD.mkdir("/tank/ble");
  if (!SD.exists("/tank/rf24"))    SD.mkdir("/tank/rf24");
  if (!SD.exists("/tank/probes"))  SD.mkdir("/tank/probes");
  if (!SD.exists("/tank/spoof"))   SD.mkdir("/tank/spoof");
  loadSpoofProfiles();
}

void saveToSD(String filename, String data) {
  if (!sdReady) { addResult("SD ERR"); return; }
  File f = SD.open(filename, FILE_WRITE);
  if (!f) { addResult("Can't create"); return; }
  f.println(data);
  f.close();
  addResult("Saved: " + filename.substring(filename.lastIndexOf('/') + 1));
}

void runSDBrowse() {
  sdFileCount = 0;
  sdFileSel   = 0;
  const char* folders[] = {"/tank/wifi", "/tank/ble", "/tank/rf24", "/tank/probes", "/tank/spoof"};
  for (int f = 0; f < 5; f++) {
    File dir = SD.open(folders[f]);
    if (!dir) continue;
    File entry = dir.openNextFile();
    while (entry && sdFileCount < MAX_SD_FILES) {
      if (!entry.isDirectory()) {
        sdFiles[sdFileCount++] = String(folders[f]) + "/" + entry.name();
      }
      entry = dir.openNextFile();
    }
    dir.close();
  }
  currentMenu = MENU_SD_BROWSE;
}

void runSDViewFile(String filename) {
  int lastSlash = filename.lastIndexOf('/');
  String shortName = lastSlash >= 0 ? filename.substring(lastSlash + 1) : filename;
  clearResults(shortName);
  resultOriginMenu = MENU_SD_BROWSE;
  resultContext    = CTX_NONE;
  File f = SD.open(filename, FILE_READ);
  if (!f) { addResult("Cannot open!"); currentMenu = MENU_RESULT; return; }
  while (f.available() && resultLineCount < MAX_RESULT_LINES) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) addResult(line);
  }
  f.close();
  currentMenu = MENU_RESULT;
}

void runSDCardInfo() {
  clearResults("SD INFO");
  resultOriginMenu = MENU_SD;
  resultContext    = CTX_NONE;
  if (!sdReady) {
    addResult("SD not ready");
  } else {
    char buf[32];
    snprintf(buf, sizeof(buf), "Total: %d MB", (int)(SD.totalBytes() / 1048576));
    addResult(buf);
    snprintf(buf, sizeof(buf), "Used:  %d MB", (int)(SD.usedBytes() / 1048576));
    addResult(buf);
    snprintf(buf, sizeof(buf), "Free:  %d MB", (int)((SD.totalBytes() - SD.usedBytes()) / 1048576));
    addResult(buf);
  }
  currentMenu = MENU_RESULT;
}

void runSDDeleteFile() {
  if (!sdReady)                    { addResult("SD not ready");    currentMenu = MENU_RESULT; return; }
  if (selectedFile.length() == 0) { addResult("No file selected"); currentMenu = MENU_RESULT; return; }
  clearResults("DELETE FILE");
  resultOriginMenu = MENU_SD;
  resultContext    = CTX_NONE;
  if (SD.remove(selectedFile)) {
    addResult("Deleted: " + selectedFile);
    selectedFile = "";
  } else {
    addResult("Delete failed!");
  }
  currentMenu = MENU_RESULT;
}

// ================================================================
// RF24 FUNCTIONS
// ================================================================
void initRF24() {
  if (radio.begin(&vspi)) {
    radio.setPALevel(RF24_PA_MAX);
    radio.setDataRate(RF24_250KBPS);
    radio.setChannel(76);
    radio.setAutoAck(false);
    radio.setRetries(0, 0);
    radio.stopListening();
    nrfReady = true;
  }
}

void spectrumAnalyzeRF24() {
  clearResults("SPECTRUM ANALYZE");
  resultOriginMenu = MENU_RF24;
  resultContext    = CTX_RF24;

  if (!nrfReady) { addResult("nRF24 not ready"); currentMenu = MENU_RESULT; return; }

  showMessage("Analyzing...", "125 channels", "2 sec each");
  memset(channelActivity, 0, sizeof(channelActivity));
  totalPackets  = 0;
  activeChannels = 0;

  for (int ch = 0; ch < NUM_CHANNELS; ch++) {
    radio.setChannel(ch);
    radio.startListening();
    unsigned long start = millis();
    int packetsOnChannel = 0;
    while (millis() - start < 2000) {
      if (radio.available()) {
        uint8_t buf[32];
        radio.read(buf, 32);
        channelActivity[ch]++;
        packetsOnChannel++;
        totalPackets++;
      }
      yield();
    }
    radio.stopListening();
    if (packetsOnChannel > 3) activeChannels++;
    if (ch % 20 == 0) {
      char buf[32];
      snprintf(buf, sizeof(buf), "CH%d: %d pkts", ch, packetsOnChannel);
      showMessage("Analyzing...", buf, "");
    }
    yield();
  }

  char buf[32];
  snprintf(buf, sizeof(buf), "Total: %d packets", totalPackets);
  addResult(buf);
  snprintf(buf, sizeof(buf), "Active ch: %d/125", activeChannels);
  addResult(buf);
  addResult("Top 5 channels:");

  for (int top = 0; top < 5; top++) {
    int maxCh = 0, maxVal = 0;
    for (int ch = 0; ch < NUM_CHANNELS; ch++) {
      if (channelActivity[ch] > maxVal) { maxVal = channelActivity[ch]; maxCh = ch; }
    }
    if (maxVal > 0) {
      snprintf(buf, sizeof(buf), "CH%3d: %d pkts", maxCh, maxVal);
      addResult(buf);
      channelActivity[maxCh] = 0;
    }
  }
  radio.setChannel(76);
  currentMenu = MENU_RESULT;
}

void captureRF24() {
  clearResults("RF24 CAPTURE");
  resultOriginMenu = MENU_RF24;
  resultContext    = CTX_RF24;

  if (!nrfReady) { addResult("nRF24 not ready"); currentMenu = MENU_RESULT; return; }

  rfPacketCount = 0;
  showMessage("Capturing...", "125 channels", "");

  for (int ch = 0; ch < NUM_CHANNELS && rfPacketCount < MAX_PACKETS; ch++) {
    radio.setChannel(ch);
    radio.startListening();
    unsigned long start = millis();
    while (millis() - start < 15 && rfPacketCount < MAX_PACKETS) {
      if (radio.available()) {
        RFPacket& p = rfPackets[rfPacketCount];
        p.len = radio.getDynamicPayloadSize();
        if (p.len > 32) p.len = 32;
        radio.read(p.data, p.len);
        p.channel = ch;
        p.time    = millis();
        rfPacketCount++;
      }
    }
    radio.stopListening();
    if (ch % 20 == 0) {
      char buf[20];
      snprintf(buf, sizeof(buf), "CH%d/%d", ch, NUM_CHANNELS);
      showMessage("Capturing...", buf, "");
    }
    yield();
  }

  char buf[32];
  snprintf(buf, sizeof(buf), "Captured: %d pkts", rfPacketCount);
  addResult(buf);
  radio.setChannel(76);
  currentMenu = MENU_RESULT;
}

void replayRF24() {
  clearResults("RF24 REPLAY");
  resultOriginMenu = MENU_RF24;
  resultContext    = CTX_RF24;

  if (!nrfReady)        { addResult("nRF24 not ready");      currentMenu = MENU_RESULT; return; }
  if (rfPacketCount == 0) { addResult("No packets to replay"); currentMenu = MENU_RESULT; return; }

  char buf[32];
  snprintf(buf, sizeof(buf), "%d packets", rfPacketCount);
  showMessage("Replaying...", buf, "PRESS BACK");

  int replayed = 0;
  bool running = true;
  unsigned long start = millis();

  while (running && millis() - start < 30000) {
    for (int i = 0; i < rfPacketCount && running; i++) {
      radio.stopListening();
      radio.setChannel(rfPackets[i].channel);
      radio.write(rfPackets[i].data, rfPackets[i].len);
      replayed++;
      if (digitalRead(BTN_BACK) == LOW) running = false;
      delay(10);
    }
  }

  snprintf(buf, sizeof(buf), "Replayed: %d times", replayed);
  addResult(buf);
  radio.setChannel(76);
  currentMenu = MENU_RESULT;
}

void saveRF24() {
  if (rfPacketCount == 0) { addResult("No data"); currentMenu = MENU_RESULT; return; }
  String out = "RF24 Capture " + getTimestamp() + "\nPackets: " + String(rfPacketCount) + "\n---\n";
  for (int i = 0; i < rfPacketCount; i++) {
    out += "CH" + String(rfPackets[i].channel) + " LEN" + String(rfPackets[i].len) + " DATA:";
    for (int j = 0; j < rfPackets[i].len; j++) {
      char h[3];
      snprintf(h, sizeof(h), "%02X", rfPackets[i].data[j]);
      out += h;
    }
    out += "\n";
  }
  saveToSD("/tank/rf24/capture_" + getTimestamp() + ".txt", out);
  currentMenu = MENU_RESULT;
}

// ================================================================
// BLE SCAN
// ================================================================
class MyScanCallbacks : public NimBLEScanCallbacks {
public:
  void onResult(NimBLEAdvertisedDevice* dev) {
    if (bleDeviceCount >= MAX_BLE_DEVICES) return;
    String addr = String(dev->getAddress().toString().c_str());
    for (int i = 0; i < bleDeviceCount; i++) {
      if (bleDevices[i].addr == addr) { bleDevices[i].rssi = dev->getRSSI(); return; }
    }
    bleDevices[bleDeviceCount].addr  = addr;
    bleDevices[bleDeviceCount].name  = String(dev->getName().c_str());
    bleDevices[bleDeviceCount].rssi  = dev->getRSSI();

    bool isTracker   = false;
    String trackerType = "";
    if (dev->haveManufacturerData()) {
      std::string mfData = dev->getManufacturerData();
      if (mfData.length() >= 2) {
        uint16_t companyID = (uint8_t)mfData[1] << 8 | (uint8_t)mfData[0];
        if (companyID == 0x004C) { isTracker = true; trackerType = "Apple"; }
        if (companyID == 0x00E0) { isTracker = true; trackerType = "Tile"; }
        if (companyID == 0x0075) { isTracker = true; trackerType = "Samsung"; }
      }
    }
    bleDevices[bleDeviceCount].isTracker   = isTracker;
    bleDevices[bleDeviceCount].trackerType = trackerType;
    bleDeviceCount++;
  }
};

MyScanCallbacks bleCallbacks;

void runBLEScan(bool trackersOnly) {
  clearResults(trackersOnly ? "BLE TRACKERS" : "BLE DEVICES");
  resultOriginMenu = MENU_BLE_SCAN;
  resultContext    = CTX_BLE;

  bleDeviceCount = 0;
  showMessage("BLE Scan", "5 seconds", "");

  pBLEScan = NimBLEDevice::getScan();
  if (pBLEScan->isScanning()) pBLEScan->stop();
  pBLEScan->setScanCallbacks(&bleCallbacks);
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  pBLEScan->clearResults();
  pBLEScan->start(5, false);

  char buf[32];
  snprintf(buf, sizeof(buf), "Found %d devices", bleDeviceCount);
  addResult(buf);

  int shown = 0;
  for (int i = 0; i < bleDeviceCount && shown < 30; i++) {
    if (trackersOnly && !bleDevices[i].isTracker) continue;
    String name = bleDevices[i].name.length() > 0 ? bleDevices[i].name : bleDevices[i].addr;
    addResult((bleDevices[i].isTracker ? "! " : "  ") + name);
    snprintf(buf, sizeof(buf), "  RSSI: %d dBm", bleDevices[i].rssi);
    addResult(buf);
    if (bleDevices[i].isTracker) addResult("  Type: " + bleDevices[i].trackerType);
    addResult("  ---");
    shown++;
  }
  if (shown == 0) addResult(trackersOnly ? "No trackers found" : "No devices found");

  currentMenu = MENU_RESULT;
}

void saveBLEScan() {
  if (bleDeviceCount == 0) { addResult("No data"); currentMenu = MENU_RESULT; return; }
  String out = "BLE Scan " + getTimestamp() + "\nDevices: " + String(bleDeviceCount) + "\n---\n";
  for (int i = 0; i < bleDeviceCount; i++) {
    out += "MAC: "  + bleDevices[i].addr + "\n";
    out += "Name: " + bleDevices[i].name + "\n";
    out += "RSSI: " + String(bleDevices[i].rssi) + "dBm\n";
    if (bleDevices[i].isTracker) out += "Type: TRACKER (" + bleDevices[i].trackerType + ")\n";
    out += "---\n";
  }
  saveToSD("/tank/ble/scan_" + getTimestamp() + ".txt", out);
  currentMenu = MENU_RESULT;
}

// ================================================================
// WIFI SCAN + CONNECT + DEVICE DISCOVERY  (Corntenna functionality)
// ================================================================
void reassertButtonPins() {
  pinMode(BTN_BACK,   INPUT_PULLUP);
  pinMode(BTN_UP,     INPUT_PULLUP);
  pinMode(BTN_DOWN,   INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);
  delay(100);
}

void randomizeWiFiMAC() {
  uint8_t mac[6];

  // Generate six random bytes
  for (int i = 0; i < 6; i++) {
    mac[i] = esp_random() & 0xFF;
  }

  //force a locally administered unicast address
  mac[0] &= 0xFE;
  mac[0] |= 0x02;

  WiFi.mode(WIFI_STA);
  delay(50);

  esp_err_t err = esp_wifi_set_mac(WIFI_IF_STA, mac);

  if (err == ESP_OK) {
    snprintf(currentRandomMAC, sizeof(currentRandomMAC), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    Serial.println();
    Serial.println("====================================");
    Serial.println(" Black_Void Identity Generated");
    Serial.print(" MAC: ");
    Serial.println(currentRandomMAC);
    Serial.printf("WiFi STA MAC: %s\n", WiFi.macAddress().c_str());
    Serial.println("====================================");
  }
  else {
    Serial.printf("MAC spoof failed (%d)\n", err);
  }
}

void runWifiScanAll() {
  showMessage("Scanning WiFi...", "networks nearby", "");
  randomizeWiFiMAC();
  WiFi.disconnect();
  delay(100);

  int n = WiFi.scanNetworks(false, true);
  wifiNetworkCount = 0;
  for (int i = 0; i < n && wifiNetworkCount < MAX_WIFI_NETWORKS; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) ssid = "[Hidden]";
    wifiNetworks[wifiNetworkCount].ssid       = ssid;
    wifiNetworks[wifiNetworkCount].bssid      = WiFi.BSSIDstr(i);
    wifiNetworks[wifiNetworkCount].rssi       = WiFi.RSSI(i);
    wifiNetworks[wifiNetworkCount].channel    = WiFi.channel(i);
    wifiNetworks[wifiNetworkCount].open       = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
    wifiNetworks[wifiNetworkCount].encryption = WiFi.encryptionType(i);

    String sec;
    switch (WiFi.encryptionType(i)) {
      case WIFI_AUTH_OPEN:            sec = "[Open]"; break;
      case WIFI_AUTH_WEP:             sec = "[WEP]"; break;
      case WIFI_AUTH_WPA_PSK:         sec = "[WPA]"; break;
      case WIFI_AUTH_WPA_WPA2_PSK:    sec = "[WPA2]"; break;
      case WIFI_AUTH_WPA2_ENTERPRISE: sec = "[WPA2-ENTERPRISE]"; break;
      case WIFI_AUTH_WPA3_PSK:        sec = "[WPA3]"; break;
      case WIFI_AUTH_WPA2_WPA3_PSK:   sec = "[WPA2/WPA3]"; break;
      case WIFI_AUTH_WAPI_PSK:        sec = "[WAPI-china wlan]"; break;
      default:                        sec = "[Unknown]"; break;
    }
    wifiNetworks[wifiNetworkCount].secLabel = sec;
    wifiNetworkCount++;
  }
  WiFi.scanDelete();
  reassertButtonPins();

  if (wifiNetworkCount == 0) {
    showBigMessage("No networks");
    delay(1000);
    currentMenu = MENU_WIFI;
    menuIndex   = 0;
    return;
  }

  nrNetIndex  = 0;
  nrNetScroll = 0;
  currentMenu = MENU_WIFI_SCANLIST;
}

void drawWifiScanList() {
  oledClear();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawBox(0, 0, 128, 12);
  u8g2.setDrawColor(0);
  u8g2.drawStr(2, 10, "SELECT NETWORK");
  u8g2.setDrawColor(1);

  int visible = 4;
  if (nrNetIndex < nrNetScroll) nrNetScroll = nrNetIndex;
  if (nrNetIndex >= nrNetScroll + visible) nrNetScroll = nrNetIndex - visible + 1;

  for (int row = 0; row < visible; row++) {
    int i = nrNetScroll + row;
    if (i >= wifiNetworkCount) break;
    int y = 24 + row * 11;
    bool highlighted = (i == nrNetIndex);

    if (highlighted) {
      u8g2.drawBox(0, y - 8, 128, 10);
      u8g2.setDrawColor(0);
    }
    String full = wifiNetworks[i].ssid + " " + wifiNetworks[i].secLabel;

    if (highlighted && (int)full.length() > MARQUEE_MAX_CHARS) {
      String looped = full + "   " + full;
      int start = nrMarqueeOffset % (full.length() + 3);
      String windowStr = looped.substring(start, start + MARQUEE_MAX_CHARS);
      u8g2.drawStr(4, y, windowStr.c_str());
    } else {
      String label = full;
      if ((int)label.length() > 20) label = label.substring(0, 20);
      u8g2.drawStr(4, y, label.c_str());
    }
    u8g2.setDrawColor(1);
  }
  oledSend();
}

// ---- password entry: Up/Down scroll, Select add/hold=connect, Back del/hold=cancel ----
void nrBeginPasswordEntry() {
  pwLen = 0;
  pwBuffer[0] = '\0';
  pwCharIdx = 0;
  currentMenu = MENU_WIFI_PWENTRY;
}

void drawPasswordEntry() {
  oledClear();
  u8g2.setFont(u8g2_font_6x10_tf);
  String ssidLine = nrChosenSSID;
  if (ssidLine.length() > 20) ssidLine = ssidLine.substring(0, 20);
  u8g2.drawStr(2, 10, ssidLine.c_str());
  u8g2.drawLine(0, 13, 128, 13);

  char masked[21];
  int shown = pwLen < 20 ? pwLen : 20;
  for (int i = 0; i < shown; i++) masked[i] = '*';
  masked[shown] = '\0';
  u8g2.drawStr(2, 26, masked);
  u8g2.drawStr(2 + shown * 6, 26, "_");

  char prev = CHARSET[(pwCharIdx - 1 + CHARSET_LEN) % CHARSET_LEN];
  char cur  = CHARSET[pwCharIdx];
  char next = CHARSET[(pwCharIdx + 1) % CHARSET_LEN];

  u8g2.setFont(u8g2_font_6x10_tf);
  char sb[2] = {prev == ' ' ? '_' : prev, '\0'};
  u8g2.drawStr(24, 48, sb);
  sb[0] = (next == ' ' ? '_' : next);
  u8g2.drawStr(100, 48, sb);

  u8g2.drawFrame(52, 36, 24, 18);
  u8g2.setFont(u8g2_font_7x14B_tf);
  char bb[2] = {cur == ' ' ? '_' : cur, '\0'};
  u8g2.drawStr(59, 50, bb);

  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(2, 62, "Sel:add Hold=Go Bk:del");
  oledSend();
}

bool handlePasswordEntryButtons() {
  bool changed = false;

  if (btnPressed(BTN_UP)) {
    pwCharIdx = (pwCharIdx + 1) % CHARSET_LEN;
    changed = true;
  }
  if (btnPressed(BTN_DOWN)) {
    pwCharIdx = (pwCharIdx - 1 + CHARSET_LEN) % CHARSET_LEN;
    changed = true;
  }

  if (digitalRead(BTN_SELECT) == LOW) {
    if (selectHeldSince == 0) selectHeldSince = millis();
    if (!selectLongFired && millis() - selectHeldSince > LONG_PRESS_MS) {
      selectLongFired = true;
      pwBuffer[pwLen] = '\0';
      nrConnectToChosenNetwork();
      changed = true;
    }
  } else {
    if (selectHeldSince != 0 && !selectLongFired) {
      if (pwLen < PW_MAX_LEN) {
        pwBuffer[pwLen++] = CHARSET[pwCharIdx];
        pwBuffer[pwLen] = '\0';
        changed = true;
      }
    }
    selectHeldSince = 0;
    selectLongFired = false;
  }

  if (digitalRead(BTN_BACK) == LOW) {
    if (backHeldSince == 0) backHeldSince = millis();
    if (!backLongFired && millis() - backHeldSince > LONG_PRESS_MS) {
      backLongFired = true;
      currentMenu = MENU_WIFI_SCANLIST;
      changed = true;
    }
  } else {
    if (backHeldSince != 0 && !backLongFired) {
      if (pwLen > 0) { pwLen--; pwBuffer[pwLen] = '\0'; changed = true; }
    }
    backHeldSince = 0;
    backLongFired = false;
  }

  return changed;
}

void nrConnectToChosenNetwork() {
  showMessage("Connecting to:", nrChosenSSID.c_str(), "");
  randomizeWiFiMAC();
  WiFi.mode(WIFI_STA);
  WiFi.begin(nrChosenSSID.c_str(), pwBuffer);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(400);
    attempts++;
    char dots[24];
    snprintf(dots, sizeof(dots), "Attempt %d/30", attempts);
    showMessage("Connecting to:", nrChosenSSID.c_str(), dots);
    if (digitalRead(BTN_BACK) == LOW) break;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    char ipLine[32];
    snprintf(ipLine, sizeof(ipLine), "IP %s", WiFi.localIP().toString().c_str());
    showMessage("Connected!", ipLine, "");
  } else {
    wifiConnected = false;
    showMessage("Connect failed", "Bad password or", "out of range");
  }
  delay(1200);
  reassertButtonPins();
  currentMenu = MENU_WIFI;
  menuIndex   = 0;
}

// ---- OUI lookup + device discovery ----
const char* nrLookupVendor(const char* mac) {
  uint8_t firstByte = strtol(mac, nullptr, 16);
  if (firstByte & 0x02) return "Random";
  uint8_t oui[3];
  sscanf(mac, "%hhx:%hhx:%hhx", &oui[0], &oui[1], &oui[2]);
  for (int i = 0; i < ouiTableSize; i++) {
    if (oui[0] == ouiTable[i].oui[0] &&
        oui[1] == ouiTable[i].oui[1] &&
        oui[2] == ouiTable[i].oui[2]) {
      return ouiTable[i].vendor;
    }
  }
  return "Unknown";
}

void nrFormatMac(const uint8_t* mac, char* out) {
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

NRDevice* nrFindDevice(IPAddress ip) {
  for (int i = 0; i < nrDeviceCount; i++)
    if (nrDevices[i].ip == ip) return &nrDevices[i];
  return nullptr;
}

void nrScanDevicePorts(NRDevice* d) {
  char result[32] = "";
  for (int p = 0; p < nrNumScanPorts; p++) {
    WiFiClient client;
    client.setTimeout(50);
    if (client.connect(d->ip, nrScanPorts[p])) {
      client.stop();
      char portStr[8];
      if (strlen(result) > 0) strncat(result, ",", sizeof(result) - strlen(result) - 1);
      snprintf(portStr, sizeof(portStr), "%d", nrScanPorts[p]);
      strncat(result, portStr, sizeof(result) - strlen(result) - 1);
    }
  }
  if (strlen(result) == 0) strncpy(result, "none", sizeof(result) - 1);
  strncpy(d->ports, result, sizeof(d->ports) - 1);
  d->ports[sizeof(d->ports) - 1] = '\0';
}

void nrAddDevice(IPAddress ip, const uint8_t* mac) {
  if (nrFindDevice(ip)) return;
  if (nrDeviceCount >= MAX_NR_DEVICES) return;
  NRDevice& d = nrDevices[nrDeviceCount++];
  d.ip = ip;
  nrFormatMac(mac, d.mac);
  strcpy(d.ports, "...");
  strncpy(d.vendor, nrLookupVendor(d.mac), sizeof(d.vendor) - 1);
  d.vendor[sizeof(d.vendor) - 1] = '\0';
}

void runDeviceDiscovery() {
  nrDeviceCount = 0;
  IPAddress localIP = WiFi.localIP();
  IPAddress mask = WiFi.subnetMask();
  uint32_t lip = (uint32_t)localIP[0] << 24 | (uint32_t)localIP[1] << 16 |
                 (uint32_t)localIP[2] << 8 | (uint32_t)localIP[3];
  uint32_t msk = (uint32_t)mask[0] << 24 | (uint32_t)mask[1] << 16 |
                 (uint32_t)mask[2] << 8 | (uint32_t)mask[3];
  uint32_t base = lip & msk;

  for (uint32_t i = 1; i <= 254; i++) {
    uint32_t h = base + i;
    IPAddress target((h >> 24) & 0xFF, (h >> 16) & 0xFF, (h >> 8) & 0xFF, h & 0xFF);
    if (target == localIP) continue;

    if (i % 8 == 0) {
      char pct[24];
      snprintf(pct, sizeof(pct), "Scanning %d%%", (int)(i * 100 / 254));
      drawProgress(pct, (int)(i * 100 / 254));
    }

    WiFiClient client;
    client.setTimeout(40);
    if (client.connect(target, 80)) client.stop();

    struct netif* nif = netif_default;
    ip4_addr_t ipaddr;
    IP4_ADDR(&ipaddr, target[0], target[1], target[2], target[3]);
    struct eth_addr* ethret = nullptr;
    const ip4_addr_t* ipret = nullptr;
    LOCK_TCPIP_CORE();
    int found = etharp_find_addr(nif, &ipaddr, &ethret, &ipret);
    UNLOCK_TCPIP_CORE();

    if (found >= 0 && ethret) {
      nrAddDevice(target, ethret->addr);
      NRDevice* d = nrFindDevice(target);
      if (d) nrScanDevicePorts(d);
    }
  }
  nrDevScroll = 0;
  currentMenu = MENU_WIFI_DEVLIST;
}

void drawWifiDeviceList() {
  oledClear();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawBox(0, 0, 128, 12);
  u8g2.setDrawColor(0);
  char hdr[24];
  snprintf(hdr, sizeof(hdr), "DEVICES (%d)", nrDeviceCount);
  u8g2.drawStr(2, 10, hdr);
  u8g2.setDrawColor(1);

  if (nrDeviceCount == 0) {
    u8g2.drawStr(4, 30, "None found");
  } else {
    int visible = 4;
    if (nrDevScroll > nrDeviceCount - visible) nrDevScroll = max(0, nrDeviceCount - visible);
    for (int row = 0; row < visible; row++) {
      int i = nrDevScroll + row;
      if (i >= nrDeviceCount) break;
      char line[24];
      snprintf(line, sizeof(line), "%s %s", nrDevices[i].vendor, nrDevices[i].ip.toString().c_str());
      u8g2.drawStr(4, 24 + row * 11, line);
    }
  }
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(2, 62, "Up/Dn scroll  Back=exit");
  oledSend();
}

void saveWifiDevices() {
  if (nrDeviceCount == 0) { showBigMessage("No devices"); delay(800); return; }
  String out = "WiFi Devices " + getTimestamp() + "\nDevices: " + String(nrDeviceCount) + "\n---\n";
  for (int i = 0; i < nrDeviceCount; i++) {
    out += "IP: "     + nrDevices[i].ip.toString()   + "\n";
    out += "MAC: "    + String(nrDevices[i].mac)     + "\n";
    out += "Vendor: " + String(nrDevices[i].vendor)  + "\n";
    out += "Ports: "  + String(nrDevices[i].ports)   + "\n";
    out += "---\n";
  }
  saveToSD("/tank/wifi/devices_" + getTimestamp() + ".txt", out);
  showBigMessage("Saved!");
  delay(800);
  currentMenu = MENU_WIFI;
  menuIndex   = 0;
}

// ================================================================
// PROBE SNIFFING
// ================================================================
void promiscuousRx(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!probeSniffing) return;
  wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
  int len = pkt->rx_ctrl.sig_len;
  if (len < 24 || len > 512) return;

  uint8_t* data = pkt->payload;
  uint16_t fc   = data[0] | (data[1] << 8);
  uint8_t typeSub = (fc >> 2) & 0x0F;

  if (typeSub == 0x04) {
    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             data[10], data[11], data[12], data[13], data[14], data[15]);

    int    offset = 24;
    String ssid   = "";
    while (offset < len - 2) {
      uint8_t tag    = data[offset];
      uint8_t tagLen = data[offset + 1];
      if (tag == 0 && tagLen > 0 && tagLen < 32) {
        char ssidBuf[33];
        memcpy(ssidBuf, &data[offset + 2], tagLen);
        ssidBuf[tagLen] = '\0';
        ssid = String(ssidBuf);
        break;
      }
      offset += 2 + tagLen;
    }

    if (ssid.length() > 0 && probeRequestCount < MAX_PROBE_REQUESTS) {
      probeRequests[probeRequestCount].mac     = String(mac);
      probeRequests[probeRequestCount].ssid    = ssid;
      probeRequests[probeRequestCount].rssi    = pkt->rx_ctrl.rssi;
      probeRequests[probeRequestCount].channel = pkt->rx_ctrl.channel;
      probeRequests[probeRequestCount].time    = millis();
      probeRequestCount++;
    }
  }
}

void startProbeSniff() {
  clearResults("PROBE SNIFFING");
  resultOriginMenu = MENU_PROBE;
  resultContext    = CTX_PROBE;

  probeRequestCount = 0;
  probeSniffing     = true;
  showMessage("Sniffing probes...", "Channel hopping", "PRESS BACK");

  esp_wifi_set_promiscuous_rx_cb(promiscuousRx);
  esp_wifi_set_promiscuous(true);

  int ch = 1;
  unsigned long lastHop = millis();

  while (probeSniffing) {
    if (digitalRead(BTN_BACK) == LOW) { probeSniffing = false; break; }
    if (millis() - lastHop > 200) {
      ch++;
      if (ch > 11) ch = 1;
      esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
      lastHop = millis();
    }
    static unsigned long lastShow = 0;
    if (millis() - lastShow > 2000) {
      lastShow = millis();
      clearResults("PROBE SNIFFING");
      char buf[32];
      snprintf(buf, sizeof(buf), "Probes: %d", probeRequestCount);
      addResult(buf);
      snprintf(buf, sizeof(buf), "Channel: %d", ch);
      addResult(buf);
      addResult("PRESS BACK TO STOP");
      drawResult();
    }
    delay(50);
  }

  esp_wifi_set_promiscuous(false);

  clearResults("PROBE RESULTS");
  resultOriginMenu = MENU_PROBE;
  resultContext    = CTX_PROBE;

  char buf[32];
  snprintf(buf, sizeof(buf), "Captured: %d", probeRequestCount);
  addResult(buf);
  for (int i = 0; i < probeRequestCount && i < 15; i++) {
    addResult(probeRequests[i].mac);
    addResult("  -> " + probeRequests[i].ssid);
    snprintf(buf, sizeof(buf), "  CH%d %ddBm", probeRequests[i].channel, probeRequests[i].rssi);
    addResult(buf);
    addResult("  ---");
  }
  currentMenu = MENU_RESULT;
}

void stopProbeSniff() {
  probeSniffing = false;
  esp_wifi_set_promiscuous(false);
  clearResults("PROBE STOPPED");
  resultOriginMenu = MENU_PROBE;
  resultContext    = CTX_PROBE;
  char buf[32];
  snprintf(buf, sizeof(buf), "Probes so far: %d", probeRequestCount);
  addResult(buf);
  addResult("Use Save Probes");
  addResult("to write to SD.");
  currentMenu = MENU_RESULT;
}

void saveProbes() {
  if (probeRequestCount == 0) { addResult("No data"); currentMenu = MENU_RESULT; return; }
  String out = "Probe Requests " + getTimestamp() +
               "\nProbes: " + String(probeRequestCount) + "\n---\n";
  for (int i = 0; i < probeRequestCount; i++) {
    out += "MAC: "     + probeRequests[i].mac               + "\n";
    out += "SSID: "    + probeRequests[i].ssid              + "\n";
    out += "Channel: " + String(probeRequests[i].channel)   + "\n";
    out += "RSSI: "    + String(probeRequests[i].rssi) + "dBm\n";
    out += "---\n";
  }
  saveToSD("/tank/probes/probes_" + getTimestamp() + ".txt", out);
  currentMenu = MENU_RESULT;
}

// ================================================================
// DEVICE SPOOFING
// ================================================================
void loadSpoofProfiles() {
  spoofProfileCount = 0;
  if (!sdReady) return;
  File f = SD.open("/tank/spoof/profiles.txt", FILE_READ);
  if (!f) return;
  while (f.available() && spoofProfileCount < MAX_SPOOF_PROFILES) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    int pos1 = line.indexOf('|');
    int pos2 = line.indexOf('|', pos1 + 1);
    int pos3 = line.indexOf('|', pos2 + 1);
    if (pos1 > 0 && pos2 > 0 && pos3 > 0) {
      spoofProfiles[spoofProfileCount].name       = line.substring(0, pos1);
      spoofProfiles[spoofProfileCount].macAddress = line.substring(pos1 + 1, pos2);
      spoofProfiles[spoofProfileCount].ipAddress  = line.substring(pos2 + 1, pos3);
      spoofProfiles[spoofProfileCount].active     = (line.substring(pos3 + 1) == "1");
      spoofProfileCount++;
    }
  }
  f.close();
}

void saveSpoofProfiles() {
  if (!sdReady) return;
  File f = SD.open("/tank/spoof/profiles.txt", FILE_WRITE);
  if (!f) return;
  for (int i = 0; i < spoofProfileCount; i++) {
    f.println(spoofProfiles[i].name + "|" + spoofProfiles[i].macAddress + "|" +
              spoofProfiles[i].ipAddress + "|" + String(spoofProfiles[i].active ? "1" : "0"));
  }
  f.close();
}

void createSpoofProfile() {
  clearResults("CREATE PROFILE");
  resultOriginMenu = MENU_SPOOF;
  resultContext    = CTX_NONE;

  char mac[18];
  snprintf(mac, sizeof(mac), "02:%02X:%02X:%02X:%02X:%02X",
           random(0x00, 0xFF), random(0x00, 0xFF), random(0x00, 0xFF),
           random(0x00, 0xFF), random(0x00, 0xFF));
  String profileName = "Spoof_" + String(millis() % 10000);
  String profileMAC  = String(mac);
  String profileIP   = "192.168.1." + String(random(2, 254));

  addResult("Name: " + profileName);
  addResult("MAC: "  + profileMAC);
  addResult("IP: "   + profileIP);
  addResult("");
  addResult("Profile saved to SD");

  if (spoofProfileCount < MAX_SPOOF_PROFILES) {
    spoofProfiles[spoofProfileCount].name       = profileName;
    spoofProfiles[spoofProfileCount].macAddress = profileMAC;
    spoofProfiles[spoofProfileCount].ipAddress  = profileIP;
    spoofProfiles[spoofProfileCount].active     = false;
    spoofProfileCount++;
    saveSpoofProfiles();
  }
  currentMenu = MENU_RESULT;
}

void activateSpoofProfile() {
  clearResults("ACTIVATE PROFILE");
  resultOriginMenu = MENU_SPOOF;
  resultContext    = CTX_NONE;

  if (spoofProfileCount == 0) {
    addResult("No profiles to activate");
    currentMenu = MENU_RESULT;
    return;
  }
  addResult("Selected: " + spoofProfiles[spoofSelectedIndex].name);
  addResult("MAC: "      + spoofProfiles[spoofSelectedIndex].macAddress);
  addResult("");
  addResult("Profile marked active");

  for (int i = 0; i < spoofProfileCount; i++) spoofProfiles[i].active = false;
  spoofProfiles[spoofSelectedIndex].active = true;
  saveSpoofProfiles();
  currentMenu = MENU_RESULT;
}

void deleteSpoofProfile() {
  clearResults("DELETE PROFILE");
  resultOriginMenu = MENU_SPOOF;
  resultContext    = CTX_NONE;

  if (spoofProfileCount == 0 || spoofSelectedIndex >= spoofProfileCount) {
    addResult("No profile selected");
    currentMenu = MENU_RESULT;
    return;
  }
  addResult("Deleted: " + spoofProfiles[spoofSelectedIndex].name);
  for (int i = spoofSelectedIndex; i < spoofProfileCount - 1; i++)
    spoofProfiles[i] = spoofProfiles[i + 1];
  spoofProfileCount--;
  if (spoofSelectedIndex >= spoofProfileCount) spoofSelectedIndex = spoofProfileCount - 1;
  if (spoofSelectedIndex < 0) spoofSelectedIndex = 0;
  saveSpoofProfiles();
  currentMenu = MENU_RESULT;
}

// ================================================================
// STATUS SCREEN
// ================================================================
void drawStatusScreen() {
  clearResults("SYSTEM STATUS");
  resultOriginMenu = MENU_MAIN;
  resultContext    = CTX_NONE;

  addResult("Tank");
  addResult("----------------");
  addResult("nRF24: " + String(nrfReady ? "READY" : "FAIL"));
  addResult("BLE: "   + String(bleReady  ? "READY" : "FAIL"));
  addResult("SD: "    + String(sdReady   ? "READY" : "NO"));
  addResult("----------------");
  char buf[32];
  snprintf(buf, sizeof(buf), "RF Packets: %d", rfPacketCount);
  addResult(buf);
  snprintf(buf, sizeof(buf), "Probes: %d", probeRequestCount);
  addResult(buf);
  snprintf(buf, sizeof(buf), "Heap: %dKB", ESP.getFreeHeap() / 1024);
  addResult(buf);
  currentMenu = MENU_RESULT;
}

// ================================================================
// HANDLE SELECT
// ================================================================
void handleSelect() {
  switch (currentMenu) {

    case MENU_MAIN:
      switch (menuIndex) {
        case 0: currentMenu = MENU_WIFI;     menuIndex = 0; break;
        case 1: currentMenu = MENU_BLE_SCAN; menuIndex = 0; break;
        case 2: currentMenu = MENU_RF24;     menuIndex = 0; break;
        case 3: currentMenu = MENU_PROBE;    menuIndex = 0; break;
        case 4: currentMenu = MENU_SPOOF;    menuIndex = 0; break;
        case 5: currentMenu = MENU_SD;       menuIndex = 0; break;
        case 6: drawStatusScreen(); break;
      }
      break;

    case MENU_WIFI:
      switch (menuIndex) {
        case 0: runWifiScanAll(); break;
        case 1:
          if (wifiConnected) { runDeviceDiscovery(); }
          else { showBigMessage("Connect first"); delay(900); }
          break;
        case 2: saveWifiDevices(); break;
        case 3: WiFi.disconnect(true); wifiConnected = false; break;
        case 4: currentMenu = MENU_MAIN; menuIndex = 0; break;
      }
      break;

    case MENU_WIFI_SCANLIST:
      if (wifiNetworkCount > 0) {
        nrChosenSSID = wifiNetworks[nrNetIndex].ssid;
        nrChosenOpen = wifiNetworks[nrNetIndex].open;
        if (nrChosenOpen) {
          pwBuffer[0] = '\0';
          nrConnectToChosenNetwork();
        } else {
          nrBeginPasswordEntry();
        }
      }
      break;

    case MENU_WIFI_DEVLIST:
      currentMenu = MENU_WIFI;
      menuIndex   = 0;
      break;

    case MENU_BLE_SCAN:
      switch (menuIndex) {
        case 0: runBLEScan(false); break;
        case 1: runBLEScan(true);  break;
        case 2: saveBLEScan();     break;
        case 3: currentMenu = MENU_MAIN; menuIndex = 0; break;
      }
      break;

    case MENU_RF24:
      switch (menuIndex) {
        case 0: spectrumAnalyzeRF24(); break;
        case 1: captureRF24();         break;
        case 2: replayRF24();          break;
        case 3: saveRF24();            break;
        case 4: currentMenu = MENU_MAIN; menuIndex = 0; break;
      }
      break;

    case MENU_PROBE:
      switch (menuIndex) {
        case 0: startProbeSniff(); break;
        case 1: stopProbeSniff();  break;
        case 2: saveProbes();      break;
        case 3: currentMenu = MENU_MAIN; menuIndex = 0; break;
      }
      break;

    case MENU_SPOOF:
      switch (menuIndex) {
        case 0: createSpoofProfile();   break;
        case 1: activateSpoofProfile(); break;
        case 2: deleteSpoofProfile();   break;
        case 3: currentMenu = MENU_MAIN; menuIndex = 0; break;
      }
      break;

    case MENU_SPOOF_LIST:
      currentMenu = MENU_SPOOF;
      menuIndex   = 0;
      break;

    case MENU_SD:
      switch (menuIndex) {
        case 0: runSDBrowse();     break;
        case 1: runSDCardInfo();   break;
        case 2: runSDDeleteFile(); break;
        case 3: currentMenu = MENU_MAIN; menuIndex = 0; break;
      }
      break;

    case MENU_SD_BROWSE:
      if (sdFileCount > 0 && sdFileSel < sdFileCount) {
        selectedFile = sdFiles[sdFileSel];
        runSDViewFile(selectedFile);
      }
      break;

    case MENU_RESULT:
      switch (resultContext) {
        case CTX_BLE:   saveBLEScan();  break;
        case CTX_RF24:  saveRF24();     break;
        case CTX_PROBE: saveProbes();   break;
        default: break;   // CTX_NONE: nothing to save
      }
      break;
  }
}

// ================================================================
// HANDLE BACK
// ================================================================
void handleBack() {
  menuScroll = 0;
  switch (currentMenu) {
    case MENU_MAIN:
      break;
    case MENU_WIFI:
    case MENU_BLE_SCAN:
    case MENU_RF24:
    case MENU_PROBE:
    case MENU_SPOOF:
    case MENU_SD:
      currentMenu = MENU_MAIN;
      menuIndex   = 0;
      break;
    case MENU_SPOOF_LIST:
      currentMenu = MENU_SPOOF;
      menuIndex   = 0;
      break;
    case MENU_SD_BROWSE:
      currentMenu = MENU_SD;
      menuIndex   = 0;
      break;
    case MENU_RESULT:
      currentMenu   = resultOriginMenu;
      menuIndex     = 0;
      resultContext = CTX_NONE;
      break;
    case MENU_WIFI_SCANLIST:
      currentMenu = MENU_WIFI;
      menuIndex   = 0;
      break;
    case MENU_WIFI_DEVLIST:
      currentMenu = MENU_WIFI;
      menuIndex   = 0;
      break;
    /* MENU_WIFI_PWENTRY is intentionally absent here — it handles its
     own long-press-Back-to-cancel inside handlePasswordEntryButtons()
     */
  }
}

// ================================================================
// SETUP
// ================================================================
void setup() {
  Serial.begin(115200);
  pinMode(BTN_UP,     INPUT_PULLUP);
  pinMode(BTN_DOWN,   INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);
  pinMode(BTN_BACK,   INPUT_PULLUP);

  Wire.begin(OLED_SDA, OLED_SCL);
  u8g2.begin();
  u8g2.setContrast(200);

  randomSeed(analogRead(0));

  drawSplash();
  currentMenu = MENU_MAIN;
  menuIndex   = 0;
  lastBtnTime = millis();
  drawMenu();
}

// ================================================================
// LOOP
// ================================================================
void loop() {
  bool redraw = false;

  // Password entry screen owns Up/Down/Select/Back itself (it needs
  // long-press detection), so it's handled completely separately.
  if (currentMenu == MENU_WIFI_PWENTRY) {
    if (handlePasswordEntryButtons()) redraw = true;
    if (redraw) drawMenu();
    delay(20);
    return;
  }

  if (btnPressed(BTN_UP)) {
    if (currentMenu == MENU_RESULT) {
      if (menuScroll > 0) { menuScroll--; redraw = true; }
    } else if (currentMenu == MENU_SD_BROWSE) {
      sdFileSel = (sdFileSel > 0) ? sdFileSel - 1 : sdFileCount - 1;
      redraw = true;
    } else if (currentMenu == MENU_SPOOF_LIST) {
      spoofSelectedIndex = (spoofSelectedIndex > 0) ? spoofSelectedIndex - 1 : spoofProfileCount - 1;
      redraw = true;
    } else if (currentMenu == MENU_WIFI_SCANLIST) {
      if (wifiNetworkCount > 0) {
        nrNetIndex = (nrNetIndex > 0) ? nrNetIndex - 1 : wifiNetworkCount - 1;
        nrMarqueeOffset = 0;
        nrMarqueeLastStep = millis();
        redraw = true;
      }
    } else if (currentMenu == MENU_WIFI_DEVLIST) {
      if (nrDevScroll > 0) { nrDevScroll--; redraw = true; }
    } else {
      int count = 0;
      switch (currentMenu) {
        case MENU_MAIN:     count = mainCount;  break;
        case MENU_WIFI:     count = wifiCount;  break;
        case MENU_BLE_SCAN: count = bleCount;   break;
        case MENU_RF24:     count = rf24Count;  break;
        case MENU_PROBE:    count = probeCount; break;
        case MENU_SPOOF:    count = spoofCount; break;
        case MENU_SD:       count = sdCount;    break;
        default: break;
      }
      if (count > 0) {
        menuIndex = (menuIndex > 0) ? menuIndex - 1 : count - 1;
        redraw = true;
      }
    }
  }

  if (btnPressed(BTN_DOWN)) {
    if (currentMenu == MENU_RESULT) {
      if (menuScroll < resultLineCount - 1) { menuScroll++; redraw = true; }
    } else if (currentMenu == MENU_SD_BROWSE) {
      sdFileSel = (sdFileSel < sdFileCount - 1) ? sdFileSel + 1 : 0;
      redraw = true;
    } else if (currentMenu == MENU_SPOOF_LIST) {
      spoofSelectedIndex = (spoofSelectedIndex < spoofProfileCount - 1) ? spoofSelectedIndex + 1 : 0;
      redraw = true;
    } else if (currentMenu == MENU_WIFI_SCANLIST) {
      if (wifiNetworkCount > 0) {
        nrNetIndex = (nrNetIndex < wifiNetworkCount - 1) ? nrNetIndex + 1 : 0;
        nrMarqueeOffset = 0;
        nrMarqueeLastStep = millis();
        redraw = true;
      }
    } else if (currentMenu == MENU_WIFI_DEVLIST) {
      if (nrDevScroll < max(0, nrDeviceCount - 4)) { nrDevScroll++; redraw = true; }
    } else {
      int count = 0;
      switch (currentMenu) {
        case MENU_MAIN:     count = mainCount;  break;
        case MENU_WIFI:     count = wifiCount;  break;
        case MENU_BLE_SCAN: count = bleCount;   break;
        case MENU_RF24:     count = rf24Count;  break;
        case MENU_PROBE:    count = probeCount; break;
        case MENU_SPOOF:    count = spoofCount; break;
        case MENU_SD:       count = sdCount;    break;
        default: break;
      }
      if (count > 0) {
        menuIndex = (menuIndex < count - 1) ? menuIndex + 1 : 0;
        redraw = true;
      }
    }
  }

  if (btnPressed(BTN_SELECT)) { handleSelect(); redraw = true; }
  if (btnPressed(BTN_BACK))   { handleBack();   redraw = true; }

  //the marquee crap
  if (currentMenu == MENU_WIFI_SCANLIST && wifiNetworkCount > 0) {
    String full = wifiNetworks[nrNetIndex].ssid + " " + wifiNetworks[nrNetIndex].secLabel;
    if ((int)full.length() > MARQUEE_MAX_CHARS && millis() - nrMarqueeLastStep > MARQUEE_STEP_MS) {
      nrMarqueeLastStep = millis();
      nrMarqueeOffset++;
      if (nrMarqueeOffset > (int)full.length() + 3) nrMarqueeOffset = 0;
      redraw = true;
    }
  }

  if (redraw) drawMenu();

  delay(20);
}
