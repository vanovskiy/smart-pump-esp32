// файл: MQTTManager.cpp
// Реализация методов класса MQTTManager

#include "MQTTManager.h"  // Подключаем заголовочный файл

// ==================== КОНСТАНТЫ ====================
// Настройки MQTT брокера Dealgate
#define MQTT_SERVER "mqtt.dealgate.ru"      // Адрес MQTT брокера
#define MQTT_PORT 1883                       // Порт MQTT (стандартный, без TLS)
#define MQTT_RECONNECT_DELAY 5000             // Задержка между попытками переподключения (5 сек)
#define MQTT_PUBLISH_INTERVAL 5000            // Интервал проверки необходимости публикации (5 сек)

// Глобальный указатель на экземпляр класса для использования в статическом callback
static MQTTManager* instance = nullptr;

// ==================== КОНСТРУКТОР ====================

/**
 * Конструктор MQTTManager
 * Инициализирует MQTT клиент, сохраняет ссылки на компоненты и устанавливает начальные значения
 * 
 * @param s - ссылка на объект весов
 * @param sm - ссылка на объект конечного автомата
 * @param wm - ссылка на объект WiFi менеджера
 */
MQTTManager::MQTTManager(Scale& s, StateMachine& sm, WiFiManager& wm) 
    : mqttClient(wifiClient), scale(s), stateMachine(sm), wifiManager(wm) {
    // Список инициализации:
    // - mqttClient инициализируется с wifiClient
    // - scale, stateMachine, wifiManager инициализируются ссылками на переданные объекты
    
    instance = this;  // Сохраняем указатель на себя для статического callback
    
    clientId = "smartpump";  // Базовый client ID (будет дополнен MAC-адресом в основном файле)
    
    // ==================== НАСТРОЙКА ТОПИКОВ ====================
    // Топики для Dealgate (формат: /devices/устройство/параметр)
    waterLevelTopic = "/devices/pump/water_level";    // Топик для уровня воды
    kettleTopic = "/devices/pump/kettle";              // Топик для наличия чайника
    fillingTopic = "/devices/pump/filling";            // Топик для команд налива
    
    // ==================== ИНИЦИАЛИЗАЦИЯ ПЕРЕМЕННЫХ ====================
    lastReconnectAttempt = 0;          // Последняя попытка переподключения не выполнялась
    lastPublishTime = 0;                // Публикация еще не выполнялась
    lastHeartbeatTime = 0;               // Heartbeat еще не отправлялся
    lastConnectionCheckTime = 0;         // Проверка соединения еще не выполнялась
    lastWaterState = -1;                 // Инициализируем некорректным значением (чтобы первая публикация точно сработала)
    lastKettlePresent = false;           // Инициализируем (по умолчанию чайника нет)
    lastMqttConnected = false;           // Инициализируем (соединения нет)
    
    messagesSent = 0;                    // Счетчик отправленных сообщений
    messagesFailed = 0;                  // Счетчик неудачных отправок
    reconnectAttempts = 0;                // Счетчик попыток переподключения
    
    commandCallback = nullptr;            // Callback пока не задан
}

// ==================== ИНИЦИАЛИЗАЦИЯ ====================

/**
 * Инициализация MQTT менеджера
 * Настраивает параметры подключения, выводит информацию и пытается подключиться
 */
void MQTTManager::begin() {
    // Настройка MQTT клиента
    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);     // Устанавливаем сервер и порт
    mqttClient.setCallback(mqttCallback);              // Устанавливаем callback для входящих сообщений
    mqttClient.setBufferSize(512);                      // Увеличиваем размер буфера для JSON сообщений
    
    // Вывод информации о конфигурации MQTT
    Serial.println("=== MQTT Configuration ===");
    Serial.printf("Client ID: %s\n", clientId.c_str());
    Serial.printf("Server: %s:%d\n", MQTT_SERVER, MQTT_PORT);
    Serial.printf("Water level topic: %s\n", waterLevelTopic.c_str());
    Serial.printf("Kettle present topic: %s\n", kettleTopic.c_str());
    Serial.println("Water level mapping:");
    Serial.println("  0 = Пустой (< 500 мл)");
    Serial.println("  1 = Низкий (500-1000 мл)");
    Serial.println("  2 = Нормальный (> 1000 мл)");
    Serial.println("Kettle present mapping:");
    Serial.println("  0 = Чайника нет");
    Serial.println("  1 = Чайник на месте");
    
    // Загружаем учетные данные из Preferences
    if (loadCredentials()) {
        Serial.println("✓ MQTT credentials loaded from Preferences");
        Serial.printf("  Username: %s\n", mqttUser.c_str());
        Serial.printf("  Password: %s\n", "********");  // Пароль не выводим в явном виде
        
        // Пробуем подключиться
        connect();
    } else {
        Serial.println("⚠ No MQTT credentials found in Preferences");
        Serial.println("  Use serial command to set:");
        Serial.println("  mqtt_set <username> <password>");
    }
    
    Serial.println("==========================");
}

// ==================== ОСНОВНОЙ ЦИКЛ ====================

/**
 * Основной цикл MQTT менеджера
 * Должен вызываться каждый loop()
 * Проверяет соединение, переподключается при необходимости, публикует данные
 */
void MQTTManager::loop() {
    unsigned long now = millis();  // Текущее время в миллисекундах
    
    // ===== ПРОВЕРКА СОСТОЯНИЯ СОЕДИНЕНИЯ (раз в секунду) =====
    if (now - lastConnectionCheckTime > 1000) {
        lastConnectionCheckTime = now;  // Обновляем время проверки
        bool currentlyConnected = mqttClient.connected();  // Текущее состояние
        
        // Если состояние изменилось - выводим сообщение
        if (currentlyConnected != lastMqttConnected) {
            lastMqttConnected = currentlyConnected;  // Запоминаем новое состояние
            if (currentlyConnected) {
                Serial.println("✓ MQTT connected to Dealgate");  // Подключились
            } else {
                Serial.println("✗ MQTT disconnected from Dealgate");  // Отключились
            }
        }
    }
    
    // ===== ПЕРЕПОДКЛЮЧЕНИЕ ПРИ НЕОБХОДИМОСТИ =====
    if (!mqttClient.connected()) {
        // Если прошло достаточно времени с последней попытки
        if (now - lastReconnectAttempt > MQTT_RECONNECT_DELAY) {
            lastReconnectAttempt = now;  // Обновляем время попытки
            reconnectAttempts++;          // Увеличиваем счетчик попыток
            Serial.printf("Attempting MQTT reconnect #%lu...\n", reconnectAttempts);
            connect();  // Пытаемся подключиться
        }
    } else {
        // Если подключены - обрабатываем входящие сообщения
        mqttClient.loop();
        
        // ===== ПУБЛИКАЦИЯ СОСТОЯНИЙ =====
        // Проверяем раз в 5 секунд, публикуем только при изменениях
        if (now - lastPublishTime > MQTT_PUBLISH_INTERVAL) {
            lastPublishTime = now;  // Обновляем время проверки
            publishWaterState();     // Публикуем уровень воды (если изменился)
            publishKettleState();    // Публикуем наличие чайника (если изменилось)
        }
    }
}

// ==================== ПОДКЛЮЧЕНИЕ К MQTT ====================

/**
 * Подключение к MQTT брокеру
 * Использует учетные данные из WiFiManager
 */
void MQTTManager::connect() {
    // Проверяем наличие учетных данных через WiFiManager
    if (!wifiManager.hasMqttCredentials()) {
        Serial.println("⚠ Cannot connect: No MQTT credentials");
        return;  // Выходим, если нет данных
    }
    
    Serial.print("Connecting to Dealgate MQTT... ");
    
    // Пытаемся подключиться с указанными учетными данными
    bool connected = mqttClient.connect(
        clientId.c_str(),                          // Client ID
        wifiManager.getMqttUser().c_str(),         // Имя пользователя
        wifiManager.getMqttPass().c_str(),         // Пароль
        NULL, 0, false, NULL                       // Дополнительные параметры (не используются)
    );
    
    if (connected) {
        Serial.println("OK");  // Успешное подключение
        
        messagesFailed = 0;  // Сбрасываем счетчик ошибок
        
        // Подписываемся на команды
        subscribe();
        
        // Отправляем текущие статусы при подключении
        publishWaterState();
        publishKettleState();
        
    } else {
        // Ошибка подключения - выводим код ошибки
        Serial.printf("FAILED (rc=%d)\n", mqttClient.state());
        messagesFailed++;  // Увеличиваем счетчик ошибок
    }
}

// ==================== ПОДПИСКА НА ТОПИКИ ====================

/**
 * Подписка на топик команд налива
 */
void MQTTManager::subscribe() {
    // Подписываемся на топик для получения команд
    if (mqttClient.subscribe(fillingTopic.c_str())) {
        Serial.printf("Subscribed to: %s\n", fillingTopic.c_str());  // Успешная подписка
    }
}

// ==================== ОБРАБОТЧИК ВХОДЯЩИХ СООБЩЕНИЙ ====================

/**
 * Статический callback для обработки входящих MQTT сообщений
 * Вызывается библиотекой PubSubClient при получении сообщения
 * 
 * @param topic - топик сообщения
 * @param payload - данные сообщения
 * @param length - длина данных
 */
void MQTTManager::mqttCallback(char* topic, byte* payload, unsigned int length) {
    if (!instance) return;  // Если экземпляр класса не создан - выходим
    
    // Создаем нуль-терминированную строку из payload
    char message[length + 1];
    memcpy(message, payload, length);  // Копируем данные
    message[length] = '\0';             // Добавляем завершающий ноль
    
    Serial.printf("MQTT command received [%s]: %s\n", topic, message);  // Отладочный вывод
    
    // Проверяем, какой это топик
    String topicStr = String(topic);
    
    // Если это топик налива (команды)
    if (topicStr == instance->fillingTopic) {
        // Сообщение должно быть просто числом (режим)
        int mode = atoi(message);  // Преобразуем строку в число
        
        // Проверяем, что команда в допустимом диапазоне (1-8) и callback задан
        if (mode >= 1 && mode <= 8 && instance->commandCallback) {
            Serial.printf("Executing filling command: mode %d\n", mode);
            instance->commandCallback(mode);  // Вызываем callback
        }
    }
}

// ==================== ПУБЛИКАЦИЯ СООБЩЕНИЙ ====================

/**
 * Внутренний метод для публикации сообщений в MQTT
 * 
 * @param topic - топик для публикации
 * @param payload - данные для отправки
 * @param retained - флаг retained сообщения (сохранять на брокере)
 * @return true - если публикация успешна
 */
bool MQTTManager::publish(const String& topic, const String& payload, bool retained) {
    if (!mqttClient.connected()) {  // Если нет подключения
        messagesFailed++;  // Увеличиваем счетчик ошибок
        return false;      // Выходим с ошибкой
    }
    
    // Пытаемся опубликовать сообщение
    bool result = mqttClient.publish(topic.c_str(), payload.c_str(), retained);
    
    if (result) {
        messagesSent++;  // Увеличиваем счетчик отправленных
        Serial.printf("MQTT publish [%s]: %s\n", topic.c_str(), payload.c_str());  // Отладка
    } else {
        messagesFailed++;  // Увеличиваем счетчик ошибок
        Serial.printf("MQTT publish FAILED [%s]\n", topic.c_str());  // Отладка ошибки
    }
    
    return result;  // Возвращаем результат
}

// ==================== РАСЧЕТ СОСТОЯНИЯ ВОДЫ ====================

/**
 * Расчет состояния воды на основе текущего объема
 * 
 * @return 0 - пустой (<500 мл), 1 - низкий (500-1000 мл), 2 - нормальный (>1000 мл)
 */
int MQTTManager::calculateWaterState() {
    // Вычисляем объем воды (текущий вес минус вес пустого чайника)
    float waterVolume = scale.getCurrentWeight() - scale.getEmptyWeight();
    if (waterVolume < 0) waterVolume = 0;  // Защита от отрицательных значений
    
    // Для воды 1 грамм = 1 миллилитр
    float volumeML = waterVolume;
    
    int state;
    if (volumeML < WATER_LEVEL_EMPTY) {
        state = 0;  // Пустой
        Serial.printf("Water volume: %.0f ml -> state 0 (EMPTY)\n", volumeML);
    } else if (volumeML <= WATER_LEVEL_LOW) {
        state = 1;  // Низкий
        Serial.printf("Water volume: %.0f ml -> state 1 (LOW)\n", volumeML);
    } else {
        state = 2;  // Нормальный
        Serial.printf("Water volume: %.0f ml -> state 2 (NORMAL)\n", volumeML);
    }
    
    return state;
}

// ==================== ПУБЛИКАЦИЯ УРОВНЯ ВОДЫ ====================

/**
 * Публикация уровня воды в MQTT
 * Публикует только при изменении состояния
 * 
 * @return true - если публикация успешна или состояние не изменилось
 */
bool MQTTManager::publishWaterState() {
    // Проверяем, есть ли чайник на весах
    if (!scale.isKettlePresent()) {
        // Если чайника нет, публикуем 0 (пустой)
        int currentState = 0;
        
        // Если состояние изменилось с предыдущего раза
        if (currentState != lastWaterState) {
            Serial.printf("Water state (no kettle): %d -> %d\n", lastWaterState, currentState);
            
            String payload = String(currentState);  // Просто число, не JSON
            bool result = publish(waterLevelTopic, payload, false);  // Публикуем
            
            if (result) {
                lastWaterState = currentState;  // Обновляем кэш
            }
            
            return result;
        }
        return true;  // Состояние не изменилось - не публикуем (но это не ошибка)
    }
    
    // Чайник есть - вычисляем состояние
    int currentState = calculateWaterState();
    
    // Публикуем, если состояние изменилось
    if (currentState != lastWaterState) {
        Serial.printf("Water state changed: %d -> %d\n", lastWaterState, currentState);
        
        // Отправляем просто число (не JSON!)
        String payload = String(currentState);
        bool result = publish(waterLevelTopic, payload, false);
        
        if (result) {
            lastWaterState = currentState;  // Обновляем кэш
        }
        
        return result;
    }
    
    return true;  // Ничего не изменилось, но это не ошибка
}

// ==================== ПУБЛИКАЦИЯ НАЛИЧИЯ ЧАЙНИКА ====================

/**
 * Публикация наличия чайника в MQTT
 * Публикует только при изменении состояния
 * 
 * @return true - если публикация успешна или состояние не изменилось
 */
bool MQTTManager::publishKettleState() {
    bool kettlePresent = scale.isKettlePresent();  // Текущее состояние
    int value = kettlePresent ? 1 : 0;              // Преобразуем в 0/1 для MQTT
    
    // Публикуем, если состояние изменилось
    if (value != lastKettlePresent) {
        Serial.printf("Kettle present changed: %d -> %d\n", lastKettlePresent, value);
        
        String payload = String(value);  // Просто число
        bool result = publish(kettleTopic, payload, false);  // Публикуем
        
        if (result) {
            lastKettlePresent = value;  // Обновляем кэш
        }
        
        return result;
    }
    
    return true;  // Ничего не изменилось
}

// ==================== РАБОТА С УЧЕТНЫМИ ДАННЫМИ ====================

/**
 * Загрузка MQTT учетных данных из Preferences
 * 
 * @return true - если данные загружены и не пустые
 */
bool MQTTManager::loadCredentials() {
    Preferences prefs;  // Создаем временный объект Preferences
    prefs.begin("mqtt", true);  // Открываем namespace "mqtt" в режиме чтения
    mqttUser = prefs.getString("user", "");  // Читаем имя пользователя
    mqttPass = prefs.getString("pass", "");  // Читаем пароль
    prefs.end();  // Закрываем Preferences
    
    // Возвращаем true, если оба поля не пустые
    return (mqttUser.length() > 0 && mqttPass.length() > 0);
}

/**
 * Сохранение MQTT учетных данных в Preferences
 * 
 * @param user - имя пользователя
 * @param pass - пароль
 * @return true - если сохранение успешно
 */
bool MQTTManager::saveCredentials(const String& user, const String& pass) {
    if (user.length() == 0 || pass.length() == 0) {  // Проверка на пустые значения
        Serial.println("✗ Cannot save empty credentials");
        return false;  // Не сохраняем пустые данные
    }
    
    preferences.begin("mqtt", false);  // Открываем namespace "mqtt" в режиме записи
    preferences.putString("user", user);  // Сохраняем имя пользователя
    preferences.putString("pass", pass);  // Сохраняем пароль
    
    // Проверяем, что данные сохранились (читаем обратно)
    String verifyUser = preferences.getString("user", "");
    String verifyPass = preferences.getString("pass", "");
    
    preferences.end();  // Закрываем Preferences
    
    // Если данные совпадают с сохраненными
    if (verifyUser == user && verifyPass == pass) {
        Serial.println("✓ MQTT credentials saved to Preferences");
        Serial.printf("  Username: %s\n", user.c_str());
        Serial.printf("  Password: %s\n", "********");  // Пароль не выводим
        
        // Сохраняем также в оперативную память
        mqttUser = user;
        mqttPass = pass;
        
        return true;  // Успех
    } else {
        Serial.println("✗ Failed to verify saved credentials");
        return false;  // Ошибка верификации
    }
}

/**
 * Очистка MQTT учетных данных из Preferences
 */
void MQTTManager::clearCredentials() {
    preferences.begin("mqtt", false);  // Открываем namespace "mqtt" в режиме записи
    preferences.remove("user");         // Удаляем ключ "user"
    preferences.remove("pass");         // Удаляем ключ "pass"
    preferences.end();                  // Закрываем Preferences
    
    // Очищаем оперативную память
    mqttUser = "";
    mqttPass = "";
    
    Serial.println("✓ MQTT credentials cleared from Preferences");
}

/**
 * Проверка наличия MQTT учетных данных
 * 
 * @return true - если данные есть (в памяти или в Preferences)
 */
bool MQTTManager::hasCredentials() {
    // Сначала проверяем в оперативной памяти (быстрее)
    if (mqttUser.length() > 0 && mqttPass.length() > 0) {
        return true;
    }
    
    // Если в памяти нет, проверяем в Preferences
    preferences.begin("mqtt", true);  // Открываем в режиме чтения
    bool hasUser = preferences.isKey("user");  // Проверяем наличие ключа "user"
    bool hasPass = preferences.isKey("pass");  // Проверяем наличие ключа "pass"
    preferences.end();  // Закрываем Preferences
    
    return hasUser && hasPass;  // Возвращаем true, если оба ключа есть
}

// ==================== УПРАВЛЕНИЕ ПОДКЛЮЧЕНИЕМ ====================

/**
 * Отключение от MQTT брокера
 */
void MQTTManager::disconnect() {
    if (mqttClient.connected()) {  // Если подключены
        mqttClient.disconnect();     // Отключаемся
        Serial.println("MQTT disconnected");  // Отладочное сообщение
    }
}

/**
 * Принудительное переподключение к MQTT брокеру
 */
void MQTTManager::reconnect() {
    disconnect();   // Сначала отключаемся
    delay(100);      // Небольшая задержка
    connect();       // Пытаемся подключиться заново
}