// файл: smart_pump_esp32.ino
// Главный файл проекта умной помпы на ESP32
// Отвечает за инициализацию всех модулей и главный цикл программы

// Подключение всех необходимых заголовочных файлов
#include "config.h"           // Основные настройки и конфигурация (пины, константы)
#include "Button.h"           // Класс для работы с кнопкой (обработка нажатий, дребезга)
#include "Scale.h"            // Класс для работы с тензодатчиком HX711 (измерение веса)
#include "PumpController.h"    // Управление помпой, реле, сервоприводом и зуммером
#include "Display.h"          // Управление OLED-дисплеем (отрисовка экранов)
#include "StateMachine.h"     // Конечный автомат (состояния IDLE, FILLING, ERROR, CALIBRATION)
#include "WiFiManager.h"      // Управление WiFi подключением и веб-порталом настройки
#include "MQTTManager.h"      // Управление MQTT подключением к Dealgate
#include <EEPROM.h>           // Библиотека для работы с энергонезависимой памятью

// ==================== ГЛОБАЛЬНЫЕ ОБЪЕКТЫ ====================
// Создаем экземпляры всех классов для использования во всей программе
Button button(PIN_BUTTON);                    // Объект кнопки на пине 27
Scale scale;                                   // Объект весов (HX711)
PumpController pump;                           // Объект управления помпой
Display display;                               // Объект дисплея
StateMachine* stateMachine = nullptr;           // Указатель на конечный автомат (создается в setup)
WiFiManager wifiManager;                        // Объект управления WiFi
MQTTManager* mqttManager = nullptr;             // Указатель на MQTT менеджер (создается в setup)

// ==================== ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ====================
// Переменные для отслеживания времени нажатия кнопки и обратного отсчета
unsigned long pressStartTime = 0;    // Время начала нажатия кнопки (в миллисекундах)
bool countdownActive = false;         // Флаг: активен ли режим обратного отсчета
int lastDisplayedSeconds = -1;        // Последнее отображенное значение секунд (для избежания лишних обновлений)
bool wifiResetPhase = false;           // Флаг: находимся ли в фазе сброса WiFi (true) или калибровки (false)

// Переменные для отслеживания состояния налива (используются для обновления дисплея)
float currentFillTarget = 0;    // Целевой вес при наливе
float currentFillStart = 0;      // Начальный вес при наливе

// ==================== ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ====================

/**
 * Генерирует уникальный идентификатор устройства на основе MAC-адреса ESP32
 * Используется как client ID для MQTT подключения
 * Формат: smartpump-XXXXXXXX (где X - последние 8 символов MAC)
 */
String generateDeviceId() {
    uint64_t chipid = ESP.getEfuseMac();           // Получаем MAC-адрес из памяти чипа
    char deviceId[24];
    snprintf(deviceId, sizeof(deviceId), "smartpump-%08X", (uint32_t)chipid);
    return String(deviceId);
}

/**
 * Callback-функция для событий WiFi
 * Вызывается WiFiManager при изменении состояния подключения
 * Обновляет статус WiFi на дисплее
 */
void onWiFiEvent(WiFiState state) {
    Serial.printf("WiFi state changed to: %d\n", state);
    display.setWiFiStatus(wifiManager.isConfigured(), wifiManager.isConnected());
}

/**
 * Callback-функция для MQTT команд
 * Вызывается MQTTManager при получении команды из облака
 * Передает команду в конечный автомат для обработки
 */
void onMqttCommand(int mode) {
    if (stateMachine) {
        stateMachine->handleMqttCommand(mode);
    }
}

/**
 * Публикует обновления состояния в MQTT
 * Вызывается каждый цикл loop(), но фактическая отправка происходит
 * только при изменении состояний (логика внутри publishWaterState/publishKettleState)
 */
void publishMqttUpdates() {
    // Проверяем, что MQTT менеджер существует, подключен и WiFi есть
    if (!mqttManager || !mqttManager->isConnected() || !wifiManager.isConnected()) {
        return;
    }
    
    // Публикуем уровень воды (публикует только при изменении)
    if (!mqttManager->publishWaterState()) {
        Serial.println("WARNING: Failed to publish water state to Dealgate");
    }
    
    // Публикуем наличие чайника (публикует только при изменении)
    if (!mqttManager->publishKettleState()) {
        Serial.println("WARNING: Failed to publish kettle presence to Dealgate");
    }
}

// ==================== ИНИЦИАЛИЗАЦИЯ ====================
/**
 * Функция setup выполняется один раз при старте микроконтроллера
 * Инициализирует все компоненты системы в правильном порядке
 */
void setup() {
    // Инициализация последовательного порта для отладки
    Serial.begin(115200);
    delay(100);
    Serial.println("Smart Pump Starting...");

    // Инициализация EEPROM (энергонезависимая память для хранения калибровки)
    EEPROM.begin(EEPROM_SIZE);

    // ===== Инициализация дисплея =====
    display.begin();
    display.setWiFiStatus(false, false);           // Начальный статус: WiFi не настроен, не подключен
    display.update(ST_INIT, ERR_NONE, false, 0, 0, 0, false, 0);  // Показываем экран загрузки

    // ===== Инициализация WiFi =====
    wifiManager.begin();                            // Запускаем WiFi менеджер
    wifiManager.setEventCallback(onWiFiEvent);      // Устанавливаем callback для событий

    // ===== Инициализация весов =====
    if (!scale.begin()) {                           // Если датчик HX711 не отвечает
        Serial.println("HX711 not found");
        display.update(ST_ERROR, ERR_HX711_TIMEOUT, false, 0, 0, 0, false, 0);
        stateMachine = new StateMachine(scale, pump, display);
        stateMachine->toError(ERR_HX711_TIMEOUT);    // Переходим в состояние ошибки
    } else {
        // Загружаем калибровку из EEPROM
        scale.loadCalibrationFromEEPROM(EEPROM_CALIB_ADDR);
        
        // Создаем конечный автомат
        stateMachine = new StateMachine(scale, pump, display);
        
        // Проверяем, откалиброваны ли весы
        if (!scale.isReady()) {
            // Если нет калибровки - переходим в режим калибровки
            display.setCalibrationMode(true);
            display.update(ST_CALIBRATION, ERR_NONE, false, scale.getCurrentWeight(), 0, 0, false, 0);
            stateMachine->toCalibration();
        } else {
            // Если калибровка есть - переходим в режим ожидания
            stateMachine->toIdle();
        }
    }

    // ===== Инициализация помпы, серво и зуммера =====
    pump.begin();
    pump.beepShort(1);  // Один короткий сигнал об успешном запуске

    // ===== Вывод информации об устройстве =====
    String deviceId = generateDeviceId();
    Serial.println("=== Device Information ===");
    Serial.println("Device ID: " + deviceId);
    Serial.println("MAC Address: " + wifiManager.getMacAddress());
    Serial.println("Chip ID: " + wifiManager.getChipId());
    Serial.println("===========================");

    // ===== Инициализация MQTT =====
    mqttManager = new MQTTManager(scale, *stateMachine, wifiManager);
    mqttManager->begin();                           // Загружает credentials и пытается подключиться
    mqttManager->setCommandCallback(onMqttCommand); // Устанавливаем callback для MQTT команд

    Serial.println("Setup complete.");
}

// ==================== ГЛАВНЫЙ ЦИКЛ ====================
/**
 * Функция loop выполняется бесконечно после setup
 * Содержит всю логику работы системы в реальном времени
 */
void loop() {
    // ===== 1. ОБНОВЛЕНИЕ WiFi =====
    wifiManager.loop();  // Поддерживает подключение, обрабатывает DNS и HTTP запросы
    
    // ===== 2. ОБНОВЛЕНИЕ MQTT (только если WiFi подключен) =====
    if (wifiManager.isConnected()) {
        if (mqttManager) {
            mqttManager->loop();  // Поддерживает MQTT соединение, обрабатывает входящие сообщения
        }
    }
    
    // ===== 3. ОБНОВЛЕНИЕ СОСТОЯНИЯ КНОПКИ =====
    button.tick();  // Должен вызываться каждый цикл для обработки дребезга и мультикликов
    
    // ===== ОБНОВЛЕНИЕ СТАТУСА ДЛЯ ДИСПЛЕЯ =====
    // Раз в секунду обновляем индикацию WiFi на дисплее
    static unsigned long lastWiFiStatusUpdate = 0;
    if (millis() - lastWiFiStatusUpdate > 1000) {
        lastWiFiStatusUpdate = millis();
        display.setWiFiStatus(wifiManager.isConfigured(), wifiManager.isConnected());
    }
    
    // ===== ОБРАБОТКА СБРОСА С ОБРАТНЫМ ОТСЧЕТОМ =====
    // Логика для VERY_LONG_PRESS (удержание кнопки более 5 секунд)
    // Показывает обратный отсчет на дисплее: 5-10 сек для сброса калибровки, 10-15 сек для полного сброса
    if (button.isPressed()) {
        // Начало нажатия
        if (pressStartTime == 0) {
            pressStartTime = millis();
            countdownActive = true;
            lastDisplayedSeconds = -1;
            wifiResetPhase = false;
        }
        
        unsigned long pressDuration = millis() - pressStartTime;
        
        // Если нажатие длится более 5 секунд - начинаем обратный отсчет
        if (pressDuration >= 5000) {
            if (pressDuration < 10000) {
                // Фаза 5-10 секунд: подготовка к сбросу калибровки
                int secondsLeft = 10 - (pressDuration / 1000);
                if (secondsLeft < 0) secondsLeft = 0;
                if (secondsLeft != lastDisplayedSeconds || wifiResetPhase) {
                    lastDisplayedSeconds = secondsLeft;
                    wifiResetPhase = false;  // Это фаза калибровки
                    display.showResetCountdown(secondsLeft, false);
                }
            } else {
                // Фаза 10-15 секунд: подготовка к полному сбросу (WiFi + калибровка)
                int secondsLeft = 15 - (pressDuration / 1000);
                if (secondsLeft < 0) secondsLeft = 0;
                if (secondsLeft != lastDisplayedSeconds || !wifiResetPhase) {
                    lastDisplayedSeconds = secondsLeft;
                    wifiResetPhase = true;   // Это фаза полного сброса
                    display.showResetCountdown(secondsLeft, true);
                }
            }
        }
    } else {
        // Кнопка отпущена - проверяем, было ли длительное нажатие
        if (countdownActive && pressStartTime > 0) {
            unsigned long pressDuration = millis() - pressStartTime;
            
            // Если удержание более 15 секунд - ПОЛНЫЙ СБРОС (WiFi + калибровка)
            if (pressDuration >= RESET_FULL_TIME) {
                Serial.println("VERY LONG PRESS (>15s): Factory reset");
                pump.beepLong(3);                  // 3 длинных сигнала
                display.showResetMessage(true);     // Показываем сообщение о полном сбросе
                
                scale.resetCalibration();           // Сбрасываем калибровку весов
                
                // Отключаем и удаляем MQTT менеджер
                if (mqttManager) {
                    mqttManager->disconnect();
                    delete mqttManager;
                    mqttManager = nullptr;
                }
                
                wifiManager.resetSettings();        // Сбрасываем настройки WiFi и MQTT
                
                delay(3000);
                ESP.restart();                      // Перезагружаем ESP32
                
            // Если удержание 10-15 секунд - СБРОС КАЛИБРОВКИ
            } else if (pressDuration >= RESET_CALIB_TIME) {
                Serial.println("LONG PRESS (10-15s): Calibration reset only");
                pump.beepLong(2);                   // 2 длинных сигнала
                display.showResetMessage(false);     // Показываем сообщение о сбросе калибровки
                
                scale.resetCalibration();            // Только сброс калибровки, WiFi не трогаем
                
                delay(2000);
                ESP.restart();                       // Перезагружаем ESP32
            }
        }
        
        // Сбрасываем переменные состояния кнопки
        pressStartTime = 0;
        countdownActive = false;
        lastDisplayedSeconds = -1;
        wifiResetPhase = false;
    }
    
    // ===== ОБРАБОТКА LONG_PRESS ДЛЯ КОНФИГУРАЦИИ WIFI =====
    // Если WiFi не настроен и пользователь держит кнопку 3 секунды - запускаем портал настройки
    static bool configModeActivated = false;
    if (button.isLongPress() && !configModeActivated) {
        if (!wifiManager.isConfigured()) {
            Serial.println("Long press: Start WiFi config portal");
            // Обновляем дисплей перед запуском портала
            display.update(ST_IDLE, ERR_NONE, scale.isKettlePresent(), 
                          scale.getCurrentWeight(), 0, 0, 
                          pump.isPowerRelayOn(), scale.getEmptyWeight());
            pump.beepShort(2);                       // 2 коротких сигнала
            wifiManager.startConfigPortal();          // Запускаем точку доступа для настройки
            configModeActivated = true;
        }
    }
    
    // Сбрасываем флаг, когда кнопка отпущена
    if (!button.isPressed()) {
        configModeActivated = false;
    }
    
    // ===== ОБНОВЛЕНИЕ КОНТРОЛЛЕРОВ =====
    pump.update();  // Обновляет состояние сервопривода (проверяет, закончилось ли движение)
    
    // ===== ОБНОВЛЕНИЕ КОНЕЧНОГО АВТОМАТА =====
    if (stateMachine != nullptr) {
        stateMachine->update();                 // Обновляем текущее состояние
        stateMachine->handleButton(button);     // Передаем события кнопки в конечный автомат
        
        // Если мы в состоянии налива - сохраняем целевые значения для дисплея
        if (stateMachine->getCurrentStateEnum() == ST_FILLING) {
            currentFillTarget = stateMachine->getFillTarget();
            currentFillStart = stateMachine->getFillStart();
        }
    }
    
    // ===== ОТПРАВКА СТАТУСА В MQTT =====
    publishMqttUpdates();
    
    // ===== ПРОВЕРКА ЗДОРОВЬЯ MQTT =====
    // Раз в минуту проверяем состояние MQTT соединения и логируем статистику
    static unsigned long lastMqttHealthCheck = 0;
    if (mqttManager && millis() - lastMqttHealthCheck > 60000) {
        lastMqttHealthCheck = millis();
        
        if (!mqttManager->isConnected() && wifiManager.isConnected()) {
            Serial.println("MQTT health check: not connected, attempting reconnect");
            mqttManager->reconnect();           // Пытаемся переподключиться
        } else if (mqttManager->isConnected()) {
            // Логируем статистику работы MQTT
            Serial.printf("MQTT stats - Sent: %lu, Failed: %lu, Reconnects: %lu\n",
                         mqttManager->getMessagesSent(),
                         mqttManager->getMessagesFailed(),
                         mqttManager->getReconnectAttempts());
        }
    }
    
    // ===== ОБНОВЛЕНИЕ ДИСПЛЕЯ =====
    // Обновляем дисплей каждые 200 мс (не слишком часто, чтобы не тратить ресурсы)
    static unsigned long lastDisplayUpdate = 0;
    if (millis() - lastDisplayUpdate > 200) {
        lastDisplayUpdate = millis();
        
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
    
    // ===== КОМАНДЫ ИЗ СЕРИЙНОГО ПОРТА =====
    // Обработка отладочных команд, вводимых пользователем в Serial Monitor
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        
        if (command == "config") {
            wifiManager.startConfigPortal();      // Запустить портал настройки
        } else if (command == "status") {
            // Вывод подробной информации о состоянии системы
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
            wifiManager.resetSettings();           // Сброс настроек WiFi
        } else if (command == "test one") {
            if (stateMachine) stateMachine->handleMqttCommand(0);  // Тестовая команда
        } else if (command == "test two") {
            if (stateMachine) stateMachine->handleMqttCommand(1);  // Тестовая команда
        } else if (command == "test stop") {
            if (stateMachine) stateMachine->handleMqttCommand(2);  // Тестовая команда
        }
    }

    // Небольшая задержка для снижения нагрузки на процессор
    delay(LOOP_DELAY);
}