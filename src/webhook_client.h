#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class ConfigManager;

class WebhookClient {
public:
    void begin(ConfigManager* cfg);
    void enqueueEvent(const String& eventType, const JsonDocument& payload);
    void loop(); // Process one queued webhook if pending

private:
    struct QueuedItem {
        char json[768];
    };

    ConfigManager* _cfg = nullptr;
    QueueHandle_t _queue = nullptr;
};
