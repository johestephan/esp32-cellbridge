#include "modem_manager.h"
#include "balance_parser.h"
#include "board_pins.h"
#include "config.h"
#include "sms_codec.h"
#include "webhook_client.h"
#include "mqtt_ha.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <ctime>

static int csqToPercent(int csq) {
    if (csq < 0 || csq > 31) return 0;
    return constrain(map(csq, 0, 31, 0, 100), 0, 100);
}

void ModemManager::begin(ConfigManager* cfg, SmsStore* store, WebhookClient* webhook, MqttHaClient* mqtt) {
    _cfg = cfg;
    _store = store;
    _webhook = webhook;
    _mqtt = mqtt;
    _mutex = xSemaphoreCreateMutex();

    // Critical hardware power-on pin (GPIO12)
    pinMode(BOARD_POWERON_PIN, OUTPUT);
    digitalWrite(BOARD_POWERON_PIN, HIGH);

    pinMode(MODEM_DTR_PIN, OUTPUT);
    digitalWrite(MODEM_DTR_PIN, LOW);

    pinMode(MODEM_RING_PIN, INPUT);
    pinMode(BOARD_BOOT_BTN_PIN, INPUT_PULLUP);
    pinMode(BOARD_BAT_ADC_PIN, INPUT);
    analogSetPinAttenuation(BOARD_BAT_ADC_PIN, ADC_11db);

    SerialAT.setRxBufferSize(4096);
    SerialAT.begin(MODEM_BAUDRATE, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
    sampleBattery();
    loadBalance();
    // Defer modem power-on & AT initialization to modemTask so WebServer starts immediately
}

static constexpr const char* kBalancePath = "/balance.json";

void ModemManager::saveBalanceLocked() {
    File f = LittleFS.open(kBalancePath, "w");
    if (!f) return;
    JsonDocument doc;
    doc["amount"] = _status.balanceAmount;
    doc["currency"] = _status.balanceCurrency;
    doc["display"] = _status.balanceDisplay;
    doc["raw"] = _status.balanceRawText;
    doc["updated_at"] = _status.balanceUpdatedAt;
    doc["low_balance_alerted"] = _lowBalanceAlerted;
    doc["last_auto_check_epoch"] = static_cast<uint64_t>(_lastAutoBalanceCheckEpoch);
    serializeJson(doc, f);
    f.close();
}

void ModemManager::loadBalance() {
    if (LittleFS.exists(kBalancePath)) {
        File f = LittleFS.open(kBalancePath, "r");
        if (f) {
            JsonDocument doc;
            if (deserializeJson(doc, f) == DeserializationError::Ok) {
                _status.balanceAmount = doc["amount"] | -1.0;
                _status.balanceCurrency = doc["currency"] | "";
                _status.balanceDisplay = doc["display"] | "";
                _status.balanceRawText = doc["raw"] | "";
                _status.balanceUpdatedAt = doc["updated_at"] | "";
                _lowBalanceAlerted = doc["low_balance_alerted"] | false;
                _lastAutoBalanceCheckEpoch = doc["last_auto_check_epoch"] | 0ULL;
            }
            f.close();
            if (_status.balanceAmount >= 0.0) return;
        }
    }

    // Seed from existing inbox history if available (avoids extra carrier query on boot)
    if (_store && _cfg) {
        auto inbox = _store->listSms("in", false, 20);
        const std::string target = _cfg->get().balanceTarget.c_str();
        for (const auto& rec : inbox) {
            if (isBalanceSenderMatch(rec.peer.c_str(), target, rec.text.c_str())) {
                ParsedBalance pb = parseBalanceText(rec.text.c_str());
                if (pb.valid) {
                    _status.balanceAmount = pb.amount;
                    _status.balanceCurrency = pb.currency.c_str();
                    _status.balanceDisplay = pb.formatted.c_str();
                    _status.balanceRawText = rec.text;
                    _status.balanceUpdatedAt = rec.timestamp;
                    saveBalanceLocked();
                    break;
                }
            }
        }
    }
}

void ModemManager::updateBalanceLocked(double amount, const String& currency, const String& display,
                                       const String& rawText, const String& timestamp, bool emitEvents) {
    _status.balanceAmount = amount;
    _status.balanceCurrency = currency;
    _status.balanceDisplay = display;
    _status.balanceRawText = rawText;
    _status.balanceUpdatedAt = timestamp.length() > 0 ? timestamp : SmsStore::fallbackTimestamp();
    _status.balancePending = false;

    if (emitEvents && _cfg) {
        const auto& c = _cfg->get();
        if (c.lowBalanceSmsEnabled && c.defaultContact.length() > 0 && amount >= 0.0) {
            if (amount <= c.lowBalanceThreshold && !_lowBalanceAlerted) {
                _lowBalanceAlerted = true;
                queueAlertSmsLocked("[" + c.hostname + "] Low prepaid balance: " + _status.balanceDisplay +
                                    " (threshold " + String(c.lowBalanceThreshold, 2) + ")");
            } else if (amount > c.lowBalanceThreshold + 0.25) {
                _lowBalanceAlerted = false;
            }
        }
        _balanceNotifyPending = true;
        if (_webhook) {
            JsonDocument d;
            d["amount"] = _status.balanceAmount;
            d["currency"] = _status.balanceCurrency;
            d["display"] = _status.balanceDisplay;
            d["updated_at"] = _status.balanceUpdatedAt;
            d["raw"] = _status.balanceRawText;
            _webhook->enqueueEvent("balance.updated", d);
        }
    }
    saveBalanceLocked();
}

static String digitsSuffix8(const String& s) {
    String d;
    for (size_t i = 0; i < s.length(); ++i) {
        char c = s[i];
        if (c >= '0' && c <= '9') d += c;
    }
    if (d.length() > 8) return d.substring(d.length() - 8);
    return d;
}

void ModemManager::queueAlertSmsLocked(const String& msg) {
    if (!_cfg || _cfg->get().defaultContact.length() == 0) return;
    if (_pendingAlertSms.size() >= 4) return;
    _pendingAlertSms.push_back(msg);
}

void ModemManager::evaluateAlertsLocked() {
    if (!_cfg) return;
    const auto& c = _cfg->get();

    // Debounce external power vs battery state transitions
    if (_status.batteryState == "charging_or_usb" || _status.batteryState == "battery") {
        if (_stablePowerState.length() == 0) {
            _stablePowerState = _status.batteryState;
            _pendingPowerState = "";
            _powerStateDebounceCount = 0;
        } else if (_status.batteryState != _stablePowerState) {
            if (_pendingPowerState == _status.batteryState) {
                ++_powerStateDebounceCount;
            } else {
                _pendingPowerState = _status.batteryState;
                _powerStateDebounceCount = 1;
            }
            if (_powerStateDebounceCount >= 3) {
                String oldState = _stablePowerState;
                _stablePowerState = _status.batteryState;
                _pendingPowerState = "";
                _powerStateDebounceCount = 0;
                if (c.powerLossSmsEnabled && c.defaultContact.length() > 0) {
                    if (oldState == "charging_or_usb" && _stablePowerState == "battery") {
                        queueAlertSmsLocked("[" + c.hostname + "] External power lost - running on battery (" +
                                            String(_status.batteryPercent) + "%, " +
                                            String(_status.batteryMv / 1000.0f, 2) + "V)");
                    } else if (oldState == "battery" && _stablePowerState == "charging_or_usb") {
                        queueAlertSmsLocked("[" + c.hostname + "] External power restored (" +
                                            String(_status.batteryPercent) + "%, " +
                                            String(_status.batteryMv / 1000.0f, 2) + "V)");
                    }
                }
            }
        } else {
            _pendingPowerState = "";
            _powerStateDebounceCount = 0;
        }
    }

    // Low battery SMS alert (only when actually running on battery, with hysteresis)
    if (_status.batteryState == "battery") {
        if (_status.batteryPercent <= c.lowBatteryThresholdPct) {
            if (_lowBatDebounceCount < 10) ++_lowBatDebounceCount;
            if (c.lowBatterySmsEnabled && !_lowBatteryAlerted && _lowBatDebounceCount >= 3 &&
                c.defaultContact.length() > 0) {
                _lowBatteryAlerted = true;
                queueAlertSmsLocked("[" + c.hostname + "] Low battery: " +
                                    String(_status.batteryPercent) + "% (" +
                                    String(_status.batteryMv / 1000.0f, 2) + "V)");
            }
        } else {
            _lowBatDebounceCount = 0;
            if (_status.batteryPercent >= c.lowBatteryThresholdPct + 10) {
                _lowBatteryAlerted = false;
            }
        }
    } else if (_status.batteryState == "charging_or_usb") {
        _lowBatDebounceCount = 0;
        _lowBatteryAlerted = false;
    }
}

void ModemManager::maybeForwardSmsLocked(const SmsRecord& rec) {
    if (!_cfg) return;
    const auto& c = _cfg->get();
    if (!c.smsForwardEnabled || c.defaultContact.length() == 0) return;

    // Avoid forwarding balance replies or messages from defaultContact itself (prevents SMS loops)
    if (isBalanceSenderMatch(rec.peer.c_str(), c.balanceTarget.c_str(), rec.text.c_str())) return;
    String peerSfx = digitsSuffix8(rec.peer);
    String defSfx = digitsSuffix8(c.defaultContact);
    if ((peerSfx.length() >= 7 && peerSfx == defSfx) || rec.peer == c.defaultContact) return;

    queueAlertSmsLocked("[FWD " + rec.peer + "] " + rec.text);
}

void ModemManager::maybeUpdateBalanceFromSmsLocked(const SmsRecord& rec, bool emitEvents) {
    if (!_cfg) return;
    if (!isBalanceSenderMatch(rec.peer.c_str(), _cfg->get().balanceTarget.c_str(), rec.text.c_str())) {
        return;
    }
    ParsedBalance pb = parseBalanceText(rec.text.c_str());
    if (pb.valid) {
        updateBalanceLocked(pb.amount, pb.currency.c_str(), pb.formatted.c_str(),
                            rec.text, rec.timestamp, emitEvents);
        Serial.printf("[Balance] Updated to %s from %s\n",
                      _status.balanceDisplay.c_str(), rec.peer.c_str());
    }
}

bool ModemManager::lock(uint32_t timeoutMs) {
    if (!_mutex) return false;
    return xSemaphoreTake(_mutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void ModemManager::unlock() {
    if (_mutex) xSemaphoreGive(_mutex);
}

void ModemManager::powerOnModem() {
    Serial.printf("[Modem] Probing %s on UART...\n", MODEM_MODEL_HINT);
    delay(300);

    String tmp;
    auto probePair = [&](int rxPin, int txPin) -> bool {
        SerialAT.end();
        SerialAT.setRxBufferSize(4096);
        SerialAT.begin(MODEM_BAUDRATE, SERIAL_8N1, rxPin, txPin);
        delay(50);
        while (SerialAT.available()) SerialAT.read();
        for (int i = 0; i < 2; ++i) {
            if (sendAtWaitOkLocked("AT", tmp, 450)) {
                Serial.printf("[Modem] Responded to AT on RX=%d, TX=%d!\n", rxPin, txPin);
                return true;
            }
        }
        return false;
    };

    if (probePair(MODEM_RX_PIN, MODEM_TX_PIN)) return;
    if (probePair(MODEM_TX_PIN, MODEM_RX_PIN)) return;

#ifdef MODEM_RESET_PIN
    pinMode(MODEM_RESET_PIN, OUTPUT);
    digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL);
    delay(100);
    digitalWrite(MODEM_RESET_PIN, MODEM_RESET_LEVEL);
    delay(2000);
    digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL);
    delay(200);
#endif

    Serial.println("[Modem] Pulsing PWRKEY (LOW->HIGH->LOW)...");
    pinMode(BOARD_PWRKEY_PIN, OUTPUT);
    digitalWrite(BOARD_PWRKEY_PIN, LOW);
    delay(100);
    digitalWrite(BOARD_PWRKEY_PIN, HIGH);
    delay(1100);
    digitalWrite(BOARD_PWRKEY_PIN, LOW);

    delay(2500);
    for (int round = 0; round < 5; ++round) {
        if (probePair(MODEM_RX_PIN, MODEM_TX_PIN)) return;
        if (probePair(MODEM_TX_PIN, MODEM_RX_PIN)) return;
        delay(500);
    }

    Serial.println("[Modem] No response yet, trying inverted PWRKEY (HIGH->LOW->HIGH)...");
    digitalWrite(BOARD_PWRKEY_PIN, HIGH);
    delay(100);
    digitalWrite(BOARD_PWRKEY_PIN, LOW);
    delay(1100);
    digitalWrite(BOARD_PWRKEY_PIN, HIGH);

    delay(2500);
    for (int round = 0; round < 4; ++round) {
        if (probePair(MODEM_RX_PIN, MODEM_TX_PIN)) return;
        if (probePair(MODEM_TX_PIN, MODEM_RX_PIN)) return;
        delay(500);
    }

    Serial.println("[Modem] Warning: Modem did not respond (check SIM/power/battery)");
}

bool ModemManager::tryUnlockSimOnceLocked() {
    if (_simPinAttempted || !_cfg) return false;
    const String& pin = _cfg->get().simPin;
    if (pin.length() == 0) return false;

    _simPinAttempted = true;
    String pinResp;
    if (sendAtWaitOkLocked("AT+CPIN=\"" + pin + "\"", pinResp, 5000)) {
        delay(1500);
        _status.simReady = true;
        _status.simState = "READY";
        return true;
    }
    return false;
}

void ModemManager::configureSmsAndImsLocked() {
    String resp;
    // Disable VoLTE IMS when CASIMS=0 so carriers (e.g. Odido NL) do not route MT-SMS into IMS where A7670E drops it
    sendAtWaitOkLocked("AT+CAVIMS=0", resp, 2000);

    // SMS in PDU mode + URC notifications (retry briefly in case SIM SMS memory is still initializing)
    sendAtWaitOkLocked("AT+CMGF=0", resp, 1500);
    if (!sendAtWaitOkLocked("AT+CPMS=\"SM\",\"SM\",\"SM\"", resp, 2000)) {
        delay(800);
        if (!sendAtWaitOkLocked("AT+CPMS=\"SM\",\"SM\",\"SM\"", resp, 2000)) {
            sendAtWaitOkLocked("AT+CPMS=\"ME\",\"ME\",\"ME\"", resp, 2000);
        }
    }
    if (!sendAtWaitOkLocked("AT+CNMI=2,1,0,0,0", resp, 1500)) {
        delay(500);
        sendAtWaitOkLocked("AT+CNMI=2,1,0,0,0", resp, 1500);
    }
}

bool ModemManager::initModemLocked() {
    String resp;
    if (!sendAtWaitOkLocked("ATE0", resp, 1500)) {
        _status.ready = false;
        return false;
    }
    _simPinAttempted = false;
    sendAtWaitOkLocked("AT+CMEE=2", resp, 1500);

    if (sendAtWaitOkLocked("AT+CGMI", resp, 1500)) {
        resp.replace("OK", ""); resp.trim();
        _status.manufacturer = resp;
    }
    if (sendAtWaitOkLocked("AT+CGMM", resp, 1500)) {
        resp.replace("OK", ""); resp.trim();
        _status.model = resp;
    }
    if (sendAtWaitOkLocked("AT+CGSN", resp, 1500)) {
        resp.replace("OK", ""); resp.trim();
        _status.imei = resp;
    }

    // SIM PIN check (retry briefly in case SIM is still initializing right after boot)
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (sendAtWaitOkLocked("AT+CPIN?", resp, 2000)) {
            if (resp.indexOf("READY") >= 0) {
                _status.simReady = true;
                _status.simState = "READY";
                break;
            }
            if (resp.indexOf("SIM PIN") >= 0) {
                _status.simState = "SIM_PIN";
                tryUnlockSimOnceLocked();
                break;
            }
            _status.simState = "ERROR";
            _status.simReady = false;
            break;
        }
        delay(600);
    }

    configureSmsAndImsLocked();

    // Voice call caller ID & allow ATH/CHUP hangup
    sendAtWaitOkLocked("AT+CLIP=1", resp, 1500);
    sendAtWaitOkLocked("AT+CVHU=0", resp, 1500);

    // Registration URCs & manual TCP socket buffer mode
    sendAtWaitOkLocked("AT+CREG=1", resp, 1500);
    sendAtWaitOkLocked("AT+CEREG=1", resp, 1500);
    sendAtWaitOkLocked("AT+CIPRXGET=1", resp, 1500);

    _status.ready = true;
    return true;
}

static int mvToLiIonPercent(int mv) {
    if (mv < 2500) return 0;
    struct Point { int mv; int pct; };
    static const Point kCurve[] = {
        {4200, 100}, {4100, 92}, {4000, 82}, {3900, 70},
        {3800, 55},  {3730, 44}, {3650, 25}, {3550, 12},
        {3400, 3},   {3250, 0}
    };
    if (mv >= kCurve[0].mv) return 100;
    constexpr size_t n = sizeof(kCurve) / sizeof(kCurve[0]);
    if (mv <= kCurve[n - 1].mv) return 0;
    for (size_t i = 0; i + 1 < n; ++i) {
        if (mv >= kCurve[i + 1].mv) {
            int spanMv = kCurve[i].mv - kCurve[i + 1].mv;
            int spanPct = kCurve[i].pct - kCurve[i + 1].pct;
            return kCurve[i + 1].pct + ((mv - kCurve[i + 1].mv) * spanPct) / spanMv;
        }
    }
    return 0;
}

void ModemManager::sampleBattery() {
    uint32_t sumMv = 0;
    for (int i = 0; i < 16; ++i) {
        sumMv += analogReadMilliVolts(BOARD_BAT_ADC_PIN);
        delay(2);
    }
    int mv = static_cast<int>((sumMv / 16) * 2); // 1:1 voltage divider on GPIO35
    _status.batteryMv = mv;
    if (mv < 2500) {
        _status.batteryPercent = 0;
        _status.batteryState = "none";
    } else if (mv >= 4215) {
        _status.batteryPercent = 100;
        _status.batteryState = "charging_or_usb";
    } else {
        _status.batteryPercent = mvToLiIonPercent(mv);
        _status.batteryState = "battery";
    }
}

void ModemManager::refreshStatusLocked() {
    sampleBattery();
    evaluateAlertsLocked();
    String resp;

    if (!sendAtWaitOkLocked("AT", resp, 1000)) {
        _status.ready = false;
        return;
    }
    _status.ready = true;

    if (sendAtWaitOkLocked("AT+CPIN?", resp, 1500)) {
        _status.simReady = (resp.indexOf("READY") >= 0);
        if (_status.simReady) {
            _status.simState = "READY";
        } else if (resp.indexOf("SIM PIN") >= 0) {
            _status.simState = "SIM_PIN";
            if (tryUnlockSimOnceLocked()) {
                configureSmsAndImsLocked();
                _smsScanRequested = true;
            }
        } else {
            _status.simState = "NOT_INSERTED";
        }
    }

    if (sendAtWaitOkLocked("AT+CSQ", resp, 1500)) {
        int idx = resp.indexOf("+CSQ:");
        if (idx >= 0) {
            int csq = resp.substring(idx + 5).toInt();
            _status.csq = csq;
            _status.rssiDbm = (csq == 99) ? -113 : (-113 + 2 * csq);
            _status.signalPercent = csqToPercent(csq);
        }
    }

    if (sendAtWaitOkLocked("AT+CEREG?", resp, 1500)) {
        int comma = resp.indexOf(',');
        if (comma >= 0) {
            _status.regStatus = resp.substring(comma + 1).toInt();
        }
    } else if (sendAtWaitOkLocked("AT+CREG?", resp, 1500)) {
        int comma = resp.indexOf(',');
        if (comma >= 0) {
            _status.regStatus = resp.substring(comma + 1).toInt();
        }
    }

    if (sendAtWaitOkLocked("AT+COPS?", resp, 2000)) {
        int q1 = resp.indexOf('"');
        int q2 = (q1 >= 0) ? resp.indexOf('"', q1 + 1) : -1;
        if (q1 >= 0 && q2 > q1) {
            _status.operatorName = resp.substring(q1 + 1, q2);
        }
    }

    if (sendAtWaitOkLocked("AT+CPSI?", resp, 1500)) {
        int p = resp.indexOf("+CPSI:");
        if (p >= 0) {
            int c1 = resp.indexOf(',', p);
            if (c1 > p) {
                _status.networkTech = resp.substring(p + 6, c1);
                _status.networkTech.trim();
            }
        }
    }

    if (sendAtWaitOkLocked("AT+NETOPEN?", resp, 1500)) {
        _status.dataConnected = (resp.indexOf("+NETOPEN: 1") >= 0);
        if (_status.dataConnected && sendAtWaitOkLocked("AT+IPADDR", resp, 1500)) {
            int p = resp.indexOf("+IPADDR:");
            if (p >= 0) {
                String ip = resp.substring(p + 8);
                int okIdx = ip.indexOf("OK");
                if (okIdx >= 0) ip = ip.substring(0, okIdx);
                ip.trim();
                _status.cellularIp = ip;
            }
        } else if (!_status.dataConnected) {
            _status.cellularIp = "";
        }
    }
}

void ModemManager::loopTaskTick() {
    String alertToSend;
    String alertTarget;
    bool shouldRunAutoBalance = false;

    if (lock(20)) {
        if (!_status.ready && (_lastRetryInitMs == 0 || millis() - _lastRetryInitMs > 20000)) {
            _lastRetryInitMs = millis();
            powerOnModem();
            if (initModemLocked()) {
                refreshStatusLocked();
                scanSimSmsLocked();
            }
            unlock();
            return;
        }

        pollUrcsLocked();

        if (_autoHangupAtMs > 0 && millis() >= _autoHangupAtMs) {
            _autoHangupAtMs = 0;
            String resp;
            sendAtWaitOkLocked("AT+CHUP", resp, 2000);
            _status.callState = "idle";
        }

        if (_smsScanRequested || (millis() - _lastSmsSweepMs > 60000)) {
            _smsScanRequested = false;
            _lastSmsSweepMs = millis();
            if (_status.ready && _status.simReady) {
                scanSimSmsLocked();
            }
        }

        if (millis() - _lastStatusPollMs > MODEM_STATUS_INTERVAL_MS) {
            _lastStatusPollMs = millis();
            refreshStatusLocked();
        }
        if (_status.balancePending && (millis() - _balanceCheckSentMs > 60000)) {
            _status.balancePending = false;
        }

        // Dequeue at most 1 pending SMS alert/forward when registered on cellular network
        bool netOk = _status.ready && _status.simReady && (_status.regStatus == 1 || _status.regStatus == 5);
        if (netOk && !_pendingAlertSms.empty() && _cfg && _cfg->get().defaultContact.length() > 0) {
            alertToSend = _pendingAlertSms.front();
            _pendingAlertSms.erase(_pendingAlertSms.begin());
            alertTarget = _cfg->get().defaultContact;
        }

        // Periodic balance check timer (every balanceCheckIntervalHours)
        if (netOk && !_status.balancePending && _cfg && _cfg->get().balanceCheckIntervalHours > 0 && millis() > 60000) {
            uint32_t intervalSec = static_cast<uint32_t>(_cfg->get().balanceCheckIntervalHours) * 3600UL;
            time_t now = time(nullptr);
            if (now > 1700000000) {
                uint64_t uNow = static_cast<uint64_t>(now);
                if (_lastAutoBalanceCheckEpoch == 0 || (uNow > _lastAutoBalanceCheckEpoch && (uNow - _lastAutoBalanceCheckEpoch) >= intervalSec)) {
                    _lastAutoBalanceCheckEpoch = uNow;
                    _lastAutoBalanceCheckMs = millis();
                    saveBalanceLocked();
                    shouldRunAutoBalance = true;
                }
            } else if (_lastAutoBalanceCheckMs == 0 || (millis() - _lastAutoBalanceCheckMs) >= (intervalSec * 1000UL)) {
                _lastAutoBalanceCheckMs = millis();
                shouldRunAutoBalance = true;
            }
        }
        unlock();
    }

    if (alertToSend.length() > 0 && alertTarget.length() > 0) {
        String err;
        if (!sendSms(alertTarget, alertToSend, err)) {
            Serial.printf("[Alert] Failed to send alert SMS to %s: %s\n", alertTarget.c_str(), err.c_str());
        }
    }

    if (shouldRunAutoBalance) {
        String msg, err;
        requestBalanceCheck(msg, err);
    }

    if (_balanceNotifyPending && _mqtt) {
        _balanceNotifyPending = false;
        _mqtt->publishStatus();
    }

    // Flush expired incomplete multipart SMS
    if (_store) {
        std::vector<SmsRecord> flushed;
        _store->checkMultipartTimeouts(flushed);
        for (const auto& rec : flushed) {
            if (_webhook) {
                JsonDocument d;
                d["id"] = rec.id;
                d["from"] = rec.peer;
                d["message"] = rec.text;
                d["timestamp"] = rec.timestamp;
                d["partial"] = true;
                _webhook->enqueueEvent("sms.received", d);
            }
            if (_mqtt) _mqtt->publishIncomingSms(rec);
        }
    }
}

ModemStatus ModemManager::getStatus() {
    ModemStatus copy;
    if (lock(500)) {
        copy = _status;
        unlock();
    } else {
        copy = _status;
    }
    return copy;
}

void ModemManager::triggerSmsScan() {
    _smsScanRequested = true;
}

void ModemManager::pollUrcsLocked() {
    while (SerialAT.available()) {
        char c = static_cast<char>(SerialAT.read());
        if (c == '\r') continue;
        if (c == '\n') {
            _rxLineBuf.trim();
            if (_rxLineBuf.length() > 0) {
                String line = _rxLineBuf;
                _rxLineBuf = "";
                handleUrcLineLocked(line);
            } else {
                _rxLineBuf = "";
            }
        } else {
            if (_rxLineBuf.length() < 512) {
                _rxLineBuf += c;
            }
        }
    }
}

void ModemManager::handleUrcLineLocked(const String& line) {
    if (line.startsWith("+CMTI:")) {
        Serial.printf("[Modem URC] %s\n", line.c_str());
        _smsScanRequested = true;
    } else if (line.startsWith("RING")) {
        _status.callState = "ringing";
        _incomingClipLogged = false;
        Serial.println("[Modem URC] RING");
    } else if (line.startsWith("+CLIP:")) {
        int q1 = line.indexOf('"');
        int q2 = (q1 >= 0) ? line.indexOf('"', q1 + 1) : -1;
        String num = (q1 >= 0 && q2 > q1) ? line.substring(q1 + 1, q2) : "Unknown";
        _status.callState = "ringing";
        _status.activeCallPeer = num;
        if (!_incomingClipLogged && _store) {
            _incomingClipLogged = true;
            CallRecord rec = _store->addCall("in", num, "", "ringing");
            _status.activeCallId = rec.id;
            _status.callStartMs = millis();

            if (_webhook) {
                JsonDocument d;
                d["id"] = rec.id;
                d["from"] = num;
                d["timestamp"] = rec.timestamp;
                _webhook->enqueueEvent("call.incoming", d);
            }
            if (_mqtt) _mqtt->publishCallEvent("ringing", num);
        }
    } else if (line.startsWith("VOICE CALL: BEGIN")) {
        _status.callState = "active";
        _status.callStartMs = millis();
        if (_store && _status.activeCallId > 0) {
            _store->updateLastCall(_status.activeCallId, "answered", 0);
        }
        if (_mqtt) _mqtt->publishCallEvent("active", _status.activeCallPeer);
    } else if (line.startsWith("VOICE CALL: END") || line == "NO CARRIER" || line == "BUSY" || line == "NO ANSWER") {
        uint32_t dur = (_status.callStartMs > 0) ? (millis() - _status.callStartMs) / 1000 : 0;
        String endStatus = (line == "NO CARRIER" && _status.callState == "ringing") ? "missed" : "ended";
        if (_store && _status.activeCallId > 0) {
            _store->updateLastCall(_status.activeCallId, endStatus, dur);
        }
        if (_webhook) {
            JsonDocument d;
            d["id"] = _status.activeCallId;
            d["peer"] = _status.activeCallPeer;
            d["status"] = endStatus;
            d["duration_sec"] = dur;
            _webhook->enqueueEvent("call.ended", d);
        }
        if (_mqtt) _mqtt->publishCallEvent(endStatus, _status.activeCallPeer);
        _status.callState = "idle";
        _status.activeCallPeer = "";
        _status.activeCallId = 0;
        _status.callStartMs = 0;
        _autoHangupAtMs = 0;
        _incomingClipLogged = false;
    } else if (line.startsWith("+CUSD:")) {
        _ussdResult = line;
        _ussdDone = true;
    } else if (line.startsWith("+CIPRXGET: 1,")) {
        int id = line.substring(13).toInt();
        if (id >= 0 && id < kMaxLinks) {
            _links[id].availHint++;
        }
    } else if (line.startsWith("+IPCLOSE:")) {
        int id = line.substring(9).toInt();
        if (id >= 0 && id < kMaxLinks) {
            _links[id].open = false;
            _links[id].remoteClosed = true;
        }
    } else if (line.startsWith("+NETOPEN:")) {
        int err = line.substring(9).toInt();
        _status.dataConnected = (err == 0);
    }
}

bool ModemManager::readLineLocked(String& lineOut, uint32_t timeoutMs) {
    lineOut = "";
    const uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        while (SerialAT.available()) {
            char c = static_cast<char>(SerialAT.read());
            if (c == '\r') continue;
            if (c == '\n') {
                lineOut.trim();
                if (lineOut.length() > 0) return true;
            } else {
                lineOut += c;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return false;
}

bool ModemManager::waitForCharLocked(char target, uint32_t timeoutMs) {
    const uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        while (SerialAT.available()) {
            char c = static_cast<char>(SerialAT.read());
            if (c == target) return true;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return false;
}

bool ModemManager::sendAtWaitOkLocked(const String& cmd, String& out, uint32_t timeoutMs) {
    pollUrcsLocked();
    out = "";
    SerialAT.print(cmd);
    SerialAT.print("\r\n");

    const uint32_t start = millis();
    String line;
    while (millis() - start < timeoutMs) {
        uint32_t rem = timeoutMs - (millis() - start);
        if (!readLineLocked(line, rem)) break;

        if (line == "OK") {
            out += "OK";
            return true;
        }
        if (line == "ERROR" || line.startsWith("+CME ERROR:") || line.startsWith("+CMS ERROR:")) {
            out += line;
            return false;
        }
        // Check if this line is an asynchronous URC unrelated to the command
        if (line.startsWith("+CMTI:") || line == "RING" || line.startsWith("+CLIP:") ||
            line.startsWith("VOICE CALL:") || line.startsWith("+CIPRXGET: 1,") ||
            line.startsWith("+IPCLOSE:")) {
            handleUrcLineLocked(line);
            continue;
        }
        out += line + "\n";
    }
    return false;
}

void ModemManager::scanSimSmsLocked() {
    String resp;
    if (!sendAtWaitOkLocked("AT+CMGF=0", resp, 1500)) return;

    SerialAT.print("AT+CMGL=4\r\n");
    const uint32_t start = millis();
    String line;
    int pendingIndex = -1;
    std::vector<int> indicesToDelete;

    while (millis() - start < 8000) {
        if (!readLineLocked(line, 2500)) break;
        if (line == "OK") break;
        if (line == "ERROR" || line.startsWith("+CMS ERROR:")) break;

        if (line.startsWith("+CMGL:")) {
            int colon = line.indexOf(':');
            int comma = line.indexOf(',', colon);
            if (colon >= 0 && comma > colon) {
                pendingIndex = line.substring(colon + 1, comma).toInt();
            }
        } else if (pendingIndex >= 0 && line.length() >= 8) {
            SmsCodec::DecodedSms decoded = SmsCodec::decodeDeliverPdu(line.c_str());
            if (decoded.valid && _store) {
                SmsRecord completed;
                if (_store->ingestDecodedSms(decoded, completed)) {
                    Serial.printf("[SMS] Received from %s: %s\n",
                                  completed.peer.c_str(), completed.text.c_str());
                    if (_webhook) {
                        JsonDocument d;
                        d["id"] = completed.id;
                        d["from"] = completed.peer;
                        d["message"] = completed.text;
                        d["timestamp"] = completed.timestamp;
                        d["parts"] = completed.parts;
                        _webhook->enqueueEvent("sms.received", d);
                    }
                    if (_mqtt) {
                        _mqtt->publishIncomingSms(completed);
                    }
                    maybeUpdateBalanceFromSmsLocked(completed, true);
                    maybeForwardSmsLocked(completed);
                }
                indicesToDelete.push_back(pendingIndex);
            }
            pendingIndex = -1;
        }
    }

    if (_cfg->get().autoDeleteSimSms) {
        for (int idx : indicesToDelete) {
            sendAtWaitOkLocked("AT+CMGD=" + String(idx), resp, 2000);
        }
    }
}

bool ModemManager::sendSms(const String& to, const String& text, String& errorOut) {
    errorOut = "";
    if (to.length() == 0 || text.length() == 0) {
        errorOut = "Recipient and message cannot be empty";
        return false;
    }

    std::vector<SmsCodec::EncodedPdu> pdus = SmsCodec::encodeSubmitPdus(
        to.c_str(), text.c_str(), _smsMsgRef++, _smsConcatRef++);

    if (pdus.empty()) {
        errorOut = "Failed to encode PDU";
        return false;
    }

    if (!lock(20000)) {
        errorOut = "Modem busy";
        return false;
    }

    String resp;
    if (!sendAtWaitOkLocked("AT+CMGF=0", resp, 2000)) {
        unlock();
        errorOut = "Failed to switch modem to PDU mode";
        return false;
    }

    bool allOk = true;
    for (const auto& p : pdus) {
        pollUrcsLocked();
        SerialAT.printf("AT+CMGS=%d\r\n", p.tpduLength);
        if (!waitForCharLocked('>', 5000)) {
            errorOut = "Timeout waiting for '>' prompt";
            allOk = false;
            break;
        }
        SerialAT.print(p.hexPdu.c_str());
        SerialAT.write(0x1A); // Ctrl+Z

        String line;
        bool partOk = false;
        const uint32_t start = millis();
        while (millis() - start < 25000) {
            if (!readLineLocked(line, 25000 - (millis() - start))) break;
            if (line == "OK") {
                partOk = true;
                break;
            }
            if (line == "ERROR" || line.startsWith("+CMS ERROR:") || line.startsWith("+CME ERROR:")) {
                errorOut = line;
                break;
            }
        }
        if (!partOk) {
            if (errorOut.length() == 0) errorOut = "SMS send timeout";
            allOk = false;
            break;
        }
    }

    unlock();

    if (_store) {
        _store->addSms("out", to, text, "", allOk ? "sent" : "failed",
                       static_cast<uint8_t>(pdus.size()), true);
    }
    return allOk;
}

bool ModemManager::dial(const String& number, uint32_t autoHangupSec, String& errorOut) {
    if (number.length() == 0) {
        errorOut = "Phone number required";
        return false;
    }
    if (!lock(5000)) {
        errorOut = "Modem busy";
        return false;
    }
    String resp;
    bool ok = sendAtWaitOkLocked("ATD" + number + ";", resp, 5000);
    if (ok) {
        _status.callState = "dialing";
        _status.activeCallPeer = number;
        _status.callStartMs = millis();
        if (_store) {
            CallRecord rec = _store->addCall("out", number, "", "dialing");
            _status.activeCallId = rec.id;
        }
        _autoHangupAtMs = (autoHangupSec > 0) ? (millis() + autoHangupSec * 1000UL) : 0;
        if (_mqtt) _mqtt->publishCallEvent("dialing", number);
    } else {
        errorOut = resp.length() ? resp : "Dial failed";
    }
    unlock();
    return ok;
}

bool ModemManager::answer() {
    if (!lock(5000)) return false;
    String resp;
    bool ok = sendAtWaitOkLocked("ATA", resp, 3000);
    if (ok) {
        _status.callState = "active";
        _status.callStartMs = millis();
    }
    unlock();
    return ok;
}

bool ModemManager::hangup() {
    if (!lock(5000)) return false;
    String resp;
    bool ok = sendAtWaitOkLocked("AT+CHUP", resp, 3000);
    if (!ok) ok = sendAtWaitOkLocked("ATH", resp, 2000);
    _autoHangupAtMs = 0;
    uint32_t dur = (_status.callStartMs > 0) ? (millis() - _status.callStartMs) / 1000 : 0;
    if (_store && _status.activeCallId > 0) {
        _store->updateLastCall(_status.activeCallId, "ended", dur);
    }
    _status.callState = "idle";
    _status.activeCallPeer = "";
    _status.activeCallId = 0;
    _status.callStartMs = 0;
    unlock();
    return ok;
}

bool ModemManager::sendDtmf(const String& digits) {
    if (digits.length() == 0) return false;
    if (!lock(5000)) return false;
    String resp;
    bool ok = true;
    for (size_t i = 0; i < digits.length(); ++i) {
        char c = digits[i];
        if (!sendAtWaitOkLocked("AT+VTS=\"" + String(c) + "\"", resp, 1500)) {
            ok = false;
            break;
        }
    }
    unlock();
    return ok;
}

bool ModemManager::sendUssd(const String& code, String& responseOut, uint32_t timeoutMs) {
    responseOut = "";
    if (!lock(timeoutMs + 2000)) {
        responseOut = "Modem busy";
        return false;
    }

    _ussdResult = "";
    _ussdDone = false;
    String resp;
    if (!sendAtWaitOkLocked("AT+CUSD=1,\"" + code + "\",15", resp, 3000)) {
        unlock();
        responseOut = resp.length() ? resp : "AT+CUSD failed";
        return false;
    }

    const uint32_t start = millis();
    String line;
    while (millis() - start < timeoutMs) {
        if (_ussdDone) break;
        if (readLineLocked(line, 500)) {
            handleUrcLineLocked(line);
        }
    }
    unlock();

    if (!_ussdDone) {
        responseOut = "USSD timeout (note: some carriers like Odido NL use SMS/app instead of USSD)";
        return false;
    }

    int q1 = _ussdResult.indexOf('"');
    int q2 = (q1 >= 0) ? _ussdResult.lastIndexOf('"') : -1;
    if (q1 >= 0 && q2 > q1) {
        responseOut = _ussdResult.substring(q1 + 1, q2);
    } else {
        responseOut = _ussdResult;
    }
    return true;
}

bool ModemManager::requestBalanceCheck(String& messageOut, String& errorOut) {
    messageOut = "";
    errorOut = "";
    if (!_cfg) {
        errorOut = "Config unavailable";
        return false;
    }
    const auto cfg = _cfg->get();
    if (cfg.balanceTarget.length() == 0) {
        errorOut = "Balance check target not configured";
        return false;
    }

    if (cfg.balanceMethod == "ussd") {
        String resp;
        if (!sendUssd(cfg.balanceTarget, resp)) {
            errorOut = resp;
            return false;
        }
        ParsedBalance pb = parseBalanceText(resp.c_str());
        if (lock(5000)) {
            _lastAutoBalanceCheckMs = millis();
            time_t now = time(nullptr);
            if (now > 1700000000) _lastAutoBalanceCheckEpoch = static_cast<uint64_t>(now);
            if (pb.valid) {
                updateBalanceLocked(pb.amount, pb.currency.c_str(), pb.formatted.c_str(),
                                    resp, SmsStore::fallbackTimestamp(), true);
                messageOut = "Balance: " + _status.balanceDisplay;
            } else {
                _status.balanceRawText = resp;
                _status.balanceUpdatedAt = SmsStore::fallbackTimestamp();
                _status.balancePending = false;
                saveBalanceLocked();
                messageOut = "USSD response received";
            }
            unlock();
        }
        return true;
    }

    String msgBody = cfg.balanceMessage.length() > 0 ? cfg.balanceMessage : "SALDO";
    if (lock(2000)) {
        _status.balancePending = true;
        _balanceCheckSentMs = millis();
        _lastAutoBalanceCheckMs = millis();
        time_t now = time(nullptr);
        if (now > 1700000000) {
            _lastAutoBalanceCheckEpoch = static_cast<uint64_t>(now);
            saveBalanceLocked();
        }
        unlock();
    }
    if (!sendSms(cfg.balanceTarget, msgBody, errorOut)) {
        if (lock(2000)) {
            _status.balancePending = false;
            unlock();
        }
        return false;
    }
    messageOut = "Balance check SMS sent to " + cfg.balanceTarget + "; awaiting reply";
    return true;
}

bool ModemManager::sendRawAt(const String& cmd, String& responseOut, uint32_t timeoutMs) {
    if (!lock(timeoutMs + 2000)) {
        responseOut = "Modem busy";
        return false;
    }
    bool ok = sendAtWaitOkLocked(cmd, responseOut, timeoutMs);
    unlock();
    return ok;
}

bool ModemManager::ensureNetworkOpen(String& errorOut) {
    errorOut = "";
    if (!lock(15000)) {
        errorOut = "Modem busy";
        return false;
    }

    String resp;
    if (sendAtWaitOkLocked("AT+NETOPEN?", resp, 1500) && resp.indexOf("+NETOPEN: 1") >= 0) {
        _status.dataConnected = true;
        unlock();
        return true;
    }

    const auto& cfg = _cfg->get();
    String apn = cfg.apn.length() ? cfg.apn : "internet.mobiel";
    sendAtWaitOkLocked("AT+CGDCONT=1,\"IP\",\"" + apn + "\"", resp, 2000);
    if (cfg.apnUser.length() > 0) {
        sendAtWaitOkLocked("AT+CGAUTH=1,1,\"" + cfg.apnPass + "\",\"" + cfg.apnUser + "\"", resp, 2000);
    }
    sendAtWaitOkLocked("AT+CIPRXGET=1", resp, 1500);

    SerialAT.print("AT+NETOPEN\r\n");
    const uint32_t start = millis();
    String line;
    bool ok = false;
    while (millis() - start < 15000) {
        if (!readLineLocked(line, 1000)) continue;
        if (line.startsWith("+NETOPEN:")) {
            int code = line.substring(9).toInt();
            ok = (code == 0);
            break;
        }
        if (line == "ERROR" || line.startsWith("+CME ERROR:")) {
            errorOut = line;
            break;
        }
    }

    if (ok) {
        _status.dataConnected = true;
        if (sendAtWaitOkLocked("AT+IPADDR", resp, 1500)) {
            int p = resp.indexOf("+IPADDR:");
            if (p >= 0) {
                String ip = resp.substring(p + 8);
                int okIdx = ip.indexOf("OK");
                if (okIdx >= 0) ip = ip.substring(0, okIdx);
                ip.trim();
                _status.cellularIp = ip;
            }
        }
    } else if (errorOut.length() == 0) {
        errorOut = "AT+NETOPEN failed (check SIM data balance / APN)";
    }

    unlock();
    return ok;
}

bool ModemManager::closeNetwork() {
    if (!lock(5000)) return false;
    for (int i = 0; i < kMaxLinks; ++i) {
        if (_links[i].open) {
            String r;
            sendAtWaitOkLocked("AT+CIPCLOSE=" + String(i), r, 1500);
            _links[i].open = false;
        }
    }
    String resp;
    bool ok = sendAtWaitOkLocked("AT+NETCLOSE", resp, 4000);
    _status.dataConnected = false;
    _status.cellularIp = "";
    unlock();
    return ok;
}

bool ModemManager::tcpOpen(int linkId, const String& host, uint16_t port, String& errorOut) {
    if (linkId < 0 || linkId >= kMaxLinks) {
        errorOut = "Invalid link ID";
        return false;
    }
    if (!ensureNetworkOpen(errorOut)) return false;

    if (!lock(15000)) {
        errorOut = "Modem busy";
        return false;
    }

    String resp;
    if (_links[linkId].open) {
        sendAtWaitOkLocked("AT+CIPCLOSE=" + String(linkId), resp, 1500);
    }
    _links[linkId].open = false;
    _links[linkId].remoteClosed = false;
    _links[linkId].availHint = 0;

    SerialAT.printf("AT+CIPOPEN=%d,\"TCP\",\"%s\",%u\r\n", linkId, host.c_str(), port);
    const uint32_t start = millis();
    String line;
    bool ok = false;
    while (millis() - start < 15000) {
        if (!readLineLocked(line, 1000)) continue;
        if (line.startsWith("+CIPOPEN:")) {
            int comma = line.indexOf(',');
            int err = (comma >= 0) ? line.substring(comma + 1).toInt() : -1;
            ok = (err == 0);
            if (!ok) errorOut = "CIPOPEN err=" + String(err);
            break;
        }
        if (line == "ERROR" || line.startsWith("+CME ERROR:")) {
            errorOut = line;
            break;
        }
        handleUrcLineLocked(line);
    }

    _links[linkId].open = ok;
    unlock();
    return ok;
}

bool ModemManager::tcpSend(int linkId, const uint8_t* data, size_t len) {
    if (linkId < 0 || linkId >= kMaxLinks || len == 0) return false;
    if (!lock(5000)) return false;

    pollUrcsLocked();
    SerialAT.printf("AT+CIPSEND=%d,%u\r\n", linkId, static_cast<unsigned>(len));
    if (!waitForCharLocked('>', 3000)) {
        unlock();
        return false;
    }

    SerialAT.write(data, len);
    _status.txBytes += len;

    const uint32_t start = millis();
    String line;
    bool ok = false;
    while (millis() - start < 8000) {
        if (!readLineLocked(line, 1500)) continue;
        if (line.startsWith("+CIPSEND:")) {
            ok = true;
            break;
        }
        if (line == "ERROR" || line.startsWith("+IPCLOSE:")) {
            handleUrcLineLocked(line);
            break;
        }
        if (line == "OK") continue;
        handleUrcLineLocked(line);
    }

    unlock();
    return ok;
}

int ModemManager::tcpRecv(int linkId, uint8_t* outBuf, size_t maxLen, bool& remoteClosed) {
    remoteClosed = false;
    if (linkId < 0 || linkId >= kMaxLinks || maxLen == 0) return -1;
    if (!lock(3000)) return 0;

    pollUrcsLocked();
    remoteClosed = _links[linkId].remoteClosed;

    SerialAT.printf("AT+CIPRXGET=2,%d,%u\r\n", linkId, static_cast<unsigned>(maxLen));

    const uint32_t start = millis();
    String line;
    int actualLen = 0;
    bool gotHeader = false;

    while (millis() - start < 3000) {
        if (!readLineLocked(line, 1000)) break;
        if (line.startsWith("+CIPRXGET: 2,")) {
            // +CIPRXGET: 2,<link>,<actual_len>,<rest_len>
            int c1 = line.indexOf(',');
            int c2 = line.indexOf(',', c1 + 1);
            int c3 = line.indexOf(',', c2 + 1);
            if (c2 > 0 && c3 > c2) {
                actualLen = line.substring(c2 + 1, c3).toInt();
                int restLen = line.substring(c3 + 1).toInt();
                _links[linkId].availHint = (restLen > 0) ? 1 : 0;
                gotHeader = true;
            }
            break;
        }
        if (line == "OK") {
            _links[linkId].availHint = 0;
            unlock();
            return 0;
        }
        if (line == "ERROR" || line.startsWith("+CME ERROR:")) {
            _links[linkId].availHint = 0;
            unlock();
            return 0;
        }
        handleUrcLineLocked(line);
    }

    if (!gotHeader || actualLen <= 0) {
        unlock();
        return 0;
    }

    size_t toRead = std::min<size_t>(static_cast<size_t>(actualLen), maxLen);
    size_t got = 0;
    const uint32_t binStart = millis();
    while (got < toRead && millis() - binStart < 3000) {
        if (SerialAT.available()) {
            outBuf[got++] = static_cast<uint8_t>(SerialAT.read());
        } else {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    // Discard any excess if actualLen > maxLen
    size_t rem = static_cast<size_t>(actualLen) - got;
    while (rem > 0 && millis() - binStart < 3000) {
        if (SerialAT.available()) {
            SerialAT.read();
            rem--;
        } else {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    // Consume trailing OK
    readLineLocked(line, 500);
    _status.rxBytes += got;

    unlock();
    return static_cast<int>(got);
}

bool ModemManager::tcpClose(int linkId) {
    if (linkId < 0 || linkId >= kMaxLinks) return false;
    if (!lock(3000)) return false;
    String resp;
    bool ok = sendAtWaitOkLocked("AT+CIPCLOSE=" + String(linkId), resp, 2000);
    _links[linkId].open = false;
    _links[linkId].remoteClosed = false;
    _links[linkId].availHint = 0;
    unlock();
    return ok;
}

bool ModemManager::isLinkOpen(int linkId) {
    if (linkId < 0 || linkId >= kMaxLinks) return false;
    return _links[linkId].open;
}

bool ModemManager::isLinkRemoteClosed(int linkId) {
    if (linkId < 0 || linkId >= kMaxLinks) return true;
    return _links[linkId].remoteClosed;
}

bool ModemManager::hasLinkDataHint(int linkId) {
    if (linkId < 0 || linkId >= kMaxLinks) return false;
    return _links[linkId].availHint > 0;
}

bool ModemManager::testCellularIp(String& externalIpOut, String& errorOut) {
    externalIpOut = "";
    const int testLink = 4;
    if (!tcpOpen(testLink, "api.ipify.org", 80, errorOut)) {
        return false;
    }

    const char* req = "GET / HTTP/1.1\r\nHost: api.ipify.org\r\nConnection: close\r\n\r\n";
    if (!tcpSend(testLink, reinterpret_cast<const uint8_t*>(req), strlen(req))) {
        tcpClose(testLink);
        errorOut = "Failed to send HTTP test request";
        return false;
    }

    String response;
    uint8_t buf[512];
    const uint32_t start = millis();
    while (millis() - start < 8000) {
        bool remoteClosed = false;
        int n = tcpRecv(testLink, buf, sizeof(buf) - 1, remoteClosed);
        if (n > 0) {
            buf[n] = '\0';
            response += reinterpret_cast<char*>(buf);
            if (response.indexOf("\r\n\r\n") >= 0 && remoteClosed) break;
        } else if (remoteClosed) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    tcpClose(testLink);

    int bodyPos = response.indexOf("\r\n\r\n");
    if (bodyPos < 0) {
        errorOut = "Invalid HTTP response over LTE";
        return false;
    }
    externalIpOut = response.substring(bodyPos + 4);
    externalIpOut.trim();
    return externalIpOut.length() > 0;
}

bool ModemManager::unlockSim(const String& pin) {
    if (pin.length() == 0) return false;
    if (!lock(10000)) return false;
    String resp;
    bool ok = sendAtWaitOkLocked("AT+CPIN=\"" + pin + "\"", resp, 5000);
    if (ok) {
        delay(1500);
        initModemLocked();
        refreshStatusLocked();
        scanSimSmsLocked();
    }
    unlock();
    return ok;
}

bool ModemManager::rebootModem() {
    if (!lock(15000)) return false;
    String resp;
    sendAtWaitOkLocked("AT+CRESET", resp, 3000);
    _status.ready = false;
    _status.dataConnected = false;
    delay(4000);
    powerOnModem();
    bool ok = initModemLocked();
    refreshStatusLocked();
    unlock();
    return ok;
}
