// файл: StateMachine.cpp
// Реализация методов конечного автомата и всех состояний
// Версия с неблокирующим зуммером и защитой от переполнения millis()

#include "StateMachine.h"
#include <Arduino.h>

// ==================== IDLE STATE ====================
IdleState::IdleState() {
    lastPowerCheckTime = 0;
    pressedHandled = false;
}

void IdleState::enter(StateMachine* sm) {
    Serial.println("Entering IDLE state");
    sm->getPump().pumpOff();
    pressedHandled = false;
}

void IdleState::exit(StateMachine* sm) {
    Serial.println("Exiting IDLE state");
}

void IdleState::update(StateMachine* sm) {
    sm->getScale().update();
    
    // Защита от переполнения millis()
    if ((long)(millis() - lastPowerCheckTime) > 1000) {
        lastPowerCheckTime = millis();
        
        float currentWeight = sm->getScale().getCurrentWeight();
        float emptyWeight = sm->getScale().getEmptyWeight();
        float waterWeight = currentWeight - emptyWeight;
        
        if (sm->getScale().isKettlePresent()) {
            if (waterWeight >= MIN_WATER_LEVEL && !sm->getPump().isPumpOn()) {
                sm->getPump().setPowerRelay(true);
            } 
            else if (waterWeight < MIN_WATER_LEVEL - WEIGHT_HYST) {
                sm->getPump().setPowerRelay(false);
            }
        } else {
            sm->getPump().setPowerRelay(false);
        }
    }
}

void IdleState::handleButton(StateMachine* sm, Button& button) {
    if (!button.isPressed()) {
        pressedHandled = false;
    }
    
    if (button.isSingleClick()) {
        Serial.println("IDLE: Single click");
        
        if (sm->getScale().isReady() && sm->getScale().isKettlePresent()) {
            float currentWater = sm->getScale().getCurrentWeight() - sm->getScale().getEmptyWeight();
            float targetWeight;
            
            if (currentWater < MIN_WATER_LEVEL) {
                targetWeight = sm->getScale().getEmptyWeight() + MIN_WATER_LEVEL;
            } else {
                targetWeight = sm->getScale().getCurrentWeight() + CUP_VOLUME;
            }
            
            float maxWeight = sm->getScale().getEmptyWeight() + FULL_WATER_LEVEL;
            if (targetWeight > maxWeight) {
                targetWeight = maxWeight;
            }
            
            sm->toFilling(targetWeight);
        } else {
            sm->getPump().beepShortNonBlocking(2);
        }
        button.resetClicks();
    }
    else if (button.isDoubleClick()) {
        Serial.println("IDLE: Double click");
        
        if (sm->getScale().isReady() && sm->getScale().isKettlePresent()) {
            float targetWeight = sm->getScale().getEmptyWeight() + FULL_WATER_LEVEL;
            sm->toFilling(targetWeight);
        } else {
            sm->getPump().beepShortNonBlocking(2);
        }
        button.resetClicks();
    }
    else if (button.isTripleClick()) {
        Serial.println("IDLE: Triple click - start calibration");
        sm->toCalibration();
        button.resetClicks();
    }
}

// ==================== FILLING STATE ====================
FillingState::FillingState(float target) {
    targetWeight = target;
    startWeight = 0;
    startTime = 0;
    fillingInit = false;
    emergencyStopFlag = false;
    requiredServoState = SERVO_OVER_KETTLE;
}

void FillingState::enter(StateMachine* sm) {
    Serial.println("Entering FILLING state");
    Serial.print("Target weight: ");
    Serial.println(targetWeight);
    
    if (!sm->getScale().isKettlePresent()) {
        sm->getPump().beepShortNonBlocking(2);
        sm->toIdle();
        return;
    }
    
    startTime = millis();
    startWeight = sm->getScale().getCurrentWeight();
    fillingInit = true;
    emergencyStopFlag = false;
    
    sm->getPump().moveServoToKettle();
    sm->getPump().beepShortNonBlocking(1);
}

void FillingState::exit(StateMachine* sm) {
    Serial.println("Exiting FILLING state");
    
    sm->getPump().pumpOff();
    
    if (sm->getPump().getServoState() != SERVO_IDLE) {
        sm->getPump().moveServoToIdle();
    }
}

void FillingState::update(StateMachine* sm) {
    sm->getScale().update();
    
    if (!fillingInit) return;
    
    float currentWeight = sm->getScale().getCurrentWeight();
    
    if (emergencyStopFlag) {
        sm->toIdle();
        return;
    }
    
    if (!sm->getScale().isKettlePresent()) {
        sm->getPump().beepShortNonBlocking(2);
        sm->toError(ERR_NO_FLOW);
        return;
    }
    
    // Защита от переполнения millis()
    if ((long)(millis() - startTime) > (long)PUMP_TIMEOUT) {
        sm->toError(ERR_FILL_TIMEOUT);
        return;
    }
    
    if ((long)(millis() - startTime) > (long)NO_FLOW_TIMEOUT) {
        if (sm->getScale().isWeightStable() && 
            fabs(currentWeight - startWeight) < 10.0f) {
            sm->toError(ERR_NO_FLOW);
            return;
        }
    }
    
    if (sm->getPump().isServoInPosition() && !sm->getPump().isPumpOn()) {
        sm->getPump().pumpOn();
        Serial.println("Pump ON");
    }
    
    if (currentWeight >= targetWeight - WEIGHT_HYST) {
        sm->getPump().beepShortNonBlocking(2);
        sm->toIdle();
        return;
    }
}

void FillingState::handleButton(StateMachine* sm, Button& button) {
    if (button.isLongPress()) {
        Serial.println("FILLING: Long press - emergency stop");
        emergencyStopFlag = true;
        sm->getPump().beepShortNonBlocking(3);
        button.resetClicks();
    }
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
    sm->getScale().update();
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
    Serial.print("Entering ERROR state. Error code: ");
    Serial.println(error);
    
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
    if (!currentState) return;
    
    Serial.printf("MQTT command mode: %d\n", mode);
    
    // Команда СТОП (8) - работает в любом состоянии
    if (mode == CMD_STOP) {
        Serial.println("MQTT: STOP command");
        if (strcmp(currentState->getName(), "FILLING") == 0) {
            emergencyStopFilling();
        } else {
            pump.beepShortNonBlocking(2);
        }
        return;
    }
    
    // Остальные команды работают только в IDLE
    if (strcmp(currentState->getName(), "IDLE") != 0) {
        Serial.println("MQTT: Not in IDLE state, ignoring command");
        pump.beepShortNonBlocking(2);
        return;
    }
    
    // Проверяем наличие чайника
    if (!scale.isReady() || !scale.isKettlePresent()) {
        Serial.println("MQTT: Kettle not present");
        pump.beepShortNonBlocking(2);
        return;
    }
    
    float currentWater = scale.getCurrentWeight() - scale.getEmptyWeight();
    if (currentWater < 0) currentWater = 0;
    
    float targetWeight = scale.getEmptyWeight();
    float maxWeight = scale.getEmptyWeight() + FULL_WATER_LEVEL;
    
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
    
    // Ограничиваем максимальным объемом
    if (targetWeight > maxWeight) {
        targetWeight = maxWeight;
    }
    
    // Проверяем, есть ли смысл наливать
    if (targetWeight <= scale.getCurrentWeight() + 10) {
        Serial.println("MQTT: Target already reached or exceeded");
        pump.beepShortNonBlocking(2);
        return;
    }
    
    // Запускаем налив
    toFilling(targetWeight);
    pump.beepShortNonBlocking(1);
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