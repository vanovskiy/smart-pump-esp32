// файл: WiFiManager.cpp
// Реализация методов класса WiFiManager

#include "WiFiManager.h"      // Подключаем заголовочный файл
#include <ArduinoJson.h>       // Подключаем библиотеку для работы с JSON (для ответов на scan)

// ==================== КОНСТАНТЫ ====================
#define AP_SSID "Smart_Pump"           // Имя точки доступа в режиме настройки
#define AP_PASSWORD "12345678"          // Пароль точки доступа
#define CONNECT_TIMEOUT 30000           // Таймаут подключения к WiFi (30 секунд)
#define DNS_PORT 53                      // Стандартный порт DNS сервера

// ==================== КОНСТРУКТОР ====================

/**
 * Конструктор WiFiManager
 * Инициализирует веб-сервер на порту 80 и устанавливает начальные значения переменных
 */
WiFiManager::WiFiManager() : server(80) {  // Инициализация server с портом 80
    configured = false;                      // По умолчанию WiFi не настроен
    currentState = WIFI_STATE_UNCONFIGURED;  // Начальное состояние - не настроен
    lastReconnectAttempt = 0;                 // Сброс времени последней попытки
    lastStatusUpdate = 0;                     // Сброс времени последнего обновления
    connectionStartTime = 0;                   // Сброс времени начала подключения
    apStartTime = 0;                           // Сброс времени запуска AP
    eventCallback = nullptr;                   // Callback не задан
}

// ==================== ИНИЦИАЛИЗАЦИЯ ====================

/**
 * Инициализация WiFiManager
 * Монтирует SPIFFS, загружает сохраненные настройки и пытается подключиться
 * Вызывается один раз в setup()
 */
void WiFiManager::begin() {
    Serial.println("WiFiManager starting...");  // Отладочное сообщение
    
    // Монтируем файловую систему SPIFFS
    // true означает форматировать, если монтирование не удалось
    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS mount failed");  // Ошибка монтирования
        return;  // Выходим, без файловой системы веб-портал не будет работать
    }
    Serial.println("SPIFFS mounted successfully");  // SPIFFS готов к работе
    
    // Открываем хранилище с именем "wifi" в режиме чтения/записи
    preferences.begin("wifi", false);
    
    // Загружаем MQTT учетные данные из отдельного хранилища
    loadMqttCredentials();
    
    // Загружаем сохраненные SSID и пароль WiFi
    ssid = preferences.getString("ssid", "");      // Если нет - пустая строка
    password = preferences.getString("pass", "");  // Если нет - пустая строка
    
    // WiFi настроен, если и SSID и пароль не пустые
    configured = (ssid.length() > 0 && password.length() > 0);
    
    if (configured) {
        // Если настройки есть - выводим SSID и пытаемся подключиться
        Serial.printf("Found saved WiFi: %s\n", ssid.c_str());
        currentState = WIFI_STATE_STATION;  // Переходим в режим станции
        connect();  // Пытаемся подключиться
    } else {
        // Если настроек нет - выводим сообщение и запускаем режим настройки
        Serial.println("No WiFi credentials found");
        currentState = WIFI_STATE_UNCONFIGURED;  // Состояние - не настроен
        startConfigPortal();  // Запускаем точку доступа для настройки
    }
}

// ==================== ОСНОВНОЙ ЦИКЛ ====================

/**
 * Основной цикл WiFiManager
 * Должен вызываться каждый loop()
 * Обрабатывает DNS запросы, HTTP запросы и проверяет состояние подключения
 */
void WiFiManager::loop() {
    // Обновляем DNS сервер (нужен для перехвата запросов в режиме captive portal)
    dnsServer.processNextRequest();
    
    // Обрабатываем входящие HTTP запросы
    server.handleClient();
    
    // Проверяем статус подключения раз в секунду
    if (millis() - lastStatusUpdate > 1000) {
        lastStatusUpdate = millis();  // Обновляем время проверки
        
        // Если мы были подключены, но соединение потеряно
        if (currentState == WIFI_STATE_CONNECTED) {
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("WiFi connection lost");  // Соединение потеряно
                currentState = WIFI_STATE_STATION;       // Переходим в режим попытки подключения
                if (eventCallback) eventCallback(currentState);  // Уведомляем callback
                lastReconnectAttempt = millis();         // Запоминаем время для переподключения
            }
        }
        
        // Если мы в режиме попытки подключения
        if (currentState == WIFI_STATE_STATION) {
            // Проверяем, удалось ли подключиться
            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("WiFi connected successfully");  // Успех!
                currentState = WIFI_STATE_CONNECTED;            // Переходим в состояние "подключено"
                if (eventCallback) eventCallback(currentState); // Уведомляем callback
                
                // Если точка доступа еще была активна - выключаем её
                if (WiFi.getMode() & WIFI_AP) {
                    stopAPMode();
                }
            }
            // Проверяем таймаут подключения
            else if ((long)(millis() - connectionStartTime) > (long)CONNECT_TIMEOUT) {
                Serial.println("Connection timeout, starting AP mode");  // Таймаут
                startConfigPortal();  // Запускаем точку доступа для перенастройки
            }
        }
    }
}

// ==================== УПРАВЛЕНИЕ ПОДКЛЮЧЕНИЕМ ====================

/**
 * Попытка подключения к сохраненной WiFi сети
 * @return true - если есть сохраненные настройки (само подключение асинхронно)
 */
bool WiFiManager::connect() {
    if (!configured) return false;  // Если нет настроек - сразу возвращаем false
    
    Serial.printf("Connecting to WiFi: %s\n", ssid.c_str());  // Отладочное сообщение
    WiFi.mode(WIFI_STA);                                       // Режим станции
    WiFi.begin(ssid.c_str(), password.c_str());               // Начинаем подключение
    
    currentState = WIFI_STATE_STATION;        // Переходим в состояние "пытаемся подключиться"
    connectionStartTime = millis();            // Запоминаем время начала попытки
    
    if (eventCallback) eventCallback(currentState);  // Уведомляем callback
    
    return true;  // Возвращаем true (но подключение еще может не состояться)
}

/**
 * Принудительное отключение от WiFi
 */
void WiFiManager::disconnect() {
    WiFi.disconnect();                                // Отключаемся
    currentState = WIFI_STATE_UNCONFIGURED;           // Состояние - не настроен
    if (eventCallback) eventCallback(currentState);   // Уведомляем callback
}

/**
 * Полный сброс всех настроек (WiFi + MQTT)
 * Очищает память и перезапускает устройство
 */
void WiFiManager::resetSettings() {
    Serial.println("=== Factory Reset ===");  // Заголовок в логе
    
    // Сброс WiFi переменных
    ssid = "";          // Очищаем SSID
    password = "";      // Очищаем пароль
    configured = false; // Сбрасываем флаг конфигурации
    
    // Открываем хранилище WiFi и очищаем его
    preferences.begin("wifi", false);
    preferences.clear();  // Очищаем все ключи в namespace wifi
    preferences.end();
    
    // Сброс MQTT учетных данных
    clearMqttCredentials();
    
    // Отключаем WiFi
    disconnect();
    
    Serial.println("✓ All settings cleared (WiFi + MQTT)");  // Подтверждение
    
    // Запускаем AP режим для новой настройки
    startConfigPortal();
}

// ==================== УПРАВЛЕНИЕ РЕЖИМОМ ТОЧКИ ДОСТУПА ====================

/**
 * Запуск режима точки доступа (AP mode)
 * Создает WiFi сеть для настройки устройства
 */
void WiFiManager::startAPMode() {
    Serial.println("Starting AP mode...");  // Отладочное сообщение
    
    WiFi.mode(WIFI_AP);                                      // Режим точки доступа
    WiFi.softAP(AP_SSID, AP_PASSWORD);                       // Запускаем AP с заданными SSID/паролем
    
    IPAddress apIP(192, 168, 4, 1);                          // Стандартный IP для AP режима
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));  // Настраиваем IP и маску
    
    // Настраиваем DNS для перехвата всех запросов (captive portal)
    dnsServer.start(DNS_PORT, "*", apIP);  // Любой домен направляем на IP точки доступа
    
    // Настраиваем маршруты HTTP сервера
    server.on("/", std::bind(&WiFiManager::handleRoot, this));          // Главная страница
    server.on("/config", std::bind(&WiFiManager::handleConfig, this));  // Страница настройки
    server.on("/save", std::bind(&WiFiManager::handleSave, this));      // Обработчик сохранения
    server.on("/scan", std::bind(&WiFiManager::handleScan, this));      // Обработчик сканирования
    server.onNotFound(std::bind(&WiFiManager::handleFileRequest, this)); // Все остальные запросы
    
    server.begin();  // Запускаем сервер
    
    currentState = WIFI_STATE_AP;      // Переходим в состояние AP
    apStartTime = millis();             // Запоминаем время запуска
    
    // Выводим информацию о точке доступа
    Serial.println("AP mode started");
    Serial.printf("SSID: %s, Password: %s\n", AP_SSID, AP_PASSWORD);
    Serial.printf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());
    Serial.println("AP mode will stay active until configuration is saved");
    
    if (eventCallback) eventCallback(currentState);  // Уведомляем callback
}

/**
 * Остановка режима точки доступа
 */
void WiFiManager::stopAPMode() {
    if (WiFi.getMode() & WIFI_AP) {        // Если режим AP активен
        server.stop();                       // Останавливаем сервер
        dnsServer.stop();                     // Останавливаем DNS
        WiFi.softAPdisconnect(true);          // Отключаем точку доступа
        Serial.println("AP mode stopped");    // Отладочное сообщение
    }
}

/**
 * Запуск портала конфигурации
 * Переключает устройство в режим точки доступа для настройки
 */
void WiFiManager::startConfigPortal() {
    if (currentState != WIFI_STATE_AP) {  // Если мы еще не в режиме AP
        stopAPMode();                       // Останавливаем текущий режим
        startAPMode();                       // Запускаем AP
    }
}

// ==================== HTTP ОБРАБОТЧИКИ ====================

/**
 * Обработчик корневой страницы "/"
 * Отправляет клиенту файл index.html из SPIFFS
 */
void WiFiManager::handleRoot() {
    // Открываем файл index.html в режиме чтения
    File file = SPIFFS.open("/index.html", "r");
    if (!file) {  // Если файл не открылся
        server.send(500, "text/plain", "File not found");  // Ошибка сервера
        return;
    }
    // Отправляем файл клиенту с типом text/html
    server.streamFile(file, "text/html");
    file.close();  // Закрываем файл
}

/**
 * Обработчик страницы конфигурации "/config"
 * Отправляет клиенту файл config.html из SPIFFS
 */
void WiFiManager::handleConfig() {
    File file = SPIFFS.open("/config.html", "r");
    if (!file) {
        server.send(500, "text/plain", "File not found");
        return;
    }
    server.streamFile(file, "text/html");
    file.close();
}

/**
 * Обработчик сохранения настроек "/save"
 * Принимает POST данные из формы и сохраняет их
 */
void WiFiManager::handleSave() {
    if (server.method() == HTTP_POST) {  // Только для POST запросов
        // Получаем все параметры из формы
        String newSSID = server.arg("ssid");               // Имя WiFi сети
        String newWiFiPass = server.arg("wifi_password");   // Пароль WiFi
        String newMqttUser = server.arg("mqtt_username");   // Логин MQTT
        String newMqttPass = server.arg("mqtt_password");   // Пароль MQTT
        
        // Проверяем, что все поля заполнены
        if (newSSID.length() == 0 || newWiFiPass.length() == 0 || 
            newMqttUser.length() == 0 || newMqttPass.length() == 0) {
            
            // Отправляем страницу с ошибкой
                File file = SPIFFS.open("/success.html", "r");
                if (file) {
                    server.streamFile(file, "text/html");
                    file.close();
                } else {
                    server.send(200, "text/html", 
                        "<html><body><h1>Configuration Saved!</h1>"
                        "<p>Device will restart...</p></body></html>");
                }
    
                // Минимальная задержка для отправки ответа
                delay(100);  // Уменьшено с 1000 до 100 мс
                ESP.restart();
            }

        Serial.println("=== Saving Configuration ===");  // Заголовок в логе
        
        // Сохраняем WiFi credentials
        ssid = newSSID;            // Запоминаем SSID
        password = newWiFiPass;    // Запоминаем пароль
        configured = true;          // Отмечаем, что теперь WiFi настроен
        
        // Открываем хранилище WiFi и сохраняем данные
        preferences.begin("wifi", false);
        preferences.putString("ssid", ssid);        // Сохраняем SSID
        preferences.putString("pass", password);    // Сохраняем пароль
        preferences.end();
        
        Serial.printf("WiFi SSID: %s\n", ssid.c_str());  // Отладочный вывод
        
        // Сохраняем MQTT credentials
        if (saveMqttCredentials(newMqttUser, newMqttPass)) {
            Serial.printf("MQTT User: %s\n", newMqttUser.c_str());  // Отладочный вывод
        }
        
        // Отправляем страницу успеха из файла
        File file = SPIFFS.open("/success.html", "r");
        if (file) {
            server.streamFile(file, "text/html");
            file.close();
        } else {
            // Запасной вариант, если файл не найден
            server.send(200, "text/html", 
                       "<html><body><h1>Configuration Saved!</h1>"
                       "<p>Device will restart...</p></body></html>");
        }
        
        // Небольшая задержка перед перезагрузкой, чтобы ответ ушел клиенту
        delay(1000);
        ESP.restart();  // Перезагружаем ESP32 для применения настроек
        
    } else {
        server.send(405, "text/html", "Method not allowed");  // Не POST запрос
    }
}

/**
 * Обработчик сканирования WiFi сетей "/scan"
 * Возвращает JSON со списком доступных сетей
 */
void WiFiManager::handleScan() {
    Serial.println("Scanning WiFi networks...");  // Отладочное сообщение
    int n = WiFi.scanComplete();  // Проверяем, завершено ли сканирование
    
    if (n == -2) {  // Сканирование еще не начиналось
        WiFi.scanNetworks(true);  // Запускаем асинхронное сканирование
        server.send(200, "application/json", "{\"scanning\":true}");  // Сообщаем, что сканируем
        return;
    } else if (n == -1) {  // Сканирование еще не завершено
        server.send(200, "application/json", "{\"scanning\":true}");  // Все еще сканируем
        return;
    } else if (n >= 0) {  // Сканирование завершено, n - количество сетей
        String json = "{\"networks\":[";  // Начинаем формировать JSON
        
        for (int i = 0; i < n; ++i) {
            if (i != 0) json += ",";  // Добавляем запятую между элементами, кроме первого
            
            json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",";          // Имя сети
            json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";       // Уровень сигнала
            json += "\"encryption\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN) + "}";  // Есть ли шифрование
        }
        
        json += "]}";  // Закрываем JSON
        
        server.send(200, "application/json", json);  // Отправляем результат
        WiFi.scanDelete();  // Очищаем результаты сканирования из памяти
    }
}

/**
 * Обработчик запросов файлов из SPIFFS
 * Отправляет запрошенный файл или перенаправляет на корень
 */
void WiFiManager::handleFileRequest() {
    String path = server.uri();  // Получаем запрошенный путь
    
    // Перенаправляем корень на index.html
    if (path == "/") {
        path = "/index.html";
    }
    
    // Проверяем существование файла в SPIFFS
    if (!SPIFFS.exists(path)) {
        // Если файл не найден, перенаправляем на корень (для captive portal)
        server.sendHeader("Location", "http://192.168.4.1", true);  // HTTP редирект
        server.send(302, "text/plain", "");  // Код 302 - временное перенаправление
        return;
    }
    
    // Открываем файл
    File file = SPIFFS.open(path, "r");
    if (!file) {
        server.send(500, "text/plain", "Failed to open file");  // Ошибка открытия
        return;
    }
    
    // Определяем MIME-тип по расширению
    String contentType = getContentType(path);
    server.streamFile(file, contentType);  // Отправляем файл
    file.close();  // Закрываем файл
}

/**
 * Определение MIME-типа файла по его расширению
 * @param filename - имя файла
 * @return MIME-тип как строка
 */
String WiFiManager::getContentType(String filename) {
    if (filename.endsWith(".html")) return "text/html";          // HTML файлы
    else if (filename.endsWith(".css")) return "text/css";       // CSS файлы
    else if (filename.endsWith(".js")) return "application/javascript";  // JavaScript
    else if (filename.endsWith(".png")) return "image/png";      // PNG изображения
    else if (filename.endsWith(".jpg")) return "image/jpeg";     // JPG изображения
    else if (filename.endsWith(".ico")) return "image/x-icon";   // Иконки
    return "text/plain";  // По умолчанию
}

/**
 * Обработчик 404 Not Found
 * Перенаправляет на главную для работы captive portal
 */
void WiFiManager::handleNotFound() {
    // Перенаправляем на главную для captive portal
    server.sendHeader("Location", "http://192.168.4.1", true);  // HTTP редирект
    server.send(302, "text/plain", "");  // Код 302 - временное перенаправление
}

// ==================== ВСПОМОГАТЕЛЬНЫЕ МЕТОДЫ ====================

/**
 * Получение текстового сообщения о текущем статусе WiFi для дисплея
 * @return строка со статусом
 */
String WiFiManager::getStatusMessage() {
    switch (currentState) {
        case WIFI_STATE_UNCONFIGURED:
            return "No WiFi config";      // WiFi не настроен
        case WIFI_STATE_STATION:
            return "Connecting...";        // Подключаемся
        case WIFI_STATE_CONNECTED:
            return "Connected";            // Подключено
        case WIFI_STATE_AP:
            return "AP Mode";               // Режим точки доступа
        default:
            return "Unknown";               // Неизвестно
    }
}

/**
 * Получение MAC-адреса ESP32
 * @return MAC-адрес в формате XX:XX:XX:XX:XX:XX
 */
String WiFiManager::getMacAddress() {
    uint8_t mac[6];  // Массив для MAC-адреса (6 байт)
    WiFi.macAddress(mac);  // Получаем MAC-адрес
    
    char macStr[18];  // Буфер для строки (2 символа * 6 + 5 двоеточий + 1 нуль-терминатор)
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);  // Форматируем
    return String(macStr);  // Возвращаем как String
}

/**
 * Получение уникального ID чипа ESP32
 * @return ID чипа в шестнадцатеричном формате
 */
String WiFiManager::getChipId() {
    uint64_t chipid = ESP.getEfuseMac();  // Получаем MAC из efuse (64 бита)
    char chipIdStr[13];  // Буфер для строки (8 + 4 символа + 1 нуль-терминатор)
    snprintf(chipIdStr, sizeof(chipIdStr), "%04X%08X",
             (uint16_t)(chipid >> 32), (uint32_t)chipid);  // Форматируем
    return String(chipIdStr);  // Возвращаем как String
}

// ==================== МЕТОДЫ ДЛЯ MQTT УЧЕТНЫХ ДАННЫХ ====================

/**
 * Проверка наличия MQTT учетных данных в памяти (не в Preferences)
 * @return true - если и логин и пароль не пустые
 */
bool WiFiManager::hasMqttCredentials() {
    // Используем уже загруженные данные, а не открываем Preferences каждый раз
    return (mqttUser.length() > 0 && mqttPass.length() > 0);
}

/**
 * Получение имени пользователя MQTT из Preferences
 * @return строка с именем пользователя
 */
String WiFiManager::getMqttUser() {
    preferences.begin("mqtt", true);  // Открываем namespace "mqtt" в режиме чтения
    String user = preferences.getString("user", "");  // Читаем ключ "user"
    preferences.end();  // Закрываем Preferences
    return user;  // Возвращаем значение
}

/**
 * Получение пароля MQTT из Preferences
 * @return строка с паролем
 */
String WiFiManager::getMqttPass() {
    preferences.begin("mqtt", true);  // Открываем namespace "mqtt" в режиме чтения
    String pass = preferences.getString("pass", "");  // Читаем ключ "pass"
    preferences.end();  // Закрываем Preferences
    return pass;  // Возвращаем значение
}

/**
 * Сохранение MQTT учетных данных в Preferences и в оперативную память
 * @param user - имя пользователя
 * @param pass - пароль
 * @return true - если сохранение успешно
 */
bool WiFiManager::saveMqttCredentials(const String& user, const String& pass) {
    if (user.length() == 0 || pass.length() == 0) {  // Проверка на пустые значения
        Serial.println("✗ Cannot save empty MQTT credentials");
        return false;  // Не сохраняем пустые данные
    }
    
    preferences.begin("mqtt", false);  // Открываем namespace "mqtt" в режиме записи
    preferences.putString("user", user);  // Сохраняем имя пользователя
    preferences.putString("pass", pass);  // Сохраняем пароль
    preferences.end();  // Закрываем Preferences
    
    // Сохраняем также в оперативную память для быстрого доступа
    mqttUser = user;
    mqttPass = pass;
    
    Serial.println("✓ MQTT credentials saved to Preferences");  // Подтверждение
    return true;  // Успех
}

/**
 * Очистка MQTT учетных данных из Preferences и из памяти
 */
void WiFiManager::clearMqttCredentials() {
    preferences.begin("mqtt", false);  // Открываем namespace "mqtt" в режиме записи
    preferences.remove("user");         // Удаляем ключ "user"
    preferences.remove("pass");         // Удаляем ключ "pass"
    preferences.end();                  // Закрываем Preferences
    
    // Очищаем оперативную память
    mqttUser = "";
    mqttPass = "";
    
    Serial.println("✓ MQTT credentials cleared");  // Подтверждение
}

/**
 * Загрузка MQTT учетных данных из Preferences в оперативную память
 * Вызывается при инициализации для кэширования данных
 */
void WiFiManager::loadMqttCredentials() {
    preferences.begin("mqtt", true);  // Открываем namespace "mqtt" в режиме чтения
    mqttUser = preferences.getString("user", "");  // Загружаем имя пользователя
    mqttPass = preferences.getString("pass", "");  // Загружаем пароль
    preferences.end();  // Закрываем Preferences
}