#ifndef NATIVE_BUILD

#include "MqttTransport.h"
#include <ESPMoleCore.h>
#include <AsyncMqttClient.h>

// PubSubClient support is optional - only include if available
#if __has_include(<PubSubClient.h>)
    #include <PubSubClient.h>
    #define ESPMOLE_HAS_PUBSUBCLIENT 1
#else
    #define ESPMOLE_HAS_PUBSUBCLIENT 0
#endif

#if defined(ESP32)
    #include <WiFi.h>
#elif defined(ESP8266)
    #include <ESP8266WiFi.h>
#endif

namespace espmole {

// =============================================================================
// Constructors / Destructor
// =============================================================================

MqttTransport::MqttTransport(Dispatcher* dispatcher)
    : dispatcher_(dispatcher)
    , standaloneMode_(false)
{
    // Integration mode - topics will be built when attachTo() is called
    memset(cmdTopic_, 0, sizeof(cmdTopic_));
    memset(respTopic_, 0, sizeof(respTopic_));
    memset(statusTopic_, 0, sizeof(statusTopic_));
    memset(eventTopic_, 0, sizeof(eventTopic_));
    memset(deviceId_, 0, sizeof(deviceId_));
}

MqttTransport::MqttTransport(Dispatcher* dispatcher, const MqttConfig& config)
    : dispatcher_(dispatcher)
    , config_(config)
    , standaloneMode_(true)
{
    memset(cmdTopic_, 0, sizeof(cmdTopic_));
    memset(respTopic_, 0, sizeof(respTopic_));
    memset(statusTopic_, 0, sizeof(statusTopic_));
    memset(eventTopic_, 0, sizeof(eventTopic_));
    memset(deviceId_, 0, sizeof(deviceId_));
}

MqttTransport::~MqttTransport() {
    if (ownsClient_ && asyncClient_) {
        asyncClient_->disconnect();
        delete asyncClient_;
        asyncClient_ = nullptr;
    }
}

// =============================================================================
// Topic Building
// =============================================================================

void MqttTransport::buildDeviceId() {
    if (config_.deviceId != nullptr) {
        strncpy(deviceId_, config_.deviceId, DEVICE_ID_MAX_LEN - 1);
        deviceId_[DEVICE_ID_MAX_LEN - 1] = '\0';
    } else {
        // Use MAC address as device ID
        uint8_t mac[6];
        WiFi.macAddress(mac);
        snprintf(deviceId_, DEVICE_ID_MAX_LEN, "%02X%02X%02X%02X%02X%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
}

void MqttTransport::buildTopics() {
    buildDeviceId();
    
    const char* base = config_.baseTopic ? config_.baseTopic : "espmole";
    
    snprintf(cmdTopic_, TOPIC_MAX_LEN, "%s/%s/cmd", base, deviceId_);
    snprintf(respTopic_, TOPIC_MAX_LEN, "%s/%s/resp", base, deviceId_);
    snprintf(statusTopic_, TOPIC_MAX_LEN, "%s/%s/status", base, deviceId_);
    snprintf(eventTopic_, TOPIC_MAX_LEN, "%s/%s/event", base, deviceId_);
}

// =============================================================================
// Standalone Mode Implementation
// =============================================================================

void MqttTransport::begin() {
    if (!standaloneMode_) {
        // Called begin() but constructed without config - use defaults
        standaloneMode_ = true;
    }
    
    buildTopics();
    
    if (config_.broker == nullptr) {
        // No broker configured, cannot start
        return;
    }
    
    // Create AsyncMqttClient
    asyncClient_ = new AsyncMqttClient();
    ownsClient_ = true;
    
    // Configure server
    asyncClient_->setServer(config_.broker, config_.port);
    
    // Configure credentials if provided
    if (config_.username != nullptr) {
        asyncClient_->setCredentials(config_.username, config_.password);
    }
    
    // Configure client ID
    if (config_.clientId != nullptr) {
        asyncClient_->setClientId(config_.clientId);
    } else {
        // Use device ID as client ID
        asyncClient_->setClientId(deviceId_);
    }
    
    // Configure LWT (Last Will Testament)
    if (config_.enableStatus) {
        asyncClient_->setWill(
            statusTopic_,
            1,  // QoS 1 for LWT
            config_.retainStatus,
            config_.lwtPayload
        );
    }
    
    // Set up callbacks using lambdas that capture 'this'
    asyncClient_->onConnect([this](bool sessionPresent) {
        this->onAsyncConnect(sessionPresent);
    });
    
    asyncClient_->onDisconnect([this](AsyncMqttClientDisconnectReason reason) {
        this->onAsyncDisconnect(static_cast<int8_t>(reason));
    });
    
    asyncClient_->onMessage([this](char* topic, char* payload,
                                    AsyncMqttClientMessageProperties properties,
                                    size_t len, size_t index, size_t total) {
        (void)properties;
        this->onAsyncMessage(topic, payload, len, index, total);
    });
    
    // Connect
    asyncClient_->connect();
}

void MqttTransport::poll() {
    // AsyncMqttClient is event-driven, so poll() mainly handles reconnection
    if (!standaloneMode_ || asyncClient_ == nullptr) {
        return;
    }
    
    bool isConnected = asyncClient_->connected();
    
    // Handle reconnection
    if (!isConnected && wasConnected_) {
        // Just disconnected
        wasConnected_ = false;
    }
    
    if (!isConnected) {
        uint32_t now = millis();
        if (now - lastReconnectAttempt_ >= config_.reconnectInterval) {
            lastReconnectAttempt_ = now;
            asyncClient_->connect();
        }
    }
}

void MqttTransport::onAsyncConnect(bool sessionPresent) {
    (void)sessionPresent;
    wasConnected_ = true;
    
    // Subscribe to command topic
    subscribeToCommandTopic();
    
    // Publish birth message
    publishBirth();
}

void MqttTransport::onAsyncDisconnect(int8_t reason) {
    (void)reason;
    wasConnected_ = false;
}

void MqttTransport::onAsyncMessage(char* topic, char* payload, 
                                    size_t len, size_t index, size_t total) {
    // Handle fragmented messages - only process when complete
    if (index != 0 || len != total) {
        // For simplicity, we don't buffer fragmented messages
        // This could be enhanced in the future
        return;
    }
    
    handleMessage(topic, reinterpret_cast<const uint8_t*>(payload), len);
}

// =============================================================================
// Integration Mode Implementation
// =============================================================================

void MqttTransport::attachTo(AsyncMqttClient* client) {
    asyncClient_ = client;
    ownsClient_ = false;
    standaloneMode_ = false;
    
    buildTopics();
    
    // For AsyncMqttClient, LWT must be set before connect()
    // User should call attachTo() before mqtt.connect()
    if (config_.enableStatus) {
        client->setWill(
            statusTopic_,
            1,  // QoS 1 for LWT
            config_.retainStatus,
            config_.lwtPayload
        );
    }
}

void MqttTransport::attachTo(PubSubClient* client) {
#if ESPMOLE_HAS_PUBSUBCLIENT
    pubSubClient_ = client;
    ownsClient_ = false;
    standaloneMode_ = false;
    
    buildTopics();
    
    // For PubSubClient, we subscribe immediately since it's typically
    // called after connect()
    subscribeToCommandTopic();
    publishBirth();
#else
    (void)client;
    // PubSubClient not available - log error or handle gracefully
#endif
}

void MqttTransport::onMqttConnect() {
    // Called by user from their onConnect callback (AsyncMqttClient integration)
    subscribeToCommandTopic();
    publishBirth();
}

bool MqttTransport::handleMessage(const char* topic, const uint8_t* payload, size_t len) {
    // Check if this is our command topic
    if (strcmp(topic, cmdTopic_) != 0) {
        // Not our topic - check if it's any ESPMole topic we should ignore
        if (isMoleTopic(topic)) {
            return true;  // ESPMole topic but not command, handled (ignored)
        }
        
        // Not an ESPMole topic - forward to user callback if set
        if (userCallback_) {
            userCallback_(topic, payload, len);
        }
        return false;  // Not handled by ESPMole
    }
    
    // Process command through dispatcher
    processCommand(payload, len);
    return true;
}

// =============================================================================
// Common Implementation
// =============================================================================

bool MqttTransport::connected() const {
    if (asyncClient_) {
        return asyncClient_->connected();
    }
#if ESPMOLE_HAS_PUBSUBCLIENT
    if (pubSubClient_) {
        return pubSubClient_->connected();
    }
#endif
    return false;
}

void MqttTransport::subscribeToCommandTopic() {
    if (asyncClient_) {
        asyncClient_->subscribe(cmdTopic_, config_.qos);
    } 
#if ESPMOLE_HAS_PUBSUBCLIENT
    else if (pubSubClient_) {
        pubSubClient_->subscribe(cmdTopic_);
    }
#endif
}

void MqttTransport::publishBirth() {
    if (!config_.enableStatus) return;
    
    mqttPublish(statusTopic_, 
                reinterpret_cast<const uint8_t*>(config_.birthPayload),
                strlen(config_.birthPayload),
                1,  // QoS 1 for birth
                config_.retainStatus);
}

bool MqttTransport::mqttPublish(const char* topic, const uint8_t* payload, 
                                 size_t len, uint8_t qos, bool retain) {
    if (asyncClient_ && asyncClient_->connected()) {
        asyncClient_->publish(topic, qos, retain, 
                              reinterpret_cast<const char*>(payload), len);
        return true;
    }
#if ESPMOLE_HAS_PUBSUBCLIENT
    if (pubSubClient_ && pubSubClient_->connected()) {
        return pubSubClient_->publish(topic, payload, len, retain);
    }
#endif
    return false;
}

bool MqttTransport::isMoleTopic(const char* topic) const {
    const char* base = config_.baseTopic ? config_.baseTopic : "espmole";
    size_t baseLen = strlen(base);
    return strncmp(topic, base, baseLen) == 0 && topic[baseLen] == '/';
}

void MqttTransport::processCommand(const uint8_t* payload, size_t len) {
    if (!dispatcher_) return;
    
    uint8_t response[RESPONSE_BUFFER_SIZE];
    size_t respLen = dispatcher_->ingest(
        PEER_MQTT,
        payload,
        len,
        response,
        sizeof(response)
    );
    
    if (respLen > 0) {
        // Publish response
        mqttPublish(respTopic_, response, respLen, config_.qos, false);
    }
}

// =============================================================================
// ITransport Interface
// =============================================================================

bool MqttTransport::send(PeerHandle peer, const uint8_t* data, size_t len) {
    (void)peer;  // MQTT responses always go to response topic
    return mqttPublish(respTopic_, data, len, config_.qos, false);
}

bool MqttTransport::broadcast(const uint8_t* data, size_t len) {
    // Broadcasts go to event topic
    return mqttPublish(eventTopic_, data, len, config_.qos, false);
}

// =============================================================================
// Additional Public Methods
// =============================================================================

bool MqttTransport::subscribe(const char* topic, uint8_t qos) {
    if (asyncClient_ && asyncClient_->connected()) {
        asyncClient_->subscribe(topic, qos);
        return true;
    }
#if ESPMOLE_HAS_PUBSUBCLIENT
    if (pubSubClient_ && pubSubClient_->connected()) {
        return pubSubClient_->subscribe(topic);
    }
#endif
    return false;
}

bool MqttTransport::publish(const char* topic, const uint8_t* payload, 
                            size_t len, uint8_t qos, bool retain) {
    return mqttPublish(topic, payload, len, qos, retain);
}

} // namespace espmole

#endif // NATIVE_BUILD
