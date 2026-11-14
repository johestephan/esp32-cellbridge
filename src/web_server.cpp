#include "web_server.h"
#include "board_pins.h"
#include "config.h"
#include "modem_manager.h"
#include "proxy_server.h"
#include "mqtt_ha.h"
#include "web_ui.h"

#include <Update.h>
#include <WiFi.h>

void GatewayWebServer::begin(ConfigManager* cfg, ModemManager* modem, SmsStore* store,
                             ProxyServer* proxy, MqttHaClient* mqtt, bool apMode) {
    _cfg = cfg;
    _modem = modem;
    _store = store;
    _proxy = proxy;
    _mqtt = mqtt;
    _apMode = apMode;

    if (_apMode) {
        _dns.start(53, "*", WiFi.softAPIP());
    }

    registerRoutes();
    _server.begin();
    Serial.printf("[Web] HTTP server started on port %d\n", WEB_SERVER_PORT);
}

void GatewayWebServer::loop() {
    if (_apMode) {
        _dns.processNextRequest();
    }
    _server.handleClient();
}

String GatewayWebServer::extractSessionCookie() {
    if (!_server.hasHeader("Cookie")) return "";
    String cookie = _server.header("Cookie");
    int idx = cookie.indexOf("a7670e_session=");
    if (idx < 0) return "";
    int start = idx + 15;
    int end = cookie.indexOf(';', start);
    String tok = (end > start) ? cookie.substring(start, end) : cookie.substring(start);
    tok.trim();
    return tok;
}

bool GatewayWebServer::hasValidSession() {
    String tok = extractSessionCookie();
    if (tok.length() < 16) return false;
    uint32_t now = millis();
    for (int i = 0; i < kMaxSessions; ++i) {
        if (_sessions[i].active && _sessions[i].token == tok) {
            if (static_cast<int32_t>(_sessions[i].expiresAtMs - now) > 0) {
                return true;
            }
            _sessions[i].active = false;
            return false;
        }
    }
    return false;
}

String GatewayWebServer::createSession() {
    uint32_t r1 = esp_random();
    uint32_t r2 = esp_random();
    char buf[24];
    snprintf(buf, sizeof(buf), "%08lx%08lx", static_cast<unsigned long>(r1), static_cast<unsigned long>(r2));
    String tok(buf);

    int slot = 0;
    uint32_t oldestExp = UINT32_MAX;
    for (int i = 0; i < kMaxSessions; ++i) {
        if (!_sessions[i].active) {
            slot = i;
            break;
        }
        if (_sessions[i].expiresAtMs < oldestExp) {
            oldestExp = _sessions[i].expiresAtMs;
            slot = i;
        }
    }
    _sessions[slot].active = true;
    _sessions[slot].token = tok;
    _sessions[slot].expiresAtMs = millis() + 24UL * 3600UL * 1000UL;
    return tok;
}

void GatewayWebServer::clearCurrentSession() {
    String tok = extractSessionCookie();
    if (tok.length() == 0) return;
    for (int i = 0; i < kMaxSessions; ++i) {
        if (_sessions[i].active && _sessions[i].token == tok) {
            _sessions[i].active = false;
            _sessions[i].token = "";
        }
    }
}

void GatewayWebServer::clearAllSessions() {
    for (int i = 0; i < kMaxSessions; ++i) {
        _sessions[i].active = false;
        _sessions[i].token = "";
    }
}

bool GatewayWebServer::requireWebAuth() {
    const auto& c = _cfg->get();
    if (c.webPass.length() == 0) return true;
    if (hasValidSession()) return true;
    if (_server.authenticate(c.webUser.c_str(), c.webPass.c_str())) return true;
    _server.sendHeader("Location", "/login", true);
    _server.send(302, "text/plain", "");
    return false;
}

bool GatewayWebServer::requireApiAuth() {
    const auto& c = _cfg->get();
    if (c.apiKey.length() == 0 && c.webPass.length() == 0) return true;

    if (hasValidSession()) return true;

    if (c.apiKey.length() > 0) {
        if (_server.hasHeader("X-API-Key") && _server.header("X-API-Key") == c.apiKey) {
            return true;
        }
        if (_server.hasHeader("Authorization")) {
            String auth = _server.header("Authorization");
            if (auth.startsWith("Bearer ") && auth.substring(7) == c.apiKey) {
                return true;
            }
        }
        if (_server.hasArg("api_key") && _server.arg("api_key") == c.apiKey) {
            return true;
        }
    }

    if (c.webPass.length() > 0 && _server.authenticate(c.webUser.c_str(), c.webPass.c_str())) {
        return true;
    }

    sendError(401, "Unauthorized (login or provide X-API-Key / Bearer token)");
    return false;
}

void GatewayWebServer::sendJson(int code, const JsonDocument& doc) {
    String out;
    serializeJson(doc, out);
    _server.sendHeader("Access-Control-Allow-Origin", "*");
    _server.send(code, "application/json", out);
}

void GatewayWebServer::sendError(int code, const String& msg) {
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = msg;
    sendJson(code, doc);
}

bool GatewayWebServer::parseJsonBody(JsonDocument& doc) {
    if (!_server.hasArg("plain")) {
        sendError(400, "Missing JSON body");
        return false;
    }
    DeserializationError err = deserializeJson(doc, _server.arg("plain"));
    if (err) {
        sendError(400, String("Invalid JSON: ") + err.c_str());
        return false;
    }
    return true;
}

void GatewayWebServer::registerRoutes() {
    // Collect headers needed for API & Session Cookie auth
    const char* headerKeys[] = {"X-API-Key", "Authorization", "Cookie"};
    _server.collectHeaders(headerKeys, 3);

    _server.on("/", HTTP_GET, [this]() {
        if (!requireWebAuth()) return;
        _server.send_P(200, "text/html", WEB_UI_HTML);
    });

    _server.on("/login", HTTP_GET, [this]() {
        if (hasValidSession() || _cfg->get().webPass.length() == 0) {
            _server.sendHeader("Location", "/", true);
            _server.send(302, "text/plain", "");
            return;
        }
        _server.send_P(200, "text/html", LOGIN_HTML);
    });

    _server.on("/api/auth/login", HTTP_POST, [this]() {
        JsonDocument body;
        String user, pass;
        if (_server.hasArg("plain")) {
            if (deserializeJson(body, _server.arg("plain")) == DeserializationError::Ok) {
                user = body["username"] | "";
                pass = body["password"] | "";
            }
        } else {
            user = _server.arg("username");
            pass = _server.arg("password");
        }

        const auto& c = _cfg->get();
        if (user == c.webUser && pass == c.webPass) {
            String tok = createSession();
            _server.sendHeader("Set-Cookie", "a7670e_session=" + tok + "; Path=/; HttpOnly; SameSite=Lax");
            JsonDocument res;
            res["ok"] = true;
            res["message"] = "Logged in";
            sendJson(200, res);
        } else {
            sendError(401, "Invalid username or password");
        }
    });

    _server.on("/api/auth/logout", HTTP_POST, [this]() {
        clearCurrentSession();
        _server.sendHeader("Set-Cookie", "a7670e_session=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax");
        JsonDocument res;
        res["ok"] = true;
        res["message"] = "Logged out";
        sendJson(200, res);
    });

    _server.on("/api/auth/password", HTTP_POST, [this]() {
        if (!requireApiAuth()) return;
        JsonDocument body;
        if (!parseJsonBody(body)) return;

        String curPass = body["current_password"] | "";
        String newPass = body["new_password"] | "";
        String newUser = body["username"] | "";

        auto& c = _cfg->get();
        if (c.webPass.length() > 0 && curPass != c.webPass) {
            sendError(403, "Current password is incorrect");
            return;
        }
        if (newPass.length() < 4) {
            sendError(400, "New password must be at least 4 characters");
            return;
        }
        if (newUser.length() > 0) {
            c.webUser = newUser;
        }
        c.webPass = newPass;
        _cfg->save();

        // Rotate sessions and issue fresh cookie for current caller
        clearAllSessions();
        String tok = createSession();
        _server.sendHeader("Set-Cookie", "a7670e_session=" + tok + "; Path=/; HttpOnly; SameSite=Lax");

        JsonDocument res;
        res["ok"] = true;
        res["message"] = "Password updated successfully";
        sendJson(200, res);
    });

    // Captive portal redirects
    _server.onNotFound([this]() {
        if (_apMode) {
            _server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
            _server.send(302, "text/plain", "");
            return;
        }
        if (_server.method() == HTTP_OPTIONS) {
            _server.sendHeader("Access-Control-Allow-Origin", "*");
            _server.sendHeader("Access-Control-Allow-Methods", "GET,POST,DELETE,OPTIONS");
            _server.sendHeader("Access-Control-Allow-Headers", "Content-Type,X-API-Key,Authorization");
            _server.send(204);
            return;
        }
        sendError(404, "Not found");
    });

    _server.on("/api/health", HTTP_GET, [this]() {
        JsonDocument doc;
        doc["ok"] = true;
        doc["name"] = FW_NAME;
        doc["version"] = FW_VERSION;
        doc["uptime_sec"] = millis() / 1000;
        sendJson(200, doc);
    });

    _server.on("/api/status", HTTP_GET, [this]() {
        if (!requireApiAuth()) return;
        ModemStatus st = _modem->getStatus();
        ProxyStats ps = _proxy->getStats();

        JsonDocument doc;
        doc["ok"] = true;
        doc["board"] = BOARD_NAME;
        doc["version"] = FW_VERSION;
        doc["ap_mode"] = _apMode;
        doc["wifi_ip"] = _apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
        doc["wifi_ssid"] = _apMode ? String(AP_SETUP_SSID) : WiFi.SSID();
        doc["wifi_rssi"] = WiFi.RSSI();
        doc["uptime_sec"] = millis() / 1000;
        doc["free_heap"] = ESP.getFreeHeap();
        doc["free_psram"] = ESP.getFreePsram();
        doc["mqtt_connected"] = _mqtt ? _mqtt->isConnected() : false;
        doc["sms_unread"] = _store->unreadCount();
        doc["sms_total"] = _store->totalSmsCount();

        JsonObject m = doc["modem"].to<JsonObject>();
        m["ready"] = st.ready;
        m["sim_ready"] = st.simReady;
        m["sim_state"] = st.simState;
        m["manufacturer"] = st.manufacturer;
        m["model"] = st.model;
        m["imei"] = st.imei;
        m["operator"] = st.operatorName;
        m["tech"] = st.networkTech;
        m["reg_status"] = st.regStatus;
        m["csq"] = st.csq;
        m["rssi_dbm"] = st.rssiDbm;
        m["signal_pct"] = st.signalPercent;
        m["data_connected"] = st.dataConnected;
        m["cellular_ip"] = st.cellularIp;
        m["call_state"] = st.callState;
        m["active_call_peer"] = st.activeCallPeer;
        m["battery_mv"] = st.batteryMv;
        m["battery_v"] = serialized(String(st.batteryMv / 1000.0f, 2));
        m["battery_pct"] = st.batteryPercent;
        m["battery_state"] = st.batteryState;

        JsonObject p = doc["proxy"].to<JsonObject>();
        p["enabled"] = ps.enabled;
        p["http_port"] = ps.httpPort;
        p["socks_port"] = ps.socksPort;
        p["active_tunnels"] = ps.activeTunnels;
        p["total_connections"] = ps.totalConnections;
        p["bytes_up"] = ps.bytesUp;
        p["bytes_down"] = ps.bytesDown;

        JsonObject b = doc["balance"].to<JsonObject>();
        const auto& c = _cfg->get();
        if (st.balanceAmount >= 0.0) {
            b["amount"] = serialized(String(st.balanceAmount, 2));
        } else {
            b["amount"] = nullptr;
        }
        b["currency"] = st.balanceCurrency;
        b["display"] = st.balanceDisplay;
        b["updated_at"] = st.balanceUpdatedAt;
        b["raw"] = st.balanceRawText;
        b["pending"] = st.balancePending;
        b["method"] = c.balanceMethod;
        b["target"] = c.balanceTarget;

        sendJson(200, doc);
    });

    // SMS endpoints
    _server.on("/api/sms", HTTP_GET, [this]() {
        if (!requireApiAuth()) return;
        String dir = _server.hasArg("direction") ? _server.arg("direction") : "";
        bool unread = _server.hasArg("unread") && _server.arg("unread") == "true";
        size_t limit = _server.hasArg("limit") ? static_cast<size_t>(_server.arg("limit").toInt()) : 100;
        if (limit == 0 || limit > MAX_SMS_HISTORY) limit = MAX_SMS_HISTORY;

        auto list = _store->listSms(dir, unread, limit);
        JsonDocument doc;
        doc["ok"] = true;
        doc["unread"] = _store->unreadCount();
        JsonArray arr = doc["messages"].to<JsonArray>();
        for (const auto& r : list) {
            JsonObject o = arr.add<JsonObject>();
            o["id"] = r.id;
            o["direction"] = r.direction;
            o["peer"] = r.peer;
            o["text"] = r.text;
            o["timestamp"] = r.timestamp;
            o["read"] = r.read;
            o["parts"] = r.parts;
            o["status"] = r.status;
        }
        sendJson(200, doc);
    });

    _server.on("/api/sms/send", HTTP_POST, [this]() {
        if (!requireApiAuth()) return;
        JsonDocument body;
        if (!parseJsonBody(body)) return;

        String to = body["to"] | (body["target"] | "");
        String msg = body["message"] | (body["text"] | (body["body"] | ""));
        String err;
        if (_modem->sendSms(to, msg, err)) {
            JsonDocument res;
            res["ok"] = true;
            res["message"] = "SMS sent";
            res["to"] = to;
            sendJson(200, res);
        } else {
            sendError(500, err);
        }
    });

    _server.on("/api/sms/scan", HTTP_POST, [this]() {
        if (!requireApiAuth()) return;
        _modem->triggerSmsScan();
        JsonDocument res;
        res["ok"] = true;
        res["message"] = "SIM SMS scan queued";
        sendJson(200, res);
    });

    _server.on("/api/sms/read", HTTP_POST, [this]() {
        if (!requireApiAuth()) return;
        JsonDocument body;
        if (!parseJsonBody(body)) return;
        if (body["all"] | false) {
            _store->markAllRead();
        } else {
            uint32_t id = body["id"] | 0U;
            _store->markRead(id);
        }
        JsonDocument res;
        res["ok"] = true;
        res["unread"] = _store->unreadCount();
        sendJson(200, res);
    });

    _server.on("/api/sms", HTTP_DELETE, [this]() {
        if (!requireApiAuth()) return;
        if (_server.hasArg("id")) {
            uint32_t id = static_cast<uint32_t>(_server.arg("id").toInt());
            if (!_store->deleteSms(id)) {
                sendError(404, "Message ID not found");
                return;
            }
        } else {
            _store->clearSms();
        }
        JsonDocument res;
        res["ok"] = true;
        sendJson(200, res);
    });

    // Call endpoints
    _server.on("/api/calls", HTTP_GET, [this]() {
        if (!requireApiAuth()) return;
        auto list = _store->listCalls(50);
        JsonDocument doc;
        doc["ok"] = true;
        JsonArray arr = doc["calls"].to<JsonArray>();
        for (const auto& c : list) {
            JsonObject o = arr.add<JsonObject>();
            o["id"] = c.id;
            o["direction"] = c.direction;
            o["peer"] = c.peer;
            o["timestamp"] = c.timestamp;
            o["duration_sec"] = c.durationSec;
            o["status"] = c.status;
        }
        sendJson(200, doc);
    });

    _server.on("/api/calls", HTTP_DELETE, [this]() {
        if (!requireApiAuth()) return;
        _store->clearCalls();
        JsonDocument res;
        res["ok"] = true;
        sendJson(200, res);
    });

    _server.on("/api/call/dial", HTTP_POST, [this]() {
        if (!requireApiAuth()) return;
        JsonDocument body;
        if (!parseJsonBody(body)) return;
        String to = body["to"] | "";
        uint32_t timeoutSec = body["timeout_sec"] | 0U;
        String err;
        if (_modem->dial(to, timeoutSec, err)) {
            JsonDocument res;
            res["ok"] = true;
            res["message"] = "Dialing " + to;
            sendJson(200, res);
        } else {
            sendError(500, err);
        }
    });

    _server.on("/api/call/answer", HTTP_POST, [this]() {
        if (!requireApiAuth()) return;
        if (_modem->answer()) {
            JsonDocument res; res["ok"] = true; res["message"] = "Call answered";
            sendJson(200, res);
        } else {
            sendError(500, "Failed to answer");
        }
    });

    _server.on("/api/call/hangup", HTTP_POST, [this]() {
        if (!requireApiAuth()) return;
        if (_modem->hangup()) {
            JsonDocument res; res["ok"] = true; res["message"] = "Hung up";
            sendJson(200, res);
        } else {
            sendError(500, "Failed to hangup");
        }
    });

    _server.on("/api/call/dtmf", HTTP_POST, [this]() {
        if (!requireApiAuth()) return;
        JsonDocument body;
        if (!parseJsonBody(body)) return;
        String digits = body["digits"] | "";
        if (_modem->sendDtmf(digits)) {
            JsonDocument res; res["ok"] = true; res["message"] = "DTMF sent";
            sendJson(200, res);
        } else {
            sendError(500, "DTMF failed");
        }
    });

    // USSD, Balance & Raw AT
    _server.on("/api/balance", HTTP_GET, [this]() {
        if (!requireApiAuth()) return;
        ModemStatus st = _modem->getStatus();
        const auto& c = _cfg->get();
        JsonDocument res;
        res["ok"] = true;
        if (st.balanceAmount >= 0.0) {
            res["amount"] = serialized(String(st.balanceAmount, 2));
        } else {
            res["amount"] = nullptr;
        }
        res["currency"] = st.balanceCurrency;
        res["display"] = st.balanceDisplay;
        res["updated_at"] = st.balanceUpdatedAt;
        res["raw"] = st.balanceRawText;
        res["pending"] = st.balancePending;
        res["method"] = c.balanceMethod;
        res["target"] = c.balanceTarget;
        sendJson(200, res);
    });

    _server.on("/api/balance/check", HTTP_POST, [this]() {
        if (!requireApiAuth()) return;
        String msg, err;
        if (_modem->requestBalanceCheck(msg, err)) {
            ModemStatus st = _modem->getStatus();
            JsonDocument res;
            res["ok"] = true;
            res["message"] = msg;
            res["pending"] = st.balancePending;
            if (st.balanceAmount >= 0.0) {
                res["amount"] = serialized(String(st.balanceAmount, 2));
            } else {
                res["amount"] = nullptr;
            }
            res["display"] = st.balanceDisplay;
            res["updated_at"] = st.balanceUpdatedAt;
            sendJson(200, res);
        } else {
            sendError(500, err);
        }
    });

    _server.on("/api/ussd", HTTP_POST, [this]() {
        if (!requireApiAuth()) return;
        JsonDocument body;
        if (!parseJsonBody(body)) return;
        String code = body["code"] | "";
        String resp;
        if (_modem->sendUssd(code, resp)) {
            JsonDocument res;
            res["ok"] = true;
            res["response"] = resp;
            sendJson(200, res);
        } else {
            sendError(500, resp);
        }
    });

    _server.on("/api/at", HTTP_POST, [this]() {
        if (!requireApiAuth()) return;
        JsonDocument body;
        if (!parseJsonBody(body)) return;
        String cmd = body["command"] | "";
        String resp;
        bool ok = _modem->sendRawAt(cmd, resp, 6000);
        JsonDocument res;
        res["ok"] = ok;
        res["response"] = resp;
        sendJson(ok ? 200 : 500, res);
    });

    // Proxy endpoints
    _server.on("/api/proxy/status", HTTP_GET, [this]() {
        if (!requireApiAuth()) return;
        ProxyStats ps = _proxy->getStats();
        ModemStatus ms = _modem->getStatus();
        JsonDocument res;
        res["ok"] = true;
        res["enabled"] = ps.enabled;
        res["data_connected"] = ms.dataConnected;
        res["cellular_ip"] = ms.cellularIp;
        res["http_port"] = ps.httpPort;
        res["socks_port"] = ps.socksPort;
        res["active_tunnels"] = ps.activeTunnels;
        res["total_connections"] = ps.totalConnections;
        res["bytes_up"] = ps.bytesUp;
        res["bytes_down"] = ps.bytesDown;
        sendJson(200, res);
    });

    _server.on("/api/proxy/toggle", HTTP_POST, [this]() {
        if (!requireApiAuth()) return;
        JsonDocument body;
        if (!parseJsonBody(body)) return;
        bool enable = body["enabled"] | false;
        _cfg->get().proxyEnabled = enable;
        _cfg->save();
        _proxy->setEnabled(enable);

        String err;
        bool netOk = true;
        if (enable) {
            netOk = _modem->ensureNetworkOpen(err);
        } else {
            _modem->closeNetwork();
        }
        if (_mqtt) _mqtt->publishStatus();

        if (netOk) {
            JsonDocument res;
            res["ok"] = true;
            res["enabled"] = enable;
            res["message"] = enable ? "4G Proxy enabled" : "4G Proxy disabled";
            sendJson(200, res);
        } else {
            sendError(500, "Proxy listening, but LTE data failed: " + err);
        }
    });

    _server.on("/api/proxy/test", HTTP_POST, [this]() {
        if (!requireApiAuth()) return;
        String extIp, err;
        if (_modem->testCellularIp(extIp, err)) {
            JsonDocument res;
            res["ok"] = true;
            res["external_ip"] = extIp;
            res["cellular_ip"] = _modem->getStatus().cellularIp;
            sendJson(200, res);
        } else {
            sendError(500, err);
        }
    });

    // Config & System
    _server.on("/api/config", HTTP_GET, [this]() {
        if (!requireApiAuth()) return;
        JsonDocument doc;
        _cfg->toJson(doc, true);
        sendJson(200, doc);
    });

    _server.on("/api/config", HTTP_POST, [this]() {
        if (!requireApiAuth()) return;
        JsonDocument body;
        if (!parseJsonBody(body)) return;

        bool wifiChanged = false, proxyChanged = false, mqttChanged = false;
        String oldPin = _cfg->get().simPin;
        _cfg->updateFromJson(body.as<JsonVariantConst>(), wifiChanged, proxyChanged, mqttChanged);
        _cfg->save();

        if (_cfg->get().simPin.length() > 0 &&
            _cfg->get().simPin != oldPin &&
            _modem->getStatus().simState == "SIM_PIN") {
            _modem->unlockSim(_cfg->get().simPin);
        }

        if (proxyChanged && _proxy) {
            _proxy->setEnabled(_cfg->get().proxyEnabled);
            if (_cfg->get().proxyEnabled) _proxy->restartListeners();
        }
        if (mqttChanged && _mqtt) {
            _mqtt->reconnect();
        }

        JsonDocument res;
        res["ok"] = true;
        res["wifi_changed"] = wifiChanged;
        res["message"] = wifiChanged ? "Saved! Rebooting to apply Wi-Fi..." : "Configuration saved";
        sendJson(200, res);

        if (wifiChanged) {
            delay(500);
            ESP.restart();
        }
    });

    _server.on("/api/modem/reboot", HTTP_POST, [this]() {
        if (!requireApiAuth()) return;
        bool ok = _modem->rebootModem();
        JsonDocument res;
        res["ok"] = ok;
        res["message"] = ok ? "Modem rebooted" : "Modem reboot failed";
        sendJson(ok ? 200 : 500, res);
    });

    _server.on("/api/system/reboot", HTTP_POST, [this]() {
        if (!requireApiAuth()) return;
        JsonDocument res;
        res["ok"] = true;
        res["message"] = "Rebooting ESP32...";
        sendJson(200, res);
        delay(300);
        ESP.restart();
    });

    // OTA Firmware Update
    _server.on("/api/ota", HTTP_POST,
        [this]() {
            if (!requireApiAuth()) return;
            if (_otaOk) {
                JsonDocument res;
                res["ok"] = true;
                res["message"] = "OTA success, rebooting";
                sendJson(200, res);
                delay(500);
                ESP.restart();
            } else {
                sendError(500, "OTA failed");
            }
        },
        [this]() {
            HTTPUpload& upload = _server.upload();
            if (upload.status == UPLOAD_FILE_START) {
                _otaOk = false;
                if (!requireApiAuth()) return;
                Serial.printf("[OTA] Start: %s\n", upload.filename.c_str());
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                    Update.printError(Serial);
                }
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                    Update.printError(Serial);
                }
            } else if (upload.status == UPLOAD_FILE_END) {
                if (Update.end(true)) {
                    _otaOk = true;
                    Serial.printf("[OTA] Success: %u bytes\n", upload.totalSize);
                } else {
                    Update.printError(Serial);
                }
            }
        }
    );
}
