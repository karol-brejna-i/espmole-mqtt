#ifndef ESPMOLE_MQTT_TRANSPORT_H
#define ESPMOLE_MQTT_TRANSPORT_H

#ifndef NATIVE_BUILD

#include <Arduino.h>
#include <ESPMoleCore.h>
#include <functional>

// Forward declaration - we don't want to force include of AsyncMqttClient
class AsyncMqttClient;
class PubSubClient;

namespace espmole {

class Dispatcher;

/**
 * MQTT configuration structure.
 * 
 * Used for standalone mode where ESPMole creates and owns the MQTT client.
 */
struct MqttConfig {
    // Connection settings
    const char* broker = nullptr;       ///< MQTT broker hostname/IP
    uint16_t port = 1883;               ///< MQTT broker port
    const char* username = nullptr;     ///< Authentication username (optional)
    const char* password = nullptr;     ///< Authentication password (optional)
    const char* clientId = nullptr;     ///< Client ID (nullptr = auto-generate from MAC)
    
    // Topic settings
    const char* baseTopic = "espmole";  ///< Base topic prefix
    const char* deviceId = nullptr;     ///< Device identifier (nullptr = MAC address)
    
    // Status/LWT settings
    bool enableStatus = true;           ///< Enable birth/LWT messages
    const char* birthPayload = "online";
    const char* lwtPayload = "offline";
    bool retainStatus = true;           ///< Retain status messages
    
    // Behavior
    uint32_t reconnectInterval = 5000;  ///< Reconnection attempt interval (ms)
    uint8_t qos = 0;                    ///< QoS level for cmd/resp topics
};

/**
 * MQTT transport for ESPMole.
 * 
 * Supports two modes of operation:
 * 
 * 1. **Standalone mode**: ESPMole creates and manages the MQTT client.
 *    Use `begin()` and `poll()` in this mode.
 * 
 * 2. **Integration mode**: User provides existing MQTT client.
 *    Use `attachTo()` and `handleMessage()` in this mode.
 * 
 * Topic structure:
 * - `espmole/<device-id>/cmd`    - Commands TO the device (subscribe)
 * - `espmole/<device-id>/resp`   - Responses FROM the device (publish)
 * - `espmole/<device-id>/status` - Online/offline status (birth/LWT)
 * - `espmole/<device-id>/event`  - Async events/broadcasts (publish)
 * 
 * Usage (Standalone):
 * @code
 *   MqttConfig config;
 *   config.broker = "mqtt.example.com";
 *   MqttTransport mqtt(&dispatcher, config);
 *   mqtt.begin();
 *   
 *   void loop() { mqtt.poll(); }
 * @endcode
 * 
 * Usage (Integration with existing client):
 * @code
 *   MqttTransport mole(&dispatcher);
 *   mole.attachTo(&existingMqttClient);
 *   
 *   // In your existing callback:
 *   void callback(char* topic, byte* payload, unsigned int len) {
 *       if (mole.handleMessage(topic, payload, len)) return;
 *       // ... your existing code ...
 *   }
 * @endcode
 */
class MqttTransport : public ITransport {
public:
    /// Callback type for user messages (non-ESPMole topics)
    using UserMessageCallback = std::function<void(const char*, const uint8_t*, size_t)>;
    
    // Topic buffer sizes
    static constexpr size_t TOPIC_MAX_LEN = 80;
    static constexpr size_t DEVICE_ID_MAX_LEN = 32;
    static constexpr size_t RESPONSE_BUFFER_SIZE = 256;

    /**
     * Construct MQTT transport for integration mode (no config needed).
     * Use attachTo() to connect to existing MQTT client.
     * 
     * @param dispatcher  Dispatcher to deliver incoming commands
     */
    explicit MqttTransport(Dispatcher* dispatcher);
    
    /**
     * Construct MQTT transport for standalone mode.
     * Use begin() to create client and connect.
     * 
     * @param dispatcher  Dispatcher to deliver incoming commands
     * @param config      MQTT configuration
     */
    MqttTransport(Dispatcher* dispatcher, const MqttConfig& config);
    
    ~MqttTransport();

    // =========================================================================
    // Standalone Mode API
    // =========================================================================
    
    /**
     * Initialize and connect to MQTT broker (standalone mode).
     * Creates MQTT client, sets up LWT, connects, subscribes, publishes birth.
     * Call in setup() after WiFi is connected.
     */
    void begin();
    
    /**
     * Process MQTT events (standalone mode).
     * Call in loop(). Handles reconnection, keep-alive, incoming messages.
     */
    void poll();
    
    /**
     * Subscribe to additional topic (standalone mode).
     * Messages will be delivered to UserMessageCallback.
     * 
     * @param topic  Topic to subscribe to
     * @param qos    QoS level (default 0)
     * @return       true if subscription initiated
     */
    bool subscribe(const char* topic, uint8_t qos = 0);
    
    /**
     * Publish to arbitrary topic (standalone mode).
     * 
     * @param topic    Topic to publish to
     * @param payload  Message payload
     * @param len      Payload length
     * @param qos      QoS level (default 0)
     * @param retain   Retain flag (default false)
     * @return         true if publish initiated
     */
    bool publish(const char* topic, const uint8_t* payload, size_t len, 
                 uint8_t qos = 0, bool retain = false);
    
    /**
     * Set callback for user's messages (non-ESPMole topics).
     * Used in standalone mode when user wants to handle additional topics.
     * 
     * @param cb  Callback function
     */
    void setUserCallback(UserMessageCallback cb) { userCallback_ = cb; }

    // =========================================================================
    // Integration Mode API
    // =========================================================================
    
    /**
     * Attach to existing AsyncMqttClient (integration mode).
     * Stores reference, subscribes to ESPMole topics, publishes birth.
     * Call BEFORE mqtt.connect() to set up LWT.
     * 
     * @param client  Existing AsyncMqttClient instance
     */
    void attachTo(AsyncMqttClient* client);
    
    /**
     * Attach to existing PubSubClient (integration mode).
     * Stores reference, subscribes to ESPMole topics, publishes birth.
     * Call AFTER mqtt.connect() since PubSubClient doesn't support pre-connect LWT well.
     * 
     * @param client  Existing PubSubClient instance
     */
    void attachTo(PubSubClient* client);
    
    /**
     * Handle incoming MQTT message (integration mode).
     * Call this at the START of your existing MQTT callback.
     * 
     * @param topic    Message topic
     * @param payload  Message payload
     * @param len      Payload length
     * @return         true if message was handled (ESPMole topic), false otherwise
     */
    bool handleMessage(const char* topic, const uint8_t* payload, size_t len);
    
    /**
     * Called when MQTT connects (integration mode, AsyncMqttClient).
     * Call this from your onConnect callback to subscribe and publish birth.
     */
    void onMqttConnect();

    // =========================================================================
    // Common API
    // =========================================================================
    
    /**
     * Check if connected to MQTT broker.
     */
    bool connected() const;
    
    /**
     * Get the full command topic (e.g., "espmole/device123/cmd").
     */
    const char* getCommandTopic() const { return cmdTopic_; }
    
    /**
     * Get the full response topic (e.g., "espmole/device123/resp").
     */
    const char* getResponseTopic() const { return respTopic_; }
    
    /**
     * Get the full status topic (e.g., "espmole/device123/status").
     */
    const char* getStatusTopic() const { return statusTopic_; }
    
    /**
     * Get the device ID being used.
     */
    const char* getDeviceId() const { return deviceId_; }

    // =========================================================================
    // ITransport Interface
    // =========================================================================
    
    bool send(PeerHandle peer, const uint8_t* data, size_t len) override;
    bool broadcast(const uint8_t* data, size_t len) override;
    const char* name() const override { return "MQTT"; }

private:
    Dispatcher* dispatcher_;
    MqttConfig config_;
    bool standaloneMode_ = false;
    
    // Topics (built during initialization)
    char cmdTopic_[TOPIC_MAX_LEN];
    char respTopic_[TOPIC_MAX_LEN];
    char statusTopic_[TOPIC_MAX_LEN];
    char eventTopic_[TOPIC_MAX_LEN];
    char deviceId_[DEVICE_ID_MAX_LEN];
    
    // Client references (only one will be non-null)
    AsyncMqttClient* asyncClient_ = nullptr;
    PubSubClient* pubSubClient_ = nullptr;
    bool ownsClient_ = false;  // true if we created the client (standalone mode)
    
    // User callback for non-ESPMole messages
    UserMessageCallback userCallback_;
    
    // State
    bool wasConnected_ = false;
    uint32_t lastReconnectAttempt_ = 0;
    
    // Internal methods
    void buildTopics();
    void buildDeviceId();
    void subscribeToCommandTopic();
    void publishBirth();
    void publishLwt();
    bool mqttPublish(const char* topic, const uint8_t* payload, size_t len, 
                     uint8_t qos = 0, bool retain = false);
    bool isMoleTopic(const char* topic) const;
    void processCommand(const uint8_t* payload, size_t len);
    
    // AsyncMqttClient callbacks (standalone mode)
    void onAsyncConnect(bool sessionPresent);
    void onAsyncDisconnect(int8_t reason);
    void onAsyncMessage(char* topic, char* payload, 
                        size_t len, size_t index, size_t total);
};

/// Special PeerHandle value for MQTT messages
constexpr PeerHandle PEER_MQTT = 0xFFFF0001;

} // namespace espmole

#endif // NATIVE_BUILD
#endif // ESPMOLE_MQTT_TRANSPORT_H
