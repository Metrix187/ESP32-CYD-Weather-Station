#pragma once
// -----------------------------------------------------------------------------
//  SECRETS TEMPLATE
// -----------------------------------------------------------------------------
//  1. Copy this file to "secrets.h" (in the same folder).
//  2. Fill in your real values below.
//
//  secrets.h is listed in .gitignore, so your private credentials will NOT be
//  committed to git. This template file is safe to commit.
// -----------------------------------------------------------------------------

// WiFi credentials
#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// OpenWeatherMap API key — get a free one at https://openweathermap.org/api
#define OWM_API_KEY   "YOUR_OPENWEATHERMAP_API_KEY"

// Default city shown on first boot, before any city is saved on the device.
// Format: "City", "City,State", or "City,State,Country".
#define DEFAULT_CITY_NAME "New York,NY,US"
