// файл: Button.cpp
// Реализация класса для работы с тактовой кнопкой
// Обрабатывает дребезг контактов, детектирует одиночные, двойные, тройные клики,
// а также длительные нажатия (long press и very long press)

#include "Button.h"

/*
Конструктор класса Button
 * Инициализирует пин кнопки и внутренние переменные
 * 
 * @param buttonPin - номер пина, к которому подключена кнопка
 */
Button::Button(uint8_t buttonPin) {
    pin = buttonPin;
    pinMode(pin, INPUT_PULLUP);      // Включаем внутренний подтягивающий резистор к питанию
                                     // Кнопка замыкает на GND, поэтому нажатие = LOW
    lastState = HIGH;                 // Начальное состояние - не нажата (HIGH)
    lastDebounceTime = 0;             // Время последнего изменения состояния (для борьбы с дребезгом)
    pressStartTime = 0;               // Время начала текущего нажатия
    clickCount = 0;                   // Счетчик кликов в текущей серии
    lastClickTime = 0;                 // Время последнего клика (для определения таймаута между кликами)
    isLongPressReported = false;       // Флаг: было ли уже сообщено о длительном нажатии
    isVeryLongPressReported = false;   // Флаг: было ли уже сообщено об очень длительном нажатии
}

/*
Основной метод, который должен вызываться каждый цикл loop()
Выполняет опрос состояния кнопки, антидребезг и детекцию событий
*/
void Button::tick() {
    bool currentState = digitalRead(pin);
    unsigned long now = millis();

    if (currentState != lastState) {
        lastDebounceTime = now;
    }

    if ((now - lastDebounceTime) > DEBOUNCE_TIME) {
        if (currentState == LOW) {
            if (pressStartTime == 0) {
                pressStartTime = now;
                isLongPressReported = false;
                isVeryLongPressReported = false;
            }
        } else {
            if (pressStartTime != 0) {
                unsigned long pressDuration = now - pressStartTime;

                if (pressDuration < LONG_PRESS_TIME) {
                    clickCount++;
                    lastClickTime = now;
                }
                pressStartTime = 0;
            }
        }
    }
    lastState = currentState;

    // Обработка таймаута мультикликов с полным сбросом
    if (clickCount > 0 && (now - lastClickTime) > DOUBLE_CLICK_TIME) {
        clickCount = 0;
        // Также сбрасываем флаги long press для чистоты
        isLongPressReported = false;
        isVeryLongPressReported = false;
    }
}

/*
Проверяет, нажата ли кнопка в данный момент
Учитывает антидребезг
@return true - кнопка нажата, false - не нажата
*/
bool Button::isPressed() {
    return (millis() - lastDebounceTime) > DEBOUNCE_TIME && digitalRead(pin) == LOW;
}

/*
Проверяет, было ли длительное нажатие (от LONG_PRESS_TIME до VERY_LONG_PRESS_TIME)
Срабатывает один раз за нажатие
@return true - зарегистрировано длительное нажатие
*/
bool Button::isLongPress() {
    if (pressStartTime > 0 && !isLongPressReported) {
        if (millis() - pressStartTime >= LONG_PRESS_TIME && 
            millis() - pressStartTime < VERY_LONG_PRESS_TIME) {
            isLongPressReported = true;
            return true;
        }
    }
    return false;
}

/*
Проверяет, было ли очень длительное нажатие (более VERY_LONG_PRESS_TIME)
Срабатывает один раз за нажатие
@return true - зарегистрировано очень длительное нажатие
*/
bool Button::isVeryLongPress() {
    if (pressStartTime > 0 && !isVeryLongPressReported) {
        if (millis() - pressStartTime >= VERY_LONG_PRESS_TIME) {
            isVeryLongPressReported = true;
            isLongPressReported = true; // Чтобы не сработало как просто длительное
            return true;
        }
    }
    return false;
}

/*
Проверяет, был ли одиночный клик
Должно вызываться после завершения серии кликов
@return true - зарегистрирован одиночный клик
*/
bool Button::isSingleClick() {
    return clickCount == 1;
}

/*
 Проверяет, был ли двойной клик
 Должно вызываться после завершения серии кликов
 @return true - зарегистрирован двойной клик
 */
bool Button::isDoubleClick() {
    return clickCount == 2;
}

/*
 Проверяет, был ли тройной клик
 Должно вызываться после завершения серии кликов
 @return true - зарегистрирован тройной клик
 */
bool Button::isTripleClick() {
    return clickCount == 3;
}

/*
 Сбрасывает счетчик кликов
 Должен вызываться после обработки мультиклика в основном коде
 */
void Button::resetClicks() {
    clickCount = 0;
}