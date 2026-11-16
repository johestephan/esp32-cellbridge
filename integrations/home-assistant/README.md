# Home Assistant Integration

There are **two ways** to connect your **ESP32 CellBridge** gateway to Home Assistant (you can also combine them):

## Option 1: MQTT Auto-Discovery (Recommended)

1. Open the gateway Web UI → **Settings & OTA** → **MQTT & Home Assistant**.
2. Enable **MQTT** and **Home Assistant MQTT Discovery**.
3. Enter your Mosquitto / Home Assistant MQTT broker host, port (`1883`), username, and password.
4. Click **Save Configuration**.

Within seconds, a new device **ESP32 CellBridge** appears in **Settings → Devices & Services → MQTT** with:
- **Notify**: `Send SMS` (`notify.<hostname>_send_sms` via `notify.send_message`)
- **Text**: `SMS Recipient` (`text.<hostname>_sms_recipient`, defaults to your configured Default Contact)
- **Sensors**: Cellular Signal (dBm), Battery Level (%), Battery Voltage (V), Mobile Operator, Prepaid Balance (EUR), Last SMS Sender, Last SMS Body, Call State
- **Binary / Availability**: Online/Offline via MQTT LWT
- **Switch**: 4G Internet Proxy (toggle cellular data + proxy on/off)
- **Buttons**: Hangup Call, Check Prepaid Balance, Reboot Modem

### Sending SMS directly via the Home Assistant Notify entity (no YAML setup required)

1. **Specify the number inline (`"<number>: <message>"`):**
```yaml
action: notify.send_message
target:
  entity_id: notify.esp32_cellbridge_send_sms
data:
  message: "+31612345678: Alarm triggered!"
```

2. **Or set `SMS Recipient` first (or omit number to use Default Contact):**
```yaml
- action: text.set_value
  target:
    entity_id: text.esp32_cellbridge_sms_recipient
  data:
    value: "+31612345678"
- action: notify.send_message
  target:
    entity_id: notify.esp32_cellbridge_send_sms
  data:
    message: "Alarm triggered!"
```

3. **Or publish JSON directly via MQTT (`to` falls back to Default Contact if omitted):**
```yaml
action: mqtt.publish
data:
  topic: "esp32_cellbridge/sms/send"
  payload: '{"to": "+31612345678", "message": "Alarm triggered!"}'
```

To place a ring-alert call (rings for 15s and hangs up automatically — €0 cost if unanswered!):
```yaml
service: mqtt.publish
data:
  topic: "esp32_cellbridge/call/dial"
  payload: '{"to": "+31612345678", "timeout_sec": 15}'
```

## Option 2: REST API + Webhook

See [`configuration.yaml`](./configuration.yaml) in this folder for `rest_command`, `notify.sms_gateway`, and an incoming SMS webhook automation example.

In your Home Assistant `secrets.yaml`:
```yaml
a7670e_api_key: "your-gateway-api-key"
```
