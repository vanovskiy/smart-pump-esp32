// файл: WebDashboard.h
// Модуль веб-интерфейса с возможностью смены пароля

#ifndef WEB_DASHBOARD_H
#define WEB_DASHBOARD_H

#include <WebServer.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include "config.h"
#include "Scale.h"
#include "PumpController.h"
#include "Display.h"
#include "StateMachine.h"
#include "WiFiManager.h"
#include "MQTTManager.h"

// Данные для входа по умолчанию
#ifndef WEB_USERNAME
#define WEB_USERNAME "admin"      // Логин по умолчанию (нельзя изменить через веб)
#endif

#ifndef WEB_PASSWORD
#define WEB_PASSWORD "admin"      // Пароль по умолчанию (можно изменить через веб)
#endif

class WebDashboard {
private:
    WebServer& server;
    Scale& scale;
    PumpController& pump;
    Display& display;
    StateMachine* stateMachine;
    WiFiManager& wifiManager;
    MQTTManager* mqttManager;
    
    // Аутентификация
    bool authEnabled;
    String username;        // Фиксированный логин (admin)
    String defaultPassword;  // Пароль по умолчанию (admin)
    String currentPassword;  // Текущий пароль (из EEPROM или default)
    
    // Приватные методы
    bool checkAuth();
    void loadPasswordFromEEPROM();
    void savePasswordToEEPROM(const String& newPass);
    void resetPasswordToDefault();
    
    // Обработчики
    void handleLogin();
    void handleLogout();
    void handleRoot();
    void handleChangePassword();
    void handleAPIStatus();
    void handleAPIFill();
    void handleAPIStop();
    void handleAPICalibrate();
    void handleAPIReboot();
    void handleNotFound();
    
    String getContentType(const String& path);
    void sendJsonResponse(int code, const JsonDocument& doc);
    void sendPlainResponse(int code, const String& text);
    
public:
    // Конструктор
    WebDashboard(WebServer& srv, Scale& s, PumpController& p, Display& d, 
                 StateMachine* sm, WiFiManager& wm, MQTTManager* mqm,
                 bool enableAuth = true);
    
    // Публичные методы
    void begin();
    void handle();
    void resetPassword();  // Для вызова при полном сбросе
};

#endif