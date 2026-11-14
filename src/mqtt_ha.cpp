#include "mqtt_ha.h"
#include "board_pins.h"
#include "config.h"
#include "modem_manager.h"
#include "proxy_server.h"

#include <ArduinoJson.h>

void MqttHaClient::begin(ConfigManager* cfg, ModemManager* modem, ProxyServer* proxy) {
    _cfg = cfg;
    _modem = modem;
    _proxy = proxy;
    _mqtt.setBufferSize(1024);
    _mqtt.setCallback([this](char* topic, byte* payload, unsigned int len) {
        this->handleMessage(topic, payload, len);
    });
}

void MqttHaClient::reconnect() {
    _discoveryPublished = false;
    if (_mqtt.connected()) {
        _mqtt.disconnect();
    }
    _lastConnectAttemptMs = 0;
}

bool MqttHaClient::isConnected() {
    return _mqtt.connected();
}

void MqttHaClient::loop() {
    if (!_cfg) return;
    const auto& c = _cfg->get();
    if (!c.mqttEnabled || c.mqttHost.length() == 0 || WiFi.status() != WL_CONNECTED) {
        if (_mqtt.connected()) _mqtt.disconnect();
        return;
    }

    if (!_mqtt.connected()) {
        if (millis() - _lastConnectAttemptMs < 10000) return;
        _lastConnectAttemptMs = millis();

        _mqtt.setServer(c.mqttHost.c_str(), c.mqttPort);
        String availTopic = c.mqttPrefix + "/availability";
        String clientId = c.hostname + "-" + String(static_cast<uint32_t>(ESP.getEfuseMac()), HEX);

        bool ok = false;
        if (c.mqttUser.length() > 0) {
            ok = _mqtt.connect(clientId.c_str(), c.mqttUser.c_str(), c.mqttPass.c_str(),
                               availTopic.c_str(), 0, true, "offline");
        } else {
            ok = _mqtt.connect(clientId.c_str(), availTopic.c_str(), 0, true, "offline");
        }

        if (ok) {
            Serial.println("[MQTT] Connected to broker");
            _mqtt.publish(availTopic.c_str(), "online", true);
            _mqtt.subscribe((c.mqttPrefix + "/sms/send").c_str());
            _mqtt.subscribe((c.mqttPrefix + "/call/dial").c_str());
            _mqtt.subscribe((c.mqttPrefix + "/call/hangup").c_str());
            _mqtt.subscribe((c.mqttPrefix + "/call/answer").c_str());
            _mqtt.subscribe((c.mqttPrefix + "/proxy/set").c_str());
            _mqtt.subscribe((c.mqttPrefix + "/sms/scan").c_str());
            _mqtt.subscribe((c.mqttPrefix + "/balance/check").c_str());
            _mqtt.subscribe((c.mqttPrefix + "/modem/reboot").c_str());

            if (c.haDiscovery && !_discoveryPublished) {
                publishDiscovery();
                _discoveryPublished = true;
            }
            publishStatus();
        }
        return;
    }

    _mqtt.loop();

    if (millis() - _lastStatusPubMs > 30000) {
        _lastStatusPubMs = millis();
        publishStatus();
    }
}

void MqttHaClient::handleMessage(char* topic, byte* payload, unsigned int length) {
    if (!_cfg || !_modem) return;
    const auto& c = _cfg->get();

    String t(topic);
    String body;
    body.reserve(length + 1);
    for (unsigned int i = 0; i < length; ++i) {
        body += static_cast<char>(payload[i]);
    }
    body.trim();

    if (t == c.mqttPrefix + "/sms/send") {
        JsonDocument doc;
        if (deserializeJson(doc, body) == DeserializationError::Ok) {
            String to = doc["to"] | "";
            String msg = doc["message"] | (doc["text"] | "");
            String err;
            _modem->sendSms(to, msg, err);
        }
    } else if (t == c.mqttPrefix + "/call/dial") {
        JsonDocument doc;
        String to = body;
        uint32_t timeoutSec = 0;
        if (deserializeJson(doc, body) == DeserializationError::Ok) {
            to = doc["to"] | "";
            timeoutSec = doc["timeout_sec"] | 0U;
        }
        String err;
        if (to.length() > 0) _modem->dial(to, timeoutSec, err);
    } else if (t == c.mqttPrefix + "/call/hangup") {
        _modem->hangup();
    } else if (t == c.mqttPrefix + "/call/answer") {
        _modem->answer();
    } else if (t == c.mqttPrefix + "/sms/scan") {
        _modem->triggerSmsScan();
    } else if (t == c.mqttPrefix + "/balance/check") {
        String msg, err;
        _modem->requestBalanceCheck(msg, err);
    } else if (t == c.mqttPrefix + "/modem/reboot") {
        _modem->rebootModem();
    } else if (t == c.mqttPrefix + "/proxy/set") {
        bool enable = (body == "ON" || body == "true" || body == "1");
        _cfg->get().proxyEnabled = enable;
        _cfg->save();
        if (_proxy) _proxy->setEnabled(enable);
        publishStatus();
    }
}

void MqttHaClient::publishIncomingSms(const SmsRecord& rec) {
    if (!_mqtt.connected() || !_cfg) return;
    const auto& c = _cfg->get();

    JsonDocument doc;
    doc["id"] = rec.id;
    doc["from"] = rec.peer;
    doc["message"] = rec.text;
    doc["timestamp"] = rec.timestamp;
    doc["parts"] = rec.parts;

    String json;
    serializeJson(doc, json);
    _mqtt.publish((c.mqttPrefix + "/sms/received").c_str(), json.c_str(), false);
    _mqtt.publish((c.mqttPrefix + "/sms/last").c_str(), json.c_str(), true);
}

void MqttHaClient::publishCallEvent(const String& state, const String& peer) {
    if (!_mqtt.connected() || !_cfg) return;
    const auto& c = _cfg->get();

    JsonDocument doc;
    doc["state"] = state;
    doc["peer"] = peer;

    String json;
    serializeJson(doc, json);
    _mqtt.publish((c.mqttPrefix + "/call/event").c_str(), json.c_str(), false);
    _mqtt.publish((c.mqttPrefix + "/call/state").c_str(), json.c_str(), true);
}

void MqttHaClient::publishStatus() {
    if (!_mqtt.connected() || !_cfg || !_modem) return;
    const auto& c = _cfg->get();
    ModemStatus st = _modem->getStatus();

    JsonDocument doc;
    doc["ready"] = st.ready;
    doc["sim_state"] = st.simState;
    doc["operator"] = st.operatorName;
    doc["tech"] = st.networkTech;
    doc["rssi_dbm"] = st.rssiDbm;
    doc["signal_pct"] = st.signalPercent;
    doc["battery_pct"] = st.batteryPercent;
    doc["battery_mv"] = st.batteryMv;
    doc["battery_v"] = serialized(String(st.batteryMv / 1000.0f, 2));
    doc["battery_state"] = st.batteryState;
    doc["data_connected"] = st.dataConnected;
    doc["cellular_ip"] = st.cellularIp;
    doc["call_state"] = st.callState;
    if (st.balanceAmount >= 0.0) {
        doc["balance"] = serialized(String(st.balanceAmount, 2));
    } else {
        doc["balance"] = nullptr;
    }
    doc["balance_display"] = st.balanceDisplay;
    doc["balance_updated_at"] = st.balanceUpdatedAt;
    doc["proxy_state"] = c.proxyEnabled ? "ON" : "OFF";
    doc["wifi_ip"] = WiFi.localIP().toString();

    String json;
    serializeJson(doc, json);
    _mqtt.publish((c.mqttPrefix + "/status").c_str(), json.c_str(), true);
    _mqtt.publish((c.mqttPrefix + "/proxy/state").c_str(), c.proxyEnabled ? "ON" : "OFF", true);
}

void MqttHaClient::publishDiscoveryEntity(const String& component, const String& objectId, const String& payloadJson) {
    const auto& c = _cfg->get();
    String topic = c.haDiscoveryPrefix + "/" + component + "/" + c.hostname + "/" + objectId + "/config";
    _mqtt.publish(topic.c_str(), payloadJson.c_str(), true);
}

void MqttHaClient::publishDiscovery() {
    const auto& c = _cfg->get();
    String p = c.mqttPrefix;
    String avail = p + "/availability";
    String statusTopic = p + "/status";

    auto makeDoc = [&](const char* name, const char* uniqSuffix) {
        JsonDocument doc;
        doc["name"] = name;
        doc["uniq_id"] = c.hostname + "_" + uniqSuffix;
        doc["avty_t"] = avail;
        JsonObject dev = doc["dev"].to<JsonObject>();
        JsonArray ids = dev["ids"].to<JsonArray>();
        ids.add(c.hostname);
        dev["name"] = "ESP32 CellBridge (" + c.hostname + ")";
        dev["mdl"] = BOARD_NAME;
        dev["mf"] = "LilyGO";
        dev["sw"] = FW_VERSION;
        return doc;
    };

    {
        auto d = makeDoc("Cellular Signal", "rssi");
        d["stat_t"] = statusTopic;
        d["val_tpl"] = "{{ value_json.rssi_dbm }}";
        d["unit_of_meas"] = "dBm";
        d["dev_cla"] = "signal_strength";
        String out; serializeJson(d, out);
        publishDiscoveryEntity("sensor", "rssi", out);
    }
    {
        auto d = makeDoc("Battery Level", "battery");
        d["stat_t"] = statusTopic;
        d["val_tpl"] = "{{ value_json.battery_pct }}";
        d["unit_of_meas"] = "%";
        d["dev_cla"] = "battery";
        String out; serializeJson(d, out);
        publishDiscoveryEntity("sensor", "battery", out);
    }
    {
        auto d = makeDoc("Battery Voltage", "battery_voltage");
        d["stat_t"] = statusTopic;
        d["val_tpl"] = "{{ value_json.battery_v }}";
        d["unit_of_meas"] = "V";
        d["dev_cla"] = "voltage";
        String out; serializeJson(d, out);
        publishDiscoveryEntity("sensor", "battery_voltage", out);
    }
    {
        auto d = makeDoc("Mobile Operator", "operator");
        d["stat_t"] = statusTopic;
        d["val_tpl"] = "{{ value_json.operator }}";
        d["ic"] = "mdi:sim";
        String out; serializeJson(d, out);
        publishDiscoveryEntity("sensor", "operator", out);
    }
    {
        auto d = makeDoc("Last SMS Sender", "last_sms_from");
        d["stat_t"] = p + "/sms/last";
        d["val_tpl"] = "{{ value_json.from }}";
        d["ic"] = "mdi:message-text";
        String out; serializeJson(d, out);
        publishDiscoveryEntity("sensor", "last_sms_from", out);
    }
    {
        auto d = makeDoc("Last SMS Body", "last_sms_body");
        d["stat_t"] = p + "/sms/last";
        d["val_tpl"] = "{{ value_json.message[:250] }}";
        d["ic"] = "mdi:message-reply-text";
        String out; serializeJson(d, out);
        publishDiscoveryEntity("sensor", "last_sms_body", out);
    }
    {
        auto d = makeDoc("Call State", "call_state");
        d["stat_t"] = statusTopic;
        d["val_tpl"] = "{{ value_json.call_state }}";
        d["ic"] = "mdi:phone";
        String out; serializeJson(d, out);
        publishDiscoveryEntity("sensor", "call_state", out);
    }
    {
        auto d = makeDoc("4G Internet Proxy", "proxy_switch");
        d["stat_t"] = p + "/proxy/state";
        d["cmd_t"] = p + "/proxy/set";
        d["ic"] = "mdi:wan";
        String out; serializeJson(d, out);
        publishDiscoveryEntity("switch", "proxy_switch", out);
    }
    {
        auto d = makeDoc("Hangup Call", "hangup_btn");
        d["cmd_t"] = p + "/call/hangup";
        d["ic"] = "mdi:phone-hangup";
        String out; serializeJson(d, out);
        publishDiscoveryEntity("button", "hangup_btn", out);
    }
    {
        auto d = makeDoc("Reboot Modem", "reboot_modem_btn");
        d["cmd_t"] = p + "/modem/reboot";
        d["ic"] = "mdi:restart";
        String out; serializeJson(d, out);
        publishDiscoveryEntity("button", "reboot_modem_btn", out);
    }
    {
        auto d = makeDoc("Prepaid Balance", "balance");
        d["stat_t"] = statusTopic;
        d["val_tpl"] = "{{ value_json.balance }}";
        d["unit_of_meas"] = "EUR";
        d["dev_cla"] = "monetary";
        d["ic"] = "mdi:cash-multiple";
        String out; serializeJson(d, out);
        publishDiscoveryEntity("sensor", "balance", out);
    }
    {
        auto d = makeDoc("Check Prepaid Balance", "check_balance_btn");
        d["cmd_t"] = p + "/balance/check";
        d["ic"] = "mdi:cash-check";
        String out; serializeJson(d, out);
        publishDiscoveryEntity("button", "check_balance_btn", out);
    }
}
