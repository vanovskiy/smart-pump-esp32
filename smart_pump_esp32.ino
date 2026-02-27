// файл: smart_pump_esp32.ino
// Главный файл проекта умной помпы на ESP32

#include "config.h"
#include "Button.h"
#include "Scale.h"
#include "PumpController.h"
#include "Display.h"
#include "StateMachine.h"
#include "WiFiManager.h"
#include "MQTTManager.h"
#include "SerialCommandHandler.h"
#include "esp_task_wdt.h"
#include "WebDashboard.h"
#include <EEPROM.h>
#include <ArduinoOTA.h>

// ==================== ГЛОБАЛЬНЫЕ ОБЪЕКТЫ ====================
Button button(PIN_BUTTON);
Scale scale;
PumpController pump;
Display display;
StateMachine* stateMachine = nullptr;
WiFiManager wifiManager;
MQTTManager* mqttManager = nullptr;
SerialCommandHandler* cmdHandler = nullptr;
WebServer webServer(80);
WebDashboard* webDashboard = nullptr;

// ==================== ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ====================
unsigned long pressStartTime = 0;
bool countdownActive = false;
int lastDisplayedSeconds = -1;
bool wifiResetPhase = false;

unsigned long lastLoopTime = 0;
const unsigned long LOOP_INTERVAL = LOOP_DELAY;

// ==================== ПРОТОТИПЫ ФУНКЦИЙ ====================
void onButtonHoldReleased(unsigned long holdDuration);
void onMultiClick(int clickCount);
String generateDeviceId();
void onWiFiEvent(WiFiState state);
void onMqttCommand(int mode);
void publishMqttUpdates();

// ==================== ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ====================
String generateDeviceId() {
    uint64_t chipid = ESP.getEfuseMac();
    char deviceId[24] = {0};
    snprintf(deviceId, sizeof(deviceId), "smartpump-%08X", (uint32_t)chipid);
    return String(deviceId);
}

void onWiFiEvent(WiFiState state) {
    display.setWiFiStatus(wifiManager.isConfigured(), wifiManager.isConnected());
}

void onMqttCommand(int mode) {
    if (stateMachine) stateMachine->handleMqttCommand(mode);
}

void onButtonHoldReleased(unsigned long holdDuration) {
    if (holdDuration >= RESET_FULL_TIME) {
        // ПОЛНЫЙ СБРОС - сбрасывает пароль!
        if (webDashboard) webDashboard->resetPassword();
        wifiManager.resetSettings();
    }
    else if (holdDuration >= RESET_CALIB_TIME) {
        // СБРОС КАЛИБРОВКИ
        scale.resetCalibration();
        ESP.restart();
    }
}

void onMultiClick(int clickCount) {
}

void publishMqttUpdates() {
    if (!mqttManager || !mqttManager->isConnected() || !wifiManager.isConnected()) return;
    mqttManager->publishWaterState();
    mqttManager->publishKettleState();
}

// ==================== ИНИЦИАЛИЗАЦИЯ ====================
void setup() {
    Serial.begin(115200);
    delay(100);
    
    Serial.println("\n\n============================================");
    Serial.println("   УМНАЯ ПОМПА - ПЕРВОНАЧАЛЬНАЯ НАСТРОЙКА");
    Serial.println("============================================\n");
    
    EEPROM.begin(EEPROM_SIZE);

    display.begin();
    display.setWiFiStatus(false, false);
    display.update(ST_INIT, ERR_NONE, false, 0, 0, 0, false, 0);

    // Инициализация Wi-Fi
    wifiManager.begin();
    wifiManager.setEventCallback(onWiFiEvent);

    // Инициализация весов
    Serial.println("\n--- Инициализация весов ---");
    bool scaleInitSuccess = scale.begin();
    
    if (scaleInitSuccess) {
        scale.loadCalibrationFromEEPROM(EEPROM_CALIB_ADDR);
    }

    // Инициализация кнопки
    button.setHoldCallback(onButtonHoldReleased);
    button.setMultiClickCallback(onMultiClick);

    pump.begin();
    pump.beepShortNonBlocking(1);

    // Создание StateMachine
    stateMachine = new StateMachine(scale, pump, display);

    // Установка начального состояния
    if (!scaleInitSuccess) {
        stateMachine->toError(ERR_HX711_TIMEOUT);
    } else if (!scale.isFactorCalibrated()) {
        Serial.println("\n⚠️ РЕЖИМ ОЖИДАНИЯ КАЛИБРОВКИ");
    } else if (!scale.isCalibrationDone()) {
        display.setCalibrationMode(true);
        display.update(ST_CALIBRATION, ERR_NONE, false, scale.getCurrentWeight(), 
                      0, 0, false, 0);
        stateMachine->toCalibration();
    } else {
        stateMachine->toIdle();
    }

    // Инициализация MQTT
    String deviceId = generateDeviceId();
    mqttManager = new MQTTManager(scale, *stateMachine, wifiManager);
    mqttManager->begin();
    mqttManager->setCommandCallback(onMqttCommand);

    // ===== ИНИЦИАЛИЗАЦИЯ WEB DASHBOARD =====

    // С аутентификацией
    webDashboard = new WebDashboard(webServer, scale, pump, display, 
                                    stateMachine, wifiManager, mqttManager,
                                    true);
    // ИЛИ без аутентификации (для отладки)
    // webDashboard = new WebDashboard(webServer, scale, pump, display, 
    //                                 stateMachine, wifiManager, mqttManager,
    //                                 false);
    webDashboard->begin();

    // ===== ИНИЦИАЛИЗАЦИЯ ОБРАБОТЧИКА КОМАНД =====
    cmdHandler = new SerialCommandHandler(scale, pump, display, stateMachine, 
                                          wifiManager, mqttManager);

    // ===== НАСТРОЙКА OTA =====
    ArduinoOTA.setHostname("smartpump");
    ArduinoOTA.setPassword("smartpump123");
    
    ArduinoOTA.onStart([]() {
        Serial.println("\n=== ОБНОВЛЕНИЕ ПО ВОЗДУХУ (OTA) ===");
        pump.pumpOff();
        if (stateMachine) stateMachine->toIdle();
        display.showOTAScreen();
    });
    
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        static int lastPercent = -1;
        int percent = (progress * 100) / total;
        if (percent != lastPercent) {
            lastPercent = percent;
            Serial.printf("\rПрогресс: %d%%", percent);
            if (percent % 10 == 0) display.showOTAScreen(percent);
        }
    });
    
    ArduinoOTA.onEnd([]() {
        Serial.println("\n\n✓ ОБНОВЛЕНИЕ ЗАВЕРШЕНО");
        display.showOTACompleteScreen();
        delay(2000);
    });
    
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.print("\n❌ ОШИБКА OTA: ");
        if (error == OTA_AUTH_ERROR) Serial.println("Ошибка авторизации");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Ошибка подключения");
        else Serial.println("Неизвестная ошибка");
    });
    
    ArduinoOTA.begin();
    Serial.println("✓ OTA готов");

    // ===== WATCHDOG =====
    esp_task_wdt_deinit();
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 10000,
        .idle_core_mask = (1 << 0),
        .trigger_panic = true
    };
    esp_task_wdt_init(&wdt_config);
    esp_task_wdt_add(NULL);
    
    Serial.println("\n✓ Watchdog инициализирован");
    Serial.println("============================================\n");
    
    // Выводим справку
    if (cmdHandler) cmdHandler->printHelp();
}

// ==================== ГЛАВНЫЙ ЦИКЛ ====================
void loop() {
    esp_task_wdt_reset();
    ArduinoOTA.handle();
    if (webDashboard) {
        webDashboard->handle();  // Обработка веб-запросов
    }
    unsigned long now = millis();
        if ((long)(now - lastLoopTime) < (long)LOOP_INTERVAL) {
            delay(1);
            return;
        }
    lastLoopTime = now;
   
    // Обновление компонентов
    wifiManager.loop();
    
    if (wifiManager.isConnected() && mqttManager) {
        mqttManager->loop();
    }
    
    button.tick();
    
    // Обновление статуса WiFi на дисплее
    static unsigned long lastWiFiStatusUpdate = 0;
    if (now - lastWiFiStatusUpdate > 1000) {
        lastWiFiStatusUpdate = now;
        display.setWiFiStatus(wifiManager.isConfigured(), wifiManager.isConnected());
    }
    
    // Обработка удержания кнопки для сброса
    // ... (код обработки удержания) ...
    
    pump.update();
    
    if (stateMachine) {
        stateMachine->update();
        stateMachine->handleButton(button);
        stateMachine->updateDisplayWaiting();
    }
    
    publishMqttUpdates();
    
    // Обработка команд из Serial
    if (cmdHandler) {
        cmdHandler->handle();  // ← Теперь это одна строка вместо 500!
    }
    
    // Обновление дисплея
    static unsigned long lastDisplayUpdate = 0;
    if (now - lastDisplayUpdate > 200) {
        lastDisplayUpdate = now;
        
        SystemState currentState = stateMachine ? stateMachine->getCurrentStateEnum() : ST_IDLE;
        ErrorType currentError = stateMachine ? stateMachine->getCurrentError() : ERR_NONE;
        float target = (currentState == ST_FILLING && stateMachine) ? stateMachine->getFillTarget() : 0;
        float start = (currentState == ST_FILLING && stateMachine) ? stateMachine->getFillStart() : 0;
        
        display.update(currentState, currentError, scale.isKettlePresent(),
                      scale.getCurrentWeight(), target, start,
                      pump.isPowerRelayOn(), scale.getEmptyWeight());
    }
}