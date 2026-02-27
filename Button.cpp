// файл: Button.cpp
// Реализация класса для работы с тактовой кнопкой
// Улучшенная версия с гистерезисом и расширенной защитой от дребезга

#include "Button.h"

Button::Button(uint8_t buttonPin) {
    pin = buttonPin;
    pinMode(pin, INPUT_PULLUP);      // Включаем внутренний подтягивающий резистор
    
    // Инициализация сырых состояний
    lastRawState = HIGH;
    lastDebounceTime = 0;
    
    // Инициализация стабильных состояний
    lastStableState = HIGH;
    stableStartTime = 0;
    stableConfirmCount = 0;
    
    // Для совместимости
    lastState = HIGH;
    
    // Инициализация нажатий
    pressStartTime = 0;
    
    // Инициализация кликов
    clickCount = 0;
    lastClickTime = 0;
    isLongPressReported = false;
    isVeryLongPressReported = false;
    
    // Инициализация удержания
    holdReleaseCallback = nullptr;
    holdActive = false;
    holdStartTime = 0;
    
    // Инициализация мультикликов
    multiClickCallback = nullptr;
    
    Serial.println("Button: инициализирована с гистерезисом");
}

void Button::tick() {
    bool currentRawState = digitalRead(pin);
    unsigned long now = millis();

    // === УЛУЧШЕННЫЙ АНТИДРЕБЕЗГ С ГИСТЕРЕЗИСОМ ===
    
    // Если сырое состояние изменилось
    if (currentRawState != lastRawState) {
        lastDebounceTime = now;
        lastRawState = currentRawState;
        stableConfirmCount = 0;  // Сбрасываем счетчик подтверждений
        
        // Для отладки (можно закомментировать)
        // Serial.printf("Button: сырое изменение -> %s\n", currentRawState ? "HIGH" : "LOW");
    }

    // Если состояние стабильно дольше времени дребезга
    if ((now - lastDebounceTime) > DEBOUNCE_TIME) {
        // Увеличиваем счетчик подтверждений (но не больше максимума)
        if (stableConfirmCount < HYSTERESIS_COUNT) {
            stableConfirmCount++;
        }
        
        // Если получили достаточно подтверждений и состояние отличается от стабильного
        if (stableConfirmCount >= HYSTERESIS_COUNT && currentRawState != lastStableState) {
            
            // Состояние изменилось и подтверждено
            lastStableState = currentRawState;
            stableStartTime = now;
            
            Serial.printf("Button: состояние СТАБИЛЬНО -> %s\n", 
                         currentRawState ? "HIGH (отжата)" : "LOW (нажата)");
            
            if (currentRawState == LOW) {
                // Кнопка нажата (подтверждено)
                if (pressStartTime == 0) {
                    pressStartTime = now;
                    isLongPressReported = false;
                    isVeryLongPressReported = false;
                    holdActive = true;
                    holdStartTime = now;
                    
                    Serial.println("Button: НАЖАТИЕ зарегистрировано");
                }
            } else {
                // Кнопка отпущена (подтверждено)
                if (pressStartTime != 0) {
                    unsigned long pressDuration = now - pressStartTime;
                    
                    // Защита от дребезга при отпускании - игнорируем слишком короткие нажатия
                    if (pressDuration > DEBOUNCE_TIME * 2) {
                        
                        if (pressDuration < LONG_PRESS_TIME) {
                            clickCount++;
                            lastClickTime = now;
                            Serial.printf("Button: КЛИК #%d, длительность=%lu мс\n", 
                                         clickCount, pressDuration);
                        }
                        
                        if (holdActive && pressDuration >= 5000 && holdReleaseCallback) {
                            Serial.printf("Button: УДЕРЖАНИЕ %lu мс\n", pressDuration);
                            holdReleaseCallback(pressDuration);
                        }
                    } else {
                        Serial.printf("Button: слишком короткое нажатие (%lu мс) - игнорируется\n", 
                                     pressDuration);
                    }
                    
                    pressStartTime = 0;
                    holdActive = false;
                }
            }
        }
    }
    
    // Обновляем lastState для обратной совместимости
    lastState = lastStableState;

    // === ОБРАБОТКА ТАЙМАУТА МУЛЬТИКЛИКОВ ===
    if (clickCount > 0 && (now - lastClickTime) > DOUBLE_CLICK_TIME) {
        Serial.printf("Button: серия кликов завершена, всего=%d\n", clickCount);
        
        // Вызываем callback для мультикликов, если он установлен
        if (multiClickCallback) {
            multiClickCallback(clickCount);
        }
        
        clickCount = 0;
    }
}

bool Button::isPressed() {
    // Возвращаем стабильное состояние для надежности
    return lastStableState == LOW;
}

bool Button::isStablePressed() {
    // Проверяем, что кнопка нажата стабильно дольше обычного времени дребезга
    return lastStableState == LOW && 
           (millis() - stableStartTime) > DEBOUNCE_TIME * 2;
}

bool Button::isLongPress() {
    if (pressStartTime > 0 && !isLongPressReported && lastStableState == LOW) {
        unsigned long pressDuration = millis() - pressStartTime;
        if (pressDuration >= LONG_PRESS_TIME && pressDuration < VERY_LONG_PRESS_TIME) {
            isLongPressReported = true;
            Serial.printf("Button: LONG_PRESS detected (%lu мс)\n", pressDuration);
            return true;
        }
    }
    return false;
}

bool Button::isVeryLongPress() {
    if (pressStartTime > 0 && !isVeryLongPressReported && lastStableState == LOW) {
        unsigned long pressDuration = millis() - pressStartTime;
        if (pressDuration >= VERY_LONG_PRESS_TIME) {
            isVeryLongPressReported = true;
            isLongPressReported = true; // Чтобы не сработало как просто длительное
            Serial.printf("Button: VERY_LONG_PRESS detected (%lu мс)\n", pressDuration);
            return true;
        }
    }
    return false;
}

bool Button::isSingleClick() {
    return clickCount == 1;
}

bool Button::isDoubleClick() {
    return clickCount == 2;
}

bool Button::isTripleClick() {
    return clickCount == 3;
}

void Button::resetClicks() {
    if (clickCount > 0) {
        Serial.printf("Button: сброс кликов (%d)\n", clickCount);
        clickCount = 0;
    }
}