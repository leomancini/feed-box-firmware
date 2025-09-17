/*
 * FeedBox Firmware - iOS-Compatible Captive Portal Version
 * 
 * Features:
 * - iOS-friendly captive portal with embedded HTML (no file system needed)
 * - EEPROM credential storage 
 * - Clean black & white flat UI design
 * - Complete captive portal redirect handling optimized for iOS
 * - Double-tap reset for factory reset
 * - LCD status display integration
 * - Friendly URLs (setup.wifi, feedbox.setup, etc.)
 * 
 * CRITICAL iOS COMPATIBILITY NOTES:
 * - iOS will NOT show captive portal if ANY response contains the word "Success"  
 * - Captive portal DETECTION URLs (/generate_204, /hotspot-detect.html, etc.) must
 *   return 200 responses with the actual captive portal content to trigger splash page
 * - Random URLs (google.com, facebook.com, etc.) should get 302 redirects to captive portal
 * - Only specific URLs (like success.txt) should return plain 200 OK responses
 * - iOS CACHES WiFi networks! It only shows captive portal on "new" networks
 * - SOLUTION: Append random 4-digit code to WiFi name each boot (FeedBox-Setup-1234)
 *   This makes iOS think it's always connecting to a new network = captive portal every time!
 * 
 * Benefits:
 * - Works reliably on ALL devices, especially iPhone/iPad
 * - Self-contained firmware (HTML embedded in code)
 * - Fast loading with minimal CSS
 * - Easy deployment and updates
 */

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

// Web server and DNS server for captive portal
ESP8266WebServer server(80);
DNSServer dnsServer;

// WiFi credentials storage
String wifiSSID = "";
String wifiPassword = "";
bool apMode = false;

// Dynamic AP name with random code (makes iOS think it's always a "new" network)
String dynamicAPName = "";

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

// Function declarations
void markBootTime();
void clearResetFlag();
bool checkDoubleTapReset();
void performFactoryReset();
void saveWiFiCredentials(String ssid, String password);
bool loadWiFiCredentials();
void clearWiFiCredentials();
void handleRoot();
void handleSave();
void handleCaptivePortalDetect();
void handleConnectTest();
void handleHotspotDetect();
void handleNotFound();
void startAPMode();
void fetchData();
void showNextPage();
void generateDynamicAPName();

// Embedded HTML for captive portal - Simplified Black & White Flat Design
const char INDEX_HTML[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <meta http-equiv="Cache-Control" content="no-store, no-cache, must-revalidate, max-age=0">
  <meta http-equiv="Pragma" content="no-cache">
  <meta http-equiv="Expires" content="0">
  <title>FeedBox WiFi Setup</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: Arial, sans-serif;
      background: white;
      color: black;
      padding: 20px;
      line-height: 1.4;
    }
    .container {
      max-width: 400px;
      margin: 0 auto;
    }
    h1 {
      text-align: center;
      margin-bottom: 30px;
      font-size: 24px;
      border-bottom: 2px solid black;
      padding-bottom: 10px;
    }
    .form-group {
      margin-bottom: 20px;
    }
    label {
      display: block;
      margin-bottom: 5px;
      font-weight: bold;
    }
    input[type="text"], input[type="password"] {
      width: 100%;
      padding: 12px;
      border: 2px solid black;
      background: white;
      font-size: 16px;
    }
    input[type="text"]:focus, input[type="password"]:focus {
      outline: none;
      border-color: #666;
    }
    .btn {
      width: 100%;
      padding: 15px;
      background: black;
      color: white;
      border: none;
      font-size: 16px;
      font-weight: bold;
      cursor: pointer;
      margin-top: 10px;
    }
    .btn:hover {
      background: #333;
    }
    .info {
      text-align: center;
      margin-top: 20px;
      padding: 15px;
      border: 1px solid black;
      font-size: 14px;
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>FeedBox WiFi Setup</h1>
    
    <form method="POST" action="/save">
      <div class="form-group">
        <label for="ssid">WiFi Network Name</label>
        <input type="text" id="ssid" name="ssid" required placeholder="Enter WiFi network name">
      </div>

      <div class="form-group">
        <label for="password">WiFi Password</label>
        <input type="password" id="password" name="password" required placeholder="Enter WiFi password">
      </div>

      <button type="submit" class="btn">Save & Connect</button>
    </form>

    <div class="info">
      Your FeedBox will save these credentials and restart to connect automatically.
    </div>
    
    <div class="info" style="margin-top: 10px; font-size: 12px; color: #666;">
      <strong>Easy URLs for this page:</strong><br>
      setup.wifi • feedbox.setup • setup.local<br><br>
      <strong>iPhone users:</strong> If above URLs don't work, try:<br>
      <strong>http://192.168.1.1</strong><br><br>
      <em>Note: WiFi network name has random numbers to ensure reliable setup on all devices.</em>
    </div>
  </div>
</body>
</html>
)=====";

const char COMPLETE_HTML[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>WiFi Setup Complete</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: Arial, sans-serif;
      background: white;
      color: black;
      padding: 20px;
      text-align: center;
      height: 100vh;
      display: flex;
      align-items: center;
      justify-content: center;
    }
    .container {
      max-width: 400px;
      border: 2px solid black;
      padding: 40px 20px;
    }
    h1 {
      font-size: 24px;
      margin-bottom: 20px;
    }
    .checkmark {
      font-size: 48px;
      margin-bottom: 20px;
    }
    p {
      margin-bottom: 15px;
      line-height: 1.5;
    }
    .small {
      font-size: 14px;
      color: #666;
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="checkmark">✓</div>
    <h1>Setup Complete!</h1>
    <p>WiFi credentials have been saved and your FeedBox is connecting.</p>
    <p class="small">The device will restart automatically and connect to your network.</p>
  </div>
</body>
</html>
)=====";

const char ERROR_HTML[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>WiFi Setup Error</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: Arial, sans-serif;
      background: white;
      color: black;
      padding: 20px;
      text-align: center;
      height: 100vh;
      display: flex;
      align-items: center;
      justify-content: center;
    }
    .container {
      max-width: 400px;
      border: 2px solid black;
      padding: 40px 20px;
    }
    h1 {
      font-size: 24px;
      margin-bottom: 20px;
    }
    .error-icon {
      font-size: 48px;
      margin-bottom: 20px;
    }
    p {
      margin-bottom: 15px;
      line-height: 1.5;
    }
    .small {
      font-size: 14px;
      color: #666;
    }
    a {
      color: black;
      text-decoration: underline;
    }
    a:hover {
      text-decoration: none;
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="error-icon">✗</div>
    <h1>Setup Error</h1>
    <p>Could not save WiFi settings. Please check that all fields are filled in correctly.</p>
    <p class="small"><a href="/">Go back to setup</a> and try again.</p>
  </div>
</body>
</html>
)=====";

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

void generateDynamicAPName() {
  // Generate a random 4-digit code (1000-9999) to make iOS think it's a "new" network
  // This forces iOS to show the captive portal every time instead of caching the network
  randomSeed(millis() + ESP.getCycleCount()); // Better randomization
  int randomCode = random(1000, 10000); // 1000-9999
  
  dynamicAPName = String(AP_SSID) + "-" + String(randomCode);
  
  Serial.println("Generated dynamic AP name: " + dynamicAPName);
  Serial.println("Random code: " + String(randomCode) + " (forces iOS to see as 'new' network)");
}


// ==== WEB SERVER HANDLERS ====
void handleRoot() {
  Serial.println("=== SETUP PAGE REQUEST ===");
  Serial.println("Serving embedded setup page to client");
  
  // Add captive portal detection headers
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleSave() {
  String ssid = server.arg("ssid");
  String password = server.arg("password");
  
  if (ssid.length() > 0) {
    saveWiFiCredentials(ssid, password);
    server.send_P(200, "text/html", COMPLETE_HTML);
    
    delay(2000);
    ESP.restart();
  } else {
    server.send_P(400, "text/html", ERROR_HTML);
  }
}

// ==== CAPTIVE PORTAL DETECTION HANDLERS ====
// CRITICAL: iOS captive portal detection URLs must return 200 responses with content
// to trigger the splash page, NOT 302 redirects!
void handleCaptivePortalDetect() {
  Serial.println("Android/Google captive portal detect - returning captive portal page");
  
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  
  // Return our setup page directly (200 response triggers captive portal detection)
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleConnectTest() {
  Serial.println("Windows connect test - returning captive portal page");
  
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  
  // Return our setup page directly (200 response triggers captive portal detection)
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleHotspotDetect() {
  Serial.println("Apple/iOS hotspot detect - returning captive portal page");
  
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");  
  server.sendHeader("Expires", "-1");
  
  // Return our setup page directly (200 response triggers captive portal detection)
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleNotFound() {
  String host = server.hostHeader();
  String uri = server.uri();
  String redirectURL = "http://" + AP_IP.toString() + "/";
  
  Serial.println("=== CAPTIVE PORTAL REQUEST ===");
  Serial.println("Host: " + host + " | URI: " + uri);
  Serial.println("Redirecting to: " + redirectURL);
  Serial.println("==============================");
  
  // Check if this is already requesting our IP - if so, serve the page directly
  if (host == AP_IP.toString() || host == "192.168.1.1") {
    Serial.println("Request is already for our IP, serving setup page directly");
    handleRoot();
    return;
  }
  
  // For random URLs (google.com, facebook.com, etc.), send 302 redirect to captive portal
  // This is different from detection URLs which need 200 responses to trigger splash page
  server.sendHeader("Location", redirectURL, true);
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache"); 
  server.sendHeader("Expires", "-1");
  
  server.send(302, "text/plain", "");  // Empty body for clean redirect
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
    // Force AP mode after factory reset (startAPMode will generate new random name)
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
  
  // Generate a unique AP name with random code for iOS captive portal detection
  generateDynamicAPName();
  
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);
  WiFi.softAP(dynamicAPName.c_str(), AP_PASSWORD); // Use dynamic name
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi Setup Mode");
  lcd.setCursor(0, 1);
  lcd.print("1.Connect to:");
  lcd.setCursor(0, 2);
  // Truncate the name if it's too long for LCD (20 characters max)
  String displayName = dynamicAPName;
  if (displayName.length() > LCD_COLS) {
    displayName = displayName.substring(0, LCD_COLS);
  }
  lcd.print(displayName);
  lcd.setCursor(0, 3);
  lcd.print("2.Go to setup.wifi");
  
  Serial.print("AP started. Connect to: ");
  Serial.println(dynamicAPName);
  Serial.print("IP: ");
  Serial.println(AP_IP);
  
  // Set up DNS server for captive portal - redirect ALL DNS queries to our IP
  dnsServer.setTTL(0); // Don't cache DNS responses
  dnsServer.start(53, "*", AP_IP);
  Serial.println("DNS server started - redirecting ALL domains to " + AP_IP.toString());
  Serial.println("Users can access setup page with any of these URLs:");
  Serial.println("  • setup.wifi");
  Serial.println("  • feedbox.setup");  
  Serial.println("  • setup.local");
  Serial.println("  • wifi.setup");
  Serial.println("  • go.setup");
  Serial.println("  • Or any other domain name!");
  
  // Set up web server routes with embedded HTML
  // CRITICAL iOS CAPTIVE PORTAL BEHAVIOR:
  // - iOS will NOT show captive portal if ANY response contains the word "Success"
  // - Captive portal DETECTION URLs must return 200 responses with content to trigger splash page
  // - Random URLs (like google.com) should get 302 redirects to the captive portal  
  // - Only specific URLs like success.txt should return plain 200 OK responses
  
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/test", [](){
    server.send(200, "text/plain", "FeedBox test page works! AP IP: " + AP_IP.toString());
  });
  
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
  
  // Additional routes from best practices
  server.on("/wpad.dat", [](){
    server.send(404, "text/plain", "Not Found");
  }); // Web Proxy Auto-Discovery - prevents Windows from panicking ESP32
  
  server.on("/canonical.html", handleHotspotDetect); // Ubuntu captive portal detection
  
  // CRITICAL: success.txt must return 200 OK, not redirect (Firefox expects this)
  server.on("/success.txt", [](){
    Serial.println("success.txt requested - returning 200 OK");
    server.send(200, "text/plain", "OK"); // Note: avoid word "success" for iOS compatibility
  });
  
  // Additional captive portal detection routes
  server.on("/msftconnecttest.com/connecttest.txt", handleConnectTest); // Microsoft connectivity test
  server.on("/msftncsi.com/ncsi.txt", handleConnectTest); // Microsoft NCSI
  server.on("/www.msftconnecttest.com/connecttest.txt", handleConnectTest); // Microsoft with www
  server.on("/ipv6.msftconnecttest.com/connecttest.txt", handleConnectTest); // Microsoft IPv6
  server.on("/connectivitycheck.gstatic.com/generate_204", handleCaptivePortalDetect); // Google connectivity
  server.on("/www.google.com/generate_204", handleCaptivePortalDetect); // Google alternative
  server.on("/clients3.google.com/generate_204", handleCaptivePortalDetect); // Google clients
  server.on("/connectivitycheck.android.com/generate_204", handleCaptivePortalDetect); // Android
  server.on("/captive.apple.com", handleHotspotDetect); // Apple captive portal
  server.on("/www.apple.com/library/test/success.html", handleHotspotDetect); // Apple test page
  server.on("/gsp1.apple.com/pep/gcc", handleHotspotDetect); // Apple GeoServices captive portal
  server.on("/gspe1.apple.com/pep/gcc", handleHotspotDetect); // Apple GeoServices alternative
  server.on("/apple.com/library/test/success.html", handleHotspotDetect); // Apple without www
  
  // Favicon - return 404 to prevent errors
  server.on("/favicon.ico", [](){
    server.send(404, "text/plain", "Not Found");
  });
  
  // Catch-all for any unhandled requests - this is the KEY for captive portal redirection
  server.onNotFound(handleNotFound);
  
  server.begin();
  Serial.println("Web server started on port 80");
  Serial.println("Captive portal is active with embedded HTML!");
  Serial.println("Connect to '" + dynamicAPName + "' and visit:");
  Serial.println("  • Any website (auto-redirect)");
  Serial.println("  • setup.wifi (friendly URL)");
  Serial.println("");
  Serial.println("iOS CAPTIVE PORTAL TIP:");
  Serial.println("The random code in the WiFi name forces iOS to show");
  Serial.println("the captive portal every time (avoids network caching).");
}

void loop() {
  unsigned long now = millis();
  
  if (apMode) {
    // Handle DNS and web server requests FREQUENTLY for good captive portal performance
    dnsServer.processNextRequest();
    server.handleClient();
    
    // Process DNS requests multiple times per loop iteration for better responsiveness
    dnsServer.processNextRequest();
    
    // Debug: Show we're processing requests (with more frequent DNS debugging)
    static unsigned long lastDebugPrint = 0;
    static unsigned long lastDNSDebug = 0;
    
    if (now - lastDebugPrint > 30000) {
      Serial.println("[DEBUG] AP Mode active - processing DNS and web requests...");
      Serial.println("AP IP: " + AP_IP.toString() + " | SSID: " + dynamicAPName);
      Serial.println("DNS Server active - redirecting all domains to our IP");
      Serial.println("Random WiFi name ensures iOS sees this as a 'new' network");
      Serial.println("If iPhone shows 'can't open page', try these steps:");
      Serial.println("  1. Turn WiFi off/on");
      Serial.println("  2. Try http://192.168.1.1 directly");
      Serial.println("  3. Clear Safari cache");
      Serial.println("  4. Try 'Forget Network' and reconnect");
      lastDebugPrint = now;
    }
    
    // More frequent DNS debugging for troubleshooting
    if (now - lastDNSDebug > 5000) {
      Serial.println("[DNS] Listening for DNS requests on port 53...");
      lastDNSDebug = now;
    }
    
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