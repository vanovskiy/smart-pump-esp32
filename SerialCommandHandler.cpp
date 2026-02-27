// файл: SerialCommandHandler.cpp
// Реализация обработчика команд из серийного порта

#include "SerialCommandHandler.h"
#include "debug.h"

SerialCommandHandler::SerialCommandHandler(Scale& s, PumpController& p, Display& d, 
                                           StateMachine* sm, WiFiManager& wm, MQTTManager* mqm)
    : scale(s), pump(p), display(d), stateMachine(sm), wifiManager(wm), mqttManager(mqm) {
    DPRINTLN("📟 SerialCommandHandler: инициализирован");
}

void SerialCommandHandler::printSeparator() {
    Serial.println("========================================");
}

void SerialCommandHandler::printWelcome() {
    printSeparator();
    Serial.println("   УМНАЯ ПОМПА - РЕЖИМ КОМАНД");
    printSeparator();
    Serial.println("Введите 'help' для списка команд");
    printSeparator();
}

void SerialCommandHandler::printHelp() {
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

bool SerialCommandHandler::confirmAction(const String& prompt) {
    Serial.println(prompt);
    Serial.println("Вы уверены? (д/н)");
    Serial.print("> ");
    
    while (!Serial.available()) {
        delay(100);
    }
    
    char confirm = Serial.read();
    // Очищаем буфер
    while (Serial.available()) {
        Serial.read();
    }
    
    return (confirm == 'д' || confirm == 'Д' || confirm == 'y' || confirm == 'Y');
}

void SerialCommandHandler::handleCalibrate() {
    Serial.println("\n=== ЗАПУСК КАЛИБРОВКИ ДАТЧИКА ===");
    if (scale.calibrateFactorViaSerial()) {
        LOG_OK("Коэффициент датчика успешно откалиброван");
    } else {
        LOG_ERROR("Ошибка калибровки");
    }
}

void SerialCommandHandler::handleFactor() {
    Serial.printf("Текущий калибровочный коэффициент: %f\n", scale.getCalibrationFactor());
    Serial.printf("Коэффициент откалиброван: %s\n", scale.isFactorCalibrated() ? "ДА" : "НЕТ");
}

void SerialCommandHandler::handleTestWeight() {
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

void SerialCommandHandler::handleStatus() {
    Serial.println("\n=== СОСТОЯНИЕ СИСТЕМЫ ===");
    
    // WiFi статус
    Serial.printf("WiFi настроен: %s\n", wifiManager.isConfigured() ? "ДА" : "НЕТ");
    Serial.printf("WiFi подключен: %s\n", wifiManager.isConnected() ? "ДА" : "НЕТ");
    if (wifiManager.isConnected()) {
        Serial.printf("Сигнал: %d dBm\n", wifiManager.getRSSI());
        Serial.printf("IP адрес: %s\n", wifiManager.getLocalIP().toString().c_str());
    }
    
    // MQTT статус
    Serial.printf("MQTT подключен: %s\n", mqttManager ? 
                 (mqttManager->isConnected() ? "ДА" : "НЕТ") : "Н/Д");
    if (mqttManager) {
        Serial.printf("Отправлено: %lu, Ошибок: %lu, Попыток: %lu\n", 
                     mqttManager->getMessagesSent(),
                     mqttManager->getMessagesFailed(),
                     mqttManager->getReconnectAttempts());
    }
    
    // Калибровка
    Serial.println("\n--- Калибровка датчика ---");
    Serial.printf("Коэффициент откалиброван: %s\n", 
                 scale.isFactorCalibrated() ? "ДА" : "НЕТ");
    Serial.printf("Коэффициент: %f\n", scale.getCalibrationFactor());
    Serial.printf("Вес пустого чайника: %.1f г\n", scale.getEmptyWeight());
    Serial.printf("Чайник откалиброван: %s\n", 
                 scale.isCalibrationDone() ? "ДА" : "НЕТ");
    
    // Текущие показания
    Serial.println("\n--- Текущие показания ---");
    Serial.printf("Чайник на месте: %s\n", 
                 scale.isKettlePresent() ? "ДА" : "НЕТ");
    Serial.printf("Текущий вес: %.1f г\n", scale.getCurrentWeight());
    
    float waterVolume = scale.getCurrentWeight() - scale.getEmptyWeight();
    if (waterVolume < 0) waterVolume = 0;
    Serial.printf("Объём воды: %.0f мл\n", waterVolume);
    Serial.printf("Кружек: %d\n", (int)(waterVolume / CUP_VOLUME));
    
    // Состояние автомата
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
    
    // Техническая информация
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

void SerialCommandHandler::handleRaw() {
    long adc = scale.getRawADC();
    float rawWeight = scale.getRawWeight();
    float filtered = scale.getCurrentWeight();
    
    Serial.println("\n=== СЫРЫЕ ДАННЫЕ ===");
    Serial.printf("ADC: %ld\n", adc);
    Serial.printf("Сырой вес: %.2f г\n", rawWeight);
    Serial.printf("Отфильтрованный: %.2f г\n", filtered);
    Serial.printf("Разница: %.2f г\n", fabs(rawWeight - filtered));
}

void SerialCommandHandler::handleEmpty() {
    Serial.printf("Вес пустого чайника: %.1f г\n", scale.getEmptyWeight());
    Serial.printf("Чайник откалиброван: %s\n", 
                 scale.isCalibrationDone() ? "ДА" : "НЕТ");
}

void SerialCommandHandler::handleTare() {
    if (confirmAction("\n⚠️ ВНИМАНИЕ: Обнуление весов!")) {
        scale.tare();
        LOG_OK("Весы обнулены");
    } else {
        Serial.println("Отменено");
    }
}

void SerialCommandHandler::handlePumpOn() {
    if (stateMachine && stateMachine->getCurrentStateEnum() == ST_IDLE) {
        pump.pumpOn();
        LOG_OK("Помпа включена принудительно");
    } else {
        LOG_WARN("Можно включить только в режиме IDLE");
    }
}

void SerialCommandHandler::handlePumpOff() {
    pump.pumpOff();
    LOG_OK("Помпа выключена");
}

void SerialCommandHandler::handleServoKettle() {
    pump.moveServoToKettle();
    LOG_INFO("Серво движется к чайнику");
}

void SerialCommandHandler::handleServoIdle() {
    pump.moveServoToIdle();
    LOG_INFO("Серво движется в безопасное положение");
}

void SerialCommandHandler::handleStats() {
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

void SerialCommandHandler::handleResetFactor() {
    if (confirmAction("\n=== СБРОС КАЛИБРОВОЧНОГО КОЭФФИЦИЕНТА ===")) {
        scale.resetFactor();
        scale.saveCalibrationToEEPROM(EEPROM_CALIB_ADDR);
        LOG_OK("Коэффициент сброшен к значению по умолчанию");
    } else {
        Serial.println("Сброс отменён");
    }
}

void SerialCommandHandler::handleResetWifi() {
    if (confirmAction("\n=== СБРОС WiFi НАСТРОЕК ===")) {
        wifiManager.resetSettings();
    } else {
        Serial.println("Сброс отменён");
    }
}

void SerialCommandHandler::handleTestMqtt(int mode) {
    if (stateMachine) {
        stateMachine->handleMqttCommand(mode);
        Serial.printf("Тестовая MQTT команда %d отправлена\n", mode);
    }
}

void SerialCommandHandler::handleConfig() {
    wifiManager.startConfigPortal();
}

void SerialCommandHandler::handleReboot() {
    if (confirmAction("\n=== ПЕРЕЗАГРУЗКА ===")) {
        Serial.println("Перезагрузка...");
        delay(100);
        ESP.restart();
    } else {
        Serial.println("Перезагрузка отменена");
    }
}

void SerialCommandHandler::handle() {
    if (!Serial.available()) return;
    
    String command = Serial.readStringUntil('\n');
    command.trim();
    String lowerCommand = command;
    lowerCommand.toLowerCase();
    
    // Основные команды
    if (lowerCommand == "calibrate" || lowerCommand == "calib" || lowerCommand == "калибровка") {
        handleCalibrate();
    }
    else if (lowerCommand == "factor" || lowerCommand == "коэффициент") {
        handleFactor();
    }
    else if (lowerCommand == "test вес" || lowerCommand == "test weight" || lowerCommand == "проверка") {
        handleTestWeight();
    }
    else if (lowerCommand == "status" || lowerCommand == "статус") {
        handleStatus();
    }
    else if (lowerCommand == "help" || lowerCommand == "помощь" || lowerCommand == "?") {
        printHelp();
    }
    // Расширенные команды
    else if (lowerCommand == "raw") {
        handleRaw();
    }
    else if (lowerCommand == "empty") {
        handleEmpty();
    }
    else if (lowerCommand == "tare") {
        handleTare();
    }
    else if (lowerCommand == "pump on") {
        handlePumpOn();
    }
    else if (lowerCommand == "pump off") {
        handlePumpOff();
    }
    else if (lowerCommand == "servo kettle") {
        handleServoKettle();
    }
    else if (lowerCommand == "servo idle") {
        handleServoIdle();
    }
    else if (lowerCommand == "stats") {
        handleStats();
    }
    else if (lowerCommand == "reset factor" || lowerCommand == "reset калибровка" || 
             lowerCommand == "сброс фактор") {
        handleResetFactor();
    }
    else if (lowerCommand == "reset wifi") {
        handleResetWifi();
    }
    else if (lowerCommand == "config") {
        handleConfig();
    }
    else if (lowerCommand == "reboot" || lowerCommand == "перезагрузка") {
        handleReboot();
    }
    // Тестовые MQTT команды
    else if (lowerCommand == "test one") {
        handleTestMqtt(1);
    }
    else if (lowerCommand == "test two") {
        handleTestMqtt(2);
    }
    else if (lowerCommand == "test three") {
        handleTestMqtt(3);
    }
    else if (lowerCommand == "test four") {
        handleTestMqtt(4);
    }
    else if (lowerCommand == "test five") {
        handleTestMqtt(5);
    }
    else if (lowerCommand == "test six") {
        handleTestMqtt(6);
    }
    else if (lowerCommand == "test full") {
        handleTestMqtt(7);
    }
    else if (lowerCommand == "test stop") {
        handleTestMqtt(8);
    }
    else if (lowerCommand.length() > 0) {
        Serial.println("❓ Неизвестная команда. Введите 'help' для списка команд.");
    }
}