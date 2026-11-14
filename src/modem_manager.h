#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <vector>

#include "config_manager.h"
#include "sms_store.h"

class WebhookClient;
class MqttHaClient;

struct ModemStatus {
    bool ready = false;
    bool simReady = false;
    String simState = "UNKNOWN";
    String manufacturer;
    String model;
    String imei;
    String operatorName;
    String networkTech;
    int regStatus = 0;        // 1=Home, 5=Roaming
    int csq = 99;             // 0..31, 99=unknown
    int rssiDbm = -113;
    int signalPercent = 0;
    bool dataConnected = false;
    String cellularIp;
    String callState = "idle"; // idle | ringing | dialing | active
    String activeCallPeer;
    uint32_t activeCallId = 0;
    uint32_t callStartMs = 0;
    int batteryMv = 0;
    int batteryPercent = 0;
    String batteryState = "unknown"; // battery | charging_or_usb | none
    uint32_t txBytes = 0;
    uint32_t rxBytes = 0;
    double balanceAmount = -1.0;
    String balanceCurrency;
    String balanceDisplay;
    String balanceRawText;
    String balanceUpdatedAt;
    bool balancePending = false;
};

struct ProxySocketState {
    bool open = false;
    bool remoteClosed = false;
    int availHint = 0;
};

class ModemManager {
public:
    static constexpr int kMaxLinks = 5; // Link IDs 0..4 for proxy sockets

    void begin(ConfigManager* cfg, SmsStore* store, WebhookClient* webhook, MqttHaClient* mqtt);
    void loopTaskTick();

    ModemStatus getStatus();

    // SMS
    bool sendSms(const String& to, const String& text, String& errorOut);
    void triggerSmsScan();

    // Voice Calls
    bool dial(const String& number, uint32_t autoHangupSec, String& errorOut);
    bool answer();
    bool hangup();
    bool sendDtmf(const String& digits);

    // USSD, Balance & Raw AT
    bool sendUssd(const String& code, String& responseOut, uint32_t timeoutMs = 15000);
    bool sendRawAt(const String& cmd, String& responseOut, uint32_t timeoutMs = 5000);
    bool requestBalanceCheck(String& messageOut, String& errorOut);

    // Cellular Data / Proxy TCP API
    bool ensureNetworkOpen(String& errorOut);
    bool closeNetwork();
    bool tcpOpen(int linkId, const String& host, uint16_t port, String& errorOut);
    bool tcpSend(int linkId, const uint8_t* data, size_t len);
    int tcpRecv(int linkId, uint8_t* outBuf, size_t maxLen, bool& remoteClosed);
    bool tcpClose(int linkId);
    bool isLinkOpen(int linkId);
    bool isLinkRemoteClosed(int linkId);
    bool hasLinkDataHint(int linkId);
    bool testCellularIp(String& externalIpOut, String& errorOut);

    // Power & SIM
    bool unlockSim(const String& pin);
    bool rebootModem();

private:
    bool lock(uint32_t timeoutMs = 15000);
    void unlock();

    void powerOnModem();
    bool initModemLocked();
    void configureSmsAndImsLocked();
    bool tryUnlockSimOnceLocked();
    void pollUrcsLocked();
    void handleUrcLineLocked(const String& line);
    void refreshStatusLocked();
    void scanSimSmsLocked();
    void sampleBattery();
    void loadBalance();
    void saveBalanceLocked();
    void maybeUpdateBalanceFromSmsLocked(const SmsRecord& rec, bool emitEvents);
    void updateBalanceLocked(double amount, const String& currency, const String& display,
                             const String& rawText, const String& timestamp, bool emitEvents);

    bool sendAtWaitOkLocked(const String& cmd, String& out, uint32_t timeoutMs = 3000);
    bool readLineLocked(String& lineOut, uint32_t timeoutMs);
    bool waitForCharLocked(char target, uint32_t timeoutMs);

    ConfigManager* _cfg = nullptr;
    SmsStore* _store = nullptr;
    WebhookClient* _webhook = nullptr;
    MqttHaClient* _mqtt = nullptr;

    SemaphoreHandle_t _mutex = nullptr;
    ModemStatus _status;
    ProxySocketState _links[kMaxLinks];

    String _rxLineBuf;
    volatile bool _smsScanRequested = false;
    volatile bool _ussdWaiting = false;
    String _ussdResult;
    bool _ussdDone = false;

    uint32_t _lastStatusPollMs = 0;
    uint32_t _lastSmsSweepMs = 0;
    uint32_t _lastRetryInitMs = 0;
    uint32_t _autoHangupAtMs = 0;
    uint8_t _smsMsgRef = 1;
    uint8_t _smsConcatRef = 1;
    String _pendingClipNumber;
    bool _incomingClipLogged = false;
    bool _simPinAttempted = false;
    volatile bool _balanceNotifyPending = false;
    uint32_t _balanceCheckSentMs = 0;
};
