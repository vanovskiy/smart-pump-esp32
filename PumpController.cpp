// файл: PumpController.cpp
// Реализация класса управления исполнительными устройствами помпы
// С неблокирующим управлением зуммером

#include "PumpController.h"

// ==================== ВНУТРЕННИЕ КОНСТАНТЫ ====================
#define SERVO_KETTLE_ANGLE 90   // Угол над чайником
#define SERVO_IDLE_ANGLE 0      // Угол в безопасной позиции

// ==================== КОНСТРУКТОР ====================
PumpController::PumpController() {
  pumpRelayState = false;
  powerRelayState = false;
  lastPowerRelayToggleTime = 0;
  currentServoState = SERVO_IDLE;
  targetServoState = SERVO_IDLE;
  servoMoveStartTime = 0;
  
  // Инициализация переменных зуммера
  buzzerState = BUZZER_IDLE;
  buzzerCurrentCount = 0;
  buzzerTargetCount = 0;
  buzzerStepStartTime = 0;
  buzzerOutputHigh = false;
  lastErrorLoopBeepTime = 0;
}

// ==================== ИНИЦИАЛИЗАЦИЯ ====================
void PumpController::begin() {
  pinMode(PIN_PUMP_RELAY, OUTPUT);
  pinMode(PIN_POWER_RELAY, OUTPUT);
  digitalWrite(PIN_PUMP_RELAY, HIGH); // Выкл (реле LOW-актив)
  digitalWrite(PIN_POWER_RELAY, HIGH);

  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  servo.attach(PIN_SERVO);
  moveServoToIdle(); // Начальная позиция
}

// ==================== ОСНОВНОЙ ЦИКЛ ОБНОВЛЕНИЯ ====================
void PumpController::update() {
  // Обновление состояния серво
  if (currentServoState == SERVO_MOVING) {
    if (millis() - servoMoveStartTime >= SERVO_MOVE_TIME) {
      currentServoState = targetServoState;
    }
  }
  
  // Обновление состояния зуммера (неблокирующее)
  updateBuzzer();
}

// ==================== УПРАВЛЕНИЕ РЕЛЕ ПОМПЫ ====================
void PumpController::pumpOn() {
  digitalWrite(PIN_PUMP_RELAY, LOW);
  pumpRelayState = true;
}

void PumpController::pumpOff() {
  digitalWrite(PIN_PUMP_RELAY, HIGH);
  pumpRelayState = false;
}

bool PumpController::isPumpOn() {
  return pumpRelayState;
}

// ==================== УПРАВЛЕНИЕ РЕЛЕ ПИТАНИЯ ЧАЙНИКА ====================
void PumpController::powerOn() {
  if (canTogglePowerRelay()) {
    digitalWrite(PIN_POWER_RELAY, LOW);
    powerRelayState = true;
    lastPowerRelayToggleTime = millis();
  }
}

void PumpController::powerOff() {
  if (canTogglePowerRelay()) {
    digitalWrite(PIN_POWER_RELAY, HIGH);
    powerRelayState = false;
    lastPowerRelayToggleTime = millis();
  }
}

void PumpController::setPowerRelay(bool state) {
  if (state && !powerRelayState) {
    powerOn();
  } else if (!state && powerRelayState) {
    powerOff();
  }
}

bool PumpController::canTogglePowerRelay() {
  return (millis() - lastPowerRelayToggleTime) >= POWER_RELAY_COOLDOWN;
}

// ==================== УПРАВЛЕНИЕ СЕРВОПРИВОДОМ ====================
void PumpController::moveServoToKettle() {
  if (currentServoState == SERVO_MOVING) return;
  
  servo.write(SERVO_KETTLE_ANGLE);
  targetServoState = SERVO_OVER_KETTLE;
  currentServoState = SERVO_MOVING;
  servoMoveStartTime = millis();
}

void PumpController::moveServoToIdle() {
  if (currentServoState == SERVO_MOVING) return;
  
  servo.write(SERVO_IDLE_ANGLE);
  targetServoState = SERVO_IDLE;
  currentServoState = SERVO_MOVING;
  servoMoveStartTime = millis();
}

bool PumpController::isServoInPosition() {
  return currentServoState != SERVO_MOVING;
}

ServoState PumpController::getServoState() {
  return currentServoState;
}

// ==================== ЭКСТРЕННАЯ ОСТАНОВКА ====================
void PumpController::emergencyStop() {
  pumpOff();
  moveServoToIdle();
}

// ==================== НЕБЛОКИРУЮЩЕЕ УПРАВЛЕНИЕ ЗУММЕРОМ ====================

/**
 * Внутренний метод обновления состояния зуммера
 * Вызывается из update() каждый цикл loop()
 * Реализует конечный автомат для неблокирующего воспроизведения сигналов
 */
void PumpController::updateBuzzer() {
  unsigned long now = millis();
  
  switch (buzzerState) {
    case BUZZER_IDLE:
      // Ничего не делаем
      break;
      
    case BUZZER_SHORT_SEQUENCE:
    case BUZZER_LONG_SEQUENCE:
      {
        // Определяем длительность сигнала в зависимости от типа
        unsigned long signalDuration = BUZZER_FEEDBACK;
        if (buzzerState == BUZZER_LONG_SEQUENCE) {
          signalDuration = BUZZER_FEEDBACK * 3;
        }
        
        if (buzzerOutputHigh) {
          // Сигнал сейчас включен - проверяем, не пора ли выключить
          if ((long)(now - buzzerStepStartTime) >= (long)signalDuration) {
            digitalWrite(PIN_BUZZER, LOW);
            buzzerOutputHigh = false;
            buzzerStepStartTime = now;
            buzzerCurrentCount++; // Увеличиваем счетчик после завершения сигнала
          }
        } else {
          // Сигнал выключен - проверяем, нужно ли включить следующий
          if (buzzerCurrentCount >= buzzerTargetCount) {
            // Все сигналы воспроизведены
            buzzerState = BUZZER_IDLE;
          } else {
            // Есть еще сигналы - ждем паузу и включаем следующий
            if (now - buzzerStepStartTime >= BUZZER_FEEDBACK * 2) {
              digitalWrite(PIN_BUZZER, HIGH);
              buzzerOutputHigh = true;
              buzzerStepStartTime = now;
            }
          }
        }
      }
      break;
      
    case BUZZER_ERROR_LOOP:
      // Специальный режим: 3 длинных + 1 короткий каждые 5 секунд
      if ((long)(now - lastErrorLoopBeepTime) > 5000) {
        // Время начать новую последовательность
        lastErrorLoopBeepTime = now;
        
        // Инициируем последовательность: сначала 3 длинных
        buzzerState = BUZZER_LONG_SEQUENCE;
        buzzerTargetCount = 3;
        buzzerCurrentCount = 0;
        buzzerStepStartTime = now;
        buzzerOutputHigh = true;
        digitalWrite(PIN_BUZZER, HIGH);
      }
      break;
  }
}

/**
 * Неблокирующее воспроизведение последовательности коротких сигналов
 * @param count - количество сигналов
 */
void PumpController::beepShortNonBlocking(int count) {
  // Если зуммер уже занят, можно либо игнорировать, либо прервать текущую последовательность
  // Здесь выбираем прерывание - начинаем новую последовательность
  buzzerState = BUZZER_SHORT_SEQUENCE;
  buzzerTargetCount = count;
  buzzerCurrentCount = 0;
  buzzerStepStartTime = millis();
  buzzerOutputHigh = true;
  digitalWrite(PIN_BUZZER, HIGH);
}

/**
 * Неблокирующее воспроизведение последовательности длинных сигналов
 * @param count - количество сигналов
 */
void PumpController::beepLongNonBlocking(int count) {
  buzzerState = BUZZER_LONG_SEQUENCE;
  buzzerTargetCount = count;
  buzzerCurrentCount = 0;
  buzzerStepStartTime = millis();
  buzzerOutputHigh = true;
  digitalWrite(PIN_BUZZER, HIGH);
}

/**
 * Неблокирующий режим для цикла ошибки
 * 3 длинных + 1 короткий сигнал, повторяется каждые 5 секунд
 */
void PumpController::errorBeepLoopNonBlocking() {
  // Просто переключаем состояние в специальный режим
  // Сам цикл будет управляться из updateBuzzer()
  if (buzzerState != BUZZER_ERROR_LOOP) {
    buzzerState = BUZZER_ERROR_LOOP;
    lastErrorLoopBeepTime = millis();
  }
}

// ==================== СТАРЫЕ МЕТОДЫ (СОВМЕСТИМОСТЬ) ====================
// Эти методы содержат delay() и помечены как устаревшие

void PumpController::beepShort(int count) {
  for (int i = 0; i < count; i++) {
    digitalWrite(PIN_BUZZER, HIGH);
    delay(BUZZER_FEEDBACK);
    digitalWrite(PIN_BUZZER, LOW);
    if (i < count - 1) delay(BUZZER_FEEDBACK * 2);
  }
}

void PumpController::beepLong(int count) {
  for (int i = 0; i < count; i++) {
    digitalWrite(PIN_BUZZER, HIGH);
    delay(BUZZER_FEEDBACK * 3);
    digitalWrite(PIN_BUZZER, LOW);
    if (i < count - 1) delay(BUZZER_FEEDBACK * 2);
  }
}

void PumpController::errorBeepLoop() {
  static unsigned long lastBeep = 0;
  if (millis() - lastBeep > 5000) {
    beepLong(3);
    beepShort(1);
    lastBeep = millis();
  }
}