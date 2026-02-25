// файл: StateMachine.h
// Заголовочный файл класса конечного автомата (State Machine)
// Реализует паттерн State для управления поведением системы в разных режимах

#ifndef STATE_MACHINE_H  // Защита от множественного включения: если не определено
#define STATE_MACHINE_H  // то определяем этот макрос

#include "config.h"       // Подключаем основной конфигурационный файл с пинами и константами
#include "Button.h"       // Подключаем класс для работы с кнопкой
#include "Scale.h"        // Подключаем класс для работы с весами
#include "PumpController.h" // Подключаем класс управления помпой
#include "Display.h"      // Подключаем класс управления дисплеем

// ==================== КОДЫ MQTT КОМАНД ====================
// Эти числовые коды приходят из MQTT топика /devices/pump/filling
// Каждое число соответствует определенной команде от Алисы

#define CMD_ONE_CUP 1      // Команда 1: одна кружка / налить до минимума (500 мл)
#define CMD_TWO_CUPS 2      // Команда 2: две кружки (500 мл)
#define CMD_THREE_CUPS 3    // Команда 3: три кружки (750 мл)
#define CMD_FOUR_CUPS 4     // Команда 4: четыре кружки (1000 мл)
#define CMD_FIVE_CUPS 5     // Команда 5: пять кружек (1250 мл)
#define CMD_SIX_CUPS 6      // Команда 6: шесть кружек (1500 мл)
#define CMD_FULL 7          // Команда 7: полный чайник (1700 мл)
#define CMD_STOP 8          // Команда 8: экстренная остановка налива

// Предварительное объявление класса StateMachine
// Это нужно, потому что класс State ссылается на StateMachine,
// а StateMachine ссылается на State - возникает циклическая зависимость
class StateMachine;

// ==================== БАЗОВЫЙ КЛАСС СОСТОЯНИЯ ====================

/**
 * Базовый класс для всех состояний (паттерн State)
 * Паттерн State позволяет объекту изменять свое поведение в зависимости от состояния
 */
class State {
public:
    // Виртуальный деструктор для корректного удаления производных классов
    // Если у класса есть виртуальные функции, нужен виртуальный деструктор
    virtual ~State() {}
    
    // Чисто виртуальная функция: вызывается при входе в состояние
    // = 0 означает, что метод абстрактный и должен быть реализован в потомках
    virtual void enter(StateMachine* sm) = 0;   // Вызывается при входе в состояние
    
    // Чисто виртуальная функция: вызывается при выходе из состояния
    virtual void exit(StateMachine* sm) = 0;    // Вызывается при выходе из состояния
    
    // Чисто виртуальная функция: вызывается каждый цикл loop()
    virtual void update(StateMachine* sm) = 0;  // Вызывается каждый цикл loop()
    
    // Чисто виртуальная функция: обработка событий кнопки
    virtual void handleButton(StateMachine* sm, Button& button) = 0; // Обработка кнопки
    
    // Чисто виртуальная функция: возвращает имя состояния для отладки
    virtual const char* getName() = 0;          // Возвращает имя состояния
    
    // Виртуальные функции для проверки типа состояния (без использования dynamic_cast)
    // По умолчанию возвращают false, переопределяются в соответствующих классах
    virtual bool isFillingState() { return false; }      // Является ли состояние FillingState?
    virtual bool isIdleState() { return false; }         // Является ли состояние IdleState?
    virtual bool isCalibrationState() { return false; }  // Является ли состояние CalibrationState?
    virtual bool isErrorState() { return false; }        // Является ли состояние ErrorState?
};

// ==================== КОНКРЕТНЫЕ СОСТОЯНИЯ ====================

/**
 * Состояние ожидания (IDLE)
 * Система находится в этом состоянии большую часть времени
 */
class IdleState : public State {  // Наследуемся от базового класса State
private:
    // Время последней проверки уровня воды (для ограничения частоты проверок)
    unsigned long lastPowerCheckTime;
    
    // Флаг: было ли уже обработано нажатие (защита от многократной обработки)
    bool pressedHandled;

public:
    // Конструктор состояния
    IdleState();
    
    // Переопределяем виртуальные методы базового класса
    // override означает, что мы точно переопределяем метод из базового класса
    void enter(StateMachine* sm) override;      // Вход в состояние
    void exit(StateMachine* sm) override;       // Выход из состояния
    void update(StateMachine* sm) override;     // Обновление состояния
    void handleButton(StateMachine* sm, Button& button) override; // Обработка кнопки
    
    // Возвращаем имя состояния (inline реализация прямо в заголовке)
    const char* getName() override { return "IDLE"; }
};

/**
 * Состояние налива воды (FILLING)
 * Активный режим, помпа качает воду
 */
class FillingState : public State {  // Наследуемся от базового класса State
private:
    float targetWeight;        // Целевой вес, который нужно достичь (в граммах)
    float startWeight;         // Вес в момент начала налива (для расчета прогресса)
    unsigned long startTime;   // Время начала налива (для контроля таймаутов)
    bool fillingInit;          // Флаг успешной инициализации налива
    bool emergencyStopFlag;    // Флаг экстренной остановки (по кнопке или MQTT)
    ServoState requiredServoState; // Требуемое положение сервопривода (всегда OVER_KETTLE)

public:
    // Конструктор принимает целевой вес налива
    FillingState(float target);
    
    // Переопределяем виртуальные методы
    void enter(StateMachine* sm) override;
    void exit(StateMachine* sm) override;
    void update(StateMachine* sm) override;
    void handleButton(StateMachine* sm, Button& button) override;
    
    // Возвращаем имя состояния
    const char* getName() override { return "FILLING"; }
    
    // Переопределяем метод проверки типа
    bool isFillingState() override { return true; }

    // Геттеры для получения целей налива (используются дисплеем)
    float getTargetWeight() { return targetWeight; }  // Получить целевой вес
    float getStartWeight() { return startWeight; }    // Получить начальный вес
    
    // Метод для установки флага экстренной остановки
    void emergencyStop() { emergencyStopFlag = true; }
};

/**
 * Состояние калибровки (CALIBRATION)
 * Пошаговый процесс калибровки весов
 */
class CalibrationState : public State {  // Наследуемся от базового класса State
private:
    CalibrationStep step;        // Текущий шаг калибровки (из перечисления в config.h)
    bool pressedHandled;          // Флаг обработки нажатия

public:
    // Конструктор состояния калибровки
    CalibrationState();
    
    // Переопределяем виртуальные методы
    void enter(StateMachine* sm) override;
    void exit(StateMachine* sm) override;
    void update(StateMachine* sm) override;
    void handleButton(StateMachine* sm, Button& button) override;
    
    // Возвращаем имя состояния
    const char* getName() override { return "CALIBRATION"; }
    
    // Установить текущий шаг калибровки
    void setStep(CalibrationStep newStep);
};

/**
 * Состояние ошибки (ERROR)
 * Аварийный режим при возникновении неисправностей
 */
class ErrorState : public State {  // Наследуемся от базового класса State
private:
    ErrorType error;           // Тип ошибки (из перечисления в config.h)
    unsigned long lastBeepTime; // Время последнего звукового сигнала

public:
    // Конструктор принимает тип ошибки
    ErrorState(ErrorType err);
    
    // Переопределяем виртуальные методы
    void enter(StateMachine* sm) override;
    void exit(StateMachine* sm) override;
    void update(StateMachine* sm) override;
    void handleButton(StateMachine* sm, Button& button) override;
    
    // Возвращаем имя состояния
    const char* getName() override { return "ERROR"; }
    
    // Сеттер и геттер для типа ошибки
    void setError(ErrorType err) { error = err; }   // Установить тип ошибки
    ErrorType getError() { return error; }          // Получить тип ошибки
};

// ==================== ГЛАВНЫЙ КЛАСС КОНЕЧНОГО АВТОМАТА ====================

/**
 * Главный класс конечного автомата
 * Управляет переходами между состояниями и делегирует им вызовы
 */
class StateMachine {
private:
    // Указатели на текущее и следующее состояние
    State* currentState;  // Указатель на текущее активное состояние
    State* nextState;      // Указатель на следующее состояние (при переходе)
    
    // Ссылки на компоненты системы (внедрение зависимостей через конструктор)
    Scale& scale;          // Ссылка на объект весов
    PumpController& pump;  // Ссылка на объект управления помпой
    Display& display;      // Ссылка на объект дисплея
    
    // Флаги для обработки переходов между состояниями
    bool stateTransitionPending;  // Флаг: ожидается ли переход в новое состояние?
    unsigned long stateEnterTime; // Время входа в текущее состояние (в миллисекундах)
    
    // Текущая ошибка (хранится отдельно для быстрого доступа без необходимости запрашивать у состояния)
    ErrorType currentError;
    
    // Цели налива (хранятся отдельно для доступа извне без dynamic_cast)
    float fillTarget;  // Текущая цель налива (вес)
    float fillStart;   // Начальный вес при наливе

public:
    /**
     * Конструктор конечного автомата
     * @param s - ссылка на объект весов
     * @param p - ссылка на объект управления помпой
     * @param disp - ссылка на объект дисплея
     */
    StateMachine(Scale& s, PumpController& p, Display& disp);
    
    // ========== Управление состояниями ==========
    
    /**
     * Инициирует переход в новое состояние
     * @param newState - указатель на объект нового состояния
     */
    void transitionTo(State* newState);
    
    /**
     * Обновляет конечный автомат (должен вызываться каждый loop)
     * Выполняет переходы и обновляет текущее состояние
     */
    void update();
    
    /**
     * Передает событие от кнопки текущему состоянию
     * @param button - объект кнопки
     */
    void handleButton(Button& button);
    
    // ========== Геттеры для компонентов ==========
    
    /** @return ссылка на объект весов */
    Scale& getScale() { return scale; }
    
    /** @return ссылка на объект управления помпой */
    PumpController& getPump() { return pump; }
    
    /** @return ссылка на объект дисплея */
    Display& getDisplay() { return display; }
    
    // ========== Вспомогательные методы ==========
    
    /** @return время нахождения в текущем состоянии (мс) */
    unsigned long getTimeInCurrentState() { return millis() - stateEnterTime; }
    
    /** @return указатель на текущее состояние */
    State* getCurrentState() { return currentState; }
    
    // ========== Методы для получения информации о состоянии ==========
    
    /** @return текущее состояние системы в виде перечисления SystemState */
    SystemState getCurrentStateEnum();
    
    /** @return текущий тип ошибки */
    ErrorType getCurrentError() { return currentError; }
    
    /** @param error - установить текущий тип ошибки */
    void setCurrentError(ErrorType error) { currentError = error; }
    
    // ========== Методы для получения целей налива ==========
    
    /** @return целевой вес налива */
    float getFillTarget() { return fillTarget; }
    
    /** @return начальный вес налива */
    float getFillStart() { return fillStart; }
    
    /** @param target - установить целевой вес налива */
    void setFillTarget(float target) { fillTarget = target; }
    
    /** @param start - установить начальный вес налива */
    void setFillStart(float start) { fillStart = start; }
    
    // ========== Методы для создания переходов (фабрика) ==========
    
    /** Перейти в состояние IDLE */
    void toIdle();
    
    /**
     * Перейти в состояние FILLING
     * @param targetWeight - целевой вес налива
     */
    void toFilling(float targetWeight);
    
    /** Перейти в состояние CALIBRATION */
    void toCalibration();
    
    /**
     * Перейти в состояние ERROR
     * @param error - тип ошибки
     */
    void toError(ErrorType error);
    
    // ========== Проверка возможности перехода ==========
    
    /**
     * Проверяет, разрешен ли переход в новое состояние
     * @param newState - целевое состояние
     * @return true если переход разрешен
     */
    bool canTransitionTo(State* newState);

    // ========== Методы для MQTT команд ==========
    
    /**
     * Обрабатывает команду из MQTT
     * @param mode - код команды (1-8)
     */
    void handleMqttCommand(int mode);
    
    /** Экстренно останавливает налив по MQTT команде */
    void emergencyStopFilling();
};

#endif // Конец защиты от множественного включения