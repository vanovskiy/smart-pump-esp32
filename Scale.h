// файл: Scale.h
// Заголовочный файл класса для работы с тензодатчиком HX711

#ifndef SCALE_H
#define SCALE_H

#include "config.h"
#include <GyverHX711.h>

// ==================== ВНУТРЕННИЕ КОНСТАНТЫ ====================
#define STABLE_WEIGHT_THRESHOLD 5.0f    // Порог стабильности веса (граммы)
#define STABLE_TIME_THRESHOLD 2000      // Время стабильности для детекции (мс)
#define MAX_WEIGHT_JUMP 500.0f          // Максимальный скачок веса (защита от выбросов)
#define EEPROM_FLAG_VALUE 0xAA           // Флаг валидных данных в EEPROM
#define DEFAULT_FACTOR 0.00042f          // Коэффициент по умолчанию

class Scale {
  private:
    // ==================== ОСНОВНЫЕ ПЕРЕМЕННЫЕ ====================
    GyverHX711 scale;
    float emptyWeight;
    float currentWeight;
    float calibrationFactor;
    
    // ==================== ДЛЯ ПРОВЕРКИ СТАБИЛЬНОСТИ ====================
    float lastReadWeight;
    unsigned long lastStableReadTime;
    
    // ==================== ДЛЯ ФИЛЬТРАЦИИ ====================
    static const int STABLE_READINGS = 5;
    float readings[STABLE_READINGS];
    int readIndex;
    
    // ==================== ДЛЯ РАБОТЫ С EEPROM ====================
    bool isCalibrated;
    bool factorCalibrated;
    int eepromAddr;

  public:
    // ==================== КОНСТРУКТОР ====================
    Scale();

    // ==================== ИНИЦИАЛИЗАЦИЯ ====================
    bool begin();
    void tare();

    // ==================== КАЛИБРОВКА КОЭФФИЦИЕНТА ====================
    bool calibrateFactorViaSerial();
    bool calibrateFactor(float knownWeight);
    void resetFactor();
    bool isFactorCalibrated() { return factorCalibrated; }

    // ==================== КАЛИБРОВКА ПУСТОГО ЧАЙНИКА ====================
    void calibrateEmpty(float weight);
    void setCalibrationFactor(float factor);
    
    // ==================== РАБОТА С EEPROM ====================
    void saveCalibrationToEEPROM(int addr);
    void loadCalibrationFromEEPROM(int addr);
    void resetCalibration();

    // ==================== ГЕТТЕРЫ (ВСЕ INLINE) ====================
    float getEmptyWeight() { return emptyWeight; }
    float getCurrentWeight() { return currentWeight; }
    float getCalibrationFactor() { return calibrationFactor; }
    float getRawWeight() {
        if (!scale.available()) return 0;
        long rawValue = scale.read();
        return rawValue * calibrationFactor;
    }
    long getRawADC() {
        if (scale.available()) return scale.read();
        return 0;
    }

    // ==================== ПРОВЕРКИ СОСТОЯНИЯ ====================
    bool update();
    bool isReady();
    bool isKettlePresent();
    bool isWeightStable();
    bool isCalibrationDone() { return isCalibrated; }
    
    // ==================== ВСПОМОГАТЕЛЬНЫЕ ====================
    long getStableRawValue(int samples = 10);
};

#endif