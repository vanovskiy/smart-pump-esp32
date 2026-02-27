// файл: smart_pump_esp32.ino
// Главный файл проекта умной помпы на ESP32
// Версия с поддержкой OTA обновлений и прогресс-баром на дисплее

#include "config.h"
#include "Button.h"
#include "Scale.h"
#include "PumpController.h"
#include "Display.h"
#include "StateMachine.h"
#include "WiFiManager.h"
#include "MQTTManager.h"
#include <EEPROM.h>
#include "esp_task_wdt.h"
#include <ArduinoOTA.h>

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

// Переменные для управления частотой loop
unsigned long lastLoopTime = 0;
const unsigned long LOOP_INTERVAL = LOOP_DELAY; // 100 мс из config.h

// ==================== ПРОТОТИПЫ ФУНКЦИЙ ====================
void onButtonHoldReleased(unsigned long holdDuration);
void onMultiClick(int clickCount);
String generateDeviceId();
void onWiFiEvent(WiFiState state);
void onMqttCommand(int mode);
void publishMqttUpdates();
void printHelp();

// ==================== ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ====================

String generateDeviceId() {
    uint64_t chipid = ESP.getEfuseMac();
    char deviceId[24] = {0};
    snprintf(deviceId, sizeof(deviceId), "smartpump-%08X", (uint32_t)chipid);
    deviceId[sizeof(deviceId) - 1] = '\0';
    return String(deviceId);
}

void onWiFiEvent(WiFiState state) {
    Serial.printf("WiFi state changed to: %d\n", state);
    display.setWiFiStatus(wifiManager.isConfigured(), wifiManager.isConnected());
}

void onMqttCommand(int mode) {
    if (stateMachine) {
        stateMachine->handleMqttCommand(mode);
    }
}

void onButtonHoldReleased(unsigned long holdDuration) {
    Serial.printf("*** BUTTON HOLD RELEASED after %lu ms ***\n", holdDuration);
    
    if (holdDuration < RESET_CALIB_TIME) {
        Serial.println("  → Hold 5-9s: Preview only, no reset");
        return;
    }
    
    if (holdDuration >= RESET_CALIB_TIME && holdDuration < RESET_FULL_TIME) {
        Serial.println("  → Hold 10-14.999s: Calibration reset only");
        pump.beepLongNonBlocking(2);
        
        if (stateMachine != nullptr) {
            display.showResetMessageNonBlocking(false, stateMachine);
        } else {
            display.showResetMessageNonBlocking(false, nullptr);
        }
        
        scale.resetCalibration();
        
        Serial.println("System will restart in 100ms...");
        delay(100);
        ESP.restart();
        return;
    }
    
    if (holdDuration >= RESET_FULL_TIME) {
        Serial.println("  → Hold >=15s: FULL FACTORY RESET");
        pump.beepLongNonBlocking(3);
        
        if (stateMachine != nullptr) {
            display.showResetMessageNonBlocking(true, stateMachine);
        } else {
            display.showResetMessageNonBlocking(true, nullptr);
        }
        
        scale.resetCalibration();
        
        if (mqttManager) {
            mqttManager->disconnect();
            delete mqttManager;
            mqttManager = nullptr;
        }
        
        wifiManager.resetSettings();
        return;
    }
}

void onMultiClick(int clickCount) {
    Serial.printf("*** MULTI-CLICK: %d clicks ***\n", clickCount);
    
    switch (clickCount) {
        case 1:
            Serial.println("  → Single click");
            break;
        case 2:
            Serial.println("  → Double click");
            break;
        case 3:
            Serial.println("  → Triple click - calibration");
            break;
        default:
            Serial.printf("  → %d clicks (ignored)\n", clickCount);
            break;
    }
}

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

void printHelp() {
    Serial.println("\n=== ДОСТУПНЫЕ КОМАНДЫ ===");
    Serial.println("  calibrate / калибровка  - Калибровка коэффициента датчика");
    Serial.println("  factor / коэффициент     - Показать текущий коэффициент");
    Serial.println("  test вес / проверка      - Проверить показания весов");
    Serial.println("  status / статус          - Состояние системы");
    Serial.println("  raw                       - Сырые данные АЦП");
    Serial.println("  empty                     - Показать вес пустого чайника");
    Serial.println("  tare                      - Обнулить весы (ОСТОРОЖНО!)");
    Serial.println("  pump on/off               - Вкл/выкл помпу принудительно");
    Serial.println("  servo kettle/idle         - Переместить серво");
    Serial.println("  stats                     - Статистика и память");
    Serial.println("  reset factor              - Сбросить коэффициент");
    Serial.println("  reset wifi                - Сбросить WiFi настройки");
    Serial.println("  reboot / перезагрузка     - Перезагрузить устройство");
    Serial.println("  config                    - Запустить WiFi точку доступа");
    Serial.println("  test one ... test full    - Тест MQTT команд 1-7");
    Serial.println("  test stop                 - Тест MQTT команды 8 (стоп)");
    Serial.println("  help / помощь / ?         - Показать эту справку");
    Serial.println("================================\n");
}

// ==================== ИНИЦИАЛИЗАЦИЯ ====================
void setup() {
    Serial.begin(115200);
    delay(100);
    
    Serial.println("\n\n============================================");
    Serial.println("   УМНАЯ ПОМПА - ПЕРВОНАЧАЛЬНАЯ НАСТРОЙКА");
    Serial.println("============================================");
    Serial.println();
    
    EEPROM.begin(EEPROM_SIZE);

    // Инициализация дисплея
    display.begin();
    display.setWiFiStatus(false, false);
    display.update(ST_INIT, ERR_NONE, false, 0, 0, 0, false, 0);

    // Инициализация WiFi
    wifiManager.begin();
    wifiManager.setEventCallback(onWiFiEvent);

    // ===== ИНИЦИАЛИЗАЦИЯ ВЕСОВ =====
    Serial.println("\n--- Инициализация весов ---");
    bool scaleInitSuccess = scale.begin();
    
    if (scaleInitSuccess) {
        scale.loadCalibrationFromEEPROM(EEPROM_CALIB_ADDR);
        if (scale.isFactorCalibrated()) {
            Serial.printf("✓ Загружен калибровочный коэффициент: %f\n", 
                         scale.getCalibrationFactor());
        } else {
            Serial.println("⚠ Калибровочный коэффициент не найден!");
            Serial.println("   Введите 'calibrate' для калибровки датчика.");
        }
    } else {
        Serial.println("✗ Ошибка инициализации HX711!");
        display.update(ST_ERROR, ERR_HX711_TIMEOUT, false, 0, 0, 0, false, 0);
    }

    // ===== ИНИЦИАЛИЗАЦИЯ КНОПКИ С CALLBACK'АМИ =====
    button.setHoldCallback(onButtonHoldReleased);
    button.setMultiClickCallback(onMultiClick);

    // ===== ИНИЦИАЛИЗАЦИЯ ПОМПЫ =====
    pump.begin();
    pump.beepShortNonBlocking(1);

    // ===== СОЗДАНИЕ STATE MACHINE =====
    if (stateMachine != nullptr) {
        delete stateMachine;
        stateMachine = nullptr;
    }
    stateMachine = new StateMachine(scale, pump, display);

    // ===== УСТАНОВКА НАЧАЛЬНОГО СОСТОЯНИЯ =====
    if (!scaleInitSuccess) {
        stateMachine->toError(ERR_HX711_TIMEOUT);
    } 
    else if (!scale.isFactorCalibrated()) {
        Serial.println("\n⚠ РЕЖИМ ОЖИДАНИЯ КАЛИБРОВКИ");
        Serial.println("Для работы устройства необходима калибровка коэффициента датчика.");
        Serial.println("Введите 'calibrate' в мониторе порта и следуйте инструкциям.\n");
        printHelp();
    }
    else if (!scale.isCalibrationDone()) {
        Serial.println("→ Требуется калибровка пустого чайника");
        Serial.println("   Поставьте пустой чайник и нажмите кнопку 3 раза.\n");
        display.setCalibrationMode(true);
        display.update(ST_CALIBRATION, ERR_NONE, false, scale.getCurrentWeight(), 
                      0, 0, false, 0);
        stateMachine->toCalibration();
    } 
    else {
        Serial.println("✓ Система готова к работе\n");
        stateMachine->toIdle();
    }

    // ===== ИНИЦИАЛИЗАЦИЯ MQTT =====
    String deviceId = generateDeviceId();
    mqttManager = new MQTTManager(scale, *stateMachine, wifiManager);
    mqttManager->begin();
    mqttManager->setCommandCallback(onMqttCommand);

    // ===== НАСТРОЙКА OTA ОБНОВЛЕНИЙ =====
    ArduinoOTA.setHostname("smartpump");
    ArduinoOTA.setPassword("smartpump123");

    ArduinoOTA.onStart([]() {
        Serial.println("\n=== ОБНОВЛЕНИЕ ПО ВОЗДУХУ (OTA) ===");
        Serial.println("⚠️ ВНИМАНИЕ: Начинается обновление прошивки");
        Serial.println("⚠️ НЕ ВЫКЛЮЧАЙТЕ ПИТАНИЕ!");
        Serial.println("⏳ Процесс займёт около 30 секунд...\n");
        
        // Отключаем критически важные процессы
        pump.pumpOff();
        if (stateMachine) stateMachine->toIdle();
        
        // Показываем экран OTA
        display.showOTAScreen();
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        static int lastPercent = -1;
        int percent = (progress * 100) / total;
        if (percent != lastPercent) {
            lastPercent = percent;
        
            // Рисуем прогресс-бар в Serial
            int barWidth = 30;
            int filled = (percent * barWidth) / 100;
        
            Serial.print("\r[");
            for (int i = 0; i < barWidth; i++) {
                if (i < filled) Serial.print("█");
                else Serial.print("░");
            }
            Serial.printf("] %d%%", percent);
            
            // Обновляем дисплей с процентами (каждые 10%)
            if (percent % 10 == 0 || percent == 100) {
                display.showOTAScreen(percent);
            }
        }
    });

    ArduinoOTA.onEnd([]() {
        Serial.println("\n\n✓ ОБНОВЛЕНИЕ ЗАВЕРШЕНО");
        Serial.println("Устройство будет перезагружено...");
        
        display.showOTACompleteScreen();
        delay(2000); // Даём время увидеть сообщение
    });

    ArduinoOTA.onError([](ota_error_t error) {
        Serial.print("\n❌ ОШИБКА OTA: ");
        if (error == OTA_AUTH_ERROR) Serial.println("Ошибка авторизации - проверьте пароль");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Ошибка подключения");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Ошибка приёма данных");
        else if (error == OTA_END_ERROR) Serial.println("Ошибка при завершении");
        else Serial.println("Неизвестная ошибка");
        
        // Возвращаем нормальный экран
        delay(3000);
        if (stateMachine) {
            stateMachine->toIdle();
        }
    });

    ArduinoOTA.begin();
    Serial.println("✓ OTA готов - подключайтесь к 'smartpump'");

    // ===== WATCHDOG =====
    esp_task_wdt_deinit();
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 10000,
        .idle_core_mask = (1 << 0),
        .trigger_panic = true
    };
    esp_task_wdt_init(&wdt_config);
    esp_task_wdt_add(NULL);
    
    Serial.println("\n✓ Watchdog initialized (10s timeout)");
    Serial.println("============================================\n");
    
    // Выводим справку при первом запуске
    printHelp();
}

// ==================== ГЛАВНЫЙ ЦИКЛ ====================
void loop() {
    // Сброс watchdog и проверка OTA в начале каждой итерации
    esp_task_wdt_reset();
    ArduinoOTA.handle();
    
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
    
    // ===== 5. ОБРАБОТКА ОБРАТНОГО ОТСЧЕТА НА ДИСПЛЕЕ =====
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
            pump.beepShortNonBlocking(2);
            wifiManager.startConfigPortal();
            configModeActivated = true;
        }
    }
    
    if (!button.isPressed()) {
        configModeActivated = false;
    }
    
    // ===== 7. ОБНОВЛЕНИЕ КОНТРОЛЛЕРОВ =====
    pump.update();
    
    // ===== 8. ОБНОВЛЕНИЕ КОНЕЧНОГО АВТОМАТА =====
    if (stateMachine != nullptr) {
        stateMachine->update();
        stateMachine->handleButton(button);
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
        float target = 0;
        float start = 0;
    
        if (stateMachine != nullptr) {
            currentState = stateMachine->getCurrentStateEnum();
            currentError = stateMachine->getCurrentError();
        
            if (currentState == ST_FILLING) {
                target = stateMachine->getFillTarget();
                start = stateMachine->getFillStart();
            }
        }
    
        display.update(currentState, currentError, scale.isKettlePresent(),
                      scale.getCurrentWeight(), target, start,
                      pump.isPowerRelayOn(), scale.getEmptyWeight());
    }
    
    // ===== 12. ОБРАБОТКА КОМАНД ИЗ СЕРИЙНОГО ПОРТА =====
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        String lowerCommand = command;
        lowerCommand.toLowerCase();
        
        // === ОСНОВНЫЕ КОМАНДЫ ===
        if (lowerCommand == "calibrate" || lowerCommand == "calib" || lowerCommand == "калибровка") {
            Serial.println("\n=== ЗАПУСК КАЛИБРОВКИ ДАТЧИКА ===");
            if (scale.calibrateFactorViaSerial()) {
                Serial.println("✓ Коэффициент датчика успешно откалиброван");
            } else {
                Serial.println("✗ Ошибка калибровки");
            }
        }
        else if (lowerCommand == "factor" || lowerCommand == "коэффициент") {
            Serial.printf("Текущий калибровочный коэффициент: %f\n", 
                         scale.getCalibrationFactor());
            Serial.printf("Коэффициент откалиброван: %s\n", 
                         scale.isFactorCalibrated() ? "ДА" : "НЕТ");
        }
        else if (lowerCommand == "test вес" || lowerCommand == "test weight" || lowerCommand == "проверка") {
            float raw = scale.getRawWeight();
            float filtered = scale.getCurrentWeight();
            long adc = scale.getRawADC();
            Serial.println("\n=== ПРОВЕРКА ВЕСОВ ===");
            Serial.printf("Сырое значение АЦП: %ld\n", adc);
            Serial.printf("Сырой вес (без фильтра): %.2f г\n", raw);
            Serial.printf("Отфильтрованный вес: %.2f г\n", filtered);
            Serial.printf("Коэффициент: %f\n", scale.getCalibrationFactor());
            Serial.printf("Разница фильтра: %.2f г\n", fabs(raw - filtered));
            
            if (scale.isKettlePresent()) {
                float water = scale.getCurrentWeight() - scale.getEmptyWeight();
                Serial.printf("Вес воды: %.1f г\n", water);
                Serial.printf("Кружек: %d\n", Display::mlToCups(water));
            } else {
                Serial.println("Чайник не обнаружен");
            }
        }
        else if (lowerCommand == "status" || lowerCommand == "статус") {
            Serial.println("\n=== СОСТОЯНИЕ СИСТЕМЫ ===");
            
            Serial.printf("WiFi настроен: %s\n", wifiManager.isConfigured() ? "ДА" : "НЕТ");
            Serial.printf("WiFi подключен: %s\n", wifiManager.isConnected() ? "ДА" : "НЕТ");
            if (wifiManager.isConnected()) {
                Serial.printf("Сигнал: %d dBm\n", wifiManager.getRSSI());
                Serial.printf("IP адрес: %s\n", wifiManager.getLocalIP().toString().c_str());
            }
            
            Serial.printf("MQTT подключен: %s\n", mqttManager ? 
                         (mqttManager->isConnected() ? "ДА" : "НЕТ") : "Н/Д");
            if (mqttManager) {
                Serial.printf("Отправлено: %lu, Ошибок: %lu, Попыток: %lu\n", 
                             mqttManager->getMessagesSent(),
                             mqttManager->getMessagesFailed(),
                             mqttManager->getReconnectAttempts());
            }
            
            Serial.println("\n--- Калибровка датчика ---");
            Serial.printf("Коэффициент откалиброван: %s\n", 
                         scale.isFactorCalibrated() ? "ДА" : "НЕТ");
            Serial.printf("Коэффициент: %f\n", scale.getCalibrationFactor());
            Serial.printf("Вес пустого чайника: %.1f г\n", scale.getEmptyWeight());
            Serial.printf("Чайник откалиброван: %s\n", 
                         scale.isCalibrationDone() ? "ДА" : "НЕТ");
            
            Serial.println("\n--- Текущие показания ---");
            Serial.printf("Чайник на месте: %s\n", 
                         scale.isKettlePresent() ? "ДА" : "НЕТ");
            Serial.printf("Текущий вес: %.1f г\n", scale.getCurrentWeight());
            
            float waterVolume = scale.getCurrentWeight() - scale.getEmptyWeight();
            if (waterVolume < 0) waterVolume = 0;
            Serial.printf("Объём воды: %.0f мл\n", waterVolume);
            Serial.printf("Кружек: %d\n", (int)(waterVolume / CUP_VOLUME));
            
            Serial.println("\n--- Состояние автомата ---");
            if (stateMachine) {
                switch (stateMachine->getCurrentStateEnum()) {
                    case ST_IDLE: Serial.println("Режим: ОЖИДАНИЕ"); break;
                    case ST_FILLING: Serial.println("Режим: НАЛИВ"); break;
                    case ST_CALIBRATION: Serial.println("Режим: КАЛИБРОВКА"); break;
                    case ST_ERROR: Serial.println("Режим: ОШИБКА"); break;
                    default: Serial.println("Режим: НЕИЗВЕСТНЫЙ"); break;
                }
            }
            
            Serial.println("\n--- Техническая информация ---");
            Serial.printf("Свободная память: %d байт\n", ESP.getFreeHeap());
            Serial.printf("Макс. свободный блок: %d байт\n", ESP.getMaxAllocHeap());
            Serial.printf("Размер скетча: %d байт\n", ESP.getSketchSize());
            Serial.printf("Свободно места в скетче: %d байт\n", ESP.getFreeSketchSpace());
            Serial.printf("Частота CPU: %d МГц\n", ESP.getCpuFreqMHz());
            Serial.printf("Температура чипа: %.2f °C\n", temperatureRead());
            Serial.printf("Время работы: %lu мс (%lu ч %02d м)\n", 
                         millis(), millis() / 3600000, (millis() / 60000) % 60);
        }
        else if (lowerCommand == "help" || lowerCommand == "помощь" || lowerCommand == "?") {
            printHelp();
        }
        
        // === РАСШИРЕННЫЕ ОТЛАДОЧНЫЕ КОМАНДЫ ===
        else if (lowerCommand == "raw") {
            long adc = scale.getRawADC();
            float rawWeight = scale.getRawWeight();
            float filtered = scale.getCurrentWeight();
            Serial.println("\n=== СЫРЫЕ ДАННЫЕ ===");
            Serial.printf("ADC: %ld\n", adc);
            Serial.printf("Сырой вес: %.2f г\n", rawWeight);
            Serial.printf("Отфильтрованный: %.2f г\n", filtered);
            Serial.printf("Разница: %.2f г\n", fabs(rawWeight - filtered));
        }
        else if (lowerCommand == "empty") {
            Serial.printf("Вес пустого чайника: %.1f г\n", scale.getEmptyWeight());
            Serial.printf("Чайник откалиброван: %s\n", 
                         scale.isCalibrationDone() ? "ДА" : "НЕТ");
        }
        else if (lowerCommand == "tare") {
            Serial.println("\n⚠ ВНИМАНИЕ: Обнуление весов!");
            Serial.println("Вы уверены? Это сделает текущий вес = 0. (Д/Н)");
            
            while (!Serial.available()) {
                delay(100);
            }
            
            char confirm = Serial.read();
            if (confirm == 'Д' || confirm == 'д' || confirm == 'Y' || confirm == 'y') {
                scale.tare();
                Serial.println("✓ Весы обнулены");
            } else {
                Serial.println("Отменено");
            }
        }
        else if (lowerCommand == "pump on") {
            if (stateMachine && stateMachine->getCurrentStateEnum() == ST_IDLE) {
                pump.pumpOn();
                Serial.println("✓ Помпа включена принудительно");
            } else {
                Serial.println("✗ Можно включить только в режиме IDLE");
            }
        }
        else if (lowerCommand == "pump off") {
            pump.pumpOff();
            Serial.println("✓ Помпа выключена");
        }
        else if (lowerCommand == "servo kettle") {
            pump.moveServoToKettle();
            Serial.println("Серво движется к чайнику");
        }
        else if (lowerCommand == "servo idle") {
            pump.moveServoToIdle();
            Serial.println("Серво движется в безопасное положение");
        }
        else if (lowerCommand == "stats") {
            Serial.println("\n=== СТАТИСТИКА ПАМЯТИ ===");
            Serial.printf("Свободная память: %d байт\n", ESP.getFreeHeap());
            Serial.printf("Мин. свободная память: %d байт\n", ESP.getMinFreeHeap());
            Serial.printf("Макс. свободный блок: %d байт\n", ESP.getMaxAllocHeap());
            Serial.printf("Размер кучи: %d байт\n", ESP.getHeapSize());
            #ifdef BOARD_HAS_PSRAM
            Serial.printf("PSRAM размер: %d байт\n", ESP.getPsramSize());
            Serial.printf("Свободно PSRAM: %d байт\n", ESP.getFreePsram());
            #endif
        }
        else if (lowerCommand == "reset factor" || lowerCommand == "reset калибровка" || 
                 lowerCommand == "сброс фактор") {
            Serial.println("\n=== СБРОС КАЛИБРОВОЧНОГО КОЭФФИЦИЕНТА ===");
            Serial.println("Вы уверены? Это удалит откалиброванный коэффициент. (Д/Н)");
            
            while (!Serial.available()) {
                delay(100);
            }
            
            char confirm = Serial.read();
            if (confirm == 'Д' || confirm == 'д' || confirm == 'Y' || confirm == 'y') {
                scale.resetFactor();
                scale.saveCalibrationToEEPROM(EEPROM_CALIB_ADDR);
                Serial.println("✓ Коэффициент сброшен к значению по умолчанию");
            } else {
                Serial.println("Сброс отменён");
            }
        }
        else if (lowerCommand == "reset wifi") {
            Serial.println("\n=== СБРОС WiFi НАСТРОЕК ===");
            Serial.println("Вы уверены? Устройство перезагрузится. (Д/Н)");
            
            while (!Serial.available()) {
                delay(100);
            }
            
            char confirm = Serial.read();
            if (confirm == 'Д' || confirm == 'д' || confirm == 'Y' || confirm == 'y') {
                wifiManager.resetSettings();
            } else {
                Serial.println("Сброс отменён");
            }
        }
        else if (lowerCommand == "test one") {
            if (stateMachine) stateMachine->handleMqttCommand(1);
        }
        else if (lowerCommand == "test two") {
            if (stateMachine) stateMachine->handleMqttCommand(2);
        }
        else if (lowerCommand == "test three") {
            if (stateMachine) stateMachine->handleMqttCommand(3);
        }
        else if (lowerCommand == "test four") {
            if (stateMachine) stateMachine->handleMqttCommand(4);
        }
        else if (lowerCommand == "test five") {
            if (stateMachine) stateMachine->handleMqttCommand(5);
        }
        else if (lowerCommand == "test six") {
            if (stateMachine) stateMachine->handleMqttCommand(6);
        }
        else if (lowerCommand == "test full") {
            if (stateMachine) stateMachine->handleMqttCommand(7);
        }
        else if (lowerCommand == "test stop") {
            if (stateMachine) stateMachine->handleMqttCommand(8);
        }
        else if (lowerCommand == "config") {
            wifiManager.startConfigPortal();
        }
        else if (lowerCommand == "reboot" || lowerCommand == "перезагрузка") {
            Serial.println("Перезагрузка...");
            delay(100);
            ESP.restart();
        }
        else if (lowerCommand.length() > 0) {
            Serial.println("❓ Неизвестная команда. Введите 'help' для списка команд.");
        }
    }
}