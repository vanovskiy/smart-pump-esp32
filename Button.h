// файл: Button.h
// Заголовочный файл класса для работы с кнопкой
// Улучшенная версия с гистерезисом и расширенной защитой от дребезга

#ifndef BUTTON_H
#define BUTTON_H

#include "config.h"

// Определение типа callback функции для обработки отпускания удержания
typedef void (*HoldReleaseCallback)(unsigned long holdDuration);

// Определение типа callback функции для мультикликов
typedef void (*MultiClickCallback)(int clickCount);

/**
 * Класс для работы с тактовой кнопкой
 * Поддерживает:
 * - Улучшенный антидребезг с гистерезисом
 * - Одиночные, двойные и тройные клики
 * - Длительное нажатие (LONG_PRESS)
 * - Очень длительное нажатие (VERY_LONG_PRESS)
 * - Callback для удержания кнопки (5+ секунд)
 * - Callback для мультикликов
 */
class Button {
  private:
    uint8_t pin;                     // Пин кнопки
    
    // ===== СЫРЫЕ СОСТОЯНИЯ (без фильтра) =====
    bool lastRawState;                // Последнее сырое состояние (без фильтра)
    unsigned long lastDebounceTime;    // Время последнего изменения сырого состояния
    
    // ===== СТАБИЛЬНЫЕ СОСТОЯНИЯ (с фильтром) =====
    bool lastStableState;              // Последнее стабильное состояние
    unsigned long stableStartTime;     // Время, когда состояние стало стабильным
    
    // ===== ГИСТЕРЕЗИС =====
    static const int HYSTERESIS_COUNT = 3;  // Сколько раз должно подтвердиться состояние
    int stableConfirmCount;                  // Текущий счетчик подтверждений
    
    // ===== ДЛЯ СОВМЕСТИМОСТИ СО СТАРЫМ КОДОМ =====
    bool lastState;                    // Последнее состояние (теперь = lastStableState)
    
    // ===== ДЛЯ ДЕТЕКЦИИ НАЖАТИЙ =====
    unsigned long pressStartTime;      // Время начала текущего нажатия

    // ===== ДЛЯ ДЕТЕКЦИИ КЛИКОВ =====
    int clickCount;                    // Счетчик кликов в текущей серии
    unsigned long lastClickTime;        // Время последнего клика
    bool isLongPressReported;           // Флаг: было ли сообщено о длительном нажатии
    bool isVeryLongPressReported;       // Флаг: было ли сообщено об очень длительном нажатии
    
    // ===== ДЛЯ ОБРАБОТКИ УДЕРЖАНИЯ =====
    HoldReleaseCallback holdReleaseCallback; // Callback при отпускании после удержания
    bool holdActive;                         // Флаг: активно ли удержание
    unsigned long holdStartTime;             // Время начала удержания
    
    // ===== ДЛЯ ОБРАБОТКИ МУЛЬТИКЛИКОВ =====
    MultiClickCallback multiClickCallback;   // Callback для мультикликов

  public:
    /**
     * Конструктор
     * @param buttonPin - пин кнопки
     */
    Button(uint8_t buttonPin);

    /**
     * Основной метод опроса кнопки
     * Должен вызываться каждый цикл loop()
     * Выполняет антидребезг с гистерезисом и отслеживание состояний
     */
    void tick();

    /**
     * Проверка текущего состояния кнопки (с учетом стабильности)
     * @return true - кнопка нажата в данный момент
     */
    bool isPressed();

    /**
     * Проверка, что кнопка стабильно нажата (без дребезга)
     * @return true - кнопка нажата стабильно более 2*DEBOUNCE_TIME
     */
    bool isStablePressed();

    /**
     * Проверка длительного нажатия (3-10 секунд)
     * @return true - было длительное нажатие
     */
    bool isLongPress();

    /**
     * Проверка очень длительного нажатия (>10 секунд)
     * @return true - было очень длительное нажатие
     */
    bool isVeryLongPress();

    /**
     * Проверка одиночного клика
     * @return true - зарегистрирован 1 клик
     */
    bool isSingleClick();

    /**
     * Проверка двойного клика
     * @return true - зарегистрированы 2 клика подряд
     */
    bool isDoubleClick();

    /**
     * Проверка тройного клика
     * @return true - зарегистрированы 3 клика подряд
     */
    bool isTripleClick();

    /**
     * Сброс счетчика кликов
     * Должен вызываться после обработки мультиклика
     */
    void resetClicks();
    
    /**
     * Установка callback для обработки отпускания после удержания
     * @param callback - функция, которая будет вызвана при отпускании кнопки
     *                   после удержания более 5 секунд
     */
    void setHoldCallback(HoldReleaseCallback callback) { 
        holdReleaseCallback = callback; 
    }
    
    /**
     * Установка callback для обработки мультикликов
     * @param callback - функция, которая будет вызвана после завершения серии кликов
     */
    void setMultiClickCallback(MultiClickCallback callback) {
        multiClickCallback = callback;
    }
    
    /**
     * Получить сырое состояние (для отладки)
     */
    bool getRawState() { return lastRawState; }
    
    /**
     * Получить стабильное состояние (для отладки)
     */
    bool getStableState() { return lastStableState; }
};

#endif