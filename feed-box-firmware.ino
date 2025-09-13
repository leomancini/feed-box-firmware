#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <hd44780.h>                      
#include <hd44780ioClass/hd44780_I2Cexp.h>
#include "config.h"

hd44780_I2Cexp lcd;

// Web server and DNS server for AP mode
ESP8266WebServer server(WEB_SERVER_PORT);
DNSServer dnsServer;

// WiFi credentials storage
String wifiSSID = "";
String wifiPassword = "";
bool apMode = false;

// LCD dimensions defined in config.h

// MAX_PAGES defined in config.h
String pages[MAX_PAGES][LCD_ROWS]; 
int pageDurations[MAX_PAGES]; // per-page display durations in seconds
int totalPages = 0;
int currentPage = 0;

unsigned long lastPageSwitch   = 0;
unsigned long currentPageInterval = 10000; // updated dynamically

unsigned long lastFetch       = 0;
// fetchInterval defined in config.h as FETCH_INTERVAL_MS

String apiURL;

// ==== CREDENTIAL STORAGE FUNCTIONS ====
void saveWiFiCredentials(String ssid, String password) {
  EEPROM.begin(EEPROM_SIZE);
  
  // Clear the areas first
  for (int i = 0; i < MAX_SSID_LENGTH; i++) {
    EEPROM.write(SSID_ADDR + i, 0);
  }
  for (int i = 0; i < MAX_PASSWORD_LENGTH; i++) {
    EEPROM.write(PASSWORD_ADDR + i, 0);
  }
  
  // Write SSID
  for (int i = 0; i < ssid.length() && i < MAX_SSID_LENGTH - 1; i++) {
    EEPROM.write(SSID_ADDR + i, ssid[i]);
  }
  
  // Write Password
  for (int i = 0; i < password.length() && i < MAX_PASSWORD_LENGTH - 1; i++) {
    EEPROM.write(PASSWORD_ADDR + i, password[i]);
  }
  
  // Set flag that credentials are stored
  EEPROM.write(CREDENTIALS_SET_ADDR, CREDENTIALS_SET_FLAG);
  
  EEPROM.commit();
  Serial.println("WiFi credentials saved to EEPROM");
}

bool loadWiFiCredentials() {
  EEPROM.begin(EEPROM_SIZE);
  
  // Check if credentials are set
  if (EEPROM.read(CREDENTIALS_SET_ADDR) != CREDENTIALS_SET_FLAG) {
    Serial.println("No stored credentials found");
    return false;
  }
  
  // Read SSID
  wifiSSID = "";
  for (int i = 0; i < MAX_SSID_LENGTH; i++) {
    byte c = EEPROM.read(SSID_ADDR + i);
    if (c == 0) break;
    wifiSSID += char(c);
  }
  
  // Read Password
  wifiPassword = "";
  for (int i = 0; i < MAX_PASSWORD_LENGTH; i++) {
    byte c = EEPROM.read(PASSWORD_ADDR + i);
    if (c == 0) break;
    wifiPassword += char(c);
  }
  
  Serial.println("Loaded credentials from EEPROM");
  Serial.println("SSID: " + wifiSSID);
  return true;
}

void clearWiFiCredentials() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.write(CREDENTIALS_SET_ADDR, 0);
  EEPROM.commit();
  Serial.println("WiFi credentials cleared");
}

// ==== DOUBLE-TAP RESET DETECTION FUNCTIONS ====
bool checkDoubleTapReset() {
  EEPROM.begin(EEPROM_SIZE);
  
  unsigned long currentBootTime = millis();
  unsigned long lastBootTime = 0;
  
  // Read last boot time from EEPROM (4 bytes)
  lastBootTime = EEPROM.read(BOOT_TIME_ADDR) |
                (EEPROM.read(BOOT_TIME_ADDR + 1) << 8) |
                (EEPROM.read(BOOT_TIME_ADDR + 2) << 16) |
                (EEPROM.read(BOOT_TIME_ADDR + 3) << 24);
  
  // Check if reset flag is set (indicates we're checking for double-tap)
  bool resetFlagSet = (EEPROM.read(RESET_FLAG_ADDR) == RESET_DETECTION_FLAG);
  
  // Store current boot time
  markBootTime();
  
  // On first boot or after long time, just set the flag and return false
  if (!resetFlagSet || lastBootTime == 0 || lastBootTime == 0xFFFFFFFF) {
    EEPROM.write(RESET_FLAG_ADDR, RESET_DETECTION_FLAG);
    EEPROM.commit();
    Serial.println("First boot or timeout - reset detection armed");
    return false;
  }
  
  // Check if this boot happened within the double-tap window
  // Note: We use ESP.getCycleCount() and system time for better accuracy
  unsigned long systemUptime = ESP.getCycleCount() / ESP.getCpuFreqMHz() / 1000; // Convert to milliseconds
  
  // If we get here within the window, it's likely a double-tap reset
  if (systemUptime < DOUBLETAP_WINDOW_MS) {
    Serial.println("Double-tap reset detected!");
    // Clear the reset flag so we don't trigger again
    EEPROM.write(RESET_FLAG_ADDR, 0);
    EEPROM.commit();
    return true;
  }
  
  // Reset the flag for next potential double-tap
  EEPROM.write(RESET_FLAG_ADDR, RESET_DETECTION_FLAG);
  EEPROM.commit();
  Serial.println("Normal boot - reset detection reset");
  return false;
}

void markBootTime() {
  EEPROM.begin(EEPROM_SIZE);
  
  // Use ESP.getCycleCount() for a more accurate timestamp
  unsigned long bootTime = ESP.getCycleCount() / ESP.getCpuFreqMHz() / 1000;
  
  // Store 4-byte timestamp in EEPROM
  EEPROM.write(BOOT_TIME_ADDR, bootTime & 0xFF);
  EEPROM.write(BOOT_TIME_ADDR + 1, (bootTime >> 8) & 0xFF);
  EEPROM.write(BOOT_TIME_ADDR + 2, (bootTime >> 16) & 0xFF);
  EEPROM.write(BOOT_TIME_ADDR + 3, (bootTime >> 24) & 0xFF);
  
  EEPROM.commit();
}

void clearResetFlag() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.write(RESET_FLAG_ADDR, 0);
  EEPROM.commit();
}

void performFactoryReset() {
  Serial.println("=== FACTORY RESET TRIGGERED ===");
  
  // Clear WiFi credentials
  clearWiFiCredentials();
  clearResetFlag();
  
  // Show reset message on LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("FACTORY RESET!");
  lcd.setCursor(0, 1);
  lcd.print("WiFi cleared");
  lcd.setCursor(0, 2);
  lcd.print("Starting setup...");
  
  delay(RESET_DISPLAY_TIME_MS);
  
  Serial.println("Factory reset complete - entering AP mode");
}

// ==== WEB SERVER HANDLERS ====
void handleRoot() {
  // Add captive portal detection headers
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  
  String html = "<!DOCTYPE html><html><head>"
                "<title>FeedBox WiFi Setup</title>"
                "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                "<style>"
                "body{font-family:Arial,sans-serif;margin:40px;background:#f0f0f0;}"
                ".container{max-width:400px;margin:0 auto;background:white;padding:20px;border-radius:8px;box-shadow:0 2px 10px rgba(0,0,0,0.1);}"
                "h1{color:#333;text-align:center;margin-bottom:30px;}"
                "input[type=text],input[type=password]{width:100%;padding:12px;margin:8px 0;box-sizing:border-box;border:2px solid #ddd;border-radius:4px;}"
                "input[type=submit]{background:#007cba;color:white;padding:14px 20px;margin:8px 0;border:none;border-radius:4px;cursor:pointer;width:100%;font-size:16px;}"
                "input[type=submit]:hover{background:#005a87;}"
                ".status{padding:10px;margin:10px 0;border-radius:4px;text-align:center;}"
                ".info{background:#e7f3ff;color:#0056b3;border:1px solid #b6d4fe;}"
                "</style></head><body>"
                "<div class='container'>"
                "<h1>🖥️ FeedBox Setup</h1>"
                "<div class='status info'>Connect to your WiFi network to continue setup</div>"
                "<form action='/save' method='post'>"
                "<label for='ssid'>WiFi Network Name (SSID):</label>"
                "<input type='text' id='ssid' name='ssid' required>"
                "<label for='password'>WiFi Password:</label>"
                "<input type='password' id='password' name='password' required>"
                "<input type='submit' value='Save & Connect'>"
                "</form>"
                "<p style='text-align:center;color:#666;font-size:14px;margin-top:20px;'>"
                "Device will restart and connect to your network</p>"
                "</div></body></html>";
  server.send(200, "text/html", html);
}

// ==== CAPTIVE PORTAL DETECTION HANDLERS ====
void handleCaptivePortalDetect() {
  // Respond to various captive portal detection requests
  // This triggers the captive portal popup on most devices
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.send(204, "text/plain", ""); // No content response triggers captive portal
}

void handleConnectTest() {
  // Windows captive portal detection
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.send(200, "text/plain", "Microsoft Connect Test");
}

void handleHotspotDetect() {
  // Apple captive portal detection - redirect to setup page
  String redirectURL = "http://" + AP_IP.toString() + "/";
  server.sendHeader("Location", redirectURL);
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.send(302, "text/html", 
              "<!DOCTYPE html><html><head><title>Setup</title></head>"
              "<body><h1>Redirecting to FeedBox Setup</h1>"
              "<script>window.location.href='" + redirectURL + "';</script></body></html>");
}

void handleSave() {
  String ssid = server.arg("ssid");
  String password = server.arg("password");
  
  if (ssid.length() > 0) {
    saveWiFiCredentials(ssid, password);
    
    String html = "<!DOCTYPE html><html><head>"
                  "<title>FeedBox WiFi Setup</title>"
                  "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                  "<meta http-equiv='refresh' content='10;url=/'>"
                  "<style>body{font-family:Arial,sans-serif;margin:40px;background:#f0f0f0;text-align:center;}"
                  ".container{max-width:400px;margin:0 auto;background:white;padding:20px;border-radius:8px;box-shadow:0 2px 10px rgba(0,0,0,0.1);}"
                  ".success{background:#d4edda;color:#155724;border:1px solid #c3e6cb;padding:15px;border-radius:4px;margin:20px 0;}"
                  "</style></head><body>"
                  "<div class='container'>"
                  "<h1>✅ Settings Saved!</h1>"
                  "<div class='success'>WiFi credentials have been saved.<br>Device will restart in a few seconds...</div>"
                  "<p>If connection fails, the device will return to setup mode.</p>"
                  "</div></body></html>";
    server.send(200, "text/html", html);
    
    delay(2000);
    ESP.restart();
  } else {
    server.send(400, "text/html", "Invalid SSID");
  }
}

void handleNotFound() {
  // Enhanced captive portal behavior
  String host = server.hostHeader();
  
  // If request is not for our IP, redirect to captive portal
  if (host != AP_IP.toString()) {
    String redirectURL = "http://" + AP_IP.toString() + "/";
    server.sendHeader("Location", redirectURL);
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");
    server.send(302, "text/plain", "Redirecting to setup page");
    return;
  }
  
  // For requests to our IP but unknown paths, serve the setup page directly
  handleRoot();
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== FeedBox Starting ===");

  apiURL = String(API_BASE_URL) + DEVICE_SERIAL_NUMBER + "/data";

  // Initialize LCD
  int status = lcd.begin(LCD_COLS, LCD_ROWS);
  if (status) {
    Serial.print("LCD init failed, status: ");
    Serial.println(status);
    while (1);
  }
  lcd.clear();

  // Check for double-tap reset (must be done early in boot process)
  if (checkDoubleTapReset()) {
    performFactoryReset();
    // Force AP mode after factory reset
    startAPMode();
    return; // Exit setup, continue in AP mode
  }

  // Try to load WiFi credentials from EEPROM
  bool hasStoredCredentials = loadWiFiCredentials();
  
  if (hasStoredCredentials) {
    lcd.print("Connecting to WiFi...");
    Serial.println("Attempting connection with stored credentials");
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
    
    unsigned long startAttemptTime = millis();
    
    while (WiFi.status() != WL_CONNECTED && 
           millis() - startAttemptTime < WIFI_CONNECT_TIMEOUT) {
      delay(500);
      Serial.print(".");
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi connected!");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      
      lcd.clear();
      lcd.print("WiFi Connected!");
      lcd.setCursor(0, 1);
      lcd.print(WiFi.localIP());
      delay(2000);
      
      // Clear the reset detection flag since we're now in normal operation
      clearResetFlag();
      
      // Fetch initial data and start normal operation
      fetchData();
      lastFetch = millis();
      return; // Exit setup, continue with normal operation
    } else {
      Serial.println("\nFailed to connect to stored WiFi");
      lcd.clear();
      lcd.print("WiFi failed!");
      lcd.setCursor(0, 1);
      lcd.print("Starting setup...");
      delay(2000);
    }
  } else {
    lcd.print("First time setup...");
    delay(2000);
  }
  
  // Start AP mode for configuration
  startAPMode();
}

void startAPMode() {
  Serial.println("Starting AP mode for WiFi configuration");
  apMode = true;
  
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  
  lcd.clear();
  lcd.print("WiFi Setup Mode");
  lcd.setCursor(0, 1);
  lcd.print("Network: ");
  lcd.print(AP_SSID);
  lcd.setCursor(0, 2);
  lcd.print("Open Network (no pwd)");
  lcd.setCursor(0, 3);
  lcd.print("IP: ");
  lcd.print(AP_IP);
  
  Serial.print("AP started. Connect to: ");
  Serial.println(AP_SSID);
  Serial.println("Open network (no password required)");
  Serial.print("IP: ");
  Serial.println(AP_IP);
  
  // Set up DNS server for captive portal
  dnsServer.start(53, "*", AP_IP);
  
  // Set up web server routes
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  
  // Captive portal detection routes for different operating systems
  server.on("/generate_204", handleCaptivePortalDetect); // Android captive portal detection
  server.on("/gen_204", handleCaptivePortalDetect); // Android alternative
  server.on("/library/test/success.html", handleHotspotDetect); // iOS/macOS captive portal detection
  server.on("/hotspot-detect.html", handleHotspotDetect); // iOS/macOS alternative
  server.on("/connecttest.txt", handleConnectTest); // Windows captive portal detection
  server.on("/redirect", handleHotspotDetect); // Generic redirect
  server.on("/mobile/status.php", handleHotspotDetect); // Some Android devices
  server.on("/check_network_status.txt", handleConnectTest); // Alternative Windows detection
  server.on("/ncsi.txt", handleConnectTest); // Windows NCSI detection
  server.on("/fwlink", handleHotspotDetect); // Microsoft redirect detection
  // Catch-all for any unhandled requests
  server.onNotFound(handleNotFound);
  
  server.begin();
  Serial.println("Web server started");
}

void loop() {
  unsigned long now = millis();
  
  if (apMode) {
    // Handle web server requests and DNS in AP mode
    dnsServer.processNextRequest();
    server.handleClient();
    
    // Optional: Add timeout to retry connection attempt
    // This allows the device to periodically try connecting again
    static unsigned long lastRetryAttempt = 0;
    if (now - lastRetryAttempt > AP_MODE_TIMEOUT) {
      lastRetryAttempt = now;
      Serial.println("Retrying connection with stored credentials...");
      if (loadWiFiCredentials()) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
        delay(10000); // Wait 10 seconds for connection
        if (WiFi.status() == WL_CONNECTED) {
          Serial.println("Successfully connected! Switching to normal mode...");
          apMode = false;
          server.stop();
          dnsServer.stop();
          clearResetFlag(); // Clear reset detection flag
          lcd.clear();
          lcd.print("WiFi Connected!");
          lcd.setCursor(0, 1);
          lcd.print(WiFi.localIP());
          delay(2000);
          fetchData();
          lastFetch = millis();
        }
      }
    }
    return;
  }

  // Normal operation mode - display pages and fetch data
  if (now - lastPageSwitch > currentPageInterval) {
    lastPageSwitch = now;
    showNextPage();
  }

  if (now - lastFetch > FETCH_INTERVAL_MS) {
    lastFetch = now;
    fetchData();
  }
}

void fetchData() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected! Attempting reconnection...");
    if (loadWiFiCredentials()) {
      WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
    } else {
      // No credentials available, switch to AP mode
      Serial.println("No WiFi credentials, switching to AP mode");
      startAPMode();
    }
    return;
  }

  WiFiClient client;
  HTTPClient http;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Getting latest data");
  Serial.printf("Requesting: %s\n", apiURL.c_str());

  if (!http.begin(client, apiURL)) {
    Serial.println("HTTP begin failed");
    return;
  }

  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    Serial.println("API response received.");

    // allocate buffer (size defined in config.h)
    DynamicJsonDocument doc(JSON_BUFFER_SIZE);

    DeserializationError error = deserializeJson(doc, http.getStream());
    if (error) {
      Serial.print("JSON parse error: ");
      Serial.println(error.c_str());
      http.end();
      return;
    }

    // detect root type
    JsonArray arr;

    if (doc.is<JsonArray>()) {
      arr = doc.as<JsonArray>();
      Serial.printf("Root is array with %d items\n", arr.size());
    } else if (doc.is<JsonObject>()) {
      Serial.println("Root is object, dumping keys:");
      serializeJsonPretty(doc, Serial);
      // try common field name "data"
      if (doc.containsKey("data")) {
        arr = doc["data"].as<JsonArray>();
        Serial.printf("Found 'data' array with %d items\n", arr.size());
      }
    } else {
      Serial.println("Unknown JSON root type");
    }

    totalPages = min((int)arr.size(), MAX_PAGES);
    for (int i = 0; i < totalPages; i++) {
      JsonObject pageObj = arr[i];
      JsonArray content = pageObj["c"].as<JsonArray>();
      int seconds = pageObj["s"] | DEFAULT_PAGE_DURATION_SEC; // default from config.h if missing

      for (int row = 0; row < LCD_ROWS; row++) {
        if (row < content.size()) {
          pages[i][row] = content[row].as<String>();
          pages[i][row].replace("\n", " ");
        } else {
          pages[i][row] = "";
        }
      }
      pageDurations[i] = seconds;
    }

    if (totalPages > 0) {
      currentPage = 0;
      lastPageSwitch = millis();
      currentPageInterval = pageDurations[0] * 1000UL;
      Serial.printf("Loaded %d pages\n", totalPages);
    } else {
      Serial.println("No pages found in JSON response");
    }

  } else {
    Serial.printf("HTTP error: %d\n", httpCode);
  }

  http.end();
}

void showNextPage() {
  if (totalPages == 0) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("No data available");
    return;
  }

  lcd.clear();
  for (int row = 0; row < LCD_ROWS; row++) {
    lcd.setCursor(0, row);
    String line = pages[currentPage][row];
    lcd.print(line);

    // pad line to clear old characters
    if (line.length() < LCD_COLS) {
      for (int i = line.length(); i < LCD_COLS; i++) {
        lcd.print(' ');
      }
    }
  }

  Serial.printf("Displayed page %d (duration %ds)\n", 
                currentPage, pageDurations[currentPage]);

  currentPage++;
  if (currentPage >= totalPages) {
    currentPage = 0;
  }

  currentPageInterval = pageDurations[currentPage] * 1000UL;
}