# ESP32 CYD Weather Station

![Status: Stable](https://img.shields.io/badge/status-stable-brightgreen)
[![License: Unlicense](https://img.shields.io/badge/license-Unlicense-blue)](LICENSE)
![Platform: ESP32-2432S028R](https://img.shields.io/badge/hardware-ESP32--2432S028R-orange)

Touch-enabled weather station for the **ESP32-2432S028R** ("Cheap Yellow Display" / CYD). Fetches real-time weather from OpenWeatherMap, swaps day/night JPEG backgrounds from an SD card, persists your city across reboots.

---

## Why this exists

The CYD is a ~$10 ESP32 board with a 2.8" 320×240 ILI9341 touch display built in, making it a natural fit for a standalone weather widget. This project uses TFT_eSPI directly (no LVGL, no heavy UI framework) with XPT2046 touch calibration, NVS persistent storage, and JPEG backgrounds loaded from the SD slot — a practical demonstration of what the hardware can do with minimal dependencies.

## Hardware

- **ESP32-2432S028R** (CYD) — tested on the diymalls 2.8" variant; hardware clones may need pin changes
- **Micro SD card** — FAT32, ≤32 GB
- **Micro USB cable** — power and programming

## Dependencies

Install via Arduino Library Manager:

1. **TFT_eSPI** by Bodmer
2. **XPT2046_Touchscreen** by Paul Stoffregen
3. **ArduinoJson** by Benoit Blanchon (v7+)
4. **TJpg_Decoder** by Bodmer

## Installation

### 1. Configure TFT_eSPI

Edit `Documents/Arduino/libraries/TFT_eSPI/User_Setup.h`:

```c
#define ILI9341_DRIVER
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1
#define TFT_BL   21
#define TFT_BACKLIGHT_ON HIGH
#define SPI_FREQUENCY       55000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000
```

### 2. Prepare the SD card

1. Format to FAT32.
2. Copy `day-aero-fit.JPG` and `night-aero-fit.JPG` from `sd card files/` to the **root** of the card.
3. Custom backgrounds must be **480×270 px JPEG**. Night falls back to the day image if missing.

### 3. Get an OpenWeatherMap API key

Free tier at [openweathermap.org](https://openweathermap.org/) is sufficient.

### 4. Set credentials

```bash
# Copy and fill in:
cp secrets.example.h secrets.h
```

```cpp
#define WIFI_SSID          "YOUR_SSID"
#define WIFI_PASSWORD      "YOUR_PASS"
#define OWM_API_KEY        "YOUR_KEY"
#define DEFAULT_CITY_NAME  "New York,NY,US"
```

`secrets.h` is gitignored — credentials never leave your machine.

### 5. Flash

Select **ESP32 Dev Module** in Arduino IDE, connect via USB, click Upload.

## Usage

- **Change city:** tap the city icon (bottom right) → on-screen keyboard → OK
- **Recalibrate touch:** tap the calibration button (bottom left) → follow the dots
- Data refreshes every 10 minutes; WiFi reconnects automatically on drop

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| `secrets.h: No such file` | Copy `secrets.example.h` → `secrets.h` |
| "SD not detected" | Reformat to FAT32; try a different card |
| White screen | Check TFT_eSPI pin config in `User_Setup.h` |
| Touch not working | Confirm `XPT2046_Touchscreen` is installed |
| Night image never shows | Put `night-aero-fit.JPG` in the SD card root |

## Status

Stable. Works as described on the tested hardware. Not hardened for production use.

## License

[The Unlicense](LICENSE) — public domain.
