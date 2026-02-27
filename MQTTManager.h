// файл: MQTTManager.h
// Заголовочный файл класса для управления MQTT подключением

#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "config.h"
#include "Scale.h"
#include "StateMachine.h"
#include "WiFiManager.h"

#define WATER_LEVEL_EMPTY 500
#define WATER_LEVEL_LOW 1000

typedef void (*CommandCallback)(int mode);

class MQTTManager {
private:
    // ==================== ОСНОВНЫЕ ОБЪЕКТЫ ====================
    WiFiClient wifiClient;
    PubSubClient mqttClient;
    Preferences preferences;  // Оставляем, но будем правильно закрывать
    
    // ==================== НАСТРОЙКИ ТОПИКОВ ====================
    String clientId;
    String waterLevelTopic;
    String kettleTopic;
    String fillingTopic;
    
    // ==================== УЧЕТНЫЕ ДАННЫЕ ====================
    String mqttUser;
    String mqttPass;
    
    // ==================== CALLBACK ====================
    CommandCallback commandCallback;
    
    // ==================== ССЫЛКИ НА КОМПОНЕНТЫ ====================
    Scale& scale;
    StateMachine& stateMachine;
    WiFiManager& wifiManager;
    
    // ==================== ТАЙМИНГИ И СТАТИСТИКА ====================
    unsigned long lastReconnectAttempt;
    unsigned long lastPublishTime;
    unsigned long lastHeartbeatTime;
    unsigned long lastConnectionCheckTime;
    unsigned long messagesSent;
    unsigned long messagesFailed;
    unsigned long reconnectAttempts;
    
    // ==================== КЭШ ====================
    int lastWaterState;
    bool lastKettlePresent;
    bool lastMqttConnected;
    
    // ==================== ПРИВАТНЫЕ МЕТОДЫ ====================
    void connect();
    void subscribe();
    static void mqttCallback(char* topic, byte* payload, unsigned int length);
    bool publish(const String& topic, const String& payload, bool retained = false);
    int calculateWaterState();
    
    // ==================== УПРАВЛЕНИЕ ПАМЯТЬЮ ====================
    void clearStrings();  // Метод для очистки строк

public:
    // ==================== КОНСТРУКТОР ====================
    MQTTManager(Scale& s, StateMachine& sm, WiFiManager& wm);
    
    // ==================== ДЕСТРУКТОР ====================
    ~MQTTManager();  // Добавляем деструктор для очистки
    
    // ==================== ОСНОВНЫЕ МЕТОДЫ ====================
    void begin();
    void loop();
    
    // ==================== УПРАВЛЕНИЕ CALLBACK ====================
    void setCommandCallback(CommandCallback cb) { commandCallback = cb; }
    
    // ==================== РАБОТА С УЧЕТНЫМИ ДАННЫМИ ====================
    bool loadCredentials();
    bool saveCredentials(const String& user, const String& pass);
    bool hasCredentials();
    void clearCredentials();
    
    // ==================== ПУБЛИКАЦИЯ СТАТУСОВ ====================
    bool publishWaterState();
    bool publishKettleState();
     
    // ==================== УПРАВЛЕНИЕ ПОДКЛЮЧЕНИЕМ ====================
    bool isConnected() { return mqttClient.connected(); }
    void disconnect();
    void reconnect();
    
    // ==================== ГЕТТЕРЫ ====================
    unsigned long getMessagesSent() { return messagesSent; }
    unsigned long getMessagesFailed() { return messagesFailed; }
    unsigned long getReconnectAttempts() { return reconnectAttempts; }
    String getCurrentUser() { return mqttUser; }
};

#endif