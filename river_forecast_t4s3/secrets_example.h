// Copy to secrets.h (git-ignored) and fill in, OR leave secrets.h empty and set the
// network at runtime from the serial monitor (115200, newline):
//     WIFI|<ssid>|<password>
// Runtime settings are saved in flash (NVS) and take priority over secrets.h.
#pragma once
#define WIFI_SSID     ""
#define WIFI_PASSWORD ""
