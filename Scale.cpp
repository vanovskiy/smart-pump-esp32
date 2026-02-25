// файл: Scale.cpp
// Реализация класса для работы с тензодатчиком HX711
// Содержит логику фильтрации, калибровки и работы с EEPROM

#include "Scale.h"
#include <EEPROM.h>

// ==================== ВНУТРЕННИЕ КОНСТАНТЫ ====================
#define STABLE_WEIGHT_THRESHOLD 5.0f    // Максимальное изменение веса (г) для признания его стабильным
#define STABLE_TIME_THRESHOLD 2000       // Минимальное время стабильности (мс) для признания веса стабильным
#define MAX_WEIGHT_JUMP 500.0f           // Защита от выбросов: максимальный скачок веса за одно измерение (г)
#define EEPROM_FLAG_VALUE 0xAA            // Маркер, указывающий, что в EEPROM сохранены валидные данные

// ==================== КОНСТРУКТОР ====================

/**
 * Конструктор Scale
 * Инициализирует объект HX711 с заданными пинами и обнуляет все переменные
 * Устанавливает коэффициент калибровки по умолчанию 0.42
 */
Scale::Scale() : 
    scale(PIN_HX711_DT, PIN_HX711_SCK),  // Передаем пины в конструктор HX711
    emptyWeight(0),
    currentWeight(0),
    calibrationFactor(0.42f),            // Коэффициент по умолчанию (требует точной настройки)
    lastReadWeight(0),
    isCalibrated(false),
    lastStableReadTime(0),
    readIndex(0),
    eepromAddr(0)
{
    // Инициализируем кольцевой буфер нулями
    for (int i = 0; i < STABLE_READINGS; i++) readings[i] = 0;
}

// ==================== ИНИЦИАЛИЗАЦИЯ ====================

/**
 * Запуск датчика и тарирование
 * Даем датчику время на стабилизацию (500 мс) и устанавливаем текущий вес как ноль
 * 
 * @return true - датчик успешно инициализирован
 */
bool Scale::begin() {
    // Ждем готовности датчика
    delay(500);
    
    // Тарируем (устанавливаем текущий вес как ноль)
    scale.tare();
    
    Serial.println("Scale initialized");
    Serial.print("Calibration factor: ");
    Serial.println(calibrationFactor);
    
    return true;
}

/**
 * Устанавливает новый коэффициент калибровки
 * Используется после точной калибровки с эталонным грузом
 * 
 * @param factor - новый коэффициент
 */
void Scale::setCalibrationFactor(float factor) {
    calibrationFactor = factor;
    Serial.print("Calibration factor set to: ");
    Serial.println(calibrationFactor);
}

/**
 * Выполняет тарирование (обнуление) весов
 * Устанавливает текущий физический вес как нулевую точку
 * Полезно для компенсации дрейфа нуля
 */
void Scale::tare() {
    scale.tare();
    Serial.println("Scale tared");
}

// ==================== КАЛИБРОВКА ====================

/**
 * Сохраняет вес пустого чайника и помечает датчик как откалиброванный
 * Вызывается пользователем при тройном клике
 * 
 * @param weight - измеренный вес пустого чайника
 */
void Scale::calibrateEmpty(float weight) {
    emptyWeight = weight;
    isCalibrated = true;
    Serial.print("Empty weight calibrated: ");
    Serial.print(emptyWeight);
    Serial.println(" g");
}

/**
 * Возвращает вес пустого чайника
 * @return вес в граммах
 */
float Scale::getEmptyWeight() {
    return emptyWeight;
}

/**
 * Получает сырое (нефильтрованное) значение веса
 * Просто умножает сырые показания АЦП на коэффициент калибровки
 * 
 * @return вес в граммах (без фильтрации)
 */
float Scale::getRawWeight() {
    if (!scale.available()) return 0;
    
    long rawValue = scale.read();
    float weight = rawValue * calibrationFactor;
    return weight;
}

/**
 * Возвращает текущий отфильтрованный вес
 * @return вес в граммах (после скользящего среднего)
 */
float Scale::getCurrentWeight() {
    return currentWeight;
}

// ==================== ИЗМЕРЕНИЕ И ФИЛЬТРАЦИЯ ====================

/**
 * Основной метод обновления веса
 * Должен вызываться каждый цикл loop()
 * 
 * Выполняет:
 * 1. Проверку доступности датчика
 * 2. Чтение сырых данных
 * 3. Защиту от аномальных скачков
 * 4. Фильтрацию скользящим средним
 * 
 * @return true - измерение успешно обновлено
 */
bool Scale::update() {
    // Если датчик не отвечает - сбрасываем вес для безопасности
    if (!scale.available()) {
        currentWeight = 0;
        return false;
    }

    // Читаем сырые данные и преобразуем в граммы
    long rawValue = scale.read();
    float newRaw = rawValue * calibrationFactor;
    
    // Вес не может быть отрицательным
    if (newRaw < 0) newRaw = 0;
    
    // Защита от аномальных скачков (помехи, выбросы)
    if (currentWeight > 0 && fabs(newRaw - currentWeight) > MAX_WEIGHT_JUMP) {
        // Слишком большой скачок - игнорируем это чтение
        return true;
    }

    // Фильтрация скользящим средним (простое усреднение последних STABLE_READINGS измерений)
    readings[readIndex] = newRaw;
    readIndex = (readIndex + 1) % STABLE_READINGS;

    float sum = 0;
    for (int i = 0; i < STABLE_READINGS; i++) {
        sum += readings[i];
    }
    currentWeight = sum / STABLE_READINGS;

    return true;
}

// ==================== ПРОВЕРКИ СОСТОЯНИЯ ====================

/**
 * Проверяет готовность весов к работе
 * @return true - датчик откалиброван и отвечает на запросы
 */
bool Scale::isReady() {
    if (!isCalibrated) return false;
    return scale.available();
}

/**
 * Определяет наличие чайника на весах
 * Использует гистерезис для предотвращения ложных срабатываний при колебаниях
 * 
 * @return true - чайник стоит на весах
 */
bool Scale::isKettlePresent() {
    // Чайник считается присутствующим, если текущий вес >= вес пустого чайника минус гистерезис
    return currentWeight >= (emptyWeight - WEIGHT_HYST);
}

/**
 * Проверяет стабильность веса (отсутствие изменений)
 * Используется для детекции отсутствия потока воды при работающей помпе
 * 
 * @return true - вес не меняется длительное время
 */
bool Scale::isWeightStable() {
    // Если вес изменился менее чем на порог...
    if (fabs(currentWeight - lastReadWeight) < STABLE_WEIGHT_THRESHOLD) {
        // ...и такое состояние длится дольше времени стабильности
        if (millis() - lastStableReadTime > STABLE_TIME_THRESHOLD) {
            return true;  // Вес стабилен
        }
    } else {
        // Вес изменился - сбрасываем таймер стабильности
        lastStableReadTime = millis();
        lastReadWeight = currentWeight;
    }
    return false;
}

// ==================== РАБОТА С EEPROM ====================

/**
 * Сохраняет калибровочные данные в EEPROM
 * Формат хранения:
 * - addr:     флаг (0xAA) - маркер наличия данных
 * - addr+4:   emptyWeight (float, 4 байта)
 * - addr+8:   calibrationFactor (float, 4 байта)
 * 
 * @param addr - начальный адрес в EEPROM (должен быть от 0 до 508)
 */
void Scale::saveCalibrationToEEPROM(int addr) {
    // Защита от выхода за пределы EEPROM (оставляем место для 4+4+флаг)
    if (addr < 0 || addr > 508) return;
    
    eepromAddr = addr;
    EEPROM.write(addr, EEPROM_FLAG_VALUE);        // Записываем флаг
    EEPROM.put(addr + 4, emptyWeight);             // Записываем вес пустого чайника
    EEPROM.put(addr + 8, calibrationFactor);       // Записываем коэффициент
    EEPROM.commit();                                // Фиксируем изменения в памяти
    
    Serial.println("Calibration saved to EEPROM");
}

/**
 * Загружает калибровочные данные из EEPROM
 * Если флаг не совпадает с EEPROM_FLAG_VALUE, данные считаются отсутствующими
 * 
 * @param addr - начальный адрес в EEPROM
 */
void Scale::loadCalibrationFromEEPROM(int addr) {
    // Проверка допустимости адреса
    if (addr < 0 || addr > 508) {
        isCalibrated = false;
        return;
    }
    
    eepromAddr = addr;
    
    // Проверяем наличие сохраненных данных по флагу
    if (EEPROM.read(addr) == EEPROM_FLAG_VALUE) {
        // Данные найдены - загружаем
        EEPROM.get(addr + 4, emptyWeight);
        EEPROM.get(addr + 8, calibrationFactor);
        isCalibrated = true;
        Serial.print("Calibration loaded: empty weight = ");
        Serial.print(emptyWeight);
        Serial.print(" g, factor = ");
        Serial.println(calibrationFactor);
    } else {
        // Данных нет - используем значения по умолчанию
        isCalibrated = false;
        emptyWeight = 0;
        calibrationFactor = 0.42f;
        Serial.println("No calibration found in EEPROM");
    }
}

/**
 * Сбрасывает калибровку
 * Очищает флаг в EEPROM и сбрасывает внутренние переменные
 */
void Scale::resetCalibration() {
    isCalibrated = false;
    emptyWeight = 0;
    calibrationFactor = 0.42f;
    
    // Если адрес был сохранен, стираем флаг в EEPROM
    if (eepromAddr >= 0 && eepromAddr <= 508) {
        EEPROM.write(eepromAddr, 0x00);  // Очищаем флаг
        EEPROM.commit();
        Serial.println("Calibration reset in EEPROM");
    }
}