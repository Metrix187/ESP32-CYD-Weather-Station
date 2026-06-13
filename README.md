# ESP32 Weather Station (CYD)

> 🚨 **Open Source Alert** 🚨  
> This project is fully open source! It works great, but it's built for fun, not for mission-critical enterprise deployment. Hack it, break it, make it yours!

A beautiful, touch-enabled weather station for the ESP32-2432S028R board (commonly known as the "Cheap Yellow Display" or CYD). This project fetches real-time weather data from OpenWeatherMap and displays it over a custom background image loaded from an SD card.

![Project Preview](sd%20card%20files/day-aero-fit.JPG)

## Features

*   **Real-time Weather**: Displays temperature, humidity, wind speed, and weather description.
*   **Touch Interface**: 
    *   Tap the city name to search/change the city using an on-screen keyboard.
    *   Calibration button for accurate touch response.
*   **Day / Night Backgrounds**: Automatically swaps between `day-aero-fit.JPG` and `night-aero-fit.JPG` based on whether it's currently day or night at your location.
*   **Custom Backgrounds**: Loads high-quality JPEG backgrounds (**480 x 270**) from a micro SD card.
*   **WiFi Connectivity**: Automatically connects to WiFi, updates data every 10 minutes, and reconnects on its own if the link drops.
*   **Persistent Settings**: Remembers your city and touch calibration even after power loss.

## Hardware Required

*   **ESP32-2432S028R Board**: This project is specifically designed for the "Cheap Yellow Display" (CYD).
    *   *Note: I have used and tested this with the "diymalls 2.8" ESP32-2432S028R ESP32 from Amazon.*
*   **Micro SD Card**: Formatted to FAT32 (max 32GB recommended).
*   **Micro USB Cable**: For programming and power.

## Software & Libraries

You will need the [Arduino IDE](https://www.arduino.cc/en/software) to upload this code. Install the following libraries via the Arduino Library Manager:

1.  **TFT_eSPI** by Bodmer
2.  **XPT2046_Touchscreen** by Paul Stoffregen
3.  **ArduinoJson** by Benoit Blanchon (**v7** or newer)
4.  **TJpg_Decoder** by Bodmer

## Installation & Setup

### 1. Configure TFT_eSPI
The `TFT_eSPI` library requires specific pin configuration for the CYD board. You must edit the `User_Setup.h` file inside your library folder (usually `Documents/Arduino/libraries/TFT_eSPI/User_Setup.h`) or select the correct setup file.

For the CYD (ESP32-2432S028R dual usb), use these settings:
```c
#define ILI9341_DRIVER
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  -1
#define TFT_BL   21
#define TFT_BACKLIGHT_ON HIGH
#define SPI_FREQUENCY  55000000
#define SPI_READ_FREQUENCY 20000000
#define SPI_TOUCH_FREQUENCY 2500000
// Note: We use XPT2046_Touchscreen for touch, not TFT_eSPI's built-in touch
```

### 2. Prepare the SD Card
1.  Format your Micro SD card to **FAT32**.
2.  Open the `sd card files` folder in this repository.
3.  Copy **both** `day-aero-fit.JPG` and `night-aero-fit.JPG` to the **root** of your SD card. The day image is shown during the day and the night image after dark. (If using your own images, make them **480 x 270** pixels and keep the same file names. Only the day image is strictly required — night falls back to the day image if it's missing.)
4.  Insert the SD card into the slot on the ESP32 board.

### 3. Get an OpenWeatherMap API Key
1.  Go to [OpenWeatherMap.org](https://openweathermap.org/).
2.  Sign up for a free account.
3.  Navigate to the "API keys" tab and generate a new key.

### 4. Configure Your Credentials
Credentials are kept out of the main sketch (and out of git) in a `secrets.h` file.

1.  In the `ESP32_Weather_Station-touch` folder, copy `secrets.example.h` to `secrets.h`.
2.  Open `secrets.h` and fill in your details:

```cpp
#define WIFI_SSID     "YOUR_WIFI_SSID"              // <- your WiFi name
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"          // <- your WiFi password
#define OWM_API_KEY   "YOUR_OPENWEATHERMAP_API_KEY" // <- your API key
#define DEFAULT_CITY_NAME "New York,NY,US"          // <- default city on first boot
```

`secrets.h` is listed in `.gitignore`, so your credentials stay on your machine and are never committed.

### 5. Upload
1.  Select your board in Arduino IDE (usually "ESP32 Dev Module").
2.  Connect your board via USB.
3.  Click Upload.

## Usage

*   **First Run**: The screen will connect to WiFi and try to fetch weather for the default city (New York).
*   **Change City**: Tap the **City Icon** (bottom right) to open the keyboard. Type your city name (e.g., `London,UK` or `Paris`) and press **OK**.
*   **Calibration**: If touch is inaccurate, tap the **Recalibrate** button (bottom left) and follow the on-screen dots.

## Troubleshooting

*   **Compile error `secrets.h: No such file or directory`**: You skipped step 4 — copy `secrets.example.h` to `secrets.h` and fill it in.
*   **"SD not detected"**: Ensure the card is FAT32 and fully inserted. Try a different card (some non-standard cards have issues with SPI).
*   **Screen is White**: Check your `TFT_eSPI` pin configuration in `User_Setup.h`.
*   **Touch not working**: Ensure you installed the `XPT2046_Touchscreen` library.
*   **Night background never shows**: Confirm `night-aero-fit.JPG` is in the SD card root. The day/night choice comes from OpenWeatherMap's icon code for your city.

## Open Source & License

This project is **fully open source**! 

Feel free to use, modify, improve, and distribute this code for your own projects. It is intended for educational and hobbyist purposes. If you create something cool with it, enjoy the vibes!

## Disclaimer

This software is provided "as is", without warranty of any kind. I have only tested this with the specific hardware mentioned above ("diymalls 2.8" ESP32-2432S028R). Variations in hardware clones may require different pin configurations.

