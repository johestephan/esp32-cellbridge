#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

#include "config_manager.h"
#include "sms_store.h"

class ModemManager;
class ProxyServer;

class MqttHaClient {
public:
    void begin(ConfigManager* cfg, ModemManager* modem, ProxyServer* proxy);
    void loop();
    void reconnect();

    bool isConnected();
    void publishIncomingSms(const SmsRecord& rec);
    void publishCallEvent(const String& state, const String& peer);
    void publishStatus();

private:
    void handleMessage(char* topic, byte* payload, unsigned int length);
    void publishDiscovery();
    void publishDiscoveryEntity(const String& component, const String& objectId, const String& payloadJson);

    ConfigManager* _cfg = nullptr;
    ModemManager* _modem = nullptr;
    ProxyServer* _proxy = nullptr;

    WiFiClient _wifiClient;
    PubSubClient _mqtt{_wifiClient};

    uint32_t _lastConnectAttemptMs = 0;
    uint32_t _lastStatusPubMs = 0;
    bool _discoveryPublished = false;
    String _smsTarget;
};
