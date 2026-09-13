/*
  Smart Plant Pot - Fully Animated Face Expressions + Open-Meteo Weather
  ESP32 DevKit V1 + LDR module + Soil moisture module + 0.96" OLED (SSD1306, I2C)
*/

#include <Wire.h>
#include <Adafruit_GFX.h>

#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>
#include <time.h> 

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);


const int LDR_PIN  = 34;
const int SOIL_PIN = 35;

// ---- CALIBRATE THESE using the Serial Monitor ----
int lightThreshold  = 2000;
int soilWetMax      = 1500;
int soilCriticalMax = 3300; 
// ---------------------------------------------------

// ---- WiFi / Location Config ----
const char* WIFI_SSID     = "";
const char* WIFI_PASSWORD = "";

float LATITUDE  = 23.6312;
float LONGITUDE = 86.9758;

// ---- NTP Time Config ----
const char* NTP_SERVER    = "pool.ntp.org";
const long  GMT_OFFSET_SEC = 19800; // UTC+5:30 (IST)
const int   DAYLIGHT_OFFSET_SEC = 0;
// --------------------------------

enum FaceState { HAPPY, CRYING, SLEEPY, SAD_SLEEPY };
FaceState currentState = HAPPY;
bool criticalThirst = false;
float mouthCurve = 0.0;   
float mouthOpenAmt = 0.0; 

unsigned long lastSensorRead = 0;
const unsigned long SENSOR_INTERVAL = 1500;
unsigned long lastFrame = 0;
const unsigned long FRAME_INTERVAL = 60; 

// Weather Timing & Screen Sequencing
unsigned long lastWeatherFetch = 0;
const unsigned long WEATHER_INTERVAL = 30UL * 60UL * 1000UL;   // Fetch every 30 minutes
const unsigned long PAGE_DURATION = 6000;                     // 6 seconds per weather page

enum WeatherDisplayState { SHOW_FACE, SHOW_CURRENT_WEATHER, SHOW_FORECAST };
WeatherDisplayState weatherDisplayState = SHOW_FACE;
unsigned long weatherStateTimer = 0;

// Open-Meteo Variables
float weatherTemp = 0;
int weatherHumidity = 0;
int weatherCode = 0;
bool isDay = true;

// Forecast Arrays
float forecastTemp[4]; 
int forecastCode[4];
bool forecastIsDay[4];
bool weatherDataValid = false;

void setup() {
  Serial.begin(115200);
  pinMode(LDR_PIN, INPUT);
  pinMode(SOIL_PIN, INPUT);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("OLED not found - check wiring"));
    while (true) delay(1000);
  }
  Wire.setClock(400000);
  display.clearDisplay();
  display.display();

  connectWiFi();
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
}

int readAveraged(int pin) {
  long sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(pin);
    delay(5);
  }
  return sum / 10;
}

// ---------------- WiFi + Weather ----------------

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("Connecting WiFi...");
  display.display();

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(300);
    display.print(".");
    display.display();
  }
}

bool fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    delay(2000);
    if (WiFi.status() != WL_CONNECTED) return false;
  }

  HTTPClient http;
  // Updated URL to pull weather_code and is_day for the hourly forecast
  String url = "http://api.open-meteo.com/v1/forecast?latitude=" + String(LATITUDE, 4) + 
               "&longitude=" + String(LONGITUDE, 4) + 
               "&current=temperature_2m,relative_humidity_2m,weather_code,is_day" +
               "&hourly=temperature_2m,weather_code,is_day" + 
               "&forecast_hours=5&timezone=auto";
               
  http.begin(url);
  int code = http.GET();
  bool ok = false;
  
  if (code == 200) {
    String payload = http.getString();
    // Increased buffer size to handle the extra hourly arrays
    DynamicJsonDocument doc(3072);
    if (!deserializeJson(doc, payload)) {
      weatherTemp     = doc["current"]["temperature_2m"];
      weatherHumidity = doc["current"]["relative_humidity_2m"];
      weatherCode     = doc["current"]["weather_code"];
      isDay           = doc["current"]["is_day"] == 1;

      for (int i = 0; i < 4; i++) {
        forecastTemp[i]  = doc["hourly"]["temperature_2m"][i + 1];
        forecastCode[i]  = doc["hourly"]["weather_code"][i + 1];
        forecastIsDay[i] = doc["hourly"]["is_day"][i + 1] == 1;
      }
      
      weatherDataValid = true;
      ok = true;
    }
  }
  http.end();
  return ok;
}

// ---------------- Dynamic Weather Icons (Accepts X/Y coords) ----------------

void drawSunIcon(int cx, int cy) {
  int r = 8;
  display.fillCircle(cx, cy, r, SSD1306_WHITE);
  for (int i = 0; i < 8; i++) {
    float angle = i * PI / 4.0;
    int x1 = cx + (int)(cos(angle) * (r + 3));
    int y1 = cy + (int)(sin(angle) * (r + 3));
    int x2 = cx + (int)(cos(angle) * (r + 6));
    int y2 = cy + (int)(sin(angle) * (r + 6));
    display.drawLine(x1, y1, x2, y2, SSD1306_WHITE);
  }
}

void drawCloudBase(int cx, int cy) {
  display.fillCircle(cx - 8, cy, 6, SSD1306_WHITE);
  display.fillCircle(cx, cy - 4, 8, SSD1306_WHITE);
  display.fillCircle(cx + 8, cy, 6, SSD1306_WHITE);
  display.fillRect(cx - 8, cy, 16, 7, SSD1306_WHITE);
}

void drawRainIcon(int cx, int cy) {
  drawCloudBase(cx, cy);
  unsigned long t = millis() % 600;
  int drop = (int)((t * 6) / 600);
  for (int i = 0; i < 3; i++) {
    int lx = cx - 8 + i * 8, ly = cy + 8 + drop;
    display.drawLine(lx, ly, lx - 1, ly + 4, SSD1306_WHITE);
  }
}

void drawThunderIcon(int cx, int cy) {
  drawCloudBase(cx, cy);
  display.drawLine(cx - 2, cy + 5, cx - 6, cy + 11, SSD1306_WHITE);
  display.drawLine(cx - 6, cy + 11, cx, cy + 11, SSD1306_WHITE);
  display.drawLine(cx, cy + 11, cx - 4, cy + 18, SSD1306_WHITE);
}

void drawSnowIcon(int cx, int cy) {
  drawCloudBase(cx, cy);
  for (int i = 0; i < 3; i++) {
    int sx = cx - 8 + i * 8, sy = cy + 9;
    display.drawLine(sx - 2, sy, sx + 2, sy, SSD1306_WHITE);
    display.drawLine(sx, sy - 2, sx, sy + 2, SSD1306_WHITE);
  }
}

void drawMistIcon(int cx, int cy) {
  for (int i = 0; i < 4; i++) {
    display.drawFastHLine(cx - 14, cy - 9 + i * 6, 28, SSD1306_WHITE);
  }
}

void drawWeatherIcon(int code, int cx, int cy, bool day) {
  if (code == 0 || code == 1) {
    if (day) drawSunIcon(cx, cy); else drawCloudBase(cx, cy); 
  }
  else if (code == 2 || code == 3) drawCloudBase(cx, cy);
  else if (code == 45 || code == 48) drawMistIcon(cx, cy);
  else if (code >= 51 && code <= 67) drawRainIcon(cx, cy);
  else if (code >= 71 && code <= 77) drawSnowIcon(cx, cy);
  else if (code >= 80 && code <= 82) drawRainIcon(cx, cy);
  else if (code >= 85 && code <= 86) drawSnowIcon(cx, cy);
  else if (code >= 95) drawThunderIcon(cx, cy);
  else drawSunIcon(cx, cy); 
}

// ---------------- Screen 1: Current Weather ----------------

void renderCurrentWeatherScreen() {
  display.clearDisplay();
  
  // Left: Animated weather icon (Centers around x=24, y=24)
  drawWeatherIcon(weatherCode, 24, 24, isDay);

  // Right: Temperature
  display.setTextSize(2);
  display.setCursor(48, 6);
  display.print((int)round(weatherTemp));
  display.drawCircle(78, 8, 2, SSD1306_WHITE); 
  display.setCursor(84, 6);
  display.print("C");

  // Right: Humidity
  display.setTextSize(1);
  display.setCursor(48, 28);
  display.print("Hum: ");
  display.print(weatherHumidity);
  display.print("%");

  // Horizontal divider
  display.drawFastHLine(0, 46, 128, SSD1306_WHITE);

  // Bottom: NTP Synchronized Time
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char timeBuf[6];
    strftime(timeBuf, sizeof(timeBuf), "%H:%M", &timeinfo);
    display.setTextSize(2);
    display.setCursor(36, 49);
    display.print(timeBuf);
  }

  display.display();
}

// ---------------- Screen 2: Forecast (Icons) ----------------

void renderForecastScreen() {
  display.clearDisplay();

  // Top Title Bar
  display.setTextSize(1);
  display.setCursor(18, 1);
  display.print("4-HOUR FORECAST");
  display.drawFastHLine(0, 11, 128, SSD1306_WHITE);

  // 4 Columns for the 4 hours
  int xPos[] = {16, 48, 80, 112}; 
  
  for (int i = 0; i < 4; i++) {
    // Hour offset labels (+1h, +2h, etc.)
    display.setCursor(xPos[i] - 8, 15);
    display.print("+"); display.print(i + 1); display.print("h");

    // Dynamic Weather Icon for that specific hour
    // Centers the icon vertically in the middle of the screen
    drawWeatherIcon(forecastCode[i], xPos[i], 36, forecastIsDay[i]);

    // Temperature value below the icon
    display.setCursor(xPos[i] - 6, 54);
    display.print((int)round(forecastTemp[i]));
    display.print((char)247); // Degree symbol
  }

  display.display();
}

// ---------------- Face building blocks ----------------

int pupilDX() { return (int)(sin(millis() / 900.0) * 2); }
int pupilDY() { return (int)(sin(millis() / 1300.0) * 1); }
int happyBob()  { return (int)(sin(millis() / 500.0) * 2); }
int sleepyBob() { return (int)(sin(millis() / 1500.0) * 2); }
int cryBob()    { return 2 + (int)(sin(millis() / 300.0) * 1); }

bool isBlinking() { return (millis() % 4000 > 3850); }
bool isFluttering() { return (millis() % 5000 > 4800); }

void drawEyesOpen(int yOff, int pdx, int pdy) {
  display.fillCircle(40, 27 + yOff, 7, SSD1306_WHITE);
  display.fillCircle(88, 27 + yOff, 7, SSD1306_WHITE);
  display.fillCircle(40 + pdx, 27 + yOff + pdy, 3, SSD1306_BLACK);
  display.fillCircle(88 + pdx, 27 + yOff + pdy, 3, SSD1306_BLACK);
}

void drawEyesClosedLine(int yOff) {
  display.fillRoundRect(32, 25 + yOff, 18, 4, 2, SSD1306_WHITE);
  display.fillRoundRect(80, 25 + yOff, 18, 4, 2, SSD1306_WHITE);
}

void drawEyesBreathingClosed(int yOff) {
  int width = 16 + (int)((millis() / 200) % 6);
  display.fillRoundRect(40 - width / 2, 25 + yOff, width, 4, 2, SSD1306_WHITE);
  display.fillRoundRect(88 - width / 2, 25 + yOff, width, 4, 2, SSD1306_WHITE);
}

void drawSleepyEyes(int yOff) {
  if (isFluttering()) {
    display.fillCircle(40, 27 + yOff, 4, SSD1306_WHITE);
    display.fillCircle(88, 27 + yOff, 4, SSD1306_WHITE);
  } else {
    drawEyesBreathingClosed(yOff);
  }
}

float mouthTargetCurve(FaceState state) {
  if (state == HAPPY) return 1.0;
  if (state == SLEEPY) return 0.0;
  return -1.0; 
}

float mouthTargetOpen(FaceState state) {
  if ((state == CRYING || state == SAD_SLEEPY) && criticalThirst) return 1.0;
  return 0.0;
}

void updateMouthAnimation() {
  float targetCurve = mouthTargetCurve(currentState);
  float targetOpen  = mouthTargetOpen(currentState);
  const float speed = 0.06; 
  mouthCurve   += (targetCurve - mouthCurve) * speed;
  mouthOpenAmt += (targetOpen  - mouthOpenAmt) * speed;
}

void drawMouthDynamic(int yOff) {
  int cx = 64, cy = 46 + yOff;

  if (mouthOpenAmt > 0.05) {
    int h = (int)(4 + mouthOpenAmt * 14 + sin(millis() / 200.0) * 2 * mouthOpenAmt);
    if (h < 2) h = 2;
    display.fillRoundRect(cx - 7, cy - h / 2, 14, h, min(5, h / 2), SSD1306_WHITE);
    if (h > 6) display.fillRoundRect(cx - 4, cy - h / 2 + 3, 8, h - 6, 3, SSD1306_BLACK);
    return;
  }

  float wobble = sin(millis() / (mouthCurve < 0 ? 150.0 : 400.0)); 
  int r = (int)(fabs(mouthCurve) * 13 + wobble);

  if (r < 2) {
    int halfLen = 4 + (int)(fabs(mouthCurve) * 8);
    display.drawLine(cx - halfLen, cy, cx + halfLen, cy, SSD1306_WHITE);
    return;
  }

  if (mouthCurve > 0) {
    int cy2 = cy - r;
    display.drawCircle(cx, cy2, r, SSD1306_WHITE);
    display.fillRect(cx - r - 2, cy2 - r - 2, 2 * r + 4, r + 2, SSD1306_BLACK);
  } else {
    int cy2 = cy + r;
    display.drawCircle(cx, cy2, r, SSD1306_WHITE);
    display.fillRect(cx - r - 2, cy2 + r, 2 * r + 4, r + 2, SSD1306_BLACK);
  }
}

void drawSadEyebrows(int yOff) {
  display.drawLine(30, 15 + yOff, 46, 19 + yOff, SSD1306_WHITE);
  display.drawLine(82, 19 + yOff, 98, 15 + yOff, SSD1306_WHITE);
}

void drawBlush(int yOff) {
  for (int i = 0; i < 3; i++) {
    display.drawLine(20, 36 + i * 3 + yOff, 28, 34 + i * 3 + yOff, SSD1306_WHITE);
    display.drawLine(100, 34 + i * 3 + yOff, 108, 36 + i * 3 + yOff, SSD1306_WHITE);
  }
}

void drawStar(int x, int y) {
  display.drawLine(x - 3, y, x + 3, y, SSD1306_WHITE);
  display.drawLine(x, y - 3, x, y + 3, SSD1306_WHITE);
}

void drawSparkles() {
  unsigned long phase = millis() % 1600;
  if (phase < 800) drawStar(10, 8);
  else drawStar(116, 10);
}

void drawTearsDetailed(int yOff) {
  unsigned long t1 = millis() % 900;
  unsigned long t2 = (millis() + 450) % 900;
  int y1 = 34 + yOff + (int)((t1 * 16) / 900);
  int y2 = 34 + yOff + (int)((t2 * 16) / 900);

  display.fillTriangle(38, y1 - 4, 42, y1 - 4, 40, y1 - 8, SSD1306_WHITE);
  display.fillCircle(40, y1, 2, SSD1306_WHITE);
  display.fillTriangle(86, y2 - 4, 90, y2 - 4, 88, y2 - 8, SSD1306_WHITE);
  display.fillCircle(88, y2, 2, SSD1306_WHITE);

  if (t1 > 850) {
    display.drawLine(36, 50 + yOff, 38, 52 + yOff, SSD1306_WHITE);
    display.drawLine(44, 50 + yOff, 42, 52 + yOff, SSD1306_WHITE);
  }
  if (t2 > 850) {
    display.drawLine(84, 50 + yOff, 86, 52 + yOff, SSD1306_WHITE);
    display.drawLine(92, 50 + yOff, 90, 52 + yOff, SSD1306_WHITE);
  }
}

void drawZzzAnimated() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  unsigned long tA = millis() % 2000;
  float pA = tA / 2000.0;
  display.setCursor(96 + (int)(pA * 14), 12 - (int)(pA * 10));
  display.print("z");

  unsigned long tB = (millis() + 1000) % 2000;
  float pB = tB / 2000.0;
  display.setCursor(96 + (int)(pB * 14), 12 - (int)(pB * 10));
  display.print("Z");
}

// ---------------- State + Render ----------------

void updateSensors() {
  int lightVal = readAveraged(LDR_PIN);
  int soilVal  = readAveraged(SOIL_PIN);

  bool goodLight = (lightVal < lightThreshold);
  bool goodWater = (soilVal < soilWetMax);
  criticalThirst = (soilVal > soilCriticalMax);

  if (!goodWater && !goodLight)      currentState = SAD_SLEEPY;
  else if (!goodWater)               currentState = CRYING;
  else if (!goodLight)               currentState = SLEEPY;
  else                               currentState = HAPPY;
}

void renderFace() {
  display.clearDisplay();
  updateMouthAnimation();

  switch (currentState) {
    case HAPPY: {
      int yOff = happyBob();
      if (isBlinking()) drawEyesClosedLine(yOff);
      else drawEyesOpen(yOff, pupilDX(), pupilDY());
      drawMouthDynamic(yOff);
      drawBlush(yOff);
      drawSparkles();
      break;
    }
    case CRYING: {
      int yOff = cryBob();
      drawSadEyebrows(yOff);
      if (isBlinking()) drawEyesClosedLine(yOff);
      else drawEyesOpen(yOff, 0, 1);
      drawMouthDynamic(yOff);
      drawTearsDetailed(yOff);
      break;
    }
    case SLEEPY: {
      int yOff = sleepyBob();
      drawSleepyEyes(yOff);
      drawMouthDynamic(yOff);
      drawZzzAnimated();
      break;
    }
    case SAD_SLEEPY: {
      int yOff = sleepyBob();
      drawSadEyebrows(yOff);
      drawSleepyEyes(yOff);
      drawMouthDynamic(yOff);
      drawTearsDetailed(yOff);
      drawZzzAnimated();
      break;
    }
  }
  display.display();
}

void loop() {
  unsigned long now = millis();

  // 1. Read Soil & Light Sensors
  if (now - lastSensorRead >= SENSOR_INTERVAL) {
    lastSensorRead = now;
    updateSensors();
  }

  // 2. Trigger Weather Fetch every WEATHER_INTERVAL
  if (lastWeatherFetch == 0 || now - lastWeatherFetch >= WEATHER_INTERVAL) {
    lastWeatherFetch = now;
    if (fetchWeather()) {
      weatherDisplayState = SHOW_CURRENT_WEATHER;
      weatherStateTimer = now;
    }
  }

  // 3. Screen State Transitions (Current -> Forecast -> Face)
  if (weatherDisplayState == SHOW_CURRENT_WEATHER) {
    if (now - weatherStateTimer >= PAGE_DURATION) {
      weatherDisplayState = SHOW_FORECAST;
      weatherStateTimer = now;
    }
  } else if (weatherDisplayState == SHOW_FORECAST) {
    if (now - weatherStateTimer >= PAGE_DURATION) {
      weatherDisplayState = SHOW_FACE;
    }
  }

  // 4. Render Active Screen
  if (now - lastFrame >= FRAME_INTERVAL) {
    lastFrame = now;
    if (weatherDisplayState == SHOW_CURRENT_WEATHER) {
      renderCurrentWeatherScreen();
    } else if (weatherDisplayState == SHOW_FORECAST) {
      renderForecastScreen();
    } else {
      renderFace();
    }
  }
}
