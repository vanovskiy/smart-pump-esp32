// файл: WebDashboard.cpp
// Реализация веб-интерфейса со сменой пароля

#include "WebDashboard.h"
#include "debug.h"

WebDashboard::WebDashboard(WebServer& srv, Scale& s, PumpController& p, Display& d, 
                           StateMachine* sm, WiFiManager& wm, MQTTManager* mqm,
                           bool enableAuth)
    : server(srv), scale(s), pump(p), display(d), 
      stateMachine(sm), wifiManager(wm), mqttManager(mqm),
      authEnabled(enableAuth), 
      username(WEB_USERNAME), 
      defaultPassword(WEB_PASSWORD) {
    
    // Загружаем пароль из EEPROM или используем default
    loadPasswordFromEEPROM();
    
    DPRINTLN("📊 WebDashboard: создан");
    if (authEnabled) {
        DPRINTLN("🔐 Аутентификация включена");
        DPRINT("🔐 Логин: "); DPRINTLN(username);
        DPRINT("🔐 Пароль: "); DPRINTLN(currentPassword);
    }
}

void WebDashboard::loadPasswordFromEEPROM() {
    // Читаем пароль из EEPROM
    char buf[32] = {0};
    for (int i = 0; i < 31; i++) {
        buf[i] = EEPROM.read(EEPROM_WEB_PASS_ADDR + i);
        if (buf[i] == 0) break;
    }
    
    String eepromPass = String(buf);
    
    // Если в EEPROM есть валидный пароль (не пустой и не "DELETED")
    if (eepromPass.length() > 0 && eepromPass != "DELETED") {
        currentPassword = eepromPass;
        LOG_INFO("🔐 Загружен сохраненный пароль из EEPROM");
    } else {
        currentPassword = defaultPassword;
        LOG_INFO("🔐 Используется пароль по умолчанию");
    }
    
    DPRINT("🔐 Текущий пароль: "); DPRINTLN(currentPassword);
}

void WebDashboard::savePasswordToEEPROM(const String& newPass) {
    // Очищаем старый пароль
    for (int i = 0; i < 32; i++) {
        EEPROM.write(EEPROM_WEB_PASS_ADDR + i, 0);
    }
    
    // Записываем новый пароль
    for (size_t i = 0; i < newPass.length() && i < 31; i++) {
        EEPROM.write(EEPROM_WEB_PASS_ADDR + i, newPass[i]);
    }
    EEPROM.write(EEPROM_WEB_PASS_ADDR + newPass.length(), 0); // null-терминатор
    EEPROM.commit();
    
    currentPassword = newPass;
    LOG_OK("🔐 Новый пароль сохранен в EEPROM");
}

void WebDashboard::resetPasswordToDefault() {
    // Помечаем, что пароль сброшен (записываем маркер "DELETED")
    const char* marker = "DELETED";
    for (int i = 0; i < 8; i++) {
        EEPROM.write(EEPROM_WEB_PASS_ADDR + i, marker[i]);
    }
    EEPROM.commit();
    
    currentPassword = defaultPassword;
    LOG_WARN("🔐 Пароль сброшен к значению по умолчанию");
}

bool WebDashboard::checkAuth() {
    if (!authEnabled) return true;
    
    if (!server.authenticate(username.c_str(), currentPassword.c_str())) {
        server.requestAuthentication();
        return false;
    }
    return true;
}

void WebDashboard::begin() {
    DENTER("WebDashboard::begin");
    
    // Страницы аутентификации
    server.on("/login", HTTP_GET, std::bind(&WebDashboard::handleLogin, this));
    server.on("/login", HTTP_POST, std::bind(&WebDashboard::handleLogin, this));
    server.on("/logout", HTTP_GET, std::bind(&WebDashboard::handleLogout, this));
    
    // Страница смены пароля
    server.on("/change-password", HTTP_POST, std::bind(&WebDashboard::handleChangePassword, this));
    
    // Главная страница
    server.on("/", HTTP_GET, [this]() {
        if (!checkAuth()) return;
        handleRoot();
    });
    
    // API endpoints
    server.on("/api/status", HTTP_GET, [this]() {
        if (!checkAuth()) return;
        handleAPIStatus();
    });
    
    server.on("/api/fill", HTTP_POST, [this]() {
        if (!checkAuth()) return;
        handleAPIFill();
    });
    
    server.on("/api/stop", HTTP_POST, [this]() {
        if (!checkAuth()) return;
        handleAPIStop();
    });
    
    server.on("/api/calibrate", HTTP_POST, [this]() {
        if (!checkAuth()) return;
        handleAPICalibrate();
    });
    
    server.on("/api/reboot", HTTP_POST, [this]() {
        if (!checkAuth()) return;
        handleAPIReboot();
    });
    
    // Статические файлы
    server.serveStatic("/dashboard.html", SPIFFS, "/dashboard.html");
    server.serveStatic("/style.css", SPIFFS, "/style.css");
    server.serveStatic("/script.js", SPIFFS, "/script.js");
    server.serveStatic("/favicon.ico", SPIFFS, "/favicon.ico");
    
    server.onNotFound(std::bind(&WebDashboard::handleNotFound, this));
    
    server.begin();
    LOG_INFO("📊 Веб-дашборд запущен");
    
    if (authEnabled) {
        Serial.println("🔐 Логин: " + username);
        Serial.println("🔐 Пароль: " + currentPassword);
        Serial.println("   (можете сменить в настройках профиля)");
    }
    
    DEXIT("WebDashboard::begin");
}

void WebDashboard::handleLogin() {
    if (server.method() == HTTP_POST) {
        String inputPass = server.arg("password");
        
        if (inputPass == currentPassword) {
            // Успешный вход
            server.requestAuthentication();
            server.sendHeader("Location", "/dashboard.html");
            server.send(302, "text/plain", "");
        } else {
            // Неверный пароль - редирект с ошибкой
            server.sendHeader("Location", "/login.html?error=1");
            server.send(302, "text/plain", "");
        }
    } else {
        // Просто показываем страницу входа
        if (SPIFFS.exists("/login.html")) {
            File file = SPIFFS.open("/login.html", "r");
            server.streamFile(file, "text/html");
            file.close();
        } else {
            server.send(500, "text/plain", "Login page not found");
        }
    }
}

void WebDashboard::handleChangePassword() {
    if (!checkAuth()) return;
    
    String oldPass = server.arg("oldPassword");
    String newPass = server.arg("newPassword");
    String confirmPass = server.arg("confirmPassword");
    
    StaticJsonDocument<200> response;
    
    // Проверка старого пароля
    if (oldPass != currentPassword) {
        response["success"] = false;
        response["message"] = "Неверный старый пароль";
        sendJsonResponse(200, response);
        return;
    }
    
    // Проверка нового пароля
    if (newPass.length() < 4) {
        response["success"] = false;
        response["message"] = "Новый пароль должен быть не менее 4 символов";
        sendJsonResponse(200, response);
        return;
    }
    
    if (newPass != confirmPass) {
        response["success"] = false;
        response["message"] = "Новый пароль и подтверждение не совпадают";
        sendJsonResponse(200, response);
        return;
    }
    
    // Сохраняем новый пароль
    savePasswordToEEPROM(newPass);
    
    response["success"] = true;
    response["message"] = "Пароль успешно изменен";
    sendJsonResponse(200, response);
    
    LOG_OK("🔐 Пароль изменен пользователем");
}

void WebDashboard::handleLogout() {
    server.requestAuthentication();
    server.sendHeader("Location", "/login");
    server.send(302, "text/plain", "");
}

void WebDashboard::handleRoot() {
    server.sendHeader("Location", "/dashboard.html", true);
    server.send(302, "text/plain", "");
}

void WebDashboard::handleAPIStatus() {
    DENTER("WebDashboard::handleAPIStatus");
    
    StaticJsonDocument<1024> doc;
    
    float currentWeight = scale.getCurrentWeight();
    float emptyWeight = scale.getEmptyWeight();
    float waterVolume = currentWeight - emptyWeight;
    if (waterVolume < 0) waterVolume = 0;
    
    doc["currentWeight"] = currentWeight;
    doc["emptyWeight"] = emptyWeight;
    doc["waterVolume"] = waterVolume;
    doc["maxVolume"] = FULL_WATER_LEVEL;
    doc["cups"] = Display::mlToCups(waterVolume);
    doc["waterLevel"] = (int)((waterVolume / FULL_WATER_LEVEL) * 100);
    
    SystemState state = stateMachine ? stateMachine->getCurrentStateEnum() : ST_IDLE;
    const char* stateNames[] = {"INIT", "IDLE", "FILLING", "CALIBRATION", "ERROR"};
    doc["systemState"] = stateNames[state];
    
    doc["kettlePresent"] = scale.isKettlePresent();
    doc["wifiConnected"] = wifiManager.isConnected();
    doc["wifiSignal"] = WiFi.RSSI();
    doc["wifiSSID"] = wifiManager.getSSID();
    doc["localIP"] = WiFi.localIP().toString();
    
    bool mqttConnected = mqttManager ? mqttManager->isConnected() : false;
    doc["mqttConnected"] = mqttConnected;
    doc["mqttSent"] = mqttManager ? mqttManager->getMessagesSent() : 0;
    doc["mqttFailed"] = mqttManager ? mqttManager->getMessagesFailed() : 0;
    
    doc["calibrationFactor"] = scale.getCalibrationFactor();
    doc["calibrationDone"] = scale.isCalibrationDone();
    doc["factorCalibrated"] = scale.isFactorCalibrated();
    
    doc["uptime"] = millis();
    doc["freeHeap"] = ESP.getFreeHeap();
    
    // Информация о пароле (безопасно - только факт смены)
    doc["passwordChanged"] = (currentPassword != defaultPassword);
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
    
    DEXIT("WebDashboard::handleAPIStatus");
}

void WebDashboard::handleAPIFill() {
    // ... (тот же код, что и раньше) ...
}

void WebDashboard::handleAPIStop() {
    // ... (тот же код, что и раньше) ...
}

void WebDashboard::handleAPICalibrate() {
    // ... (тот же код, что и раньше) ...
}

void WebDashboard::handleAPIReboot() {
    // ... (тот же код, что и раньше) ...
}

void WebDashboard::handleNotFound() {
    if (!checkAuth()) return;
    
    String path = server.uri();
    DPRINTLN("📊 404: " + path);
    
    server.sendHeader("Location", "/dashboard.html", true);
    server.send(302, "text/plain", "");
}

void WebDashboard::sendPlainResponse(int code, const String& text) {
    server.send(code, "text/plain", text);
}

void WebDashboard::sendJsonResponse(int code, const JsonDocument& doc) {
    String response;
    serializeJson(doc, response);
    server.send(code, "application/json", response);
}

void WebDashboard::handle() {
    server.handleClient();
}

// Публичный метод для сброса пароля (вызывается при factory reset)
void WebDashboard::resetPassword() {
    resetPasswordToDefault();
    LOG_WARN("🔐 Пароль сброшен к значению по умолчанию (admin)");
}