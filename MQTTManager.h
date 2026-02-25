// файл: MQTTManager.h
// Заголовочный файл класса для управления MQTT подключением к Dealgate
// Отвечает за: публикацию состояния устройства, получение команд, поддержание соединения

#ifndef MQTT_MANAGER_H  // Защита от множественного включения: если не определено
#define MQTT_MANAGER_H  // то определяем этот макрос

#include <WiFi.h>               // Подключаем библиотеку WiFi для работы с сетью
#include <PubSubClient.h>       // Подключаем MQTT клиент (библиотека PubSubClient)
#include <ArduinoJson.h>        // Подключаем библиотеку для работы с JSON (для heartbeat)
#include <Preferences.h>        // Подключаем библиотеку для хранения данных в энергонезависимой памяти
#include "config.h"             // Подключаем основной конфигурационный файл
#include "Scale.h"              // Подключаем класс весов для получения данных о весе
#include "StateMachine.h"       // Подключаем класс конечного автомата для передачи команд
#include "WiFiManager.h"        // Подключаем класс управления WiFi для получения учетных данных

// ==================== КОНСТАНТЫ УРОВНЕЙ ВОДЫ ====================
// Эти значения определяют, в какой диапазон попадает текущий объем воды
#define WATER_LEVEL_EMPTY 500      // Нижняя граница: < 500 мл = состояние 0 (пустой)
#define WATER_LEVEL_LOW 1000        // Верхняя граница: 500-1000 мл = состояние 1 (низкий)
                                    // > 1000 мл = состояние 2 (нормальный)

// Определение типа указателя на функцию обратного вызова для MQTT команд
// Будет вызываться при получении команды из топика filling
typedef void (*CommandCallback)(int mode);

/**
 * Класс MQTTManager управляет подключением к MQTT брокеру Dealgate
 * 
 * Основные функции:
 * - Подключение к брокеру mqtt.dealgate.ru:1883
 * - Публикация уровня воды (0, 1, 2) в топик /devices/pump/water_level
 * - Публикация наличия чайника (0, 1) в топик /devices/pump/kettle
 * - Получение команд налива (1-8) из топика /devices/pump/filling
 * - Автоматическое переподключение при обрыве связи
 * - Отправка отладочной информации (heartbeat)
 */
class MQTTManager {
private:
    // ==================== ОСНОВНЫЕ ОБЪЕКТЫ ====================
    WiFiClient wifiClient;           // WiFi клиент для MQTT соединения
    PubSubClient mqttClient;          // MQTT клиент (обертка над WiFi клиентом)
    Preferences preferences;          // Объект для работы с энергонезависимой памятью
    
    // ==================== НАСТРОЙКИ ТОПИКОВ ====================
    String clientId;                   // Идентификатор клиента для MQTT
    String waterLevelTopic;             // Топик для публикации уровня воды
    String kettleTopic;                 // Топик для публикации наличия чайника
    String fillingTopic;                // Топик для подписки на команды налива
    
    // ==================== УЧЕТНЫЕ ДАННЫЕ ====================
    String mqttUser;                    // Имя пользователя MQTT (из Dealgate)
    String mqttPass;                    // Пароль MQTT (из Dealgate)
    
    // ==================== CALLBACK ====================
    CommandCallback commandCallback;    // Указатель на функцию для обработки команд
    
    // ==================== ССЫЛКИ НА КОМПОНЕНТЫ ====================
    Scale& scale;                       // Ссылка на объект весов (для получения данных)
    StateMachine& stateMachine;          // Ссылка на конечный автомат (для передачи команд)
    WiFiManager& wifiManager;            // Ссылка на WiFi менеджер (для получения учетных данных)
    
    // ==================== ТАЙМИНГИ И СТАТИСТИКА ====================
    unsigned long lastReconnectAttempt;  // Время последней попытки переподключения
    unsigned long lastPublishTime;       // Время последней публикации состояния
    unsigned long lastHeartbeatTime;      // Время последней отправки heartbeat
    unsigned long lastConnectionCheckTime; // Время последней проверки соединения
    unsigned long messagesSent;           // Счетчик отправленных сообщений
    unsigned long messagesFailed;         // Счетчик неудачных отправок
    unsigned long reconnectAttempts;      // Счетчик попыток переподключения
    
    // ==================== КЭШ ДЛЯ ОТСЛЕЖИВАНИЯ ИЗМЕНЕНИЙ ====================
    int lastWaterState;                   // Последнее опубликованное состояние воды (0,1,2)
    bool lastKettlePresent;                // Последнее опубликованное наличие чайника (true/false)
    bool lastMqttConnected;                 // Последнее известное состояние MQTT соединения
    
    // ==================== ПРИВАТНЫЕ МЕТОДЫ ====================
    void connect();                         // Подключение к MQTT брокеру
    void subscribe();                        // Подписка на топики
    static void mqttCallback(char* topic, byte* payload, unsigned int length);  // Статический callback
    bool publish(const String& topic, const String& payload, bool retained = false);  // Публикация сообщения
    
    // Методы для расчета состояний
    int calculateWaterState();               // Расчет состояния воды по текущему объему
    
public:
    // ==================== КОНСТРУКТОР ====================
    /**
     * Конструктор MQTTManager
     * @param s - ссылка на объект весов
     * @param sm - ссылка на объект конечного автомата
     * @param wm - ссылка на объект WiFi менеджера
     */
    MQTTManager(Scale& s, StateMachine& sm, WiFiManager& wm);
    
    // ==================== ОСНОВНЫЕ МЕТОДЫ ====================
    void begin();        // Инициализация (вызывается в setup)
    void loop();         // Основной цикл (вызывается в loop)
    
    // ==================== УПРАВЛЕНИЕ CALLBACK ====================
    void setCommandCallback(CommandCallback cb) { commandCallback = cb; }  // Установка callback
    
    // ==================== ПУБЛИЧНЫЕ МЕТОДЫ ДЛЯ РАБОТЫ С УЧЕТНЫМИ ДАННЫМИ ====================
    bool loadCredentials();                         // Загрузка учетных данных из Preferences
    bool saveCredentials(const String& user, const String& pass);  // Сохранение учетных данных
    bool hasCredentials();                          // Проверка наличия учетных данных
    void clearCredentials();                         // Очистка учетных данных
    
    // ==================== ПУБЛИКАЦИЯ СТАТУСОВ ====================
    bool publishWaterState();     // Публикация уровня воды (0,1,2)
    bool publishKettleState();    // Публикация наличия чайника (0,1)
     
    // ==================== УПРАВЛЕНИЕ ПОДКЛЮЧЕНИЕМ ====================
    bool isConnected() { return mqttClient.connected(); }  // Проверка подключения
    void disconnect();                                      // Отключение от MQTT
    void reconnect();                                       // Принудительное переподключение
    
    // ==================== ГЕТТЕРЫ ====================
    unsigned long getMessagesSent() { return messagesSent; }        // Получить количество отправленных
    unsigned long getMessagesFailed() { return messagesFailed; }    // Получить количество ошибок
    unsigned long getReconnectAttempts() { return reconnectAttempts; } // Получить количество переподключений
    String getCurrentUser() { return mqttUser; }                    // Получить текущего пользователя
};

#endif // Конец защиты от множественного включения