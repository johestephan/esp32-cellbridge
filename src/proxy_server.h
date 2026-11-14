#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiServer.h>
#include <WiFiClient.h>

#include "config.h"
#include "config_manager.h"

class ModemManager;

struct ProxyStats {
    bool enabled = false;
    uint16_t httpPort = HTTP_PROXY_PORT;
    uint16_t socksPort = SOCKS5_PROXY_PORT;
    int activeTunnels = 0;
    uint32_t totalConnections = 0;
    uint32_t bytesUp = 0;
    uint32_t bytesDown = 0;
};

class ProxyServer {
public:
    void begin(ConfigManager* cfg, ModemManager* modem);
    void setEnabled(bool enabled);
    void restartListeners();
    void loop();
    ProxyStats getStats() const;

private:
    enum class TunnelState {
        Free = 0,
        HttpHandshake,
        SocksGreeting,
        SocksAuth,
        SocksRequest,
        Relaying
    };

    struct TunnelSlot {
        TunnelState state = TunnelState::Free;
        WiFiClient client;
        int linkId = -1;
        String rxBuf;
        uint32_t lastActivityMs = 0;
        uint32_t lastPollMs = 0;
        bool isSocks = false;
    };

    void acceptNewClients();
    void processSlot(int idx);
    void closeSlot(int idx);
    bool checkHttpProxyAuth(const String& headers) const;
    bool requiresAuth() const;

    static bool parseHostPort(const String& hostPort, uint16_t defaultPort, String& hostOut, uint16_t& portOut);

    ConfigManager* _cfg = nullptr;
    ModemManager* _modem = nullptr;

    WiFiServer* _httpServer = nullptr;
    WiFiServer* _socksServer = nullptr;
    bool _enabled = false;
    uint16_t _httpPort = HTTP_PROXY_PORT;
    uint16_t _socksPort = SOCKS5_PROXY_PORT;

    TunnelSlot _slots[MAX_PROXY_CLIENTS];
    uint32_t _totalConnections = 0;
    uint32_t _bytesUp = 0;
    uint32_t _bytesDown = 0;
};
