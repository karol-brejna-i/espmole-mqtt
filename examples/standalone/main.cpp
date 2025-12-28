/**
 * ESPMole MQTT Backend - Standalone Mode Example
 * 
 * This example shows ESPMole managing the entire MQTT stack.
 * No existing MQTT code required.
 */

#include <WiFi.h>
#include <ESPMoleCore.h>
#include <MqttTransport.h>

// ============================================================
// Configuration - Set via environment variables during build:
//   WIFI_SSID, WIFI_PASS, MQTT_BROKER, MQTT_PORT
// ============================================================
#ifndef WIFI_SSID
#define WIFI_SSID "your-ssid"
#endif
#ifndef WIFI_PASS
#define WIFI_PASS "your-password"
#endif
#ifndef MQTT_BROKER
#define MQTT_BROKER "192.168.1.100"
#endif
#ifndef MQTT_PORT
#define MQTT_PORT 1883
#endif

// ESPMole components
espmole::Dispatcher dispatcher;
espmole::CliProtocol protocol;

// MQTT configuration
espmole::MqttConfig mqttConfig;

// MQTT transport (standalone mode)
espmole::MqttTransport* mole = nullptr;

// Example command handler
static bool ledState = false;

espmole::CommandResult ledCommandHandler(const espmole::RequestView& req, void* ctx) {
    (void)req;
    (void)ctx;
    ledState = !ledState;
    // digitalWrite(LED_BUILTIN, ledState);
    const char* msg = ledState ? "LED ON" : "LED OFF";
    return espmole::CommandResult::ok(msg, strlen(msg));
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\n=== ESPMole MQTT Standalone Example ===");
    
    // Connect to WiFi
    Serial.printf("Connecting to WiFi: %s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();
    Serial.printf("Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    
    // Configure MQTT
    mqttConfig.broker = MQTT_BROKER;
    mqttConfig.port = MQTT_PORT;
    mqttConfig.deviceId = nullptr;  // Use MAC address
    
    // Create transport
    mole = new espmole::MqttTransport(&dispatcher, mqttConfig);
    
    // Initialize ESPMole
    dispatcher.setProtocol(&protocol);
    dispatcher.setTransport(mole);
    
    // Register custom commands
    dispatcher.registerCommand("led", ledCommandHandler);
    
    // Optional: handle non-ESPMole messages
    mole->setUserCallback([](const char* topic, const uint8_t* payload, size_t len) {
        Serial.printf("User topic: %s = %.*s\n", topic, (int)len, payload);
    });
    
    // Connect to MQTT broker
    mole->begin();
    
    Serial.println("ESPMole MQTT ready!");
    Serial.printf("Command topic: %s\n", mole->getCommandTopic());
    Serial.printf("Response topic: %s\n", mole->getResponseTopic());
}

void loop() {
    // Process MQTT events
    mole->poll();
    
    // Your other code here...
    delay(10);
}
