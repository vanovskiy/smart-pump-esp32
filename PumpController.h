// файл: PumpController.h
// Заголовочный файл класса для управления исполнительными устройствами помпы
// Отвечает за: реле помпы, реле питания чайника, сервопривод, зуммер (неблокирующий)

#ifndef PUMP_CONTROLLER_H
#define PUMP_CONTROLLER_H

#include "config.h"
#include <ESP32Servo.h>

/**
 * Класс PumpController объединяет управление всеми исполнительными устройствами:
 * - Реле помпы (включение/выключение подачи воды)
 * - Реле питания чайника (автоматическое включение при достаточном уровне воды)
 * - Сервопривод (перемещение трубки налива над чайником)
 * - Зуммер (звуковая индикация событий) - неблокирующий режим
 */
class PumpController {
private:
  // ==================== СЕРВОПРИВОД ====================
  Servo servo;                          // Объект сервопривода
  ServoState currentServoState;          // Текущее состояние серво
  ServoState targetServoState;           // Целевое состояние
  unsigned long servoMoveStartTime;      // Время начала движения серво
  
  // ==================== РЕЛЕ ====================
  bool pumpRelayState;                   // Состояние реле помпы
  bool powerRelayState;                   // Состояние реле питания чайника
  unsigned long lastPowerRelayToggleTime; // Время последнего переключения реле питания
  
  // ==================== ЗУММЕР (НЕБЛОКИРУЮЩИЙ) ====================
  /**
   * Состояния конечного автомата зуммера
   */
  enum BuzzerState {
    BUZZER_IDLE,           // Зуммер выключен, ожидание
    BUZZER_SHORT_SEQUENCE, // Воспроизведение последовательности коротких сигналов
    BUZZER_LONG_SEQUENCE,  // Воспроизведение последовательности длинных сигналов
    BUZZER_ERROR_LOOP      // Специальный режим для ошибки (3 длинных + 1 короткий, повтор)
  };
  
  BuzzerState buzzerState = BUZZER_IDLE;     // Текущее состояние зуммера
  int buzzerCurrentCount = 0;                 // Сколько сигналов уже воспроизведено
  int buzzerTargetCount = 0;                  // Сколько сигналов нужно воспроизвести всего
  unsigned long buzzerStepStartTime = 0;      // Время начала текущего шага
  bool buzzerOutputHigh = false;               // Текущее состояние выхода зуммера (HIGH/LOW)
  
  // Для режима ERROR_LOOP
  unsigned long lastErrorLoopBeepTime = 0;    // Время последнего сигнала в цикле ошибки
  
  /**
   * Обновление состояния зуммера (вызывается из update)
   * Содержит конечный автомат для неблокирующего воспроизведения сигналов
   */
  void updateBuzzer();

public:
  // ==================== КОНСТРУКТОР ====================
  PumpController();

  // ==================== ИНИЦИАЛИЗАЦИЯ ====================
  void begin();
  void update();  // Должен вызываться каждый loop() для обновления серво и зуммера

  // ==================== УПРАВЛЕНИЕ ПОМПОЙ ====================
  void pumpOn();
  void pumpOff();
  bool isPumpOn();

  // ==================== УПРАВЛЕНИЕ ПИТАНИЕМ ЧАЙНИКА ====================
  void powerOn();
  void powerOff();
  void setPowerRelay(bool state);
  bool canTogglePowerRelay();
  bool isPowerRelayOn() { return powerRelayState; }

  // ==================== УПРАВЛЕНИЕ СЕРВОПРИВОДОМ ====================
  void moveServoToKettle();
  void moveServoToIdle();
  void emergencyStop();
  bool isServoInPosition();
  ServoState getServoState();

  // ==================== УПРАВЛЕНИЕ ЗУММЕРОМ (НЕБЛОКИРУЮЩИЕ МЕТОДЫ) ====================
  /**
   * Воспроизвести последовательность коротких сигналов (неблокирующий режим)
   * @param count - количество сигналов (по умолчанию 1)
   */
  void beepShortNonBlocking(int count = 1);
  
  /**
   * Воспроизвести последовательность длинных сигналов (неблокирующий режим)
   * @param count - количество сигналов (по умолчанию 1)
   */
  void beepLongNonBlocking(int count = 1);
  
  /**
   * Специальный цикл звуковой индикации для режима ошибки (неблокирующий)
   * 3 длинных + 1 короткий сигнал каждые 5 секунд
   * Должен вызываться из loop() при нахождении в состоянии ошибки
   */
  void errorBeepLoopNonBlocking();
  
  // ==================== СТАРЫЕ МЕТОДЫ (ОСТАВЛЕНЫ ДЛЯ СОВМЕСТИМОСТИ) ====================
  // Внимание: Эти методы содержат delay() и будут удалены в будущем
  // Используйте неблокирующие версии выше
  void beepShort(int count = 1) __attribute__((deprecated("Используйте beepShortNonBlocking")));
  void beepLong(int count = 1) __attribute__((deprecated("Используйте beepLongNonBlocking")));
  void errorBeepLoop() __attribute__((deprecated("Используйте errorBeepLoopNonBlocking")));
};

#endif