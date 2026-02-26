// файл: smart_pump_esp32.ino
// Главный файл проекта умной помпы на ESP32
// Версия с неблокирующими задержками (без delay в loop)

#include "config.h"
#include "Button.h"
#include "Scale.h"
#include "PumpController.h"
#include "Display.h"
#include "StateMachine.h"
#include "WiFiManager.h"
#include "MQTTManager.h"
#include <EEPROM.h>

// ==================== ГЛОБАЛЬНЫЕ ОБЪЕКТЫ ====================
Button button(PIN_BUTTON);
Scale scale;
PumpController pump;
Display display;
StateMachine* stateMachine = nullptr;
WiFiManager wifiManager;
MQTTManager* mqttManager = nullptr;

// ==================== ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ====================
unsigned long pressStartTime = 0;
bool countdownActive = false;
int lastDisplayedSeconds = -1;
bool wifiResetPhase = false;

// Переменные для отслеживания состояния налива
float currentFillTarget = 0;
float currentFillStart = 0;

// ==================== УПРАВЛЕНИЕ ЧАСТОТОЙ LOOP ====================
unsigned long lastLoopTime = 0;
const unsigned long LOOP_INTERVAL = LOOP_DELAY; // 100 мс из config.h

// ==================== ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ====================

/**
 * Генерирует уникальный идентификатор устройства на основе MAC-адреса ESP32
 */
String generateDeviceId() {
    uint64_t chipid = ESP.getEfuseMac();
    char deviceId[24];
    snprintf(deviceId, sizeof(deviceId), "smartpump-%08X", (uint32_t)chipid);
    return String(deviceId);
}

/**
 * Callback для событий WiFi
 */
void onWiFiEvent(WiFiState state) {
    Serial.printf("WiFi state changed to: %d\n", state);
    display.setWiFiStatus(wifiManager.isConfigured(), wifiManager.isConnected());
}

/**
 * Callback для MQTT команд
 */
void onMqttCommand(int mode) {
    if (stateMachine) {
        stateMachine->handleMqttCommand(mode);
    }
}

/**
 * Публикация MQTT обновлений
 */
void publishMqttUpdates() {
    if (!mqttManager || !mqttManager->isConnected() || !wifiManager.isConnected()) {
        return;
    }
    
    if (!mqttManager->publishWaterState()) {
        Serial.println("WARNING: Failed to publish water state to Dealgate");
    }
    
    if (!mqttManager->publishKettleState()) {
        Serial.println("WARNING: Failed to publish kettle presence to Dealgate");
    }
}

// ==================== ИНИЦИАЛИЗАЦИЯ ====================
void setup() {
    Serial.begin(115200);
    delay(100);  // В setup допустимо
    Serial.println("Smart Pump Starting...");

    EEPROM.begin(EEPROM_SIZE);

    // Инициализация дисплея
    display.begin();
    display.setWiFiStatus(false, false);
    display.update(ST_INIT, ERR_NONE, false, 0, 0, 0, false, 0);

    // Инициализация WiFi
    wifiManager.begin();
    wifiManager.setEventCallback(onWiFiEvent);

    // Инициализация весов
    if (!scale.begin()) {
        Serial.println("HX711 not found");
        display.update(ST_ERROR, ERR_HX711_TIMEOUT, false, 0, 0, 0, false, 0);
        stateMachine = new StateMachine(scale, pump, display);
        stateMachine->toError(ERR_HX711_TIMEOUT);
    } else {
        scale.loadCalibrationFromEEPROM(EEPROM_CALIB_ADDR);
        stateMachine = new StateMachine(scale, pump, display);
        
        if (!scale.isReady()) {
            display.setCalibrationMode(true);
            display.update(ST_CALIBRATION, ERR_NONE, false, scale.getCurrentWeight(), 0, 0, false, 0);
            stateMachine->toCalibration();
        } else {
            stateMachine->toIdle();
        }
    }

    pump.begin();
    pump.beepShortNonBlocking(1);  // Используем неблокирующий зуммер

    // Генерируем уникальный deviceId
    String deviceId = generateDeviceId();
    Serial.println("=== Device Information ===");
    Serial.println("Device ID: " + deviceId);
    Serial.println("MAC Address: " + wifiManager.getMacAddress());
    Serial.println("Chip ID: " + wifiManager.getChipId());
    Serial.println("===========================");

    // Инициализация MQTT
    mqttManager = new MQTTManager(scale, *stateMachine, wifiManager);
    mqttManager->begin();
    mqttManager->setCommandCallback(onMqttCommand);

    Serial.println("Setup complete.");
}

// ==================== ГЛАВНЫЙ ЦИКЛ (НЕБЛОКИРУЮЩИЙ) ====================
void loop() {
    unsigned long now = millis();
    
    // ===== УПРАВЛЕНИЕ ЧАСТОТОЙ ВЫПОЛНЕНИЯ =====
        if ((long)(now - lastLoopTime) < (long)LOOP_INTERVAL) {
           delay(1);
            return;
        }
    lastLoopTime = now;
    
    // ===== 1. ОБНОВЛЕНИЕ WiFi =====
    wifiManager.loop();
    
    // ===== 2. ОБНОВЛЕНИЕ MQTT (только если WiFi подключен) =====
    if (wifiManager.isConnected()) {
        if (mqttManager) {
            mqttManager->loop();
        }
    }
    
    // ===== 3. ОБНОВЛЕНИЕ СОСТОЯНИЯ КНОПКИ =====
    button.tick();
    
    // ===== 4. ОБНОВЛЕНИЕ СТАТУСА ДЛЯ ДИСПЛЕЯ =====
    static unsigned long lastWiFiStatusUpdate = 0;
    if (now - lastWiFiStatusUpdate > 1000) {
        lastWiFiStatusUpdate = now;
        display.setWiFiStatus(wifiManager.isConfigured(), wifiManager.isConnected());
    }
    
    // ===== 5. ОБРАБОТКА СБРОСА С ОБРАТНЫМ ОТСЧЕТОМ =====
    if (button.isPressed()) {
        if (pressStartTime == 0) {
            pressStartTime = now;
            countdownActive = true;
            lastDisplayedSeconds = -1;
            wifiResetPhase = false;
        }
        
        unsigned long pressDuration = now - pressStartTime;
        
        if (pressDuration >= 5000) {
            if (pressDuration < 10000) {
                int secondsLeft = 10 - (pressDuration / 1000);
                if (secondsLeft < 0) secondsLeft = 0;
                if (secondsLeft != lastDisplayedSeconds || wifiResetPhase) {
                    lastDisplayedSeconds = secondsLeft;
                    wifiResetPhase = false;
                    display.showResetCountdown(secondsLeft, false);
                }
            } else {
                int secondsLeft = 15 - (pressDuration / 1000);
                if (secondsLeft < 0) secondsLeft = 0;
                if (secondsLeft != lastDisplayedSeconds || !wifiResetPhase) {
                    lastDisplayedSeconds = secondsLeft;
                    wifiResetPhase = true;
                    display.showResetCountdown(secondsLeft, true);
                }
            }
        }
    } else {
        if (countdownActive && pressStartTime > 0) {
            unsigned long pressDuration = now - pressStartTime;
            
            if (pressDuration >= RESET_FULL_TIME) {
                Serial.println("VERY LONG PRESS (>15s): Factory reset");
                pump.beepLongNonBlocking(3);  // Неблокирующий зуммер
                
                // Используем неблокирующее отображение сообщения
                display.showResetMessageNonBlocking(true, stateMachine);
                
                // Сброс калибровки
                scale.resetCalibration();
                
                // Отключаем MQTT
                if (mqttManager) {
                    mqttManager->disconnect();
                    delete mqttManager;
                    mqttManager = nullptr;
                }
                
                // Сброс WiFi
                wifiManager.resetSettings();
                
                // Неблокирующее ожидание перед перезагрузкой
                // Просто выходим, остальное сделает система
                Serial.println("System will restart...");
                delay(100);  // Короткая задержка для отправки логов
                ESP.restart();
                
            } else if (pressDuration >= RESET_CALIB_TIME) {
                Serial.println("LONG PRESS (10-15s): Calibration reset only");
                pump.beepLongNonBlocking(2);  // Неблокирующий зуммер
                
                // Используем неблокирующее отображение сообщения
                display.showResetMessageNonBlocking(false, stateMachine);
                
                scale.resetCalibration();
                
                Serial.println("System will restart...");
                delay(100);  // Короткая задержка для отправки логов
                ESP.restart();
            }
        }
        
        pressStartTime = 0;
        countdownActive = false;
        lastDisplayedSeconds = -1;
        wifiResetPhase = false;
    }
    
    // ===== 6. ОБРАБОТКА LONG_PRESS ДЛЯ КОНФИГУРАЦИИ WIFI =====
    static bool configModeActivated = false;
    if (button.isLongPress() && !configModeActivated) {
        if (!wifiManager.isConfigured()) {
            Serial.println("Long press: Start WiFi config portal");
            display.update(ST_IDLE, ERR_NONE, scale.isKettlePresent(), 
                          scale.getCurrentWeight(), 0, 0, 
                          pump.isPowerRelayOn(), scale.getEmptyWeight());
            pump.beepShortNonBlocking(2);  // Неблокирующий зуммер
            wifiManager.startConfigPortal();
            configModeActivated = true;
        }
    }
    
    if (!button.isPressed()) {
        configModeActivated = false;
    }
    
    // ===== 7. ОБНОВЛЕНИЕ КОНТРОЛЛЕРОВ =====
    pump.update();  // Обновляет серво и зуммер
    
    // ===== 8. ОБНОВЛЕНИЕ КОНЕЧНОГО АВТОМАТА =====
    if (stateMachine != nullptr) {
        stateMachine->update();
        stateMachine->handleButton(button);
        
        if (stateMachine->getCurrentStateEnum() == ST_FILLING) {
            currentFillTarget = stateMachine->getFillTarget();
            currentFillStart = stateMachine->getFillStart();
        }
        
        // Обновляем ожидания дисплея
        stateMachine->updateDisplayWaiting();
    }
    
    // ===== 9. ОТПРАВКА СТАТУСА В MQTT =====
    publishMqttUpdates();
    
    // ===== 10. ПРОВЕРКА ЗДОРОВЬЯ MQTT =====
    static unsigned long lastMqttHealthCheck = 0;
    if (mqttManager && now - lastMqttHealthCheck > 60000) {
        lastMqttHealthCheck = now;
        
        if (!mqttManager->isConnected() && wifiManager.isConnected()) {
            Serial.println("MQTT health check: not connected, attempting reconnect");
            mqttManager->reconnect();
        } else if (mqttManager->isConnected()) {
            Serial.printf("MQTT stats - Sent: %lu, Failed: %lu, Reconnects: %lu\n",
                         mqttManager->getMessagesSent(),
                         mqttManager->getMessagesFailed(),
                         mqttManager->getReconnectAttempts());
        }
    }
    
    // ===== 11. ОБНОВЛЕНИЕ ДИСПЛЕЯ =====
    static unsigned long lastDisplayUpdate = 0;
    if (now - lastDisplayUpdate > 200) {
        lastDisplayUpdate = now;
        
        SystemState currentState = ST_IDLE;
        ErrorType currentError = ERR_NONE;
        
        if (stateMachine != nullptr) {
            currentState = stateMachine->getCurrentStateEnum();
            currentError = stateMachine->getCurrentError();
        }
        
        display.update(currentState, currentError, scale.isKettlePresent(),
                      scale.getCurrentWeight(), currentFillTarget, currentFillStart,
                      pump.isPowerRelayOn(), scale.getEmptyWeight());
    }
    
    // ===== 12. КОМАНДЫ ИЗ СЕРИЙНОГО ПОРТА =====
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        
        if (command == "config") {
            wifiManager.startConfigPortal();
        } else if (command == "status") {
            Serial.println("=== Device Status ===");
            Serial.printf("WiFi Configured: %s\n", wifiManager.isConfigured() ? "YES" : "NO");
            Serial.printf("WiFi Connected: %s\n", wifiManager.isConnected() ? "YES" : "NO");
            Serial.printf("WiFi State: %d\n", wifiManager.getState());
            Serial.printf("MQTT Connected: %s\n", mqttManager ? (mqttManager->isConnected() ? "YES" : "NO") : "N/A");
            Serial.printf("Signal Strength: %d dBm\n", wifiManager.getRSSI());
            Serial.printf("Kettle Present: %s\n", scale.isKettlePresent() ? "YES" : "NO");
            
            float waterVolume = scale.getCurrentWeight() - scale.getEmptyWeight();
            if (waterVolume < 0) waterVolume = 0;
            Serial.printf("Water: %.0f ml (%d cups)\n", waterVolume, (int)(waterVolume / CUP_VOLUME));
            
        } else if (command == "reset") {
            wifiManager.resetSettings();
        } else if (command == "test one") {
            if (stateMachine) stateMachine->handleMqttCommand(0);
        } else if (command == "test two") {
            if (stateMachine) stateMachine->handleMqttCommand(1);
        } else if (command == "test stop") {
            if (stateMachine) stateMachine->handleMqttCommand(2);
        } else if (command == "reboot") {
            Serial.println("Rebooting...");
            delay(100);
            ESP.restart();
        }
    }
    
    // ВАЖНО: Мы НЕ используем delay(LOOP_DELAY) в конце loop
    // Вместо этого мы управляем частотой в начале функции
}