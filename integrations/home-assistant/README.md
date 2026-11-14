# Home Assistant Integration

There are **two ways** to connect your **ESP32 CellBridge** gateway to Home Assistant (you can also combine them):

## Option 1: MQTT Auto-Discovery (Recommended)

1. Open the gateway Web UI → **Settings & OTA** → **MQTT & Home Assistant**.
2. Enable **MQTT** and **Home Assistant MQTT Discovery**.
3. Enter your Mosquitto / Home Assistant MQTT broker host, port (`1883`), username, and password.
4. Click **Save Configuration**.

Within seconds, a new device **ESP32 CellBridge** appears in **Settings → Devices & Services → MQTT** with:
- **Sensors**: Cellular Signal (dBm), Battery Level (%), Battery Voltage (V), Mobile Operator, Prepaid Balance (EUR), Last SMS Sender, Last SMS Body, Call State
- **Binary / Availability**: Online/Offline via MQTT LWT
- **Switch**: 4G Internet Proxy (toggle cellular data + proxy on/off)
- **Buttons**: Hangup Call, Check Prepaid Balance, Reboot Modem

To send an SMS from an HA automation via MQTT:
```yaml
service: mqtt.publish
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
