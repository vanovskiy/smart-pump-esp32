// файл: StateMachine.cpp
// Реализация методов конечного автомата и всех состояний

#include "StateMachine.h"  // Подключаем заголовочный файл
#include <Arduino.h>       // Подключаем Arduino-функции (millis, Serial и т.д.)

// ==================== IDLE STATE ====================
// Реализация методов состояния ожидания

/**
 * Конструктор состояния IdleState
 * Инициализирует переменные начальными значениями
 */
IdleState::IdleState() {
    lastPowerCheckTime = 0;   // Время последней проверки - 0 (еще не проверяли)
    pressedHandled = false;   // Нажатие еще не обработано
}

/**
 * Вызывается при входе в состояние IDLE
 * @param sm - указатель на конечный автомат
 */
void IdleState::enter(StateMachine* sm) {
    // Выводим отладочное сообщение
    Serial.println("Entering IDLE state");
    
    // Выключаем помпу (на всякий случай)
    sm->getPump().pumpOff();
    
    // Сбрасываем флаг обработки нажатия
    pressedHandled = false;
}

/**
 * Вызывается при выходе из состояния IDLE
 * @param sm - указатель на конечный автомат
 */
void IdleState::exit(StateMachine* sm) {
    // Выводим отладочное сообщение
    Serial.println("Exiting IDLE state");
    // При выходе из IDLE ничего особого делать не нужно
}

/**
 * Обновление состояния IDLE (вызывается каждый цикл loop)
 * @param sm - указатель на конечный автомат
 */
void IdleState::update(StateMachine* sm) {
    // Обновляем показания весов
    sm->getScale().update();
    
    // Проверяем уровень воды не чаще раза в секунду
    if (millis() - lastPowerCheckTime > 1000) {
        lastPowerCheckTime = millis();  // Запоминаем время проверки
        
        // Получаем текущие значения
        float currentWeight = sm->getScale().getCurrentWeight();  // Текущий вес
        float emptyWeight = sm->getScale().getEmptyWeight();      // Вес пустого чайника
        float waterWeight = currentWeight - emptyWeight;          // Вес воды
        
        // Если чайник присутствует на весах
        if (sm->getScale().isKettlePresent()) {
            // Если воды достаточно (больше MIN_WATER_LEVEL) и помпа выключена
            if (waterWeight >= MIN_WATER_LEVEL && !sm->getPump().isPumpOn()) {
                sm->getPump().setPowerRelay(true);  // Включаем питание чайника
            } 
            // Если воды меньше минимума (с учетом гистерезиса)
            else if (waterWeight < MIN_WATER_LEVEL - WEIGHT_HYST) {
                sm->getPump().setPowerRelay(false); // Выключаем питание чайника
            }
        } else {
            // Чайника нет - питание точно выключаем
            sm->getPump().setPowerRelay(false);
        }
    }
    
    // Дисплей обновляется в главном цикле, здесь не нужно
}

/**
 * Обработка нажатий кнопки в состоянии IDLE
 * @param sm - указатель на конечный автомат
 * @param button - объект кнопки
 */
void IdleState::handleButton(StateMachine* sm, Button& button) {
    // Если кнопка не нажата, сбрасываем флаг обработки
    if (!button.isPressed()) {
        pressedHandled = false;
    }
    
    // Обработка одиночного клика
    if (button.isSingleClick()) {
        Serial.println("IDLE: Single click");  // Отладочное сообщение
        
        // Проверяем готовность весов и наличие чайника
        if (sm->getScale().isReady() && sm->getScale().isKettlePresent()) {
            // Вычисляем текущий объем воды
            float currentWater = sm->getScale().getCurrentWeight() - sm->getScale().getEmptyWeight();
            float targetWeight;  // Целевой вес
            
            // Если воды меньше минимального уровня
            if (currentWater < MIN_WATER_LEVEL) {
                // Наливаем до минимального уровня
                targetWeight = sm->getScale().getEmptyWeight() + MIN_WATER_LEVEL;
            } else {
                // Иначе добавляем еще одну кружку
                targetWeight = sm->getScale().getCurrentWeight() + CUP_VOLUME;
            }
            
            // Ограничиваем максимальным уровнем
            float maxWeight = sm->getScale().getEmptyWeight() + FULL_WATER_LEVEL;
            if (targetWeight > maxWeight) {
                targetWeight = maxWeight;  // Не больше максимума
            }
            
            // Запускаем налив
            sm->toFilling(targetWeight);
        } else {
            // Если чайника нет - два коротких сигнала
            sm->getPump().beepShort(2);
        }
        // Сбрасываем счетчик кликов
        button.resetClicks();
    }
    // Обработка двойного клика
    else if (button.isDoubleClick()) {
        Serial.println("IDLE: Double click");  // Отладочное сообщение
        
        // Проверяем готовность весов и наличие чайника
        if (sm->getScale().isReady() && sm->getScale().isKettlePresent()) {
            // Целевой вес - полный чайник
            float targetWeight = sm->getScale().getEmptyWeight() + FULL_WATER_LEVEL;
            // Запускаем налив
            sm->toFilling(targetWeight);
        } else {
            // Если чайника нет - два коротких сигнала
            sm->getPump().beepShort(2);
        }
        // Сбрасываем счетчик кликов
        button.resetClicks();
    }
    // Обработка тройного клика
    else if (button.isTripleClick()) {
        Serial.println("IDLE: Triple click - start calibration");  // Отладочное сообщение
        // Запускаем режим калибровки
        sm->toCalibration();
        // Сбрасываем счетчик кликов
        button.resetClicks();
    }
}

// ==================== FILLING STATE ====================
// Реализация методов состояния налива

/**
 * Конструктор состояния FillingState
 * @param target - целевой вес налива
 */
FillingState::FillingState(float target) {
    targetWeight = target;           // Сохраняем целевой вес
    startWeight = 0;                  // Начальный вес пока неизвестен
    startTime = 0;                    // Время старта пока неизвестно
    fillingInit = false;              // Инициализация еще не выполнена
    emergencyStopFlag = false;        // Флаг остановки сброшен
    requiredServoState = SERVO_OVER_KETTLE; // Серво должно быть над чайником
}

/**
 * Вызывается при входе в состояние FILLING
 * @param sm - указатель на конечный автомат
 */
void FillingState::enter(StateMachine* sm) {
    // Выводим отладочные сообщения
    Serial.println("Entering FILLING state");
    Serial.print("Target weight: ");
    Serial.println(targetWeight);
    
    // Проверяем, есть ли чайник на весах
    if (!sm->getScale().isKettlePresent()) {
        // Если чайника нет - два коротких сигнала и возврат в IDLE
        sm->getPump().beepShort(2);
        sm->toIdle();
        return;  // Выходим, дальше не выполняем
    }
    
    // Запоминаем время начала и начальный вес
    startTime = millis();
    startWeight = sm->getScale().getCurrentWeight();
    
    // Устанавливаем флаги
    fillingInit = true;           // Инициализация успешна
    emergencyStopFlag = false;    // Сбрасываем флаг остановки
    
    // Перемещаем серво в позицию над чайником
    sm->getPump().moveServoToKettle();
    
    // Один короткий сигнал - начало налива
    sm->getPump().beepShort(1);
}

/**
 * Вызывается при выходе из состояния FILLING
 * @param sm - указатель на конечный автомат
 */
void FillingState::exit(StateMachine* sm) {
    // Выводим отладочное сообщение
    Serial.println("Exiting FILLING state");
    
    // Выключаем помпу (на всякий случай)
    sm->getPump().pumpOff();
    
    // Если серво не в безопасной позиции, возвращаем его
    if (sm->getPump().getServoState() != SERVO_IDLE) {
        sm->getPump().moveServoToIdle();
    }
}

/**
 * Обновление состояния FILLING (вызывается каждый цикл loop)
 * @param sm - указатель на конечный автомат
 */
void FillingState::update(StateMachine* sm) {
    // Обновляем показания весов
    sm->getScale().update();
    
    // Если инициализация не выполнена - выходим
    if (!fillingInit) return;
    
    // Получаем текущий вес
    float currentWeight = sm->getScale().getCurrentWeight();
    
    // Проверка флага экстренной остановки
    if (emergencyStopFlag) {
        sm->toIdle();  // Переходим в IDLE (помпа уже выключится при выходе)
        return;
    }
    
    // Проверка: чайник все еще на месте?
    if (!sm->getScale().isKettlePresent()) {
        // Если чайник убрали - ошибка "нет потока"
        sm->getPump().beepShort(2);
        sm->toError(ERR_NO_FLOW);
        return;
    }
    
    // Проверка общего таймаута налива (слишком долго)
    if (millis() - startTime > PUMP_TIMEOUT) {
        sm->toError(ERR_FILL_TIMEOUT);  // Ошибка таймаута
        return;
    }
    
    // Проверка отсутствия потока (вес не меняется)
    if ((millis() - startTime) > NO_FLOW_TIMEOUT) {
        // Если вес стабилен И изменение веса меньше 10 грамм
        if (sm->getScale().isWeightStable() && 
            fabs(currentWeight - startWeight) < 10.0f) {
            sm->toError(ERR_NO_FLOW);  // Ошибка отсутствия потока
            return;
        }
    }
    
    // Если серво уже в позиции И помпа еще не включена
    if (sm->getPump().isServoInPosition() && !sm->getPump().isPumpOn()) {
        sm->getPump().pumpOn();  // Включаем помпу
        Serial.println("Pump ON");  // Отладочное сообщение
    }
    
    // Проверка достижения цели (с учетом гистерезиса)
    if (currentWeight >= targetWeight - WEIGHT_HYST) {
        // Два коротких сигнала - успешное завершение
        sm->getPump().beepShort(2);
        sm->toIdle();  // Возвращаемся в IDLE
        return;
    }
    
    // Дисплей обновляется в главном цикле
}

/**
 * Обработка нажатий кнопки в состоянии FILLING
 * @param sm - указатель на конечный автомат
 * @param button - объект кнопки
 */
void FillingState::handleButton(StateMachine* sm, Button& button) {
    // Обработка длительного нажатия (3+ секунд)
    if (button.isLongPress()) {
        Serial.println("FILLING: Long press - emergency stop");  // Отладочное сообщение
        emergencyStopFlag = true;  // Устанавливаем флаг экстренной остановки
        sm->getPump().beepShort(3);  // Три коротких сигнала - остановка
        button.resetClicks();  // Сбрасываем счетчик кликов
    }
}

// ==================== CALIBRATION STATE ====================
// Реализация методов состояния калибровки

/**
 * Конструктор состояния CalibrationState
 */
CalibrationState::CalibrationState() {
    step = CALIB_WAIT_REMOVE;  // Начинаем с шага "уберите чайник"
    pressedHandled = false;      // Нажатие еще не обработано
}

/**
 * Вызывается при входе в состояние CALIBRATION
 * @param sm - указатель на конечный автомат
 */
void CalibrationState::enter(StateMachine* sm) {
    Serial.println("Entering CALIBRATION state");  // Отладочное сообщение
    
    // Выключаем помпу и питание чайника
    sm->getPump().pumpOff();
    sm->getPump().setPowerRelay(false);
    
    // Устанавливаем начальный шаг
    step = CALIB_WAIT_REMOVE;
    pressedHandled = false;
    
    // Включаем режим калибровки на дисплее
    sm->getDisplay().setCalibrationMode(true);
}

/**
 * Вызывается при выходе из состояния CALIBRATION
 * @param sm - указатель на конечный автомат
 */
void CalibrationState::exit(StateMachine* sm) {
    Serial.println("Exiting CALIBRATION state");  // Отладочное сообщение
    
    // Выключаем режим калибровки на дисплее
    sm->getDisplay().setCalibrationMode(false);
}

/**
 * Обновление состояния CALIBRATION (вызывается каждый цикл loop)
 * @param sm - указатель на конечный автомат
 */
void CalibrationState::update(StateMachine* sm) {
    // Просто обновляем показания весов
    sm->getScale().update();
    // Дисплей обновляется в главном цикле
}

/**
 * Обработка нажатий кнопки в состоянии CALIBRATION
 * @param sm - указатель на конечный автомат
 * @param button - объект кнопки
 */
void CalibrationState::handleButton(StateMachine* sm, Button& button) {
    // Если кнопка не нажата, сбрасываем флаг обработки
    if (!button.isPressed()) {
        pressedHandled = false;
    }
    
    // Если кнопка нажата и нажатие еще не обработано
    if (button.isPressed() && !pressedHandled) {
        pressedHandled = true;  // Помечаем как обработанное
        
        // Шаг 1: убрать чайник
        if (step == CALIB_WAIT_REMOVE) {
            // Тарируем весы (обнуляем)
            sm->getScale().tare();
            // Переходим к шагу "поставить пустой чайник"
            step = CALIB_WAIT_PLACE;
        }
        // Шаг 2: поставить пустой чайник
        else if (step == CALIB_WAIT_PLACE) {
            // Получаем текущий вес (это вес пустого чайника)
            float emptyWeight = sm->getScale().getCurrentWeight();
            
            // Проверяем, что вес реалистичный (100-5000 грамм)
            if (emptyWeight > 100 && emptyWeight < 5000) {
                // Сохраняем вес пустого чайника
                sm->getScale().calibrateEmpty(emptyWeight);
                // Сохраняем калибровку в EEPROM
                sm->getScale().saveCalibrationToEEPROM(0);
                // Один короткий сигнал - успех
                sm->getPump().beepShort(1);
                // Показываем сообщение об успешной калибровке
                sm->getDisplay().setCalibrationSuccess(true);
                delay(2000);  // Ждем 2 секунды
                sm->getDisplay().setCalibrationSuccess(false);
                // Возвращаемся в IDLE
                sm->toIdle();
            } else {
                // Вес нереалистичный - длинный сигнал ошибки
                sm->getPump().beepLong(1);
                delay(2000);  // Ждем 2 секунды
                // Остаемся на том же шаге
                step = CALIB_WAIT_PLACE;
            }
        }
        
        // Сбрасываем счетчик кликов
        button.resetClicks();
    }
}

/**
 * Устанавливает текущий шаг калибровки
 * @param newStep - новый шаг
 */
void CalibrationState::setStep(CalibrationStep newStep) {
    step = newStep;
}

// ==================== ERROR STATE ====================
// Реализация методов состояния ошибки

/**
 * Конструктор состояния ErrorState
 * @param err - тип ошибки
 */
ErrorState::ErrorState(ErrorType err) {
    error = err;          // Сохраняем тип ошибки
    lastBeepTime = 0;     // Время последнего сигнала - 0
}

/**
 * Вызывается при входе в состояние ERROR
 * @param sm - указатель на конечный автомат
 */
void ErrorState::enter(StateMachine* sm) {
    // Выводим отладочное сообщение с кодом ошибки
    Serial.print("Entering ERROR state. Error code: ");
    Serial.println(error);
    
    // Аварийно отключаем все исполнительные устройства
    sm->getPump().pumpOff();           // Выключаем помпу
    sm->getPump().setPowerRelay(false); // Выключаем питание чайника
    sm->getPump().moveServoToIdle();    // Возвращаем серво в безопасную позицию
    
    // Запоминаем время входа в состояние ошибки
    lastBeepTime = millis();
}

/**
 * Вызывается при выходе из состояния ERROR
 * @param sm - указатель на конечный автомат
 */
void ErrorState::exit(StateMachine* sm) {
    Serial.println("Exiting ERROR state");  // Отладочное сообщение
}

/**
 * Обновление состояния ERROR (вызывается каждый цикл loop)
 * @param sm - указатель на конечный автомат
 */
void ErrorState::update(StateMachine* sm) {
    // Периодическое звуковое оповещение об ошибке
    if (millis() - lastBeepTime > 5000) {  // Каждые 5 секунд
        sm->getPump().beepLong(3);          // 3 длинных сигнала
        sm->getPump().beepShort(1);         // 1 короткий
        lastBeepTime = millis();             // Обновляем время
    }
    
    // Дисплей обновляется в главном цикле
}

/**
 * Обработка нажатий кнопки в состоянии ERROR
 * @param sm - указатель на конечный автомат
 * @param button - объект кнопки
 */
void ErrorState::handleButton(StateMachine* sm, Button& button) {
    // VERY_LONG_PRESS обрабатывается глобально в основном файле
    // Здесь ничего не делаем - из ошибки можно выйти только перезагрузкой
}

// ==================== STATE MACHINE ====================
// Реализация методов главного класса конечного автомата

/**
 * Конструктор StateMachine
 * @param s - ссылка на объект весов
 * @param p - ссылка на объект управления помпой
 * @param disp - ссылка на объект дисплея
 */
StateMachine::StateMachine(Scale& s, PumpController& p, Display& disp) 
    : scale(s), pump(p), display(disp) {  // Инициализация ссылок
    currentState = nullptr;           // Изначально нет текущего состояния
    nextState = nullptr;               // Нет следующего состояния
    stateTransitionPending = false;    // Нет ожидающих переходов
    stateEnterTime = 0;                 // Время входа не определено
}

/**
 * Экстренная остановка налива по MQTT команде
 */
void StateMachine::emergencyStopFilling() {
    // Проверяем, есть ли текущее состояние и является ли оно FILLING
    if (currentState && strcmp(currentState->getName(), "FILLING") == 0) {
        pump.emergencyStop();           // Экстренная остановка помпы
        pump.beepShort(3);               // Три коротких сигнала
        toIdle();                        // Переходим в IDLE
        Serial.println("Emergency stop from MQTT");  // Отладочное сообщение
    }
}

/**
 * Обработка MQTT команд
 * @param mode - код команды (1-8)
 */
void StateMachine::handleMqttCommand(int mode) {
    // Если нет текущего состояния - игнорируем
    if (!currentState) return;
    
    // Выводим полученную команду
    Serial.printf("MQTT command mode: %d\n", mode);
    
    // Команда СТОП (8) - работает в любом состоянии
    if (mode == CMD_STOP) {
        Serial.println("MQTT: STOP command");
        // Если мы в состоянии налива - останавливаем
        if (strcmp(currentState->getName(), "FILLING") == 0) {
            emergencyStopFilling();  // Экстренная остановка
            pump.beepShort(3);        // Три коротких сигнала
        } else {
            pump.beepShort(2);        // Два сигнала - нечего останавливать
        }
        return;  // Выходим
    }
    
    // Остальные команды работают только в IDLE
    if (strcmp(currentState->getName(), "IDLE") != 0) {
        Serial.println("MQTT: Not in IDLE state, ignoring command");
        pump.beepShort(2);  // Два сигнала - команда отклонена
        return;
    }
    
    // Проверяем наличие чайника
    if (!scale.isReady() || !scale.isKettlePresent()) {
        Serial.println("MQTT: Kettle not present");
        pump.beepShort(2);  // Два сигнала - чайника нет
        return;
    }
    
    // Вычисляем текущий объем воды
    float currentWater = scale.getCurrentWeight() - scale.getEmptyWeight();
    if (currentWater < 0) currentWater = 0;  // Защита от отрицательных значений
    
    // Базовый целевой вес - вес пустого чайника
    float targetWeight = scale.getEmptyWeight();
    // Максимально возможный вес
    float maxWeight = scale.getEmptyWeight() + FULL_WATER_LEVEL;
    
    // Обрабатываем команду в зависимости от режима
    switch (mode) {
        case CMD_ONE_CUP:  // 1 - Одна кружка/минимум
            Serial.println("MQTT: One cup / minimum mode");
            if (currentWater < MIN_WATER_LEVEL) {
                // Если меньше 500 мл, наливаем до 500
                targetWeight = scale.getEmptyWeight() + MIN_WATER_LEVEL;
            } else {
                // Если больше 500 мл, добавляем еще одну кружку (250 мл)
                targetWeight = scale.getCurrentWeight() + CUP_VOLUME;
            }
            break;
            
        case CMD_TWO_CUPS:  // 2 - Две кружки (500 мл)
            Serial.println("MQTT: Two cups (500 ml)");
            targetWeight = scale.getEmptyWeight() + 500.0f;
            break;
            
        case CMD_THREE_CUPS:  // 3 - Три кружки (750 мл)
            Serial.println("MQTT: Three cups (750 ml)");
            targetWeight = scale.getEmptyWeight() + 750.0f;
            break;
            
        case CMD_FOUR_CUPS:  // 4 - Четыре кружки (1000 мл)
            Serial.println("MQTT: Four cups (1000 ml)");
            targetWeight = scale.getEmptyWeight() + 1000.0f;
            break;
            
        case CMD_FIVE_CUPS:  // 5 - Пять кружек (1250 мл)
            Serial.println("MQTT: Five cups (1250 ml)");
            targetWeight = scale.getEmptyWeight() + 1250.0f;
            break;
            
        case CMD_SIX_CUPS:  // 6 - Шесть кружек (1500 мл)
            Serial.println("MQTT: Six cups (1500 ml)");
            targetWeight = scale.getEmptyWeight() + 1500.0f;
            break;
            
        case CMD_FULL:  // 7 - Полный чайник (1700 мл)
            Serial.println("MQTT: Full kettle (1700 ml)");
            targetWeight = scale.getEmptyWeight() + FULL_WATER_LEVEL;
            break;
            
        default:  // Неизвестная команда
            Serial.printf("MQTT: Unknown command mode %d\n", mode);
            pump.beepShort(2);  // Два сигнала - ошибка
            return;
    }
    
    // Ограничиваем максимальным объемом
    if (targetWeight > maxWeight) {
        targetWeight = maxWeight;
    }
    
    // Проверяем, есть ли смысл наливать
    // Защита от налива в уже полный чайник (допуск 10 грамм)
    if (targetWeight <= scale.getCurrentWeight() + 10) {
        Serial.println("MQTT: Target already reached or exceeded");
        pump.beepShort(2);  // Два сигнала - цель уже достигнута
        return;
    }
    
    // Запускаем налив
    toFilling(targetWeight);
    pump.beepShort(1);  // Один сигнал - команда принята
}

/**
 * Инициирует переход в новое состояние
 * @param newState - указатель на объект нового состояния
 */
void StateMachine::transitionTo(State* newState) {
    // Проверяем, разрешен ли переход
    if (!canTransitionTo(newState)) {
        Serial.println("Transition denied!");  // Отладочное сообщение
        return;  // Выходим, переход не выполняется
    }
    
    // Запоминаем следующее состояние
    nextState = newState;
    // Устанавливаем флаг ожидающего перехода
    stateTransitionPending = true;
}

/**
 * Обновляет конечный автомат (должен вызываться каждый loop)
 * Выполняет переходы и обновляет текущее состояние
 */
void StateMachine::update() {
    // Если есть ожидающий переход и следующее состояние задано
    if (stateTransitionPending && nextState != nullptr) {
        // Если есть текущее состояние
        if (currentState != nullptr) {
            // Вызываем exit для текущего состояния
            currentState->exit(this);
            // Удаляем текущее состояние (освобождаем память)
            delete currentState;
        }
        
        // Устанавливаем новое текущее состояние
        currentState = nextState;
        // Очищаем следующее состояние
        nextState = nullptr;
        // Сбрасываем флаг перехода
        stateTransitionPending = false;
        // Запоминаем время входа в состояние
        stateEnterTime = millis();
        
        // Если новое состояние задано
        if (currentState != nullptr) {
            // Вызываем enter для нового состояния
            currentState->enter(this);
        }
    }
    
    // Если есть текущее состояние
    if (currentState != nullptr) {
        // Вызываем update текущего состояния
        currentState->update(this);
    }
}

/**
 * Передает событие от кнопки текущему состоянию
 * @param button - объект кнопки
 */
void StateMachine::handleButton(Button& button) {
    // Если есть текущее состояние
    if (currentState != nullptr) {
        // Вызываем обработчик кнопки текущего состояния
        currentState->handleButton(this, button);
    }
}

/**
 * Проверяет, разрешен ли переход в новое состояние
 * @param newState - целевое состояние
 * @return true если переход разрешен (всегда true в текущей реализации)
 */
bool StateMachine::canTransitionTo(State* newState) {
    // В текущей реализации разрешаем любые переходы
    return true;
}

/**
 * Перейти в состояние IDLE
 */
void StateMachine::toIdle() {
    // Создаем новое состояние IdleState и инициируем переход
    transitionTo(new IdleState());
}

/**
 * Перейти в состояние FILLING
 * @param targetWeight - целевой вес налива
 */
void StateMachine::toFilling(float targetWeight) {
    // Сохраняем цель налива для доступа извне
    fillTarget = targetWeight;
    // Создаем новое состояние FillingState и инициируем переход
    transitionTo(new FillingState(targetWeight));
}

/**
 * Перейти в состояние CALIBRATION
 */
void StateMachine::toCalibration() {
    // Создаем новое состояние CalibrationState и инициируем переход
    transitionTo(new CalibrationState());
}

/**
 * Перейти в состояние ERROR
 * @param error - тип ошибки
 */
void StateMachine::toError(ErrorType error) {
    // Сохраняем тип ошибки
    currentError = error;
    // Создаем новое состояние ErrorState и инициируем переход
    transitionTo(new ErrorState(error));
}

/**
 * Получить текущее состояние системы в виде перечисления SystemState
 * @return значение из перечисления SystemState
 */
SystemState StateMachine::getCurrentStateEnum() {
    // Если нет текущего состояния - возвращаем IDLE
    if (currentState == nullptr) return ST_IDLE;
    
    // Сравниваем по имени класса (простой способ)
    const char* name = currentState->getName();
    if (strcmp(name, "IDLE") == 0) return ST_IDLE;
    if (strcmp(name, "FILLING") == 0) return ST_FILLING;
    if (strcmp(name, "CALIBRATION") == 0) return ST_CALIBRATION;
    if (strcmp(name, "ERROR") == 0) return ST_ERROR;
    
    // По умолчанию (если имя не распознано)
    return ST_IDLE;
}