// файл: MQTTManager.cpp
// Реализация методов класса MQTTManager с исправлением утечек памяти

#include "MQTTManager.h"

// ==================== КОНСТАНТЫ ====================
#define MQTT_SERVER "mqtt.dealgate.ru"
#define MQTT_PORT 1883
#define MQTT_RECONNECT_DELAY 5000
#define MQTT_PUBLISH_INTERVAL 5000

static MQTTManager* instance = nullptr;

// ==================== КОНСТРУКТОР ====================
MQTTManager::MQTTManager(Scale& s, StateMachine& sm, WiFiManager& wm) 
    : mqttClient(wifiClient), 
      scale(s), 
      stateMachine(sm), 
      wifiManager(wm),
      lastReconnectAttempt(0),
      lastPublishTime(0),
      lastHeartbeatTime(0),
      lastConnectionCheckTime(0),
      messagesSent(0),
      messagesFailed(0),
      reconnectAttempts(0),
      lastWaterState(-1),
      lastKettlePresent(false),
      lastMqttConnected(false),
      commandCallback(nullptr)
{
    instance = this;
    clientId = "smartpump";
    
    // Инициализируем топики
    waterLevelTopic = "/devices/pump/water_level";
    kettleTopic = "/devices/pump/kettle";
    fillingTopic = "/devices/pump/filling";
}

// ==================== ДЕСТРУКТОР ====================
MQTTManager::~MQTTManager() {
    // Отключаемся от MQTT
    if (mqttClient.connected()) {
        mqttClient.disconnect();
    }
    
    // Очищаем строки для освобождения памяти
    clearStrings();
    
    // Закрываем Preferences, если они были открыты
    preferences.end();
    
    // Обнуляем instance
    if (instance == this) {
        instance = nullptr;
    }
    
    Serial.println("MQTTManager destroyed, memory freed");
}

// ==================== ОЧИСТКА СТРОК ====================
void MQTTManager::clearStrings() {
    // Очищаем все строки (освобождаем память)
    clientId = String();
    waterLevelTopic = String();
    kettleTopic = String();
    fillingTopic = String();
    mqttUser = String();
    mqttPass = String();
}

// ==================== ИНИЦИАЛИЗАЦИЯ ====================
void MQTTManager::begin() {
    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setBufferSize(512);
    
    Serial.println("=== MQTT Конфигурация ===");
    Serial.printf("Client ID: %s\n", clientId.c_str());
    Serial.printf("Сервер: %s:%d\n", MQTT_SERVER, MQTT_PORT);
    Serial.printf("Топик уровня воды: %s\n", waterLevelTopic.c_str());
    Serial.printf("Топик наличия чайника: %s\n", kettleTopic.c_str());
    Serial.println("Уровни воды:");
    Serial.println("  0 = Пустой (< 500 мл)");
    Serial.println("  1 = Низкий (500-1000 мл)");
    Serial.println("  2 = Нормальный (> 1000 мл)");
    
    if (loadCredentials()) {
        Serial.println("✓ Учетные данные MQTT загружены из Preferences");
        Serial.printf("  Пользователь: %s\n", mqttUser.c_str());
        Serial.printf("  Пароль: %s\n", "********");
        
        connect();
    } else {
        Serial.println("⚠ Учетные данные MQTT не найдены в Preferences");
        Serial.println("  Используйте команду для установки:");
        Serial.println("  mqtt_set <логин> <пароль>");
    }
    
    Serial.println("==========================");
}

// ==================== ОСНОВНОЙ ЦИКЛ ====================
void MQTTManager::loop() {
    unsigned long now = millis();
    
    if (now - lastConnectionCheckTime > 1000) {
        lastConnectionCheckTime = now;
        bool currentlyConnected = mqttClient.connected();
        
        if (currentlyConnected != lastMqttConnected) {
            lastMqttConnected = currentlyConnected;
            if (currentlyConnected) {
                Serial.println("✓ MQTT подключен к Dealgate");
            } else {
                Serial.println("✗ MQTT отключен от Dealgate");
            }
        }
    }
    
    if (!mqttClient.connected()) {
        if (now - lastReconnectAttempt > MQTT_RECONNECT_DELAY) {
            lastReconnectAttempt = now;
            reconnectAttempts++;
            Serial.printf("Попытка переподключения MQTT #%lu...\n", reconnectAttempts);
            connect();
        }
    } else {
        mqttClient.loop();
        
        if (now - lastPublishTime > MQTT_PUBLISH_INTERVAL) {
            lastPublishTime = now;
            publishWaterState();
            publishKettleState();
        }
    }
}

// ==================== ПОДКЛЮЧЕНИЕ ====================
void MQTTManager::connect() {
    if (!wifiManager.hasMqttCredentials()) {
        Serial.println("⚠ Невозможно подключиться: нет учетных данных MQTT");
        return;
    }
    
    Serial.print("Подключение к Dealgate MQTT... ");
    
    bool connected = mqttClient.connect(
        clientId.c_str(),
        wifiManager.getMqttUser().c_str(),
        wifiManager.getMqttPass().c_str(),
        NULL, 0, false, NULL
    );
    
    if (connected) {
        Serial.println("OK");
        messagesFailed = 0;
        subscribe();
        publishWaterState();
        publishKettleState();
    } else {
        Serial.printf("ОШИБКА (код=%d)\n", mqttClient.state());
        messagesFailed++;
    }
}

// ==================== ПОДПИСКА ====================
void MQTTManager::subscribe() {
    if (mqttClient.subscribe(fillingTopic.c_str())) {
        Serial.printf("Подписка на топик: %s\n", fillingTopic.c_str());
    }
}

// ==================== CALLBACK ====================
void MQTTManager::mqttCallback(char* topic, byte* payload, unsigned int length) {
    if (!instance) return;
    
    // Создаем копию сообщения с нуль-терминатором
    char* message = new char[length + 1];
    if (!message) return;  // Проверка выделения памяти
    
    memcpy(message, payload, length);
    message[length] = '\0';
    
    Serial.printf("MQTT команда получена [%s]: %s\n", topic, message);
    
    String topicStr = String(topic);
    
    if (topicStr == instance->fillingTopic) {
        int mode = atoi(message);
        
        if (mode >= 1 && mode <= 8 && instance->commandCallback) {
            Serial.printf("Выполнение команды налива: режим %d\n", mode);
            instance->commandCallback(mode);
        }
    }
    
    // Освобождаем память
    delete[] message;
}

// ==================== ПУБЛИКАЦИЯ ====================
bool MQTTManager::publish(const String& topic, const String& payload, bool retained) {
    if (!mqttClient.connected()) {
        messagesFailed++;
        return false;
    }
    
    // Ограничиваем длину payload для предотвращения переполнения буфера
    String safePayload = payload;
    if (safePayload.length() > 250) {
        safePayload = safePayload.substring(0, 250);
        Serial.println("⚠ Payload слишком длинный, обрезан");
    }
    
    bool result = mqttClient.publish(topic.c_str(), safePayload.c_str(), retained);
    
    if (result) {
        messagesSent++;
        Serial.printf("MQTT публикация [%s]: %s\n", topic.c_str(), safePayload.c_str());
    } else {
        messagesFailed++;
        Serial.printf("MQTT публикация ОШИБКА [%s]\n", topic.c_str());
    }
    
    return result;
}

// ==================== РАСЧЕТ УРОВНЯ ВОДЫ ====================
int MQTTManager::calculateWaterState() {
    float waterVolume = scale.getCurrentWeight() - scale.getEmptyWeight();
    if (waterVolume < 0) waterVolume = 0;
    
    float volumeML = waterVolume;
    
    int state;
    if (volumeML < WATER_LEVEL_EMPTY) {
        state = 0;
        Serial.printf("Объем воды: %.0f мл -> состояние 0 (ПУСТО)\n", volumeML);
    } else if (volumeML <= WATER_LEVEL_LOW) {
        state = 1;
        Serial.printf("Объем воды: %.0f мл -> состояние 1 (НИЗКИЙ)\n", volumeML);
    } else {
        state = 2;
        Serial.printf("Объем воды: %.0f мл -> состояние 2 (НОРМАЛЬНЫЙ)\n", volumeML);
    }
    
    return state;
}

// ==================== ПУБЛИКАЦИЯ УРОВНЯ ВОДЫ ====================
bool MQTTManager::publishWaterState() {
    if (!scale.isKettlePresent()) {
        int currentState = 0;
        
        if (currentState != lastWaterState) {
            Serial.printf("Состояние воды (нет чайника): %d -> %d\n", lastWaterState, currentState);
            
            String payload = String(currentState);
            bool result = publish(waterLevelTopic, payload, false);
            
            if (result) {
                lastWaterState = currentState;
            }
            
            return result;
        }
        return true;
    }
    
    int currentState = calculateWaterState();
    
    if (currentState != lastWaterState) {
        Serial.printf("Состояние воды изменилось: %d -> %d\n", lastWaterState, currentState);
        
        String payload = String(currentState);
        bool result = publish(waterLevelTopic, payload, false);
        
        if (result) {
            lastWaterState = currentState;
        }
        
        return result;
    }
    
    return true;
}

// ==================== ПУБЛИКАЦИЯ НАЛИЧИЯ ЧАЙНИКА ====================
bool MQTTManager::publishKettleState() {
    bool kettlePresent = scale.isKettlePresent();
    int value = kettlePresent ? 1 : 0;
    
    if (value != lastKettlePresent) {
        Serial.printf("Наличие чайника изменилось: %d -> %d\n", lastKettlePresent, value);
        
        String payload = String(value);
        bool result = publish(kettleTopic, payload, false);
        
        if (result) {
            lastKettlePresent = value;
        }
        
        return result;
    }
    
    return true;
}

// ==================== РАБОТА С УЧЕТНЫМИ ДАННЫМИ ====================
bool MQTTManager::loadCredentials() {
    // Больше НЕ работаем с Preferences напрямую
    mqttUser = wifiManager.getMqttUser();
    mqttPass = wifiManager.getMqttPass();
    return (mqttUser.length() > 0 && mqttPass.length() > 0);
}

bool MQTTManager::saveCredentials(const String& user, const String& pass) {
    // Делегируем сохранение WiFiManager'у
    return wifiManager.saveMqttCredentials(user, pass);
}

void MQTTManager::clearCredentials() {
    wifiManager.clearMqttCredentials();
    mqttUser = "";
    mqttPass = "";
}

bool MQTTManager::hasCredentials() {
    return wifiManager.hasMqttCredentials();
}

// ==================== УПРАВЛЕНИЕ ПОДКЛЮЧЕНИЕМ ====================
void MQTTManager::disconnect() {
    if (mqttClient.connected()) {
        mqttClient.disconnect();
        Serial.println("MQTT отключен");
    }
}

void MQTTManager::reconnect() {
    disconnect();
    delay(100);
    connect();
}