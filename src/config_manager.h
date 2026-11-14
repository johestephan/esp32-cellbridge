#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

struct GatewayConfig {
    // Wi-Fi & Device
    String wifiSsid;
    String wifiPass;
    String wifiFallbackSsid;
    String wifiFallbackPass;
    String hostname;

    // Security
    String apiKey;
    String webUser;
    String webPass;

    // Cellular (Odido NL prepaid defaults)
    String apn;
    String apnUser;
    String apnPass;
    String simPin;
    bool autoDeleteSimSms = true;
    String balanceMethod = "sms";    // "sms" | "ussd"
    String balanceTarget = "1144";   // Odido NL prepaid short code or USSD (e.g. *101#)
    String balanceMessage = "SALDO"; // SMS body when balanceMethod == "sms"

    // Webhook (Node-Red / Home Assistant)
    bool webhookEnabled = false;
    String webhookUrl;
    String webhookSecret;

    // MQTT + Home Assistant Auto-Discovery
    bool mqttEnabled = false;
    String mqttHost;
    uint16_t mqttPort = 1883;
    String mqttUser;
    String mqttPass;
    String mqttPrefix = "esp32_cellbridge";
    bool haDiscovery = true;
    String haDiscoveryPrefix = "homeassistant";

    // 4G LTE Internet Proxy
    bool proxyEnabled = false;
    uint16_t httpProxyPort = 8080;
    uint16_t socksProxyPort = 1080;
    String proxyUser;
    String proxyPass;
};

class ConfigManager {
public:
    bool begin();
    bool load();
    bool save();
    void resetDefaults();

    GatewayConfig& get() { return _cfg; }
    const GatewayConfig& get() const { return _cfg; }

    void toJson(JsonDocument& doc, bool maskSecrets = true) const;
    bool updateFromJson(JsonVariantConst obj, bool& wifiChanged, bool& proxyChanged, bool& mqttChanged);

private:
    GatewayConfig _cfg;
    static constexpr const char* kConfigPath = "/config.json";
};
