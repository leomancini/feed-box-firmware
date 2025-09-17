#ifndef CONFIG_H
#define CONFIG_H

// Include device-specific configuration
#include "device_config.h"

// ==== NETWORK CONFIGURATION ====
// WiFi credentials are configured via AP mode web interface
const char* API_BASE_URL = "http://feed-box-api.noshado.ws/devices/";

// AP MODE CONFIGURATION
const char* AP_SSID = "FeedBox-Setup";
const char* AP_PASSWORD = ""; // Open network (no password)
const IPAddress AP_IP(192, 168, 1, 1);
const IPAddress AP_GATEWAY(192, 168, 1, 1);
const IPAddress AP_SUBNET(255, 255, 255, 0);
const int WEB_SERVER_PORT = 80;

// LCD CONFIGURATION
const int LCD_COLS = 20;
const int LCD_ROWS = 4;

// LCD WIRING to ESP8266 via I2C
// LCD VCC → 3V3
// LCD GND → GND
// LCD SDA → D2
// LCD SCL → D1

// TIMING CONFIGURATION
const unsigned long FETCH_INTERVAL_MS = 15UL * 60UL * 1000UL; // 15 minutes
const unsigned long DEFAULT_PAGE_DURATION_MS = 10000; // 10 seconds

// MEMORY CONFIGURATION
const int MAX_PAGES = 100;
const size_t JSON_BUFFER_SIZE = 12 * 1024; // 12KB for JSON parsing

// API CONFIGURATION
const int DEFAULT_PAGE_DURATION_SEC = 10; // fallback if API doesn't specify duration

// CREDENTIAL STORAGE CONFIGURATION
const int EEPROM_SIZE = 512;
const int SSID_ADDR = 0;
const int PASSWORD_ADDR = 100;
const int CREDENTIALS_SET_ADDR = 200;
const int BOOT_TIME_ADDR = 210; // Store last boot timestamp (4 bytes)
const int RESET_FLAG_ADDR = 220; // Double-tap reset detection flag
const int MAX_SSID_LENGTH = 32;
const int MAX_PASSWORD_LENGTH = 64;
const byte CREDENTIALS_SET_FLAG = 0xAA;
const byte RESET_DETECTION_FLAG = 0xBB;

// CONNECTION TIMEOUT SETTINGS
const unsigned long WIFI_CONNECT_TIMEOUT = 20000; // 20 seconds
const unsigned long AP_MODE_TIMEOUT = 300000; // 5 minutes in AP mode before retry

// DOUBLE-TAP RESET CONFIGURATION
const unsigned long DOUBLETAP_WINDOW_MS = 3000; // 3 seconds window for double-tap detection
const unsigned long RESET_DISPLAY_TIME_MS = 2000; // Time to show reset message on LCD

#endif // CONFIG_H
