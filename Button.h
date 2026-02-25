// файл: Button.h
// Заголовочный файл класса для работы с кнопкой
// Определяет интерфейс для взаимодействия с кнопкой

#ifndef BUTTON_H
#define BUTTON_H

#include "config.h"  // Подключаем конфигурацию с временными константами

/**
 * Класс для работы с тактовой кнопкой
 * Поддерживает:
 * - Антидребезг
 * - Одиночные, двойные и тройные клики
 * - Длительное нажатие (LONG_PRESS)
 * - Очень длительное нажатие (VERY_LONG_PRESS)
 * 
 * Использование:
 *   Button button(PIN_BUTTON);
 *   button.tick(); // вызывать каждый loop
 *   if (button.isSingleClick()) { ... }
 *   button.resetClicks();
 */
class Button {
  private:
    uint8_t pin;                     // Пин кнопки
    bool lastState;                   // Последнее стабильное состояние
    unsigned long lastDebounceTime;    // Время последнего изменения (для антидребезга)
    unsigned long pressStartTime;      // Время начала текущего нажатия

    // Для детекции кликов
    int clickCount;                    // Счетчик кликов в текущей серии
    unsigned long lastClickTime;        // Время последнего клика
    bool isLongPressReported;           // Флаг: было ли сообщено о длительном нажатии
    bool isVeryLongPressReported;       // Флаг: было ли сообщено об очень длительном нажатии

  public:
    /**
     * Конструктор
     * @param buttonPin - пин кнопки
     */
    Button(uint8_t buttonPin);

    /**
     * Основной метод опроса кнопки
     * Должен вызываться каждый цикл loop()
     * Выполняет антидребезг и отслеживание состояний
     */
    void tick();

    /**
     * Проверка текущего состояния кнопки
     * @return true - кнопка нажата в данный момент
     */
    bool isPressed();

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
};

#endif