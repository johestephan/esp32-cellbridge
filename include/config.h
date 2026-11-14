#pragma once

#define FW_NAME                     "esp32-cellbridge"
#define FW_VERSION                  "1.0.0"

// Optional local secrets file (gitignored). Copy include/secrets.h.example -> include/secrets.h
#if defined(__has_include)
  #if __has_include("secrets.h")
    #include "secrets.h"
  #endif
#endif

#ifndef DEFAULT_WIFI_SSID
  #define DEFAULT_WIFI_SSID           ""
#endif
#ifndef DEFAULT_WIFI_PASS
  #define DEFAULT_WIFI_PASS           ""
#endif
#ifndef DEFAULT_WIFI_FALLBACK_SSID
  #define DEFAULT_WIFI_FALLBACK_SSID  ""
#endif
#ifndef DEFAULT_WIFI_FALLBACK_PASS
  #define DEFAULT_WIFI_FALLBACK_PASS  ""
#endif
#ifndef DEFAULT_HOSTNAME
  #define DEFAULT_HOSTNAME            "esp32-cellbridge"
#endif
#ifndef DEFAULT_API_KEY
  #define DEFAULT_API_KEY             ""
#endif
#ifndef DEFAULT_WEB_USER
  #define DEFAULT_WEB_USER            "admin"
#endif
#ifndef DEFAULT_WEB_PASS
  #define DEFAULT_WEB_PASS            "nimda"
#endif

// Cellular defaults (Odido Netherlands Prepaid)
#ifndef DEFAULT_APN
  #define DEFAULT_APN               "internet.mobiel"
#endif
#ifndef DEFAULT_APN_USER
  #define DEFAULT_APN_USER          ""
#endif
#ifndef DEFAULT_APN_PASS
  #define DEFAULT_APN_PASS          ""
#endif
#ifndef DEFAULT_SIM_PIN
  #define DEFAULT_SIM_PIN           ""
#endif

// Network & Server Ports
#define WEB_SERVER_PORT             80
#define HTTP_PROXY_PORT             8080
#define SOCKS5_PROXY_PORT           1080
#define AP_SETUP_SSID               "ESP32-CellBridge-Setup"
#define AP_SETUP_PASS               "12345678"

// Storage & Limits
#define MAX_SMS_HISTORY             150
#define MAX_CALL_HISTORY            50
#define MAX_PROXY_CLIENTS           4
#define MAX_MULTIPART_PARTS         6
#define WIFI_CONNECT_TIMEOUT_MS     20000
#define MODEM_POLL_INTERVAL_MS      15
#define MODEM_STATUS_INTERVAL_MS    10000
