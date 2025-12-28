# espmole-backend-mqtt

MQTT transport backend for ESPMole - enables remote command/response communication over MQTT.

## Features

- **Standalone Mode**: ESPMole creates and manages the MQTT client
- **Integration Mode**: Attach to your existing MQTT client with minimal code changes
- **Birth/LWT**: Automatic online/offline status messages
- **Topic Isolation**: Uses `espmole/<device-id>/` prefix to avoid conflicts
- **Library Support**: Works with AsyncMqttClient (PubSubClient support planned)

## Installation

### PlatformIO

```ini
lib_deps = 
    espmole/espmole-backend-mqtt
```

## Usage

### Standalone Mode (No Existing MQTT Code)

```cpp
#include <WiFi.h>
#include <ESPMoleCore.h>
#include <MqttTransport.h>

espmole::Dispatcher dispatcher;
espmole::CliProtocol protocol;

espmole::MqttConfig config;
config.broker = "mqtt.example.com";
config.deviceId = "my-esp32";

espmole::MqttTransport mole(&dispatcher, config);

void setup() {
    WiFi.begin("ssid", "password");
    while (WiFi.status() != WL_CONNECTED) delay(100);
    
    dispatcher.setProtocol(&protocol);
    dispatcher.setTransport(&mole);
    mole.begin();
}

void loop() {
    mole.poll();
}
```

### Integration Mode (Existing MQTT App)

Add just one line to your existing callback:

```cpp
#include <MqttTransport.h>

espmole::MqttTransport mole(&dispatcher);

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    if (mole.handleMessage(topic, payload, length)) return;  // ← ADD THIS
    // ... your existing code unchanged ...
}

void setup() {
    // ... your existing setup ...
    mole.attachTo(&mqttClient);  // ← ADD THIS
}
```

## Topics

| Topic | Direction | Purpose |
|-------|-----------|---------|
| `espmole/<device>/cmd` | Subscribe | Commands TO device |
| `espmole/<device>/resp` | Publish | Responses FROM device |
| `espmole/<device>/status` | Publish | Online/offline (retained) |
| `espmole/<device>/event` | Publish | Async broadcasts |

## Testing with mosquitto

```bash
# Subscribe to responses
mosquitto_sub -h mqtt.example.com -t "espmole/my-esp32/resp"

# Send command
mosquitto_pub -h mqtt.example.com -t "espmole/my-esp32/cmd" -m "ping"

# Check status
mosquitto_sub -h mqtt.example.com -t "espmole/my-esp32/status"
```

## License

Apache-2.0
