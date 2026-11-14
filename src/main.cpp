#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include "board_pins.h"
#include "config.h"
#include "config_manager.h"
#include "modem_manager.h"
#include "mqtt_ha.h"
#include "proxy_server.h"
#include "sms_store.h"
#include "web_server.h"
#include "webhook_client.h"

ConfigManager configMgr;
SmsStore smsStore;
WebhookClient webhookClient;
MqttHaClient mqttClient;
ModemManager modemMgr;
ProxyServer proxyServer;
GatewayWebServer webServer;

bool g_apMode = false;
uint32_t g_lastWifiCheckMs = 0;
uint32_t g_bootBtnDownMs = 0;

static void modemTask(void* param) {
    (void)param;
    for (;;) {
        modemMgr.loopTaskTick();
        vTaskDelay(pdMS_TO_TICKS(MODEM_POLL_INTERVAL_MS));
    }
}

static void proxyTask(void* param) {
    (void)param;
    for (;;) {
        proxyServer.loop();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static bool tryConnectNetwork(const String& ssid, const String& pass, const String& hostname, uint32_t timeoutMs) {
    if (ssid.length() == 0) return false;

    WiFi.disconnect(true);
    delay(100);
    WiFi.setHostname(hostname.c_str());
    WiFi.begin(ssid.c_str(), pass.c_str());
    Serial.printf("[WiFi] Connecting to '%s'...\n", ssid.c_str());

    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start < timeoutMs)) {
        delay(250);
        Serial.print(".");
    }
    Serial.println();
    return WiFi.status() == WL_CONNECTED;
}

static bool connectWifi() {
    const auto& cfg = configMgr.get();
    if (cfg.wifiSsid.length() == 0 && cfg.wifiFallbackSsid.length() == 0) {
        return false;
    }

    WiFi.mode(WIFI_STA);

    bool tryPrimary = (cfg.wifiSsid.length() > 0);
    bool primaryVisible = false;
    bool fallbackVisible = false;

    if (cfg.wifiSsid.length() > 0 && cfg.wifiFallbackSsid.length() > 0) {
        Serial.println("[WiFi] Scanning for primary/fallback networks...");
        int n = WiFi.scanNetworks(false, true);
        for (int i = 0; i < n; ++i) {
            String found = WiFi.SSID(i);
            if (found == cfg.wifiSsid) primaryVisible = true;
            if (found == cfg.wifiFallbackSsid) fallbackVisible = true;
        }
        WiFi.scanDelete();
        Serial.printf("[WiFi] Scan result: primary('%s')=%s, fallback('%s')=%s\n",
                      cfg.wifiSsid.c_str(), primaryVisible ? "found" : "not found",
                      cfg.wifiFallbackSsid.c_str(), fallbackVisible ? "found" : "not found");
        if (!primaryVisible && fallbackVisible) {
            tryPrimary = false;
            Serial.printf("[WiFi] Primary '%s' not found, using fallback '%s'\n",
                          cfg.wifiSsid.c_str(), cfg.wifiFallbackSsid.c_str());
        }
    }

    bool connected = false;
    String activeSsid;
    if (tryPrimary) {
        connected = tryConnectNetwork(cfg.wifiSsid, cfg.wifiPass, cfg.hostname, 15000);
        if (connected) activeSsid = cfg.wifiSsid;
    }
    if (!connected && cfg.wifiFallbackSsid.length() > 0) {
        connected = tryConnectNetwork(cfg.wifiFallbackSsid, cfg.wifiFallbackPass, cfg.hostname, 15000);
        if (connected) activeSsid = cfg.wifiFallbackSsid;
    }

    if (connected) {
        Serial.printf("[WiFi] Connected to '%s'! IP: %s\n",
                      activeSsid.c_str(), WiFi.localIP().toString().c_str());
        if (MDNS.begin(cfg.hostname.c_str())) {
            MDNS.addService("http", "tcp", WEB_SERVER_PORT);
            Serial.printf("[mDNS] http://%s.local/\n", cfg.hostname.c_str());
        }
        // Central European Time (NL/Odido default)
        configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.google.com");
        return true;
    }

    Serial.println("[WiFi] All configured networks failed, starting setup AP...");
    return false;
}

static void startSetupAp() {
    g_apMode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SETUP_SSID, AP_SETUP_PASS);
    Serial.printf("[WiFi AP] SSID: '%s' Pass: '%s' IP: %s\n",
                  AP_SETUP_SSID, AP_SETUP_PASS, WiFi.softAPIP().toString().c_str());
}

void setup() {
    // 1. Assert peripheral power rail immediately (required for battery operation)
    pinMode(BOARD_POWERON_PIN, OUTPUT);
    digitalWrite(BOARD_POWERON_PIN, HIGH);

    Serial.begin(115200);
    delay(300);
    Serial.printf("\n=== %s v%s (%s) ===\n", FW_NAME, FW_VERSION, BOARD_NAME);

    // 2. Load config & persistent SMS/call storage
    configMgr.begin();
    smsStore.begin();
    webhookClient.begin(&configMgr);

    // 3. Connect Wi-Fi or start Captive Portal AP
    if (!connectWifi()) {
        startSetupAp();
    }

    // 4. Initialize Cellular Modem
    modemMgr.begin(&configMgr, &smsStore, &webhookClient, &mqttClient);

    // 5. Initialize Proxy, MQTT & Web Server
    proxyServer.begin(&configMgr, &modemMgr);
    mqttClient.begin(&configMgr, &modemMgr, &proxyServer);
    webServer.begin(&configMgr, &modemMgr, &smsStore, &proxyServer, &mqttClient, g_apMode);

    // 6. Start background FreeRTOS tasks
    xTaskCreatePinnedToCore(modemTask, "modemTask", 8192, nullptr, 2, nullptr, 0);
    xTaskCreatePinnedToCore(proxyTask, "proxyTask", 8192, nullptr, 1, nullptr, 1);
}

void loop() {
    webServer.loop();
    webhookClient.loop();
    if (!g_apMode) {
        mqttClient.loop();
    }

    // Hold IO0 button for 4 seconds to reset Wi-Fi & enter Setup AP mode
    if (digitalRead(BOARD_BOOT_BTN_PIN) == LOW) {
        if (g_bootBtnDownMs == 0) {
            g_bootBtnDownMs = millis();
        } else if (millis() - g_bootBtnDownMs > 4000) {
            Serial.println("[Button] IO0 held 4s -> clearing Wi-Fi & rebooting to Setup AP");
            configMgr.get().wifiSsid = "";
            configMgr.get().wifiPass = "";
            configMgr.get().wifiFallbackSsid = "";
            configMgr.get().wifiFallbackPass = "";
            configMgr.save();
            delay(300);
            ESP.restart();
        }
    } else {
        g_bootBtnDownMs = 0;
    }

    // Wi-Fi STA reconnect watchdog
    if (!g_apMode && millis() - g_lastWifiCheckMs > 20000) {
        g_lastWifiCheckMs = millis();
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[WiFi] Disconnected, re-trying primary/fallback...");
            connectWifi();
        }
    }

    delay(2);
}
