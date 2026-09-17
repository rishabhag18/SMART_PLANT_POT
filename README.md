# 🌿 Smart Plant Pot with Animated Faces & Weather Forecast

An ESP32-based smart planter companion that visually communicates your plant's needs through adorable, dynamic OLED face animations. When it's not showing you how your plant feels, it connects to Wi-Fi to fetch real-time weather data and a 4-hour forecast using the free Open-Meteo API.

---

## ✨ Features

* **Dynamic Animated Expressions:** The OLED face reacts to its environment (Happy, Crying, Sleepy, Sad/Sleepy) with smooth mouth curves, blinking eyes, tears, and "Zzz" sleeping animations.
* **Environmental Sensing:** Monitors soil moisture and ambient light levels to determine the plant's current state.
* **No-Key Weather API:** Uses the free [Open-Meteo API](https://open-meteo.com/) (no registration or API keys required) to fetch current weather and forecasts.
* **Sequential Display:** Cycles between the plant's face (default state), Current Weather (temperature, humidity, dynamic weather icon, and NTP-synced clock), and a 4-Hour Forecast.
* **Custom Weather Icons:** Procedurally generated weather icons (sun, rain, snow, mist, thunder, clouds) based on WMO weather codes.

---

## 🛠️ Hardware Requirements

1. **ESP32 Development Board** (e.g., NodeMCU ESP32 DevKit V1)
2. **0.96" OLED Display** (SSD1306, I2C interface, 128x64 resolution)
3. **Capacitive Soil Moisture Sensor** (v1.2 or similar analog sensor)
4. **LDR (Light Dependent Resistor) Module** (Analog output)
5. Breadboard and jumper wires

---

## 🔌 Circuit Diagram & Connections

The ESP32 reads analog values from the sensors on specific ADC (Analog-to-Digital Converter) pins and drives the OLED via standard I2C pins.

| Component | Component Pin | ESP32 Pin | Notes |
| --- | --- | --- | --- |
| **OLED (I2C)** | VCC | 3V3 | Power |
|  | GND | GND | Ground |
|  | SDA | GPIO 21 | Standard I2C Data |
|  | SCL | GPIO 22 | Standard I2C Clock |
| **Soil Moisture** | VCC | 3V3 | Power |
|  | GND | GND | Ground |
|  | AOUT (Analog) | GPIO 35 | ADC1_CH7 |
| **LDR Module** | VCC | 3V3 | Power |
|  | GND | GND | Ground |
|  | AOUT (Analog) | GPIO 34 | ADC1_CH6 |

> **Note:** Do not power the sensors from the VIN/5V pin unless your specific sensor modules are 5V-only and have logic level shifters, as the ESP32 pins are strictly 3.3V tolerant.

---

## 💻 Software Setup

### 1. Arduino IDE Board Support

Ensure you have the ESP32 board package installed in your Arduino IDE.

* Go to **File > Preferences**, add `[https://dl.espressif.com/dl/package_esp32_index.json](https://dl.espressif.com/dl/package_esp32_index.json)` to the Additional Boards Manager URLs.
* Go to **Tools > Board > Boards Manager**, search for `esp32`, and install.

### 2. Required Libraries

Install the following libraries via the Arduino Library Manager (**Sketch > Include Library > Manage Libraries**):

* `Adafruit SSD1306` (by Adafruit)
* `Adafruit GFX Library` (by Adafruit)
* `ArduinoJson` (by Benoit Blanchon) - *Ensure you install version 6.x or newer.*

*(Libraries like `WiFi.h`, `HTTPClient.h`, and `Wire.h` are built into the ESP32 core).*

---

## ⚙️ Configuration

Before uploading the code to your ESP32, you **must** update the variables at the top of the sketch.

### Wi-Fi & Location

```cpp
const char* WIFI_SSID     = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Set to your exact coordinates for accurate weather
float LATITUDE  = 23.6312; 
float LONGITUDE = 86.9758;

```

### Timezone Setting

The default time is set for **IST (UTC+5:30)**. To adjust this for your local time, change the `GMT_OFFSET_SEC`.

* *Example for EST (UTC-5):* `-18000`
* *Example for London (UTC+0):* `0`

```cpp
const long  GMT_OFFSET_SEC = 19800; // 19800 seconds = 5.5 hours

```

### Sensor Calibration

Every soil sensor and LDR is different. You should check the serial monitor after uploading to find your baseline values in dry/wet soil and dark/bright rooms, then adjust these variables:

```cpp
int lightThreshold  = 2000; // Above this = dark, Below this = bright
int soilWetMax      = 1500; // Below this = happily watered
int soilCriticalMax = 3300; // Above this = critically thirsty

```

*(On the ESP32, a 12-bit ADC returns values from `0` to `4095`. For most capacitive soil sensors, a lower number means wetter soil).*

---

## 🧠 How the Logic Works

### The Plant's "Emotions"

The code reads the sensors every 1.5 seconds (`SENSOR_INTERVAL`) and determines the state:

1. **`HAPPY`**: Plenty of light and water. The face smiles, bobs around, blinks, and shows sparkles.
2. **`SLEEPY`**: Good water, but low light. The face closes its eyes and animates floating "Zzz" characters.
3. **`CRYING`**: Good light, but soil is dry. The face frowns, eyebrows furrow, and animated tears fall.
4. **`SAD_SLEEPY`**: Low light and dry soil. The plant tries to sleep but cries at the same time.

### Weather Polling & Screens

1. **Weather Fetching:** Every 30 minutes (`WEATHER_INTERVAL`), the ESP32 hits the Open-Meteo API.
2. **JSON Parsing:** `ArduinoJson` unpacks the payload, extracting the current temp/humidity, WMO weather code, Day/Night status, and a 4-hour forecast array.
3. **Screen Cycle:**
* The default view is the **Face**.
* When weather is fetched successfully, it interrupts the face to show **Current Weather** for 6 seconds.
* It then transitions to the **4-Hour Forecast** for 6 seconds.
* Finally, it returns to the animated **Face**.
