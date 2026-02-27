// файл: SerialCommandHandler.h
// Обработчик команд из серийного порта
// Вынесен из основного файла для улучшения архитектуры

#ifndef SERIAL_COMMAND_HANDLER_H
#define SERIAL_COMMAND_HANDLER_H

#include <Arduino.h>
#include "config.h"
#include "Scale.h"
#include "PumpController.h"
#include "Display.h"
#include "StateMachine.h"
#include "WiFiManager.h"
#include "MQTTManager.h"

class SerialCommandHandler {
private:
    // Ссылки на все компоненты системы
    Scale& scale;
    PumpController& pump;
    Display& display;
    StateMachine* stateMachine;
    WiFiManager& wifiManager;
    MQTTManager* mqttManager;
    
    // Приватные методы обработки команд
    void handleCalibrate();
    void handleFactor();
    void handleTestWeight();
    void handleStatus();
    void handleRaw();
    void handleEmpty();
    void handleTare();
    void handlePumpOn();
    void handlePumpOff();
    void handleServoKettle();
    void handleServoIdle();
    void handleStats();
    void handleResetFactor();
    void handleResetWifi();
    void handleTestMqtt(int mode);
    void handleConfig();
    void handleReboot();
    void handleHelp();
    
    // Вспомогательные методы
    bool confirmAction(const String& prompt);
    void printWelcome();
    void printSeparator();

public:
    // Конструктор
    SerialCommandHandler(Scale& s, PumpController& p, Display& d, 
                         StateMachine* sm, WiFiManager& wm, MQTTManager* mqm);
    
    // Основной метод обработки команд
    void handle();
    
    // Метод для вывода справки (публичный)
    void printHelp();
};

#endif