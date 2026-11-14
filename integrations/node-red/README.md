# Node-Red Integration

1. In Node-Red, open **Menu (☰) → Import** and import [`flow.json`](./flow.json).
2. Update the `Set API Key Header` function node with your gateway's `api_key` (or remove the header if no API key is configured).
3. Update `http://esp32-cellbridge.local` if your network does not resolve mDNS (use the ESP32's IP address).
4. In the gateway Web UI (**Settings & OTA → Webhook**), enable webhooks and set:
   - **Webhook URL**: `http://<YOUR_NODE_RED_IP>:1880/a7670e-webhook`

### Webhook Payload Format

**Incoming SMS (`sms.received`):**
```json
{
  "event": "sms.received",
  "device": "esp32-cellbridge",
  "data": {
    "id": 12,
    "from": "+31612345678",
    "message": "Hoi!",
    "timestamp": "2026-11-14T17:05:00+01:00",
    "parts": 1
  }
}
```

**Incoming Call (`call.incoming`):**
```json
{
  "event": "call.incoming",
  "device": "esp32-cellbridge",
  "data": {
    "id": 3,
    "from": "+31612345678",
    "timestamp": "uptime:420s"
  }
}
```
