// файл: Scale.cpp
// Реализация класса для работы с тензодатчиком HX711

#include "Scale.h"
#include <EEPROM.h>
#include "debug.h"

// ==================== ВНУТРЕННИЕ КОНСТАНТЫ ====================
#define STABLE_WEIGHT_THRESHOLD 5.0f
#define STABLE_TIME_THRESHOLD 2000
#define MAX_WEIGHT_JUMP 500.0f
#define EEPROM_FLAG_VALUE 0xAA
#define DEFAULT_FACTOR 0.00042f

// ==================== КОНСТРУКТОР ====================
Scale::Scale() : 
    scale(PIN_HX711_DT, PIN_HX711_SCK),
    emptyWeight(0),
    currentWeight(0),
    calibrationFactor(DEFAULT_FACTOR),
    lastReadWeight(0),
    lastStableReadTime(0),
    readIndex(0),
    eepromAddr(0),
    isCalibrated(false),
    factorCalibrated(false)
{
    for (int i = 0; i < STABLE_READINGS; i++) readings[i] = 0;
    DPRINTLN("⚖️ Весы: объект создан");
}

// ==================== ИНИЦИАЛИЗАЦИЯ ====================
bool Scale::begin() {
    delay(500);
    scale.tare();
    
    LOG_INFO("⚖️ Весы инициализированы");
    DPRINTF("⚖️ Коэффициент по умолчанию: %f\n", calibrationFactor);
    
    return true;
}

void Scale::tare() {
    scale.tare();
    LOG_OK("⚖️ Тарирование выполнено");
}

// ==================== КАЛИБРОВКА КОЭФФИЦИЕНТА ====================
bool Scale::calibrateFactorViaSerial() {
    Serial.println("\n=== РЕЖИМ КАЛИБРОВКИ ДАТЧИКА ===");
    Serial.println("Этот режим калибрует коэффициент преобразования для вашего конкретного датчика.");
    Serial.println("Вам понадобится груз с ИЗВЕСТНЫМ ВЕСОМ (например, 500г, 1000г, 2000г).");
    Serial.println();
    
    while (true) {
        Serial.println("Шаг 1: Положите на весы груз с ИЗВЕСТНЫМ ВЕСОМ");
        Serial.println("Сейчас будет отображаться сырое значение АЦП. Дождитесь стабилизации...");
        
        unsigned long startTime = millis();
        while (millis() - startTime < 5000) {
            if (scale.available()) {
                Serial.printf("\rСырое значение АЦП: %8ld", scale.read());
            }
            delay(100);
        }
        Serial.println("\n");
        
        Serial.println("Шаг 2: Введите точный вес вашего груза В ГРАММАХ");
        Serial.print("> ");
        
        float knownWeight = 0;
        while (!Serial.available()) {
            delay(100);
        }
        
        String input = Serial.readStringUntil('\n');
        input.trim();
        knownWeight = input.toFloat();
        
        if (knownWeight <= 0) {
            Serial.println("Ошибка: неверный вес! Введите положительное число.");
            continue;
        }
        
        Serial.printf("Вы ввели: %.1f г\n", knownWeight);
        
        Serial.println("Шаг 3: Измеряем стабильное сырое значение...");
        long rawValue = getStableRawValue(20);
        
        float newFactor = knownWeight / rawValue;
        
        Serial.printf("Сырое значение АЦП: %ld\n", rawValue);
        Serial.printf("Рассчитанный коэффициент: %f\n", newFactor);
        
        Serial.println("\nШаг 4: Подтвердить калибровку? (Д/Н)");
        Serial.print("> ");
        
        while (!Serial.available()) {
            delay(100);
        }
        
        char confirm = Serial.read();
        if (confirm == 'Д' || confirm == 'д' || confirm == 'Y' || confirm == 'y') {
            calibrationFactor = newFactor;
            factorCalibrated = true;
            saveCalibrationToEEPROM(eepromAddr);
            
            LOG_OK("⚖️ Калибровка выполнена успешно!");
            DPRINTF("⚖️ Новый коэффициент: %f\n", calibrationFactor);
            return true;
        } else {
            Serial.println("\nКалибровка отменена. Начинаем заново...\n");
            delay(1000);
        }
    }
}

bool Scale::calibrateFactor(float knownWeight) {
    if (knownWeight <= 0) return false;
    
    long rawValue = getStableRawValue(20);
    calibrationFactor = knownWeight / rawValue;
    factorCalibrated = true;
    
    DPRINTF("⚖️ Коэффициент откалиброван: %f (АЦП=%ld, вес=%.1f)\n", 
            calibrationFactor, rawValue, knownWeight);
    
    return true;
}

void Scale::resetFactor() {
    calibrationFactor = DEFAULT_FACTOR;
    factorCalibrated = false;
    LOG_WARN("⚖️ Калибровочный коэффициент сброшен к значению по умолчанию");
}

// ==================== КАЛИБРОВКА ПУСТОГО ЧАЙНИКА ====================
void Scale::calibrateEmpty(float weight) {
    emptyWeight = weight;
    isCalibrated = true;
    LOG_OK("⚖️ Вес пустого чайника откалиброван");
    DPRINTF("⚖️ Вес пустого чайника: %.1f г\n", emptyWeight);
}

void Scale::setCalibrationFactor(float factor) {
    calibrationFactor = factor;
    LOG_INFO("⚖️ Коэффициент калибровки установлен");
    DPRINTF("⚖️ Новый коэффициент: %f\n", calibrationFactor);
}

// ==================== ВСПОМОГАТЕЛЬНЫЕ ====================
long Scale::getStableRawValue(int samples) {
    long sum = 0;
    int count = 0;
    
    for (int i = 0; i < samples; i++) {
        if (scale.available()) {
            sum += scale.read();
            count++;
        }
        delay(50);
    }
    
    if (count > 0) return sum / count;
    return 0;
}

// ==================== ОБНОВЛЕНИЕ И ФИЛЬТРАЦИЯ ====================
bool Scale::update() {
    if (!scale.available()) {
        currentWeight = 0;
        return false;
    }

    long rawValue = scale.read();
    float newRaw = rawValue * calibrationFactor;
    
    if (newRaw < 0) newRaw = 0;
    
    if (currentWeight > 0 && fabs(newRaw - currentWeight) > MAX_WEIGHT_JUMP) {
        DPRINTF("⚖️ Скачок веса: %.1f -> %.1f (игнорируется)\n", currentWeight, newRaw);
        return true;
    }

    readings[readIndex] = newRaw;
    readIndex = (readIndex + 1) % STABLE_READINGS;

    float sorted[STABLE_READINGS];
    memcpy(sorted, readings, sizeof(readings));
    
    for (int i = 1; i < STABLE_READINGS; i++) {
        float key = sorted[i];
        int j = i - 1;
        while (j >= 0 && sorted[j] > key) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = key;
    }
    
    currentWeight = sorted[STABLE_READINGS / 2];

    return true;
}

// ==================== ПРОВЕРКИ СОСТОЯНИЯ ====================
bool Scale::isReady() {
    return factorCalibrated && scale.available();
}

bool Scale::isKettlePresent() {
    return currentWeight >= (emptyWeight - WEIGHT_HYST);
}

bool Scale::isWeightStable() {
    if (fabs(currentWeight - lastReadWeight) < STABLE_WEIGHT_THRESHOLD) {
        if (millis() - lastStableReadTime > STABLE_TIME_THRESHOLD) {
            return true;
        }
    } else {
        lastStableReadTime = millis();
        lastReadWeight = currentWeight;
    }
    return false;
}

// ==================== РАБОТА С EEPROM ====================
void Scale::saveCalibrationToEEPROM(int addr) {
    if (addr < 0 || addr > 508) return;
    
    eepromAddr = addr;
    EEPROM.write(addr, EEPROM_FLAG_VALUE);
    EEPROM.put(addr + 4, emptyWeight);
    EEPROM.put(addr + 8, calibrationFactor);
    EEPROM.write(addr + 12, factorCalibrated ? EEPROM_FLAG_VALUE : 0);
    EEPROM.commit();
    
    LOG_OK("⚖️ Калибровка сохранена в EEPROM");
}

void Scale::loadCalibrationFromEEPROM(int addr) {
    if (addr < 0 || addr > 508) {
        isCalibrated = false;
        factorCalibrated = false;
        return;
    }
    
    eepromAddr = addr;
    
    if (EEPROM.read(addr) == EEPROM_FLAG_VALUE) {
        EEPROM.get(addr + 4, emptyWeight);
        EEPROM.get(addr + 8, calibrationFactor);
        factorCalibrated = (EEPROM.read(addr + 12) == EEPROM_FLAG_VALUE);
        isCalibrated = true;
        
        LOG_INFO("⚖️ Калибровка загружена из EEPROM");
        DPRINTF("⚖️   Вес пустого: %.1f г, коэф: %f\n", emptyWeight, calibrationFactor);
    } else {
        isCalibrated = false;
        factorCalibrated = false;
        emptyWeight = 0;
        calibrationFactor = DEFAULT_FACTOR;
        LOG_WARN("⚖️ Калибровка не найдена в EEPROM");
    }
}

void Scale::resetCalibration() {
    isCalibrated = false;
    factorCalibrated = false;
    emptyWeight = 0;
    calibrationFactor = DEFAULT_FACTOR;
    
    if (eepromAddr >= 0 && eepromAddr <= 508) {
        EEPROM.write(eepromAddr, 0x00);
        EEPROM.write(eepromAddr + 12, 0x00);
        float zero = 0.0f;
        EEPROM.put(eepromAddr + 4, zero);
        EEPROM.put(eepromAddr + 8, DEFAULT_FACTOR);
        EEPROM.commit();
    }
    LOG_WARN("⚖️ Калибровка сброшена к значениям по умолчанию");
}