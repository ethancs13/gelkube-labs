// Copy this file to secrets.h (same folder) and fill in your values.
// secrets.h is in .gitignore, so your Wi-Fi password never ends up on GitHub.
#pragma once

#define WIFI_SSID  "your-wifi-name"      // must be a 2.4 GHz network: the ESP32 can't see 5 GHz
#define WIFI_PASS  "your-wifi-password"

// gelkube's address on your home Wi-Fi (find it with: ip -br a show wlp0s20f3)
#define STATUS_URL "http://192.168.0.110:8787/status"
