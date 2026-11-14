#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <vector>

#include "sms_codec.h"

struct SmsRecord {
    uint32_t id = 0;
    String direction;   // "in" | "out"
    String peer;        // phone number or sender ID
    String text;
    String timestamp;
    bool read = false;
    uint8_t parts = 1;
    String status;      // "received" | "sent" | "failed"
};

struct CallRecord {
    uint32_t id = 0;
    String direction;   // "in" | "out"
    String peer;
    String timestamp;
    uint32_t durationSec = 0;
    String status;      // "ringing" | "answered" | "ended" | "missed" | "dialing"
};

struct MultipartAssembly {
    bool active = false;
    String sender;
    uint16_t refId = 0;
    uint8_t totalParts = 0;
    String timestamp;
    uint32_t firstSeenMs = 0;
    std::vector<String> parts;
    std::vector<bool> received;
};

class SmsStore {
public:
    bool begin();

    // SMS operations
    SmsRecord addSms(const String& direction, const String& peer, const String& text,
                     const String& timestamp, const String& status, uint8_t parts = 1, bool read = false);

    // Handle a decoded incoming SMS (reassembles multipart automatically).
    // Returns true and sets `completed` if a full message is ready.
    bool ingestDecodedSms(const SmsCodec::DecodedSms& decoded, SmsRecord& completed);

    // Flush stale incomplete multipart messages after timeout
    void checkMultipartTimeouts(std::vector<SmsRecord>& flushed);

    std::vector<SmsRecord> listSms(const String& directionFilter = "", bool unreadOnly = false, size_t limit = 100);
    bool markRead(uint32_t id);
    void markAllRead();
    bool deleteSms(uint32_t id);
    void clearSms();
    size_t unreadCount();
    size_t totalSmsCount();

    // Call history
    CallRecord addCall(const String& direction, const String& peer, const String& timestamp, const String& status);
    void updateLastCall(uint32_t id, const String& status, uint32_t durationSec);
    std::vector<CallRecord> listCalls(size_t limit = 50);
    void clearCalls();

    static String fallbackTimestamp();

private:
    bool loadSms();
    bool saveSms();
    bool loadCalls();
    bool saveCalls();

    SemaphoreHandle_t _mutex = nullptr;
    std::vector<SmsRecord> _sms;
    std::vector<CallRecord> _calls;
    MultipartAssembly _multipart[4];
    uint32_t _nextSmsId = 1;
    uint32_t _nextCallId = 1;

    static constexpr const char* kSmsPath = "/sms.json";
    static constexpr const char* kCallsPath = "/calls.json";
};
