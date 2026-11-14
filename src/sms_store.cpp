#include "sms_store.h"
#include "config.h"

#include <LittleFS.h>
#include <ctime>

bool SmsStore::begin() {
    _mutex = xSemaphoreCreateMutex();
    loadSms();
    loadCalls();
    return true;
}

String SmsStore::fallbackTimestamp() {
    time_t now = time(nullptr);
    if (now > 1700000000) {
        struct tm tmInfo;
        localtime_r(&now, &tmInfo);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tmInfo);
        return String(buf);
    }
    return String("uptime:") + String(millis() / 1000) + "s";
}

bool SmsStore::loadSms() {
    if (!LittleFS.exists(kSmsPath)) return true;
    File f = LittleFS.open(kSmsPath, "r");
    if (!f) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err || !doc.is<JsonArray>()) return false;

    _sms.clear();
    uint32_t maxId = 0;
    for (JsonObject obj : doc.as<JsonArray>()) {
        SmsRecord r;
        r.id = obj["id"] | 0U;
        r.direction = obj["dir"] | "in";
        r.peer = obj["peer"] | "";
        r.text = obj["text"] | "";
        r.timestamp = obj["ts"] | "";
        r.read = obj["read"] | false;
        r.parts = obj["parts"] | 1;
        r.status = obj["status"] | "received";
        if (r.id > maxId) maxId = r.id;
        _sms.push_back(r);
    }
    _nextSmsId = maxId + 1;
    return true;
}

bool SmsStore::saveSms() {
    File f = LittleFS.open(kSmsPath, "w");
    if (!f) return false;

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (const auto& r : _sms) {
        JsonObject obj = arr.add<JsonObject>();
        obj["id"] = r.id;
        obj["dir"] = r.direction;
        obj["peer"] = r.peer;
        obj["text"] = r.text;
        obj["ts"] = r.timestamp;
        obj["read"] = r.read;
        obj["parts"] = r.parts;
        obj["status"] = r.status;
    }
    size_t written = serializeJson(doc, f);
    f.close();
    return written > 0;
}

bool SmsStore::loadCalls() {
    if (!LittleFS.exists(kCallsPath)) return true;
    File f = LittleFS.open(kCallsPath, "r");
    if (!f) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err || !doc.is<JsonArray>()) return false;

    _calls.clear();
    uint32_t maxId = 0;
    for (JsonObject obj : doc.as<JsonArray>()) {
        CallRecord c;
        c.id = obj["id"] | 0U;
        c.direction = obj["dir"] | "in";
        c.peer = obj["peer"] | "";
        c.timestamp = obj["ts"] | "";
        c.durationSec = obj["dur"] | 0U;
        c.status = obj["status"] | "ended";
        if (c.id > maxId) maxId = c.id;
        _calls.push_back(c);
    }
    _nextCallId = maxId + 1;
    return true;
}

bool SmsStore::saveCalls() {
    File f = LittleFS.open(kCallsPath, "w");
    if (!f) return false;

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (const auto& c : _calls) {
        JsonObject obj = arr.add<JsonObject>();
        obj["id"] = c.id;
        obj["dir"] = c.direction;
        obj["peer"] = c.peer;
        obj["ts"] = c.timestamp;
        obj["dur"] = c.durationSec;
        obj["status"] = c.status;
    }
    size_t written = serializeJson(doc, f);
    f.close();
    return written > 0;
}

SmsRecord SmsStore::addSms(const String& direction, const String& peer, const String& text,
                           const String& timestamp, const String& status, uint8_t parts, bool read) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    SmsRecord r;
    r.id = _nextSmsId++;
    r.direction = direction;
    r.peer = peer;
    r.text = text;
    r.timestamp = timestamp.length() ? timestamp : fallbackTimestamp();
    r.read = read;
    r.parts = parts;
    r.status = status;

    _sms.push_back(r);
    while (_sms.size() > MAX_SMS_HISTORY) {
        _sms.erase(_sms.begin());
    }
    saveSms();
    xSemaphoreGive(_mutex);
    return r;
}

bool SmsStore::ingestDecodedSms(const SmsCodec::DecodedSms& decoded, SmsRecord& completed) {
    if (!decoded.valid) return false;

    String sender = String(decoded.sender.c_str());
    String text = String(decoded.text.c_str());
    String ts = decoded.timestamp.empty() ? fallbackTimestamp() : String(decoded.timestamp.c_str());

    if (!decoded.isMultipart || decoded.totalParts <= 1 || decoded.totalParts > MAX_MULTIPART_PARTS) {
        completed = addSms("in", sender, text, ts, "received", 1, false);
        return true;
    }

    xSemaphoreTake(_mutex, portMAX_DELAY);
    int slot = -1;
    int freeSlot = -1;
    for (int i = 0; i < 4; ++i) {
        if (_multipart[i].active) {
            if (_multipart[i].sender == sender &&
                _multipart[i].refId == decoded.refId &&
                _multipart[i].totalParts == decoded.totalParts) {
                slot = i;
                break;
            }
        } else if (freeSlot < 0) {
            freeSlot = i;
        }
    }

    if (slot < 0) {
        slot = (freeSlot >= 0) ? freeSlot : 0;
        _multipart[slot].active = true;
        _multipart[slot].sender = sender;
        _multipart[slot].refId = decoded.refId;
        _multipart[slot].totalParts = decoded.totalParts;
        _multipart[slot].timestamp = ts;
        _multipart[slot].firstSeenMs = millis();
        _multipart[slot].parts.assign(decoded.totalParts, "");
        _multipart[slot].received.assign(decoded.totalParts, false);
    }

    if (decoded.partNumber >= 1 && decoded.partNumber <= decoded.totalParts) {
        size_t idx = decoded.partNumber - 1;
        _multipart[slot].parts[idx] = text;
        _multipart[slot].received[idx] = true;
    }

    bool allDone = true;
    for (bool r : _multipart[slot].received) {
        if (!r) { allDone = false; break; }
    }

    if (!allDone) {
        xSemaphoreGive(_mutex);
        return false;
    }

    String combined;
    for (const auto& p : _multipart[slot].parts) {
        combined += p;
    }
    uint8_t totalParts = _multipart[slot].totalParts;
    String finalTs = _multipart[slot].timestamp;
    _multipart[slot].active = false;
    xSemaphoreGive(_mutex);

    completed = addSms("in", sender, combined, finalTs, "received", totalParts, false);
    return true;
}

void SmsStore::checkMultipartTimeouts(std::vector<SmsRecord>& flushed) {
    flushed.clear();
    const uint32_t now = millis();
    std::vector<MultipartAssembly> expired;

    xSemaphoreTake(_mutex, portMAX_DELAY);
    for (int i = 0; i < 4; ++i) {
        if (_multipart[i].active && (now - _multipart[i].firstSeenMs > 90000UL)) {
            expired.push_back(_multipart[i]);
            _multipart[i].active = false;
        }
    }
    xSemaphoreGive(_mutex);

    for (const auto& e : expired) {
        String combined;
        for (size_t i = 0; i < e.parts.size(); ++i) {
            combined += e.received[i] ? e.parts[i] : "[...missing part...]";
        }
        flushed.push_back(addSms("in", e.sender, combined, e.timestamp, "received_partial", e.totalParts, false));
    }
}

std::vector<SmsRecord> SmsStore::listSms(const String& directionFilter, bool unreadOnly, size_t limit) {
    std::vector<SmsRecord> out;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    for (auto it = _sms.rbegin(); it != _sms.rend(); ++it) {
        if (directionFilter.length() > 0 && it->direction != directionFilter) continue;
        if (unreadOnly && it->read) continue;
        out.push_back(*it);
        if (out.size() >= limit) break;
    }
    xSemaphoreGive(_mutex);
    return out;
}

bool SmsStore::markRead(uint32_t id) {
    bool found = false;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    for (auto& r : _sms) {
        if (r.id == id) {
            r.read = true;
            found = true;
            break;
        }
    }
    if (found) saveSms();
    xSemaphoreGive(_mutex);
    return found;
}

void SmsStore::markAllRead() {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    for (auto& r : _sms) {
        r.read = true;
    }
    saveSms();
    xSemaphoreGive(_mutex);
}

bool SmsStore::deleteSms(uint32_t id) {
    bool found = false;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    for (auto it = _sms.begin(); it != _sms.end(); ++it) {
        if (it->id == id) {
            _sms.erase(it);
            found = true;
            break;
        }
    }
    if (found) saveSms();
    xSemaphoreGive(_mutex);
    return found;
}

void SmsStore::clearSms() {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _sms.clear();
    saveSms();
    xSemaphoreGive(_mutex);
}

size_t SmsStore::unreadCount() {
    size_t count = 0;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    for (const auto& r : _sms) {
        if (r.direction == "in" && !r.read) count++;
    }
    xSemaphoreGive(_mutex);
    return count;
}

size_t SmsStore::totalSmsCount() {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    size_t c = _sms.size();
    xSemaphoreGive(_mutex);
    return c;
}

CallRecord SmsStore::addCall(const String& direction, const String& peer,
                             const String& timestamp, const String& status) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    CallRecord c;
    c.id = _nextCallId++;
    c.direction = direction;
    c.peer = peer;
    c.timestamp = timestamp.length() ? timestamp : fallbackTimestamp();
    c.durationSec = 0;
    c.status = status;
    _calls.push_back(c);
    while (_calls.size() > MAX_CALL_HISTORY) {
        _calls.erase(_calls.begin());
    }
    saveCalls();
    xSemaphoreGive(_mutex);
    return c;
}

void SmsStore::updateLastCall(uint32_t id, const String& status, uint32_t durationSec) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    for (auto it = _calls.rbegin(); it != _calls.rend(); ++it) {
        if (it->id == id) {
            it->status = status;
            if (durationSec > 0) it->durationSec = durationSec;
            saveCalls();
            break;
        }
    }
    xSemaphoreGive(_mutex);
}

std::vector<CallRecord> SmsStore::listCalls(size_t limit) {
    std::vector<CallRecord> out;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    for (auto it = _calls.rbegin(); it != _calls.rend(); ++it) {
        out.push_back(*it);
        if (out.size() >= limit) break;
    }
    xSemaphoreGive(_mutex);
    return out;
}

void SmsStore::clearCalls() {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _calls.clear();
    saveCalls();
    xSemaphoreGive(_mutex);
}
