#include "webhook_client.h"
#include "config_manager.h"
#include "config.h"

#include <HTTPClient.h>
#include <WiFi.h>

void WebhookClient::begin(ConfigManager* cfg) {
    _cfg = cfg;
    _queue = xQueueCreate(10, sizeof(QueuedItem));
}

void WebhookClient::enqueueEvent(const String& eventType, const JsonDocument& payload) {
    if (!_cfg || !_queue) return;
    const auto& c = _cfg->get();
    if (!c.webhookEnabled || c.webhookUrl.length() == 0) return;

    JsonDocument doc;
    doc["event"] = eventType;
    doc["device"] = c.hostname;
    doc["data"] = payload.as<JsonVariantConst>();

    QueuedItem item;
    memset(&item, 0, sizeof(item));
    size_t len = serializeJson(doc, item.json, sizeof(item.json) - 1);
    if (len > 0) {
        xQueueSend(_queue, &item, 0);
    }
}

void WebhookClient::loop() {
    if (!_cfg || !_queue) return;
    if (WiFi.status() != WL_CONNECTED) return;

    QueuedItem item;
    if (xQueueReceive(_queue, &item, 0) != pdTRUE) return;

    const auto& c = _cfg->get();
    if (!c.webhookEnabled || c.webhookUrl.length() == 0) return;

    HTTPClient http;
    http.setTimeout(5000);
    if (http.begin(c.webhookUrl)) {
        http.addHeader("Content-Type", "application/json");
        http.addHeader("User-Agent", String(FW_NAME) + "/" + FW_VERSION);
        if (c.webhookSecret.length() > 0) {
            http.addHeader("X-Webhook-Secret", c.webhookSecret);
        }
        int code = http.POST(reinterpret_cast<uint8_t*>(item.json), strlen(item.json));
        Serial.printf("[Webhook] POST %s -> HTTP %d\n", c.webhookUrl.c_str(), code);
        http.end();
    }
}
