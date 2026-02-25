// файл: WiFiManager.h
// Заголовочный файл класса для управления WiFi подключением и веб-порталом настройки
// Отвечает за: подключение к WiFi, создание точки доступа, веб-интерфейс, хранение учетных данных

#ifndef WIFI_MANAGER_H  // Защита от множественного включения: если не определено
#define WIFI_MANAGER_H  // то определяем этот макрос

#include <WiFi.h>         // Подключаем библиотеку для работы с WiFi на ESP32
#include <WebServer.h>    // Подключаем библиотеку для создания веб-сервера
#include <Preferences.h>  // Подключаем библиотеку для хранения данных в энергонезависимой памяти
#include <DNSServer.h>    // Подключаем библиотеку для DNS сервера (нужен для captive portal)
#include <SPIFFS.h>       // Подключаем библиотеку для работы с файловой системой SPIFFS

// Режимы работы WiFi (состояния WiFi менеджера)
enum WiFiState {
    WIFI_STATE_UNCONFIGURED,  // WiFi не настроен (нет сохраненных credentials)
    WIFI_STATE_STATION,       // Режим станции (пытаемся подключиться к роутеру)
    WIFI_STATE_AP,            // Режим точки доступа (раздаем WiFi для настройки)
    WIFI_STATE_CONNECTED      // Успешно подключены к роутеру
};

// Определение типа указателя на функцию обратного вызова для событий WiFi
// Позволяет другим классам (например, Display) узнавать об изменениях состояния WiFi
typedef void (*WiFiEventCallback)(WiFiState state);

/**
 * Класс WiFiManager управляет всеми аспектами WiFi соединения:
 * - Подключение к сохраненной WiFi сети
 * - Создание точки доступа для первоначальной настройки
 * - Веб-сервер с формами для ввода SSID/пароля и MQTT учетных данных
 * - Captive portal для автоматического перенаправления на страницу настройки
 * - Хранение учетных данных в Preferences
 */
class WiFiManager {
private:
    // ==================== ОСНОВНЫЕ ОБЪЕКТЫ ====================
    Preferences preferences;  // Объект для работы с энергонезависимой памятью
    WebServer server;          // Веб-сервер на порту 80
    DNSServer dnsServer;       // DNS сервер для перехвата запросов в режиме AP
    
    // ==================== НАСТРОЙКИ WIFI ====================
    String ssid;               // Имя WiFi сети (сохраненное)
    String password;            // Пароль WiFi сети (сохраненный)
    bool configured;            // Флаг: есть ли сохраненные настройки WiFi
    WiFiState currentState;     // Текущее состояние WiFi менеджера
    
    // ==================== НАСТРОЙКИ MQTT ====================
    // Хранятся здесь же для централизованного доступа
    String mqttUser;            // Имя пользователя MQTT (из Dealgate)
    String mqttPass;            // Пароль MQTT (из Dealgate)
    
    // ==================== ТАЙМИНГИ ====================
    unsigned long lastReconnectAttempt;  // Время последней попытки переподключения
    unsigned long lastStatusUpdate;      // Время последнего обновления статуса
    unsigned long connectionStartTime;   // Время начала попытки подключения (для таймаута)
    unsigned long apStartTime;            // Время запуска AP режима
    
    // ==================== CALLBACK ====================
    WiFiEventCallback eventCallback;      // Указатель на функцию обратного вызова
    
    // ==================== ОБРАБОТЧИКИ HTTP ====================
    void handleRoot();          // Обработчик корневой страницы "/"
    void handleConfig();        // Обработчик страницы конфигурации "/config"
    void handleSave();          // Обработчик сохранения данных из формы
    void handleScan();          // Обработчик сканирования WiFi сетей
    void handleNotFound();      // Обработчик 404 (не найдено)
    void handleFileRequest();   // Обработчик запросов файлов из SPIFFS
    
    // ==================== ВНУТРЕННИЕ МЕТОДЫ ====================
    void startAPMode();         // Запуск режима точки доступа
    void stopAPMode();          // Остановка режима точки доступа
    String getContentType(String filename);  // Определение MIME-типа по расширению файла
    
public:
    // ==================== КОНСТРУКТОР ====================
    WiFiManager();
    
    // ==================== ОСНОВНЫЕ МЕТОДЫ ====================
    void begin();               // Инициализация (вызывается в setup)
    void loop();                // Основной цикл (вызывается в loop)
    
    // ==================== УПРАВЛЕНИЕ WIFI ====================
    bool connect();             // Попытка подключения к сохраненной сети
    void disconnect();          // Отключение от WiFi
    void resetSettings();       // Полный сброс всех настроек (WiFi + MQTT)
    
    // ==================== РЕЖИМ КОНФИГУРАЦИИ ====================
    void startConfigPortal();   // Запуск портала конфигурации (точка доступа)
    bool isConfigPortalActive() { return currentState == WIFI_STATE_AP; }  // Активен ли портал?
    
    // ==================== СТАТУС ====================
    bool isConnected() { return currentState == WIFI_STATE_CONNECTED; }    // Подключены ли к WiFi?
    bool isConfigured() { return configured; }                              // Есть ли сохраненные настройки?
    String getSSID() { return ssid; }                                       // Имя текущей WiFi сети
    int getRSSI() { return WiFi.RSSI(); }                                   // Уровень сигнала
    WiFiState getState() { return currentState; }                           // Текущее состояние
    IPAddress getAPIP() { return WiFi.softAPIP(); }                         // IP адрес в режиме AP
    IPAddress getLocalIP() { return WiFi.localIP(); }                       // IP адрес в режиме станции
    
    // ==================== МЕТОДЫ ДЛЯ MQTT УЧЕТНЫХ ДАННЫХ ====================
    bool hasMqttCredentials();                      // Проверка наличия MQTT учетных данных
    String getMqttUser();                           // Получение имени пользователя MQTT
    String getMqttPass();                           // Получение пароля MQTT
    bool saveMqttCredentials(const String& user, const String& pass);  // Сохранение MQTT данных
    void clearMqttCredentials();                     // Очистка MQTT данных
    void loadMqttCredentials();                      // Загрузка MQTT данных из памяти
    
    // ==================== CALLBACK ====================
    void setEventCallback(WiFiEventCallback cb) { eventCallback = cb; }  // Установка callback
    
    // ==================== ДЛЯ ДИСПЛЕЯ ====================
    String getStatusMessage();  // Получение текстового статуса для отображения на дисплее
    
    // ==================== ДОПОЛНИТЕЛЬНЫЕ МЕТОДЫ ====================
    String getMacAddress();  // Получение MAC-адреса ESP32
    String getChipId();      // Получение уникального ID чипа
};

#endif // Конец защиты от множественного включения