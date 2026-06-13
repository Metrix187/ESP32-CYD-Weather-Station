/*
 * ESP32 Weather Station (CYD - Cheap Yellow Display)
 * 
 * This project displays current weather information on a 2.8" TFT Touchscreen (ESP32-2432S028R).
 * 
 * Features:
 * - WiFi connectivity to fetch weather data
 * - OpenWeatherMap API integration
 * - Touchscreen interface for city entry and calibration
 * - SD Card support for background images
 * - On-screen keyboard for changing cities without recompiling
 * 
 * HARDWARE:
 * - Board: ESP32-2432S028R (commonly known as "Cheap Yellow Display" or CYD)
 * - Display: 2.8" ILI9341 TFT with XPT2046 Touchscreen
 * - SD Card: Micro SD card formatted FAT32 (for background images)
 * 
 * LIBRARIES REQUIRED (Install via Arduino Library Manager):
 * - TFT_eSPI by Bodmer
 * - XPT2046_Touchscreen by Paul Stoffregen
 * - ArduinoJson by Benoit Blanchon
 * - TJpg_Decoder by Bodmer
 * 
 * SETUP:
 * 1. Configure your WiFi credentials and OpenWeatherMap API key below.
 * 2. Copy the images from the "sd card files" folder to the root of a Micro SD card.
 * 3. Insert the SD card into the CYD board.
 * 4. Upload this sketch to the ESP32.
 * 
 * NOTE: Tested with the "diymalls 2.8" ESP32-2432S028R ESP32 from Amazon.
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include "TFT_eSPI.h"
#include <Preferences.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <FS.h>
using fs::FS; // compatibility alias required by some WebServer versions
#include <SD.h>
#include <TJpg_Decoder.h>

// =========================================================================
//                          USER CONFIGURATION
// =========================================================================
// Credentials live in a separate, gitignored file so they never end up in
// version control. Copy "secrets.example.h" to "secrets.h" and fill it in.
#include "secrets.h"

// WiFi Configuration (values defined in secrets.h)
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// OpenWeatherMap API Configuration (values defined in secrets.h)
String openWeatherMapApiKey = OWM_API_KEY;
String city = DEFAULT_CITY_NAME;               // Format: City or City,State or City,State,Country

// Display Configuration
const int UPDATE_INTERVAL = 600000;            // Weather refresh interval, ms (10 minutes)
// =========================================================================

// Global objects
TFT_eSPI tft = TFT_eSPI();
String weatherData;
unsigned long lastWeatherUpdateMs = 0;
const String DEFAULT_CITY = DEFAULT_CITY_NAME; // Default fallback city

// =========================================================================
//                        HARDWARE PIN DEFINITIONS
// =========================================================================
// These pins are specific to the ESP32-2432S028R (CYD) board.
// 
// TFT_eSPI Library Configuration (User_Setup.h or User_Setup_Select.h):
// Ensure your TFT_eSPI library is configured for ILI9341 and these pins:
// #define ILI9341_DRIVER
// #define TFT_MISO 12
// #define TFT_MOSI 13
// #define TFT_SCLK 14
// #define TFT_CS   15
// #define TFT_DC   2
// #define TFT_RST  -1  // Connected to RST
// #define TFT_BL   21  // Backlight
// #define TOUCH_CS 33  // (We use XPT2046_Touchscreen lib instead of TFT_eSPI touch)
// =========================================================================

Preferences prefs;
bool lastWifiConnected = false;
bool sdAvailable = false;

// WiFi reconnect handling (non-blocking, used by loop() when the link drops)
unsigned long lastReconnectAttemptMs = 0;
const unsigned long WIFI_RECONNECT_INTERVAL_MS = 10000; // retry every 10s while down

// SD Card (HSPI) Pins for CYD
static const int SD_MISO_PIN = 19;
static const int SD_MOSI_PIN = 23;
static const int SD_SCK_PIN  = 18;
static const int SD_CS_PIN   = 5;

// Background Image Paths (day + night variants, each with a case-insensitive fallback)
static const char* BG_DAY_UPPER   = "/day-aero-fit.JPG";
static const char* BG_DAY_LOWER   = "/day-aero-fit.jpg";
static const char* BG_NIGHT_UPPER = "/night-aero-fit.JPG";
static const char* BG_NIGHT_LOWER = "/night-aero-fit.jpg";

// ---------------------------- Touch & UI State ----------------------------
// Dedicated touch pins for XPT2046 on this board
static const int TOUCH_CS_PIN   = 33;
static const int TOUCH_IRQ_PIN  = 36; // can be -1 if not wired
static const int TOUCH_MISO_PIN = 39;
static const int TOUCH_MOSI_PIN = 32;
static const int TOUCH_SCLK_PIN = 25;

// Small post-calibration pixel offsets to fine‑tune alignment
// If touches register to the RIGHT of your finger, make TOUCH_X_OFFSET negative.
// If touches register to the LEFT, make TOUCH_X_OFFSET positive.
// Same idea for Y (positive moves detection DOWN, negative moves UP).
static const int16_t TOUCH_X_OFFSET = -10;  // tweak as needed
static const int16_t TOUCH_Y_OFFSET = 0;

SPIClass touchSPI(VSPI);
SPIClass sdSPI(HSPI);
XPT2046_Touchscreen touch(TOUCH_CS_PIN, TOUCH_IRQ_PIN);

struct TouchCalibration {
  uint16_t xMin;
  uint16_t xMax;
  uint16_t yMin;
  uint16_t yMax;
  bool invertX;
  bool invertY;
  bool swapXY;
};
TouchCalibration touchCal = {220, 3850, 220, 3850, false, false, false}; // neutral defaults; calibration determines swap/invert

enum Screen {
  SCREEN_WEATHER = 0,
  SCREEN_KEYBOARD = 1
};

Screen currentScreen = SCREEN_WEATHER;

struct TouchRect {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
  bool contains(int16_t tx, int16_t ty) const {
    return tx >= x && tx < (x + w) && ty >= y && ty < (y + h);
  }
};

struct Key {
  String label;
  TouchRect rect;
};

// Button on weather screen
TouchRect btnChangeCity = {0, 0, 0, 0};
TouchRect btnCalibrate = {0, 0, 0, 0};

// Keyboard state
static const int MAX_KEYS = 40;
Key keyboardKeys[MAX_KEYS];
int keyboardKeyCount = 0;
String inputBuffer = "";
unsigned long lastTouchHandledMs = 0;
const uint16_t TOUCH_DEBOUNCE_MS = 180;

// Optional: TFT backlight control (set to your BL pin if needed)
#ifdef TFT_BL
const int TFT_BACKLIGHT_PIN = TFT_BL;
#else
const int TFT_BACKLIGHT_PIN = -1; // Set to your display's BL/LED pin or leave -1 if not used
#endif

// Function Prototypes
void drawWeatherUI(JsonDocument& doc); // <-- FIX: Updated function signature for new library
String httpGETRequest(const char* serverName);
void tftSelfTest();
int drawWrapped(const String& text, int x, int y, int font, int maxWidth);
String normalizeCity(const String& input);
String buildWeatherUrl(const String& cityParam);
bool fetchAndDisplay(const String& cityParam, String& errorMessage);
// Touch/Keyboard helpers
bool readTouch(int16_t &sx, int16_t &sy);
void drawChangeCityButton();
void drawWifiDisconnectedScreen();
void drawKeyboardScreen();
void clearKeyboardArea();
void addKey(const String& label, int16_t x, int16_t y, int16_t w, int16_t h);
void redrawInputField();
void processKeyboardTouch(int16_t tx, int16_t ty);
void commitCityFromKeyboard();
void loadTouchCalibration();
void saveTouchCalibration();
void ensureTouchCalibration();
bool waitForTouchSample(uint16_t &rx, uint16_t &ry, uint32_t timeoutMs);
bool tftJpgOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap);
void drawBackground(bool isNight);
void drawErrorBanner(const String& message);

void setup() {
  Serial.begin(115200);

  // Open persistent storage once for the whole app (city + touch calibration).
  prefs.begin("weather", false);

  tft.init();
  tft.setRotation(3);  // Try 3 if 1 shows sideways; test 0–3
  // Init backlight if controlled via GPIO comes later

  // Init touch on dedicated SPI bus
  touchSPI.begin(TOUCH_SCLK_PIN, TOUCH_MISO_PIN, TOUCH_MOSI_PIN, TOUCH_CS_PIN);
  touch.begin(touchSPI);
  // Use base rotation; we handle swap/invert in our mapping
  touch.setRotation(0);
  loadTouchCalibration();
  ensureTouchCalibration();
  
  // Ensure backlight is ON if controlled via GPIO
  if (TFT_BACKLIGHT_PIN >= 0) {
    pinMode(TFT_BACKLIGHT_PIN, OUTPUT);
    digitalWrite(TFT_BACKLIGHT_PIN, HIGH);
  }

  // Basic display self-test to confirm panel is active
  tftSelfTest();

  // Initialize JPEG decoder output callback
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tftJpgOutput);

  // Initialize separate SPI bus for SD using board's pins (HSPI), then mount SD
  sdSPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (SD.begin(SD_CS_PIN, sdSPI, 25000000)) {
    sdAvailable = true;
    Serial.printf("SD mounted (CS=%d, SCK=%d, MISO=%d, MOSI=%d)\n",
                  SD_CS_PIN, SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN);
  } else {
    Serial.println("SD not detected or failed to mount. Using color background.");
  }

  tft.fillScreen(TFT_NAVY);

  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setCursor(20, 20);
  tft.setTextSize(2);
  tft.println("Connecting to WiFi...");
  tft.print("SSID: ");
  tft.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);   // let the stack auto-retry; loop() adds a backstop
  WiFi.begin(ssid, password);

  // IMPROVED ERROR HANDLING: Add timeout for WiFi connection
  int wifiAttempts = 0;
  const int maxWifiAttempts = 20; // 10 seconds timeout
  
  while (WiFi.status() != WL_CONNECTED && wifiAttempts < maxWifiAttempts) {
    delay(500);
    Serial.print(".");
    tft.print(".");
    wifiAttempts++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi connection failed!");
    drawWifiDisconnectedScreen();
    return; // Exit setup if WiFi fails
  }
  
  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  lastWifiConnected = true;

  tft.fillScreen(TFT_NAVY);
  tft.println("WiFi Connected!");
  tft.println("Fetching weather...");
  tft.println("");
  tft.setTextColor(TFT_WHITE, TFT_NAVY);

  // Load persisted city (if available)
  {
    String saved = prefs.getString("city", "");
    if (saved.length() > 0) {
      city = saved;
    }
  }

  // Initial weather fetch
  String initErr;
  fetchAndDisplay(city, initErr);
  delay(1000);
}

void loop() {
  bool nowConnected = (WiFi.status() == WL_CONNECTED);
  if (nowConnected) {
    if (!lastWifiConnected) {
      // Optionally clear or refresh UI on reconnect
      String err;
      fetchAndDisplay(city, err);
    }
    // Periodic weather update without blocking web server
    unsigned long now = millis();
    if (lastWeatherUpdateMs == 0 || now - lastWeatherUpdateMs >= (unsigned long)UPDATE_INTERVAL) {
      String err;
      fetchAndDisplay(city, err);
      lastWeatherUpdateMs = now;
    }
  } else {
    if (lastWifiConnected) {
      Serial.println("WiFi Disconnected");
      drawWifiDisconnectedScreen();
      lastReconnectAttemptMs = millis(); // wait one interval before the first retry
    }
    // Non-blocking reconnect backstop: nudge the radio every few seconds so the
    // station recovers on its own instead of staying stuck on the error screen.
    unsigned long now = millis();
    if (now - lastReconnectAttemptMs >= WIFI_RECONNECT_INTERVAL_MS) {
      lastReconnectAttemptMs = now;
      Serial.println("Attempting WiFi reconnect...");
      WiFi.disconnect();
      WiFi.begin(ssid, password);
    }
  }
  lastWifiConnected = nowConnected;

  // Touch handling
  int16_t tx, ty;
  if (readTouch(tx, ty)) {
    unsigned long now = millis();
    if (now - lastTouchHandledMs >= TOUCH_DEBOUNCE_MS) {
      lastTouchHandledMs = now;
      Serial.printf("Touch detected at: %d, %d\n", tx, ty);
      if (currentScreen == SCREEN_WEATHER) {
        if (btnChangeCity.contains(tx, ty)) {
          currentScreen = SCREEN_KEYBOARD;
          inputBuffer = city; // preload with current city for convenience
          drawKeyboardScreen();
        } else if (btnCalibrate.contains(tx, ty)) {
          prefs.putBool("xpt_ok", false);
          ensureTouchCalibration();
          String err;
          fetchAndDisplay(city, err);
        }
      } else if (currentScreen == SCREEN_KEYBOARD) {
        processKeyboardTouch(tx, ty);
      }
    }
  }

  delay(10); // small yield for stability
}


String httpGETRequest(const char* serverName) {
  WiFiClientSecure client;
  HTTPClient http;
    
  // IMPROVED ERROR HANDLING: Add timeout and better error checking
  http.setTimeout(10000); // 10 second timeout
  // Use insecure mode for TLS to avoid certificate management on device
  // For production, set the CA cert instead of using insecure mode
  client.setInsecure();
  http.begin(client, serverName);
  
  int httpResponseCode = http.GET();
  
  String payload = "error"; 
  
  if (httpResponseCode > 0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    
    if (httpResponseCode == 200) {
      payload = http.getString();
    } else {
      Serial.print("HTTP Error: ");
      Serial.println(httpResponseCode);
      // Print response body to diagnose API error details
      String errorBody = http.getString();
      if (errorBody.length() > 0) {
        Serial.println("Error body: " + errorBody);
      }
      payload = "error";
    }
  } else {
    Serial.print("Connection Error: ");
    Serial.println(httpResponseCode);
    payload = "error";
  }

  http.end();
  return payload;
}

// Draw text wrapped within maxWidth. Returns total height used (in pixels)
int drawWrapped(const String& text, int x, int y, int font, int maxWidth) {
  int lineHeight = tft.fontHeight(font);
  int usedHeight = 0;
  String line = "";
  String word = "";

  auto flushLine = [&](bool includeTrailing) {
    if (line.length()) {
      tft.drawString(line, x, y + usedHeight, font);
      usedHeight += lineHeight + 2;
      line = "";
    }
    if (includeTrailing && word.length()) {
      tft.drawString(word, x, y + usedHeight, font);
      usedHeight += lineHeight + 2;
      word = "";
    }
  };

  for (size_t i = 0; i < text.length(); i++) {
    char c = text[i];
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
      String tentative = line.length() ? line + " " + word : word;
      if (tft.textWidth(tentative, font) <= maxWidth) {
        line = tentative;
      } else {
        if (line.length()) {
          tft.drawString(line, x, y + usedHeight, font);
          usedHeight += lineHeight + 2;
          line = word; // start new line with the word
        } else {
          // Single long word: print it anyway to avoid infinite loop
          tft.drawString(word, x, y + usedHeight, font);
          usedHeight += lineHeight + 2;
          line = "";
        }
      }
      word = "";
      if (c == '\n') {
        flushLine(false);
      }
    } else {
      word += c;
    }
  }

  // Flush remaining
  if (word.length()) {
    String tentative = line.length() ? line + " " + word : word;
    if (tft.textWidth(tentative, font) <= maxWidth) {
      line = tentative;
      word = "";
    } else {
      if (line.length()) {
        tft.drawString(line, x, y + usedHeight, font);
        usedHeight += lineHeight + 2;
        line = word;
        word = "";
      } else {
        tft.drawString(word, x, y + usedHeight, font);
        usedHeight += lineHeight + 2;
        line = "";
      }
    }
  }
  if (line.length()) {
    tft.drawString(line, x, y + usedHeight, font);
    usedHeight += lineHeight; // final line height
  }

  return usedHeight;
}

// Layout UI to match the wallpaper's visual framing/icons
void drawWeatherUI(JsonDocument& doc) {
  // Choose day vs night artwork from the OWM icon code, whose last character is
  // 'd' for day or 'n' for night (e.g. "04d" / "01n").
  const char* icon = doc["weather"][0]["icon"] | "01d";
  size_t iconLen = strlen(icon);
  bool isNight = (iconLen > 0 && icon[iconLen - 1] == 'n');

  drawBackground(isNight);

  // Basic text setup
  tft.setTextDatum(TL_DATUM);
  tft.setTextWrap(false, false);
  tft.setTextPadding(0);

  // Extract values with safety defaults
  const char* cityName = doc["name"] | "Unknown";
  double tempC = doc["main"]["temp"] | 0.0;
  const char* description = doc["weather"][0]["description"] | "Unknown";
  int humidity = doc["main"]["humidity"] | 0;
  double windSpeed = doc["wind"]["speed"] | 0.0;
  double tempF = (tempC * 9.0 / 5.0) + 32.0;
  double windSpeedMph = windSpeed * 2.237;

  const int sideMargin = 16;
  const int topMargin  = 22;

  // --- Top Row: City (left) and Temperature (right) ---
  // City name
  int cityFont = 2;
  // Slightly shrink city font if it's too wide
  if (tft.textWidth(cityName, cityFont) > (tft.width() / 2)) {
    cityFont = 1;
  }
  // Foreground only -> transparent background over wallpaper
  tft.setTextColor(TFT_WHITE);
  tft.drawString(cityName, sideMargin, topMargin, cityFont);

  // Temperature, right aligned near the weather icon in the wallpaper
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(TR_DATUM);
  int tempX = tft.width() - sideMargin;
  int tempY = topMargin;
  String tempStr = String(tempF, 1) + " F";
  tft.drawString(tempStr, tempX, tempY, 2);

  // Restore default datum
  tft.setTextDatum(TL_DATUM);

  // --- Second Row: Description centered-ish under city name ---
  tft.setTextColor(TFT_WHITE);
  int descY = topMargin + tft.fontHeight(cityFont) + 10;
  tft.drawString(String(description), sideMargin, descY, 2);

  // --- Middle Panel: Humidity and Wind, aligned with their icons ---
  // Panel frame (to match the rounded rectangle on the wallpaper)
  int panelX = sideMargin;
  int panelY = descY + 26;
  int panelW = tft.width() - sideMargin * 2;
  int panelH = 80; // smaller box height
  tft.drawRoundRect(panelX, panelY, panelW, panelH, 8, TFT_WHITE);

  // Humidity row
  int iconOffsetX = 10;
  int textOffsetX = 30;
  int row1Y = panelY + 18;
  int row2Y = panelY + 46;

  // Humidity icon (simple droplet)
  tft.drawLine(panelX + iconOffsetX, row1Y - 6, panelX + iconOffsetX + 4, row1Y, TFT_WHITE);
  tft.drawLine(panelX + iconOffsetX + 4, row1Y, panelX + iconOffsetX, row1Y + 6, TFT_WHITE);
  tft.drawLine(panelX + iconOffsetX, row1Y + 6, panelX + iconOffsetX - 4, row1Y, TFT_WHITE);
  tft.drawLine(panelX + iconOffsetX - 4, row1Y, panelX + iconOffsetX, row1Y - 6, TFT_WHITE);

  tft.setTextColor(TFT_WHITE);
  tft.drawString(String("Humidity: ") + String(humidity) + "%", panelX + textOffsetX, row1Y - 8, 2);

  // Wind icon (simple flowing lines)
  int windIconX = panelX + iconOffsetX - 2;
  tft.drawLine(windIconX, row2Y - 6, windIconX + 14, row2Y - 6, TFT_WHITE);
  tft.drawLine(windIconX + 4, row2Y, windIconX + 18, row2Y, TFT_WHITE);
  tft.drawLine(windIconX, row2Y + 6, windIconX + 12, row2Y + 6, TFT_WHITE);

  tft.drawString(String("Wind: ") + String(windSpeedMph, 1) + " mph", panelX + textOffsetX, row2Y - 8, 2);

  // Bottom buttons
  drawChangeCityButton();
}

void tftSelfTest() {
  uint16_t colors[] = {TFT_BLACK, TFT_RED, TFT_GREEN, TFT_BLUE, TFT_CYAN, TFT_MAGENTA, TFT_YELLOW, TFT_WHITE, TFT_NAVY};
  for (int i = 0; i < 9; i++) {
    tft.fillScreen(colors[i]);
    delay(150);
  }
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  int cx = tft.width() / 2;
  int cy = tft.height() / 2;
  tft.drawString("TFT OK", cx, cy, 4);
  delay(400);
}

// ========================= Web & Weather Helpers =========================

String normalizeCity(const String& input) {
  String s = input;
  s.trim();
  // Split into up to three parts by comma
  String p1 = s;
  String p2 = "";
  String p3 = "";
  int first = s.indexOf(',');
  if (first >= 0) {
    p1 = s.substring(0, first);
    String rest = s.substring(first + 1);
    rest.trim();
    int second = rest.indexOf(',');
    if (second >= 0) {
      p2 = rest.substring(0, second);
      p3 = rest.substring(second + 1);
    } else {
      p2 = rest;
    }
  }
  p1.trim();
  p2.trim();
  p3.trim();
  // If no country provided, default to US
  if (p3.length() == 0) {
    if (p2.length() == 0) {
      return p1 + ",US"; // City -> City,US
    } else {
      return p1 + "," + p2 + ",US"; // City,State -> City,State,US
    }
  }
  return p1 + "," + p2 + "," + p3;
}

String buildWeatherUrl(const String& cityParam) {
  String q = normalizeCity(cityParam);
  // Minimal URL encoding for spaces
  q.replace(" ", "%20");
  return String("https://api.openweathermap.org/data/2.5/weather?q=") + q + "&appid=" + openWeatherMapApiKey + "&units=metric";
}

bool fetchAndDisplay(const String& cityParam, String& errorMessage) {
  String serverPath = buildWeatherUrl(cityParam);
  Serial.println(String("Requesting URL: ") + serverPath);
  weatherData = httpGETRequest(serverPath.c_str());
  if (weatherData == "error") {
    errorMessage = "API request failed";
    drawErrorBanner("API request failed - retrying soon");
    return false;
  }

  JsonDocument doc; // ArduinoJson v7: elastic capacity, nothing to outgrow
  DeserializationError error = deserializeJson(doc, weatherData);
  if (error) {
    errorMessage = String("JSON parse failed: ") + error.c_str();
    drawErrorBanner(String("JSON error: ") + error.c_str());
    return false;
  }

  drawWeatherUI(doc);
  return true;
}

// =============================== Touch/Keyboard ===============================
bool readTouch(int16_t &sx, int16_t &sy) {
  // Polling only; IRQ line is optional and not required
  if (!touch.touched()) return false;
  TS_Point p = touch.getPoint(); // raw 0..4095
  uint16_t rx = p.x, ry = p.y;
  // Debug: show raw pressure if needed
  // Serial.printf("raw x=%u y=%u z=%u\n", rx, ry, p.z);
  // Apply swap and invert
  uint32_t ax = rx, ay = ry;
  if (touchCal.swapXY) { uint32_t tmp = ax; ax = ay; ay = tmp; }
  if (touchCal.invertX) ax = 4095 - ax;
  if (touchCal.invertY) ay = 4095 - ay;
  // Constrain and map to screen coords
  if (ax < touchCal.xMin) ax = touchCal.xMin;
  if (ax > touchCal.xMax) ax = touchCal.xMax;
  if (ay < touchCal.yMin) ay = touchCal.yMin;
  if (ay > touchCal.yMax) ay = touchCal.yMax;
  int16_t px = (int16_t) map((int32_t)ax, touchCal.xMin, touchCal.xMax, 0, tft.width());
  int16_t py = (int16_t) map((int32_t)ay, touchCal.yMin, touchCal.yMax, 0, tft.height());
  // Apply small global pixel offset for fine alignment
  px += TOUCH_X_OFFSET;
  py += TOUCH_Y_OFFSET;
  if (px < 0 || py < 0 || px >= tft.width() || py >= tft.height()) return false;
  sx = px;
  sy = py;
  return true;
}

void loadTouchCalibration() {
  // prefs is opened once in setup(); just read here.
  bool ok = prefs.getBool("xpt_ok", false);
  if (!ok) return;
  touchCal.xMin = prefs.getUShort("xpt_xmin", touchCal.xMin);
  touchCal.xMax = prefs.getUShort("xpt_xmax", touchCal.xMax);
  touchCal.yMin = prefs.getUShort("xpt_ymin", touchCal.yMin);
  touchCal.yMax = prefs.getUShort("xpt_ymax", touchCal.yMax);
  touchCal.invertX = prefs.getBool("xpt_invx", touchCal.invertX);
  touchCal.invertY = prefs.getBool("xpt_invy", touchCal.invertY);
  touchCal.swapXY  = prefs.getBool("xpt_swap", touchCal.swapXY);
}

void saveTouchCalibration() {
  prefs.putUShort("xpt_xmin", touchCal.xMin);
  prefs.putUShort("xpt_xmax", touchCal.xMax);
  prefs.putUShort("xpt_ymin", touchCal.yMin);
  prefs.putUShort("xpt_ymax", touchCal.yMax);
  prefs.putBool("xpt_invx", touchCal.invertX);
  prefs.putBool("xpt_invy", touchCal.invertY);
  prefs.putBool("xpt_swap", touchCal.swapXY);
  prefs.putBool("xpt_ok", true);
}

bool waitForTouchSample(uint16_t &rx, uint16_t &ry, uint32_t timeoutMs) {
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (touch.touched()) {
      TS_Point p = touch.getPoint();
      rx = p.x; ry = p.y;
      return true;
    }
    delay(5);
  }
  return false;
}

void ensureTouchCalibration() {
  // If we already have calibration, just use it
  if (prefs.getBool("xpt_ok", false)) return;

  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Touch to calibrate (4 corners)", 10, 10, 2);
  const int sz = 8;
  struct Corner { int x; int y; } corners[4] = {
    {sz, sz},
    {tft.width()-sz-1, sz},
    {tft.width()-sz-1, tft.height()-sz-1},
    {sz, tft.height()-sz-1}
  };
  uint16_t rx[4], ry[4];
  for (int i = 0; i < 4; i++) {
    // draw target
    tft.fillRect(corners[i].x - sz, corners[i].y - 1, sz*2+1, 3, TFT_MAGENTA);
    tft.fillRect(corners[i].x - 1, corners[i].y - sz, 3, sz*2+1, TFT_MAGENTA);
    uint16_t trX, trY;
    if (!waitForTouchSample(trX, trY, 6000)) {
      tft.drawString("Calibration timeout; using defaults", 10, 30, 2);
      delay(800);
      saveTouchCalibration();
      return;
    }
    rx[i] = trX; ry[i] = trY;
    delay(400);
    tft.fillScreen(TFT_BLACK);
    tft.drawString("Touch to calibrate (4 corners)", 10, 10, 2);
  }
  // Determine swap and invert directions based on how raw changes between corners
  // Index mapping: 0=TL, 1=TR, 2=BR, 3=BL in screen coordinates
  int dx_raw_x = (int)rx[1] - (int)rx[0]; // TL -> TR movement along screen X
  int dx_raw_y = (int)ry[1] - (int)ry[0];
  int dy_raw_x = (int)rx[3] - (int)rx[0]; // TL -> BL movement along screen Y
  int dy_raw_y = (int)ry[3] - (int)ry[0];

  // If raw X changes less than raw Y when moving along screen X, then axes are swapped
  touchCal.swapXY = (abs(dx_raw_x) < abs(dx_raw_y));

  // Determine inversion for X axis (left->right should increase after mapping)
  if (!touchCal.swapXY) {
    touchCal.invertX = (dx_raw_x < 0);
    touchCal.invertY = (dy_raw_y < 0);
  } else {
    touchCal.invertX = (dx_raw_y < 0);
    touchCal.invertY = (dy_raw_x < 0);
  }

  // Compute min/max on the axes we will use after swap
  if (!touchCal.swapXY) {
    touchCal.xMin = min(min(rx[0], rx[1]), min(rx[2], rx[3]));
    touchCal.xMax = max(max(rx[0], rx[1]), max(rx[2], rx[3]));
    touchCal.yMin = min(min(ry[0], ry[1]), min(ry[2], ry[3]));
    touchCal.yMax = max(max(ry[0], ry[1]), max(ry[2], ry[3]));
  } else {
    touchCal.xMin = min(min(ry[0], ry[1]), min(ry[2], ry[3]));
    touchCal.xMax = max(max(ry[0], ry[1]), max(ry[2], ry[3]));
    touchCal.yMin = min(min(rx[0], rx[1]), min(rx[2], rx[3]));
    touchCal.yMax = max(max(rx[0], rx[1]), max(rx[2], rx[3]));
  }
  saveTouchCalibration();
  Serial.printf("XPT cal: x[%u..%u] y[%u..%u] swap=%d invX=%d invY=%d\n",
    touchCal.xMin, touchCal.xMax, touchCal.yMin, touchCal.yMax,
    touchCal.swapXY, touchCal.invertX, touchCal.invertY);
}
bool tftJpgOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (y >= tft.height()) return 0;
  tft.pushImage(x, y, w, h, bitmap);
  return 1;
}
void drawBackground(bool isNight) {
  if (sdAvailable) {
    const char* path = nullptr;
    if (isNight) {
      // Prefer the night image; fall back to day art if it's missing.
      if (SD.exists(BG_NIGHT_UPPER)) path = BG_NIGHT_UPPER;
      else if (SD.exists(BG_NIGHT_LOWER)) path = BG_NIGHT_LOWER;
    }
    if (!path) {
      if (SD.exists(BG_DAY_UPPER)) path = BG_DAY_UPPER;
      else if (SD.exists(BG_DAY_LOWER)) path = BG_DAY_LOWER;
    }
    if (path) {
      // Clear once in case JPEG is smaller; then draw at 0,0
      tft.fillScreen(TFT_NAVY);
      TJpgDec.drawSdJpg(0, 0, path);
      return;
    } else {
      Serial.println("Background image not found on SD, using color background.");
    }
  }
  tft.fillScreen(TFT_NAVY);
}
void drawChangeCityButton() {
  const int margin = 10;
  const int btnH = 36;
  const int gap = 10;
  const int btnW = (tft.width() - (margin * 2) - gap) / 2;
  const int y = tft.height() - btnH - margin;

  // Left: Recalibrate
  int x1 = margin;
  btnCalibrate = { (int16_t)x1, (int16_t)y, (int16_t)btnW, (int16_t)btnH };
  tft.fillRoundRect(x1, y, btnW, btnH, 8, TFT_DARKGREY);
  tft.drawRoundRect(x1, y, btnW, btnH, 8, TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.drawString("Recalibrate", x1 + btnW / 2, y + btnH / 2, 2);

  // Right: compact "city" icon button for changing city
  const int iconSize = 32;
  const int iconX = tft.width() - margin - iconSize;
  const int iconY = y;  // align to bottom margin
  btnChangeCity = { (int16_t)iconX, (int16_t)iconY, (int16_t)iconSize, (int16_t)iconSize };

  uint16_t iconBg = TFT_DARKCYAN;
  tft.fillRoundRect(iconX, iconY, iconSize, iconSize, 8, iconBg);
  tft.drawRoundRect(iconX, iconY, iconSize, iconSize, 8, TFT_WHITE);

  // Simple skyline-style city icon
  int baseY = iconY + iconSize - 6;
  int barW = iconSize / 5;
  int gapX = 2;
  int bar1X = iconX + 4;
  tft.fillRect(bar1X, baseY - 10, barW, 10, TFT_WHITE);   // small building
  int bar2X = bar1X + barW + gapX;
  tft.fillRect(bar2X, baseY - 16, barW, 16, TFT_WHITE);   // tall building
  int bar3X = bar2X + barW + gapX;
  tft.fillRect(bar3X, baseY - 13, barW, 13, TFT_WHITE);   // medium building

  tft.setTextDatum(TL_DATUM);
}

void drawWifiDisconnectedScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("wifi disconnected :(", tft.width() / 2, tft.height() / 2, 2);
  tft.setTextDatum(TL_DATUM);
}

// Non-destructive error indicator: a thin red strip across the top, leaving the
// last good weather frame visible underneath instead of wiping the whole screen.
void drawErrorBanner(const String& message) {
  const int h = 22;
  tft.fillRect(0, 0, tft.width(), h, TFT_RED);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.drawString(message, 6, h / 2, 2);
  tft.setTextDatum(TL_DATUM);
}

void addKey(const String& label, int16_t x, int16_t y, int16_t w, int16_t h) {
  if (keyboardKeyCount >= MAX_KEYS) return;
  keyboardKeys[keyboardKeyCount].label = label;
  keyboardKeys[keyboardKeyCount].rect = {x, y, w, h};
  keyboardKeyCount++;

  uint16_t bg = TFT_DARKGREY;
  if (label == "OK") bg = TFT_DARKGREEN;
  else if (label == "CANCEL") bg = TFT_MAROON;

  tft.fillRoundRect(x, y, w, h, 6, bg);
  tft.drawRoundRect(x, y, w, h, 6, TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, bg);
  tft.drawString(label, x + w / 2, y + h / 2, 2);
  tft.setTextDatum(TL_DATUM);
}

void redrawInputField() {
  const int margin = 8;
  const int fieldH = 36;
  int x = margin;
  int y = margin;
  int w = tft.width() - margin * 2;
  tft.fillRoundRect(x, y, w, fieldH, 6, TFT_BLACK);
  tft.drawRoundRect(x, y, w, fieldH, 6, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  String displayText = inputBuffer;
  // Simple overflow handling: trim left if too wide
  int font = 2;
  while (tft.textWidth(displayText, font) > (w - 10) && displayText.length() > 0) {
    displayText.remove(0, 1);
  }
  tft.drawString(displayText, x + 5, y + 8, font);
}

void drawKeyboardScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextWrap(false, false);
  keyboardKeyCount = 0;

  // Header
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Enter City (e.g., New York,NY or City,State,Country)", 8, 40, 2);

  // Input field
  redrawInputField();

  // Layout
  const int left = 8;
  const int right = tft.width() - 8;
  const int availableW = right - left;
  const int keyH = 30;
  const int gap = 4;
  int startY = 78;

  // Rows
  const char* row1 = "QWERTYUIOP";
  const char* row2 = "ASDFGHJKL";
  const char* row3 = "ZXCVBNM";

  // Row 1: 10 keys
  int cols1 = 10;
  int keyW1 = (availableW - (cols1 - 1) * gap) / cols1;
  for (int i = 0; i < cols1; i++) {
    int x = left + i * (keyW1 + gap);
    addKey(String(row1[i]), x, startY, keyW1, keyH);
  }

  // Row 2: 9 keys centered
  int cols2 = 9;
  int keyW2 = (availableW - (cols2 - 1) * gap) / cols2;
  int offset2 = (availableW - (cols2 * keyW2 + (cols2 - 1) * gap)) / 2;
  int row2Y = startY + keyH + gap;
  for (int i = 0; i < cols2; i++) {
    int x = left + offset2 + i * (keyW2 + gap);
    addKey(String(row2[i]), x, row2Y, keyW2, keyH);
  }

  // Row 3: 7 keys centered
  int cols3 = 7;
  int keyW3 = (availableW - (cols3 - 1) * gap) / cols3;
  int offset3 = (availableW - (cols3 * keyW3 + (cols3 - 1) * gap)) / 2;
  int row3Y = row2Y + keyH + gap;
  for (int i = 0; i < cols3; i++) {
    int x = left + offset3 + i * (keyW3 + gap);
    addKey(String(row3[i]), x, row3Y, keyW3, keyH);
  }

  // Row 4: special keys
  int row4Y = row3Y + keyH + gap;
  int cols4 = 4;
  int keyW4 = (availableW - (cols4 - 1) * gap) / cols4;
  addKey("SPACE", left + 0 * (keyW4 + gap), row4Y, keyW4, keyH);
  addKey("BACK",  left + 1 * (keyW4 + gap), row4Y, keyW4, keyH);
  addKey("CANCEL",left + 2 * (keyW4 + gap), row4Y, keyW4, keyH);
  addKey("OK",    left + 3 * (keyW4 + gap), row4Y, keyW4, keyH);
}

void processKeyboardTouch(int16_t tx, int16_t ty) {
  for (int i = 0; i < keyboardKeyCount; i++) {
    if (keyboardKeys[i].rect.contains(tx, ty)) {
      const String label = keyboardKeys[i].label;
      if (label == "SPACE") {
        inputBuffer += " ";
      } else if (label == "BACK") {
        if (inputBuffer.length() > 0) inputBuffer.remove(inputBuffer.length() - 1);
      } else if (label == "OK") {
        commitCityFromKeyboard();
        return;
      } else if (label == "CANCEL") {
        // Go back without changes
        currentScreen = SCREEN_WEATHER;
        String err;
        fetchAndDisplay(city, err); // redraw weather
        return;
      } else {
        // Append first char of label (letters)
        if (label.length() > 0) inputBuffer += label[0];
      }
      redrawInputField();
      return;
    }
  }
}

void commitCityFromKeyboard() {
  String entered = inputBuffer;
  entered.trim();
  if (entered.length() == 0) {
    entered = DEFAULT_CITY;
  }
  String normalized = normalizeCity(entered);
  String err;
  bool ok = fetchAndDisplay(normalized, err);
  if (ok) {
    city = normalized;
  } else {
    city = DEFAULT_CITY;
    String fallbackErr;
    fetchAndDisplay(city, fallbackErr);
  }
  prefs.putString("city", city);
  currentScreen = SCREEN_WEATHER;
}