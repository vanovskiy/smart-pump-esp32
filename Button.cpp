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
    bool currentState = digitalRead(pin);  // Читаем текущее состояние пина
    unsigned long now = millis();          // Текущее время в миллисекундах

    // ===== 1. АНТИДРЕБЕЗГ =====
    // Если состояние изменилось - запоминаем время изменения
    if (currentState != lastState) {
        lastDebounceTime = now;
    }

    // Если состояние стабильно дольше времени дребезга - считаем его валидным
    if ((now - lastDebounceTime) > DEBOUNCE_TIME) {
        // Обрабатываем стабильное состояние
        if (currentState == LOW) { 
            // КНОПКА НАЖАТА (LOW, потому что INPUT_PULLUP)
            if (pressStartTime == 0) {
                pressStartTime = now;          // Запоминаем время начала нажатия
                isLongPressReported = false;    // Сбрасываем флаги для новой серии
                isVeryLongPressReported = false;
            }
        } else { 
            // КНОПКА ОТПУЩЕНА (HIGH)
            if (pressStartTime != 0) {
                // Нажатие было завершено, теперь анализируем его длительность
                unsigned long pressDuration = now - pressStartTime;

                // Проверяем, не было ли это длительным нажатием
                // Длительные нажатия обрабатываются отдельно через isLongPress/isVeryLongPress
                if (pressDuration < LONG_PRESS_TIME) {
                    // Короткое нажатие - увеличиваем счетчик кликов
                    clickCount++;
                    lastClickTime = now;        // Запоминаем время этого клика
                }
                pressStartTime = 0;              // Сбрасываем время начала нажатия
            }
        }
    }
    lastState = currentState;  // Обновляем последнее состояние для следующей итерации

    // ===== 2. ОБРАБОТКА ТАЙМАУТОВ ДЛЯ МУЛЬТИКЛИКОВ =====
    // Если прошло достаточно времени после последнего клика, значит серия кликов завершена
    // Значение clickCount остается для последующего считывания через методы isSingleClick и т.д.
    if (clickCount > 0 && (now - lastClickTime) > DOUBLE_CLICK_TIME) {
        // Таймаут истек - серия кликов завершена
        // Ничего не делаем, clickCount будет считан и сброшен через resetClicks()
        // Это позволяет основному коду обработать мультиклик после паузы
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