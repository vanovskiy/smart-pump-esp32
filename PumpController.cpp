// файл: PumpController.cpp
// Реализация класса управления исполнительными устройствами помпы
// Содержит логику работы с реле, сервоприводом и зуммером

#include "PumpController.h"

// ==================== ВНУТРЕННИЕ КОНСТАНТЫ ====================
// Углы поворота сервопривода (настраиваются под конкретную механику)
#define SERVO_KETTLE_ANGLE 90   // Угол, при котором трубка находится над чайником
#define SERVO_IDLE_ANGLE 0      // Угол, при котором трубка в безопасной позиции (сбоку)

// ==================== КОНСТРУКТОР ====================

/**
 * Конструктор PumpController
 * Инициализирует все переменные начальными значениями
 * Реле по умолчанию выключены, серво в состоянии IDLE
 */
PumpController::PumpController() {
    pumpRelayState = false;              // Помпа выключена
    powerRelayState = false;              // Питание чайника выключено
    lastPowerRelayToggleTime = 0;         // Время последнего переключения (пока 0)
    currentServoState = SERVO_IDLE;       // Серво в безопасной позиции
    targetServoState = SERVO_IDLE;         // Целевое состояние совпадает с текущим
    servoMoveStartTime = 0;                // Движение не начато
}

// ==================== ИНИЦИАЛИЗАЦИЯ ====================

/**
 * Настройка пинов и начального состояния оборудования
 * Вызывается один раз в setup()
 */
void PumpController::begin() {
    // Настройка пинов реле как выходов
    pinMode(PIN_PUMP_RELAY, OUTPUT);
    pinMode(PIN_POWER_RELAY, OUTPUT);
    
    // Реле активны низким уровнем (LOW = включено, HIGH = выключено)
    digitalWrite(PIN_PUMP_RELAY, HIGH);   // Помпа выключена
    digitalWrite(PIN_POWER_RELAY, HIGH);  // Питание чайника выключено

    // Настройка пина зуммера
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);        // Зуммер выключен

    // Подключение сервопривода к указанному пину
    servo.attach(PIN_SERVO);
    
    // Устанавливаем серво в безопасную позицию при старте
    moveServoToIdle();
}

/**
 * Обновление состояния сервопривода
 * Проверяет, не истекло ли время движения, и обновляет состояние
 * Должен вызываться каждый цикл loop()
 */
void PumpController::update() {
    // Если серво в состоянии движения
    if (currentServoState == SERVO_MOVING) {
        // Проверяем, прошло ли достаточно времени для завершения движения
        if (millis() - servoMoveStartTime >= SERVO_MOVE_TIME) {
            // Движение завершено - устанавливаем целевое состояние как текущее
            currentServoState = targetServoState;
        }
    }
}

// ==================== УПРАВЛЕНИЕ РЕЛЕ ПОМПЫ ====================

/**
 * Включение реле помпы (начало подачи воды)
 * Реле активны низким уровнем, поэтому LOW = включено
 */
void PumpController::pumpOn() {
    digitalWrite(PIN_PUMP_RELAY, LOW);
    pumpRelayState = true;
}

/**
 * Выключение реле помпы (остановка подачи воды)
 * HIGH = выключено
 */
void PumpController::pumpOff() {
    digitalWrite(PIN_PUMP_RELAY, HIGH);
    pumpRelayState = false;
}

/**
 * Проверка состояния реле помпы
 * @return true - помпа включена, false - выключена
 */
bool PumpController::isPumpOn() {
    return pumpRelayState;
}

// ==================== УПРАВЛЕНИЕ РЕЛЕ ПИТАНИЯ ЧАЙНИКА ====================

/**
 * Включение реле питания чайника
 * Учитывает защиту от частых переключений (cooldown)
 * Если с момента последнего переключения прошло меньше POWER_RELAY_COOLDOWN,
 * включение не произойдет
 */
void PumpController::powerOn() {
    if (canTogglePowerRelay()) {
        digitalWrite(PIN_POWER_RELAY, LOW);
        powerRelayState = true;
        lastPowerRelayToggleTime = millis();  // Запоминаем время переключения
    }
}

/**
 * Выключение реле питания чайника
 * Учитывает защиту от частых переключений
 */
void PumpController::powerOff() {
    if (canTogglePowerRelay()) {
        digitalWrite(PIN_POWER_RELAY, HIGH);
        powerRelayState = false;
        lastPowerRelayToggleTime = millis();  // Запоминаем время переключения
    }
}

/**
 * Установка состояния реле питания (включено/выключено)
 * @param state - желаемое состояние (true = включить, false = выключить)
 */
void PumpController::setPowerRelay(bool state) {
    if (state && !powerRelayState) {
        powerOn();      // Вызов с проверкой cooldown внутри
    } else if (!state && powerRelayState) {
        powerOff();     // Вызов с проверкой cooldown внутри
    }
}

/**
 * Проверка возможности переключения реле питания
 * Защищает реле от слишком частых переключений, которые могут привести к износу
 * 
 * @return true - можно переключать (прошло достаточно времени)
 */
bool PumpController::canTogglePowerRelay() {
    return (millis() - lastPowerRelayToggleTime) >= POWER_RELAY_COOLDOWN;
}

// ==================== УПРАВЛЕНИЕ СЕРВОПРИВОДОМ ====================

/**
 * Перемещение трубки налива в позицию над чайником
 * Устанавливает угол SERVO_KETTLE_ANGLE (90°)
 * Защита от повторного вызова во время движения
 */
void PumpController::moveServoToKettle() {
    // Если серво уже движется - игнорируем команду
    if (currentServoState == SERVO_MOVING) return;
    
    servo.write(SERVO_KETTLE_ANGLE);       // Отправляем команду серво
    targetServoState = SERVO_OVER_KETTLE;  // Целевое состояние - над чайником
    currentServoState = SERVO_MOVING;      // Переходим в состояние движения
    servoMoveStartTime = millis();          // Запоминаем время начала движения
}

/**
 * Перемещение трубки налива в безопасную позицию (сбоку)
 * Устанавливает угол SERVO_IDLE_ANGLE (0°)
 * Защита от повторного вызова во время движения
 */
void PumpController::moveServoToIdle() {
    // Если серво уже движется - игнорируем команду
    if (currentServoState == SERVO_MOVING) return;
    
    servo.write(SERVO_IDLE_ANGLE);         // Отправляем команду серво
    targetServoState = SERVO_IDLE;         // Целевое состояние - безопасная позиция
    currentServoState = SERVO_MOVING;      // Переходим в состояние движения
    servoMoveStartTime = millis();          // Запоминаем время начала движения
}

/**
 * Проверка, достиг ли сервопривод целевой позиции
 * @return true - серво на месте и не движется
 */
bool PumpController::isServoInPosition() {
    // Серво считается в позиции, если оно не в состоянии движения
    return currentServoState != SERVO_MOVING;
}

/**
 * Получение текущего состояния сервопривода
 * @return состояние из перечисления ServoState
 */
ServoState PumpController::getServoState() {
    return currentServoState;
}

// ==================== ЭКСТРЕННАЯ ОСТАНОВКА ====================

/**
 * Экстренная остановка процесса налива
 * Выключает помпу и возвращает трубку в безопасную позицию
 * Питание чайника не трогает - оно управляется отдельно по уровню воды
 */
void PumpController::emergencyStop() {
    pumpOff();              // Немедленно выключаем помпу
    moveServoToIdle();      // Возвращаем трубку в безопасную позицию
}

// ==================== УПРАВЛЕНИЕ ЗУММЕРОМ ====================

/**
 * Подача коротких звуковых сигналов
 * Используется для: успешных действий, подтверждений, уведомлений
 * 
 * @param count - количество сигналов (по умолчанию 1)
 */
void PumpController::beepShort(int count) {
    for (int i = 0; i < count; i++) {
        digitalWrite(PIN_BUZZER, HIGH);            // Включаем зуммер
        delay(BUZZER_FEEDBACK);                    // Держим включенным
        digitalWrite(PIN_BUZZER, LOW);             // Выключаем
        
        // Пауза между сигналами (кроме последнего)
        if (i < count - 1) delay(BUZZER_FEEDBACK * 2);
    }
}

/**
 * Подача длинных звуковых сигналов
 * Используется для: важных событий, предупреждений, сбросов
 * 
 * @param count - количество сигналов (по умолчанию 1)
 */
void PumpController::beepLong(int count) {
    for (int i = 0; i < count; i++) {
        digitalWrite(PIN_BUZZER, HIGH);            // Включаем зуммер
        delay(BUZZER_FEEDBACK * 3);                 // Длинный сигнал (в 3 раза длиннее)
        digitalWrite(PIN_BUZZER, LOW);              // Выключаем
        
        // Пауза между сигналами (кроме последнего)
        if (i < count - 1) delay(BUZZER_FEEDBACK * 2);
    }
}

/**
 * Специальный цикл звуковой сигнализации для режима ошибки
 * Воспроизводит 3 длинных + 1 короткий сигнал каждые 5 секунд
 * Должен вызываться из loop() в состоянии ошибки
 */
void PumpController::errorBeepLoop() {
    static unsigned long lastBeep = 0;
    
    // Каждые 5 секунд
    if (millis() - lastBeep > 5000) {
        beepLong(3);        // 3 длинных сигнала
        beepShort(1);       // 1 короткий
        lastBeep = millis(); // Обновляем время последнего сигнала
    }
}