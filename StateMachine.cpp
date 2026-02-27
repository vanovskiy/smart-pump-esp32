// файл: StateMachine.cpp
// Реализация методов конечного автомата и всех состояний
// Версия с неблокирующим зуммером и защитой от переполнения millis()

#include "StateMachine.h"
#include "debug.h"
#include <Arduino.h>

static bool checkScaleError(StateMachine* sm, const char* stateName) {
    if (sm == nullptr) {
        Serial.println("ERROR: StateMachine is null in checkScaleError");
        return false;
    }
    
    if (!sm->getScale().isReady()) {
        Serial.printf("[%s] Scale not ready, entering ERROR state\n", stateName);
        sm->toError(ERR_HX711_TIMEOUT);
        return false;
    }
    return true;
}

// ==================== IDLE STATE ====================
IdleState::IdleState() {
    lastPowerCheckTime = 0;
    pressedHandled = false;
    DPRINTLN("🏁 IdleState: создан");
}

void IdleState::enter(StateMachine* sm) {
    DENTER("IdleState::enter");
    LOG_INFO("🏁 Вход в режим ОЖИДАНИЕ");
    sm->getPump().pumpOff();
    pressedHandled = false;
    DEXIT("IdleState::enter");
}

void IdleState::exit(StateMachine* sm) {
    DENTER("IdleState::exit");
    LOG_INFO("🏁 Выход из режима ОЖИДАНИЕ");
    DEXIT("IdleState::exit");
}

void IdleState::update(StateMachine* sm) {
    DENTER("IdleState::update");
    
    // Проверка готовности весов
    if (!checkScaleError(sm, "IDLE")) {
        DEXIT("IdleState::update (scale error)");
        return;
    }
    
    // Проверяем результат update()
    if (!sm->getScale().update()) {
        LOG_ERROR("🏁 Ошибка чтения весов в режиме ожидания!");
        sm->toError(ERR_HX711_TIMEOUT);
        DEXIT("IdleState::update (scale update failed)");
        return;
    }
    
    // Защита от переполнения millis()
    if ((long)(millis() - lastPowerCheckTime) > 1000) {
        lastPowerCheckTime = millis();
        
        float currentWeight = sm->getScale().getCurrentWeight();
        float emptyWeight = sm->getScale().getEmptyWeight();
        float waterWeight = currentWeight - emptyWeight;
        
        DVALF("Текущий вес", currentWeight);
        DVALF("Вес пустого", emptyWeight);
        DVALF("Вес воды", waterWeight);
        
        if (sm->getScale().isKettlePresent()) {
            DPRINTLN("🏁 Чайник на месте");
            
            if (waterWeight >= MIN_WATER_LEVEL && !sm->getPump().isPumpOn()) {
                LOG_OK("🏁 Включение питания чайника (вода ≥ 500мл)");
                sm->getPump().setPowerRelay(true);
            } 
            else if (waterWeight < MIN_WATER_LEVEL - WEIGHT_HYST) {
                LOG_INFO("🏁 Выключение питания чайника (вода < 500мл)");
                sm->getPump().setPowerRelay(false);
            }
        } else {
            DPRINTLN("🏁 Чайник отсутствует");
            sm->getPump().setPowerRelay(false);
        }
    }
    
    DEXIT("IdleState::update");
}

void IdleState::handleButton(StateMachine* sm, Button& button) {
    DENTER("IdleState::handleButton");
    
    if (!button.isPressed()) {
        pressedHandled = false;
    }
    
    if (button.isSingleClick()) {
        LOG_INFO("🏁 Одинарный клик в режиме ожидания");
        
        if (sm->getScale().isReady() && sm->getScale().isKettlePresent()) {
            float currentWater = sm->getScale().getCurrentWeight() - sm->getScale().getEmptyWeight();
            float targetWeight;
            
            if (currentWater < MIN_WATER_LEVEL) {
                targetWeight = sm->getScale().getEmptyWeight() + MIN_WATER_LEVEL;
                LOG_INFO("🏁 Долив до минимального уровня (500мл)");
            } else {
                targetWeight = sm->getScale().getCurrentWeight() + CUP_VOLUME;
                LOG_INFO("🏁 Добавление одной кружки (250мл)");
            }
            
            float maxWeight = sm->getScale().getEmptyWeight() + FULL_WATER_LEVEL;
            if (targetWeight > maxWeight) {
                targetWeight = maxWeight;
                LOG_INFO("🏁 Ограничено максимальным уровнем (1700мл)");
            }
            
            DPRINTF("🏁 Целевой вес: %.1f г\n", targetWeight);
            sm->toFilling(targetWeight);
        } else {
            LOG_WARN("🏁 Невозможно налить: нет чайника или весы не готовы");
            sm->getPump().beepShortNonBlocking(2);
        }
        button.resetClicks();
    }
    else if (button.isDoubleClick()) {
        LOG_INFO("🏁 Двойной клик в режиме ожидания");
        
        if (sm->getScale().isReady() && sm->getScale().isKettlePresent()) {
            float targetWeight = sm->getScale().getEmptyWeight() + FULL_WATER_LEVEL;
            DPRINTF("🏁 Налив до полного: %.1f г\n", targetWeight);
            sm->toFilling(targetWeight);
        } else {
            LOG_WARN("🏁 Невозможно налить: нет чайника или весы не готовы");
            sm->getPump().beepShortNonBlocking(2);
        }
        button.resetClicks();
    }
    else if (button.isTripleClick()) {
        LOG_INFO("🏁 Тройной клик - запуск калибровки");
        sm->toCalibration();
        button.resetClicks();
    }
    
    DEXIT("IdleState::handleButton");
}

// ==================== FILLING STATE ====================
FillingState::FillingState(float target) {
    targetWeight = target;
    startWeight = 0;
    startTime = 0;
    fillingInit = false;
    emergencyStopFlag = false;
    requiredServoState = SERVO_OVER_KETTLE;
    DPRINTF("💧 FillingState: создан с целевым весом %.1f г\n", target);
}

void FillingState::enter(StateMachine* sm) {
    DENTER("FillingState::enter");
    LOG_INFO("💧 Вход в режим НАЛИВ");
    DPRINTF("💧 Целевой вес: %.1f г\n", targetWeight);
    
    if (!sm->getScale().isKettlePresent()) {
        LOG_WARN("💧 Чайник отсутствует! Налив невозможен");
        sm->getPump().beepShortNonBlocking(2);
        sm->toIdle();
        DEXIT("FillingState::enter (no kettle)");
        return;
    }
    
    startTime = millis();
    startWeight = sm->getScale().getCurrentWeight();
    fillingInit = true;
    emergencyStopFlag = false;
    
    DPRINTF("💧 Стартовый вес: %.1f г\n", startWeight);
    DPRINTF("💧 Требуется налить: %.1f г\n", targetWeight - startWeight);
    
    sm->getPump().moveServoToKettle();
    sm->getPump().beepShortNonBlocking(1);
    
    DEXIT("FillingState::enter");
}

void FillingState::exit(StateMachine* sm) {
    DENTER("FillingState::exit");
    LOG_INFO("💧 Выход из режима НАЛИВ");
    
    sm->getPump().pumpOff();
    LOG_INFO("💧 Помпа выключена");
    
    if (sm->getPump().getServoState() != SERVO_IDLE) {
        LOG_INFO("💧 Возврат сервопривода в исходное положение");
        sm->getPump().moveServoToIdle();
    }
    
    DEXIT("FillingState::exit");
}

void FillingState::update(StateMachine* sm) {
    DENTER("FillingState::update");
    
    // Проверка готовности весов
    if (!checkScaleError(sm, "FILLING")) {
        DEXIT("FillingState::update (scale error)");
        return;
    }
    
    // Проверяем результат update()
    if (!sm->getScale().update()) {
        LOG_ERROR("💧 Ошибка чтения весов в режиме налива!");
        sm->toError(ERR_HX711_TIMEOUT);
        DEXIT("FillingState::update (scale update failed)");
        return;
    }
    
    if (!fillingInit) {
        LOG_WARN("💧 Налив не инициализирован");
        DEXIT("FillingState::update (not initialized)");
        return;
    }
    
    float currentWeight = sm->getScale().getCurrentWeight();
    DVALF("Текущий вес", currentWeight);
    DVALF("Целевой вес", targetWeight);
    
    float remaining = targetWeight - currentWeight;
    DVALF("Осталось налить", remaining);
    
    if (emergencyStopFlag) {
        LOG_WARN("💧 Экстренная остановка налива (кнопка/MQTT)");
        sm->toIdle();
        DEXIT("FillingState::update (emergency stop)");
        return;
    }
    
    if (!sm->getScale().isKettlePresent()) {
        LOG_ERROR("💧 Чайник пропал во время налива!");
        sm->getPump().beepShortNonBlocking(2);
        sm->toError(ERR_NO_FLOW);
        DEXIT("FillingState::update (kettle lost)");
        return;
    }
    
    // Защита от переполнения millis()
    unsigned long now = millis();
    unsigned long elapsed = now - startTime;
    DVALUL("Прошло времени", elapsed);
    
    if ((long)elapsed > (long)PUMP_TIMEOUT) {
        LOG_ERROR("💧 Превышено время налива (2 минуты)");
        sm->toError(ERR_FILL_TIMEOUT);
        DEXIT("FillingState::update (timeout)");
        return;
    }
    
    if ((long)elapsed > (long)NO_FLOW_TIMEOUT) {
        if (sm->getScale().isWeightStable() && 
            fabs(currentWeight - startWeight) < 10.0f) {
            LOG_ERROR("💧 Нет потока воды - вес не меняется");
            sm->toError(ERR_NO_FLOW);
            DEXIT("FillingState::update (no flow)");
            return;
        }
    }
    
    if (sm->getPump().isServoInPosition() && !sm->getPump().isPumpOn()) {
        sm->getPump().pumpOn();
        LOG_OK("💧 Помпа включена");
    }
    
    if (currentWeight >= targetWeight - WEIGHT_HYST) {
        LOG_OK("💧 Целевой вес достигнут");
        DPRINTF("💧 Итоговый вес: %.1f г\n", currentWeight);
        sm->getPump().beepShortNonBlocking(2);
        sm->toIdle();
        DEXIT("FillingState::update (target reached)");
        return;
    }
    
    DEXIT("FillingState::update (continuing)");
}

void FillingState::handleButton(StateMachine* sm, Button& button) {
    DENTER("FillingState::handleButton");
    
    if (button.isLongPress()) {
        LOG_WARN("💧 Длительное нажатие - экстренная остановка налива");
        emergencyStopFlag = true;
        sm->getPump().beepShortNonBlocking(3);
        button.resetClicks();
    }
    
    DEXIT("FillingState::handleButton");
}

// ==================== CALIBRATION STATE ====================
CalibrationState::CalibrationState() {
    step = CALIB_WAIT_REMOVE;
    pressedHandled = false;
}

void CalibrationState::enter(StateMachine* sm) {
    Serial.println("Entering CALIBRATION state");
    sm->getPump().pumpOff();
    sm->getPump().setPowerRelay(false);
    
    step = CALIB_WAIT_REMOVE;
    pressedHandled = false;
    
    sm->getDisplay().setCalibrationMode(true);
}

void CalibrationState::exit(StateMachine* sm) {
    Serial.println("Exiting CALIBRATION state");
    sm->getDisplay().setCalibrationMode(false);
}

void CalibrationState::update(StateMachine* sm) {
    // Проверка готовности весов для калибровки
    if (!checkScaleError(sm, "CALIBRATION")) {
        return;
    }
    
    if (!sm->getScale().update()) {
        Serial.println("CALIBRATION: Scale update failed");
        // В режиме калибровки не переходим в ошибку, просто продолжаем
        return;
    }
    // Дисплей обновляется в главном цикле
}

void CalibrationState::handleButton(StateMachine* sm, Button& button) {
    if (!button.isPressed()) {
        pressedHandled = false;
    }
    
    if (button.isPressed() && !pressedHandled) {
        pressedHandled = true;
        
        if (step == CALIB_WAIT_REMOVE) {
            sm->getScale().tare();
            step = CALIB_WAIT_PLACE;
        }
        else if (step == CALIB_WAIT_PLACE) {
            float emptyWeight = sm->getScale().getCurrentWeight();
            
            if (emptyWeight > 100 && emptyWeight < 5000) {
                sm->getScale().calibrateEmpty(emptyWeight);
                sm->getScale().saveCalibrationToEEPROM(0);
                sm->getPump().beepShortNonBlocking(1);
                sm->getDisplay().setCalibrationSuccess(true);
                sm->getDisplay().showCalibrationSuccessNonBlocking(sm);
                // НЕ вызываем sm->toIdle() здесь - это сделает Display после таймера
            } else {
                sm->getPump().beepLongNonBlocking(1);
                sm->getDisplay().showCalibrationErrorNonBlocking(sm);
                // Шаг останется тем же, вернемся к нему после таймера
            }
        }
        
        button.resetClicks();
    }
}

void CalibrationState::setStep(CalibrationStep newStep) {
    step = newStep;
}

// ==================== ERROR STATE ====================
ErrorState::ErrorState(ErrorType err) {
    error = err;
    lastBeepTime = 0;
}

void ErrorState::enter(StateMachine* sm) {
    LOG_ERROR("⚠️ Вход в режим ОШИБКА");
    DPRINTF("⚠️ Код ошибки: %d\n", error);
    
    sm->getPump().pumpOff();
    sm->getPump().setPowerRelay(false);
    sm->getPump().moveServoToIdle();
    
    lastBeepTime = millis();
}

void ErrorState::exit(StateMachine* sm) {
    Serial.println("Exiting ERROR state");
}

void ErrorState::update(StateMachine* sm) {
    // Используем неблокирующий метод для цикла ошибки
    sm->getPump().errorBeepLoopNonBlocking();
}

void ErrorState::handleButton(StateMachine* sm, Button& button) {
    // VERY_LONG_PRESS обрабатывается глобально
}

// ==================== STATE MACHINE ====================
StateMachine::StateMachine(Scale& s, PumpController& p, Display& disp) 
    : scale(s), pump(p), display(disp) {
    currentState = nullptr;
    nextState = nullptr;
    stateTransitionPending = false;
    stateEnterTime = 0;
}

void StateMachine::emergencyStopFilling() {
    if (currentState && strcmp(currentState->getName(), "FILLING") == 0) {
        pump.emergencyStop();
        pump.beepShortNonBlocking(3);
        toIdle();
        Serial.println("Emergency stop from MQTT");
    }
}

void StateMachine::handleMqttCommand(int mode) {
    // ===== ВАЛИДАЦИЯ 1: Проверка допустимости mode =====
    if (mode < 1 || mode > 8) {
        pump.beepShortNonBlocking(2);  // Два сигнала - ошибка
        return;
    }
    if (!currentState) return;
    
    Serial.printf("MQTT command mode: %d\n", mode);
    
    // ===== ВАЛИДАЦИЯ 2: Команда СТОП (8) - работает всегда =====
    if (mode == CMD_STOP) {
        Serial.println("MQTT: STOP command");
        if (strcmp(currentState->getName(), "FILLING") == 0) {
            emergencyStopFilling();
        } else {
            pump.beepShortNonBlocking(2);
        }
        return;
    }
    
    // ===== ВАЛИДАЦИЯ 3: Остальные команды только в IDLE =====
    if (strcmp(currentState->getName(), "IDLE") != 0) {
        Serial.println("MQTT: Not in IDLE state, ignoring command");
        pump.beepShortNonBlocking(2);
        return;
    }
    
    // ===== ВАЛИДАЦИЯ 4: Проверка наличия чайника =====
    if (!scale.isReady() || !scale.isKettlePresent()) {
        Serial.println("MQTT: Kettle not present");
        pump.beepShortNonBlocking(2);
        return;
    }
    
    float currentWater = scale.getCurrentWeight() - scale.getEmptyWeight();
    if (currentWater < 0) currentWater = 0;
    
    float targetWeight = scale.getEmptyWeight();
    float maxWeight = scale.getEmptyWeight() + FULL_WATER_LEVEL;
    
    // ===== ВАЛИДАЦИЯ 5: Обработка конкретной команды =====
    switch (mode) {
        case CMD_ONE_CUP:
            Serial.println("MQTT: One cup / minimum mode");
            if (currentWater < MIN_WATER_LEVEL) {
                targetWeight = scale.getEmptyWeight() + MIN_WATER_LEVEL;
            } else {
                targetWeight = scale.getCurrentWeight() + CUP_VOLUME;
            }
            break;
            
        case CMD_TWO_CUPS:
            Serial.println("MQTT: Two cups (500 ml)");
            targetWeight = scale.getEmptyWeight() + 500.0f;
            break;
            
        case CMD_THREE_CUPS:
            Serial.println("MQTT: Three cups (750 ml)");
            targetWeight = scale.getEmptyWeight() + 750.0f;
            break;
            
        case CMD_FOUR_CUPS:
            Serial.println("MQTT: Four cups (1000 ml)");
            targetWeight = scale.getEmptyWeight() + 1000.0f;
            break;
            
        case CMD_FIVE_CUPS:
            Serial.println("MQTT: Five cups (1250 ml)");
            targetWeight = scale.getEmptyWeight() + 1250.0f;
            break;
            
        case CMD_SIX_CUPS:
            Serial.println("MQTT: Six cups (1500 ml)");
            targetWeight = scale.getEmptyWeight() + 1500.0f;
            break;
            
        case CMD_FULL:
            Serial.println("MQTT: Full kettle (1700 ml)");
            targetWeight = scale.getEmptyWeight() + FULL_WATER_LEVEL;
            break;
            
        default:
            Serial.printf("MQTT: Unknown command mode %d\n", mode);
            pump.beepShortNonBlocking(2);
            return;
    }
    
    // ===== ВАЛИДАЦИЯ 6: Ограничение максимальным объемом =====
    if (targetWeight > maxWeight) {
        targetWeight = maxWeight;
    }
    
    // ===== ВАЛИДАЦИЯ 7: Проверка, есть ли смысл наливать =====
    if (targetWeight <= scale.getCurrentWeight() + 10) {
        Serial.println("MQTT: Target already reached or exceeded");
        pump.beepShortNonBlocking(2);
        return;
    }
    
    // ===== ВСЕ ПРОВЕРКИ ПРОЙДЕНЫ - ЗАПУСКАЕМ НАЛИВ =====
    toFilling(targetWeight);
    pump.beepShortNonBlocking(1); // Один сигнал - команда принята
}

void StateMachine::transitionTo(State* newState) {
    if (!canTransitionTo(newState)) {
        Serial.println("Transition denied!");
        return;
    }
    
    nextState = newState;
    stateTransitionPending = true;
}

void StateMachine::update() {
    if (stateTransitionPending && nextState != nullptr) {
        if (currentState != nullptr) {
            currentState->exit(this);
            delete currentState;
        }
        
        currentState = nextState;
        nextState = nullptr;
        stateTransitionPending = false;
        stateEnterTime = millis();
        
        if (currentState != nullptr) {
            currentState->enter(this);
        }
    }
    
    if (currentState != nullptr) {
        currentState->update(this);
    }
}

void StateMachine::handleButton(Button& button) {
    if (currentState != nullptr) {
        currentState->handleButton(this, button);
    }
}

bool StateMachine::canTransitionTo(State* newState) {
    // Если нет текущего состояния - любой переход разрешен (начальное состояние)
    if (currentState == nullptr) return true;
    
    // Получаем имена текущего и целевого состояний
    const char* currentName = currentState->getName();
    const char* newName = newState->getName();
    
    // ==================== МАТРИЦА РАЗРЕШЕННЫХ ПЕРЕХОДОВ ====================
    // IDLE -> любые состояния разрешены
    if (strcmp(currentName, "IDLE") == 0) {
        // Из IDLE можно перейти в любое состояние
        return true;
    }
    
    // FILLING -> только в IDLE или ERROR
    if (strcmp(currentName, "FILLING") == 0) {
        if (strcmp(newName, "IDLE") == 0 || strcmp(newName, "ERROR") == 0) {
            return true;  // Разрешено: завершение налива или ошибка
        }
        Serial.printf("Transition denied: Cannot go from FILLING to %s\n", newName);
        return false;
    }
    
    // CALIBRATION -> только в IDLE (через таймер) или ERROR
    if (strcmp(currentName, "CALIBRATION") == 0) {
        if (strcmp(newName, "IDLE") == 0 || strcmp(newName, "ERROR") == 0) {
            return true;  // Разрешено: завершение калибровки или ошибка
        }
        Serial.printf("Transition denied: Cannot go from CALIBRATION to %s\n", newName);
        return false;
    }
    
    // ERROR -> только в ERROR (никаких переходов, только перезагрузка)
    if (strcmp(currentName, "ERROR") == 0) {
        if (strcmp(newName, "ERROR") == 0) {
            return true;  // Остаемся в ошибке
        }
        Serial.printf("Transition denied: Cannot exit ERROR state without reboot\n");
        return false;
    }
    
    // По умолчанию запрещаем неизвестные переходы
    Serial.printf("Transition denied: Unknown state combination %s -> %s\n", 
                  currentName, newName);
    return false;
}

void StateMachine::toIdle() {
    transitionTo(new IdleState());
}

void StateMachine::toFilling(float targetWeight) {
    fillTarget = targetWeight;
    transitionTo(new FillingState(targetWeight));
}

void StateMachine::toCalibration() {
    transitionTo(new CalibrationState());
}

void StateMachine::toError(ErrorType error) {
    currentError = error;
    transitionTo(new ErrorState(error));
}

SystemState StateMachine::getCurrentStateEnum() {
    if (currentState == nullptr) return ST_IDLE;
    
    const char* name = currentState->getName();
    if (strcmp(name, "IDLE") == 0) return ST_IDLE;
    if (strcmp(name, "FILLING") == 0) return ST_FILLING;
    if (strcmp(name, "CALIBRATION") == 0) return ST_CALIBRATION;
    if (strcmp(name, "ERROR") == 0) return ST_ERROR;
    
    return ST_IDLE;
}