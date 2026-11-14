#include "config_manager.h"
#include "config.h"

#include <LittleFS.h>

bool ConfigManager::begin() {
    if (!LittleFS.begin(true)) {
        Serial.println("[Config] LittleFS mount failed!");
        return false;
    }
    resetDefaults();
    load();
    return true;
}

void ConfigManager::resetDefaults() {
    _cfg.wifiSsid = DEFAULT_WIFI_SSID;
    _cfg.wifiPass = DEFAULT_WIFI_PASS;
    _cfg.wifiFallbackSsid = DEFAULT_WIFI_FALLBACK_SSID;
    _cfg.wifiFallbackPass = DEFAULT_WIFI_FALLBACK_PASS;
    _cfg.hostname = DEFAULT_HOSTNAME;

    _cfg.apiKey = DEFAULT_API_KEY;
    _cfg.webUser = DEFAULT_WEB_USER;
    _cfg.webPass = DEFAULT_WEB_PASS;

    _cfg.apn = DEFAULT_APN;
    _cfg.apnUser = DEFAULT_APN_USER;
    _cfg.apnPass = DEFAULT_APN_PASS;
    _cfg.simPin = DEFAULT_SIM_PIN;
    _cfg.autoDeleteSimSms = true;
    _cfg.balanceMethod = "sms";
    _cfg.balanceTarget = "1144";
    _cfg.balanceMessage = "SALDO";

    _cfg.webhookEnabled = false;
    _cfg.webhookUrl = "";
    _cfg.webhookSecret = "";

    _cfg.mqttEnabled = false;
    _cfg.mqttHost = "";
    _cfg.mqttPort = 1883;
    _cfg.mqttUser = "";
    _cfg.mqttPass = "";
    _cfg.mqttPrefix = "esp32_cellbridge";
    _cfg.haDiscovery = true;
    _cfg.haDiscoveryPrefix = "homeassistant";

    _cfg.proxyEnabled = false;
    _cfg.httpProxyPort = HTTP_PROXY_PORT;
    _cfg.socksProxyPort = SOCKS5_PROXY_PORT;
    _cfg.proxyUser = "";
    _cfg.proxyPass = "";
}

bool ConfigManager::load() {
    if (!LittleFS.exists(kConfigPath)) {
        Serial.println("[Config] No config.json yet, using defaults");
        return false;
    }
    File f = LittleFS.open(kConfigPath, "r");
    if (!f) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        Serial.printf("[Config] Failed to parse config.json: %s\n", err.c_str());
        return false;
    }

    bool dummy1 = false, dummy2 = false, dummy3 = false;
    updateFromJson(doc.as<JsonVariantConst>(), dummy1, dummy2, dummy3);
    Serial.println("[Config] Loaded /config.json");
    return true;
}

bool ConfigManager::save() {
    File f = LittleFS.open(kConfigPath, "w");
    if (!f) {
        Serial.println("[Config] Failed to open config.json for write");
        return false;
    }
    JsonDocument doc;
    toJson(doc, false);
    size_t written = serializeJson(doc, f);
    f.close();
    return written > 0;
}

static String maskOrEmpty(const String& s) {
    return s.length() > 0 ? "********" : "";
}

void ConfigManager::toJson(JsonDocument& doc, bool maskSecrets) const {
    doc["wifi_ssid"] = _cfg.wifiSsid;
    doc["wifi_pass"] = maskSecrets ? maskOrEmpty(_cfg.wifiPass) : _cfg.wifiPass;
    doc["wifi_fallback_ssid"] = _cfg.wifiFallbackSsid;
    doc["wifi_fallback_pass"] = maskSecrets ? maskOrEmpty(_cfg.wifiFallbackPass) : _cfg.wifiFallbackPass;
    doc["hostname"] = _cfg.hostname;

    doc["api_key"] = maskSecrets ? maskOrEmpty(_cfg.apiKey) : _cfg.apiKey;
    doc["web_user"] = _cfg.webUser;
    doc["web_pass"] = maskSecrets ? maskOrEmpty(_cfg.webPass) : _cfg.webPass;

    doc["apn"] = _cfg.apn;
    doc["apn_user"] = _cfg.apnUser;
    doc["apn_pass"] = maskSecrets ? maskOrEmpty(_cfg.apnPass) : _cfg.apnPass;
    doc["sim_pin"] = maskSecrets ? maskOrEmpty(_cfg.simPin) : _cfg.simPin;
    doc["auto_delete_sim_sms"] = _cfg.autoDeleteSimSms;
    doc["balance_method"] = _cfg.balanceMethod;
    doc["balance_target"] = _cfg.balanceTarget;
    doc["balance_message"] = _cfg.balanceMessage;

    doc["webhook_enabled"] = _cfg.webhookEnabled;
    doc["webhook_url"] = _cfg.webhookUrl;
    doc["webhook_secret"] = maskSecrets ? maskOrEmpty(_cfg.webhookSecret) : _cfg.webhookSecret;

    doc["mqtt_enabled"] = _cfg.mqttEnabled;
    doc["mqtt_host"] = _cfg.mqttHost;
    doc["mqtt_port"] = _cfg.mqttPort;
    doc["mqtt_user"] = _cfg.mqttUser;
    doc["mqtt_pass"] = maskSecrets ? maskOrEmpty(_cfg.mqttPass) : _cfg.mqttPass;
    doc["mqtt_prefix"] = _cfg.mqttPrefix;
    doc["ha_discovery"] = _cfg.haDiscovery;
    doc["ha_discovery_prefix"] = _cfg.haDiscoveryPrefix;

    doc["proxy_enabled"] = _cfg.proxyEnabled;
    doc["http_proxy_port"] = _cfg.httpProxyPort;
    doc["socks_proxy_port"] = _cfg.socksProxyPort;
    doc["proxy_user"] = _cfg.proxyUser;
    doc["proxy_pass"] = maskSecrets ? maskOrEmpty(_cfg.proxyPass) : _cfg.proxyPass;
}

static void applySecretField(JsonVariantConst obj, const char* key, String& target) {
    if (!obj[key].is<const char*>()) return;
    String val = obj[key].as<String>();
    if (val != "********") {
        target = val;
    }
}

bool ConfigManager::updateFromJson(JsonVariantConst obj, bool& wifiChanged, bool& proxyChanged, bool& mqttChanged) {
    wifiChanged = false;
    proxyChanged = false;
    mqttChanged = false;

    if (obj["wifi_ssid"].is<const char*>()) {
        String v = obj["wifi_ssid"].as<String>();
        if (v != _cfg.wifiSsid) wifiChanged = true;
        _cfg.wifiSsid = v;
    }
    if (obj["wifi_pass"].is<const char*>()) {
        String old = _cfg.wifiPass;
        applySecretField(obj, "wifi_pass", _cfg.wifiPass);
        if (_cfg.wifiPass != old) wifiChanged = true;
    }
    if (obj["wifi_fallback_ssid"].is<const char*>()) {
        String v = obj["wifi_fallback_ssid"].as<String>();
        if (v != _cfg.wifiFallbackSsid) wifiChanged = true;
        _cfg.wifiFallbackSsid = v;
    }
    if (obj["wifi_fallback_pass"].is<const char*>()) {
        String old = _cfg.wifiFallbackPass;
        applySecretField(obj, "wifi_fallback_pass", _cfg.wifiFallbackPass);
        if (_cfg.wifiFallbackPass != old) wifiChanged = true;
    }
    if (obj["hostname"].is<const char*>()) {
        String v = obj["hostname"].as<String>();
        if (v.length() > 0 && v != _cfg.hostname) {
            _cfg.hostname = v;
            wifiChanged = true;
        }
    }

    applySecretField(obj, "api_key", _cfg.apiKey);
    if (obj["web_user"].is<const char*>()) _cfg.webUser = obj["web_user"].as<String>();
    applySecretField(obj, "web_pass", _cfg.webPass);

    if (obj["apn"].is<const char*>()) _cfg.apn = obj["apn"].as<String>();
    if (obj["apn_user"].is<const char*>()) _cfg.apnUser = obj["apn_user"].as<String>();
    applySecretField(obj, "apn_pass", _cfg.apnPass);
    applySecretField(obj, "sim_pin", _cfg.simPin);
    if (obj["auto_delete_sim_sms"].is<bool>()) _cfg.autoDeleteSimSms = obj["auto_delete_sim_sms"].as<bool>();
    if (obj["balance_method"].is<const char*>()) {
        String m = obj["balance_method"].as<String>();
        m.toLowerCase();
        if (m == "sms" || m == "ussd") _cfg.balanceMethod = m;
    }
    if (obj["balance_target"].is<const char*>()) {
        String t = obj["balance_target"].as<String>();
        t.trim();
        if (t.length() > 0) _cfg.balanceTarget = t;
    }
    if (obj["balance_message"].is<const char*>()) {
        _cfg.balanceMessage = obj["balance_message"].as<String>();
    }

    if (obj["webhook_enabled"].is<bool>()) _cfg.webhookEnabled = obj["webhook_enabled"].as<bool>();
    if (obj["webhook_url"].is<const char*>()) _cfg.webhookUrl = obj["webhook_url"].as<String>();
    applySecretField(obj, "webhook_secret", _cfg.webhookSecret);

    if (obj["mqtt_enabled"].is<bool>()) {
        bool b = obj["mqtt_enabled"].as<bool>();
        if (b != _cfg.mqttEnabled) mqttChanged = true;
        _cfg.mqttEnabled = b;
    }
    if (obj["mqtt_host"].is<const char*>()) {
        String v = obj["mqtt_host"].as<String>();
        if (v != _cfg.mqttHost) mqttChanged = true;
        _cfg.mqttHost = v;
    }
    if (obj["mqtt_port"].is<int>()) {
        uint16_t p = obj["mqtt_port"].as<uint16_t>();
        if (p > 0 && p != _cfg.mqttPort) { _cfg.mqttPort = p; mqttChanged = true; }
    }
    if (obj["mqtt_user"].is<const char*>()) {
        String v = obj["mqtt_user"].as<String>();
        if (v != _cfg.mqttUser) mqttChanged = true;
        _cfg.mqttUser = v;
    }
    if (obj["mqtt_pass"].is<const char*>()) {
        String old = _cfg.mqttPass;
        applySecretField(obj, "mqtt_pass", _cfg.mqttPass);
        if (_cfg.mqttPass != old) mqttChanged = true;
    }
    if (obj["mqtt_prefix"].is<const char*>()) {
        String v = obj["mqtt_prefix"].as<String>();
        if (v.length() > 0 && v != _cfg.mqttPrefix) { _cfg.mqttPrefix = v; mqttChanged = true; }
    }
    if (obj["ha_discovery"].is<bool>()) {
        bool b = obj["ha_discovery"].as<bool>();
        if (b != _cfg.haDiscovery) mqttChanged = true;
        _cfg.haDiscovery = b;
    }
    if (obj["ha_discovery_prefix"].is<const char*>()) {
        String v = obj["ha_discovery_prefix"].as<String>();
        if (v.length() > 0 && v != _cfg.haDiscoveryPrefix) { _cfg.haDiscoveryPrefix = v; mqttChanged = true; }
    }

    if (obj["proxy_enabled"].is<bool>()) {
        bool b = obj["proxy_enabled"].as<bool>();
        if (b != _cfg.proxyEnabled) proxyChanged = true;
        _cfg.proxyEnabled = b;
    }
    if (obj["http_proxy_port"].is<int>()) {
        uint16_t p = obj["http_proxy_port"].as<uint16_t>();
        if (p > 0 && p != _cfg.httpProxyPort) { _cfg.httpProxyPort = p; proxyChanged = true; }
    }
    if (obj["socks_proxy_port"].is<int>()) {
        uint16_t p = obj["socks_proxy_port"].as<uint16_t>();
        if (p > 0 && p != _cfg.socksProxyPort) { _cfg.socksProxyPort = p; proxyChanged = true; }
    }
    if (obj["proxy_user"].is<const char*>()) _cfg.proxyUser = obj["proxy_user"].as<String>();
    applySecretField(obj, "proxy_pass", _cfg.proxyPass);

    return true;
}
