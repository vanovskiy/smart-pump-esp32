#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ==================== ОТЛАДКА ====================
// Раскомментируйте следующую строку для включения отладочного вывода
// #define DEBUG 1

// ==================== ПИНЫ ПОДКЛЮЧЕНИЯ ====================
#define PIN_PUMP_RELAY 26
#define PIN_POWER_RELAY 25
#define PIN_HX711_DT 16
#define PIN_HX711_SCK 4
#define PIN_BUTTON 27
#define PIN_DISPLAY_SDA 21
#define PIN_DISPLAY_SCL 22
#define PIN_SERVO 32
#define PIN_BUZZER 33

// ==================== ПАРАМЕТРЫ СИСТЕМЫ ====================
#define CUP_VOLUME 250.0f
#define WEIGHT_HYST 20.0f
#define MIN_WATER_LEVEL 500.0f
#define FULL_WATER_LEVEL 1700.0f
#define EMPTY_KETTLE_OFFSET 0.0f

// ==================== ВРЕМЕННЫЕ КОНСТАНТЫ ====================
#define DEBOUNCE_TIME 50
#define LONG_PRESS_TIME 3000
#define VERY_LONG_PRESS_TIME 10000
#define DOUBLE_CLICK_TIME 400
#define PUMP_TIMEOUT 120000
#define NO_FLOW_TIMEOUT 5000
#define POWER_RELAY_COOLDOWN 2000
#define SERVO_MOVE_TIME 1000
#define BUZZER_FEEDBACK 100
#define LOOP_DELAY 100

// ==================== ПАМЯТЬ ====================
#define EEPROM_SIZE 512
#define EEPROM_CALIB_ADDR 0

// ==================== СБРОС ====================
#define RESET_CALIB_TIME 10000
#define RESET_FULL_TIME 15000

// ==================== MQTT НАСТРОЙКИ ====================
#define MQTT_RECONNECT_INTERVAL 5000
#define MQTT_PUBLISH_INTERVAL 2000

// ==================== СОСТОЯНИЯ СИСТЕМЫ ====================
enum SystemState {
    ST_INIT,
    ST_IDLE,
    ST_FILLING,
    ST_CALIBRATION,
    ST_ERROR
};

enum CalibrationStep {
    CALIB_WAIT_REMOVE,
    CALIB_WAIT_PLACE
};

enum ErrorType {
    ERR_NONE,
    ERR_HX711_TIMEOUT,
    ERR_NO_FLOW,
    ERR_FILL_TIMEOUT
};

enum ServoState {
    SERVO_IDLE,
    SERVO_MOVING,
    SERVO_OVER_KETTLE
};

#endif