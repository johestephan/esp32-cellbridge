#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h>

#include "config_manager.h"
#include "sms_store.h"

class ModemManager;
class ProxyServer;
class MqttHaClient;

class GatewayWebServer {
public:
    void begin(ConfigManager* cfg, ModemManager* modem, SmsStore* store,
               ProxyServer* proxy, MqttHaClient* mqtt, bool apMode);
    void loop();

private:
    struct AuthSession {
        bool active = false;
        String token;
        uint32_t expiresAtMs = 0;
    };

    static constexpr int kMaxSessions = 8;

    bool hasValidSession();
    String createSession();
    void clearCurrentSession();
    void clearAllSessions();
    String extractSessionCookie();

    bool requireApiAuth();
    bool requireWebAuth();
    void sendJson(int code, const JsonDocument& doc);
    void sendError(int code, const String& msg);
    bool parseJsonBody(JsonDocument& doc);

    void registerRoutes();

    ConfigManager* _cfg = nullptr;
    ModemManager* _modem = nullptr;
    SmsStore* _store = nullptr;
    ProxyServer* _proxy = nullptr;
    MqttHaClient* _mqtt = nullptr;

    WebServer _server{80};
    DNSServer _dns;
    AuthSession _sessions[kMaxSessions];
    bool _apMode = false;
    bool _otaOk = false;
};
