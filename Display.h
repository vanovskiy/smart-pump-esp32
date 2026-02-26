// файл: Display.h
// Заголовочный файл класса для управления OLED-дисплеем SSD1306
// Отвечает за отрисовку всех экранов интерфейса пользователя

#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <U8g2lib.h>
#include "config.h"

// ==================== ПРЕДВАРИТЕЛЬНОЕ ОБЪЯВЛЕНИЕ ====================
// Сообщаем компилятору, что класс StateMachine существует
// без необходимости включать весь заголовочный файл
class StateMachine;

// ==================== ИКОНКИ ДЛЯ ДИСПЛЕЯ ====================
// Иконки хранятся в PROGMEM (памяти программ) для экономии оперативной памяти
// Формат: массив байтов для монохромного дисплея 128x64

/**
 * Иконка питания/включения чайника (16x16 пикселей)
 * Отображается в левом верхнем углу, когда реле питания включено
 */
extern const unsigned char icon_power_16x16[];

/**
 * Иконка Wi-Fi (16x16 пикселей)
 * Отображается в правом верхнем углу при активном подключении
 */
extern const unsigned char icon_wifi_16x16[];

/**
 * Иконка отсутствия Wi-Fi (16x16 пикселей)
 * Отображается, когда нет подключения к Wi-Fi
 */
extern const unsigned char icon_no_wifi_16x16[];

/**
 * Иконка кружки (20x20 пикселей)
 * Отображается рядом с количеством кружек воды
 */
extern const unsigned char cup_20x20[];

/**
 * Класс Display управляет выводом информации на OLED-экран
 * Реализует отрисовку всех состояний системы:
 * - Инициализация
 * - Ожидание (IDLE)
 * - Налив воды (FILLING)
 * - Калибровка (CALIBRATION)
 * - Ошибки (ERROR)
 * - Специальные экраны (сброс, успешная калибровка)
 */
class Display {
private:
  // ==================== ОСНОВНЫЕ ОБЪЕКТЫ ====================
  /**
   * Объект дисплея из библиотеки U8g2
   * SSD1306, 128x64, аппаратный I2C
   */
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled;
  
  // ==================== СОСТОЯНИЯ ДИСПЛЕЯ ====================
  bool calibrationInProgress = false;  // Флаг: активен режим калибровки
  bool calibrationSuccess = false;      // Флаг: калибровка успешно завершена
  
  // ==================== УПРАВЛЕНИЕ МИГАНИЕМ ====================
  unsigned long lastBlinkTime = 0;      // Время последнего переключения иконки
  bool wifiIconVisible = true;           // Флаг видимости мигающей иконки
  
  // ==================== СТАТУС Wi-Fi ====================
  bool isWiFiConfigured = false;         // Настроен ли Wi-Fi (есть сохраненные credentials)
  bool isWiFiConnected = false;          // Подключен ли к Wi-Fi в данный момент
  
  // ==================== ВСПОМОГАТЕЛЬНЫЕ МЕТОДЫ ОТРИСОВКИ ====================
  
  /**
   * Рисует прогресс-бар
   * @param x - координата X левого верхнего угла
   * @param y - координата Y левого верхнего угла
   * @param w - ширина
   * @param h - высота
   * @param p - процент заполнения (0-100)
   */
  void drawProgressBar(int x, int y, int w, int h, int p);
  
  /**
   * Рисует иконку Wi-Fi с учетом состояния подключения
   * Поддерживает мигание в режиме точки доступа
   */
  void drawWiFiIcon();
  
  /**
   * Рисует иконку питания, если реле включено
   * @param isOn - состояние реле
   */
  void drawPowerIcon(bool isOn);
  
  /**
   * Рисует текст по центру экрана
   * @param y - вертикальная позиция
   * @param text - текст для отображения
   * @param font - шрифт (по умолчанию u8g2_font_fub14_tf)
   */
  void drawCenteredText(int y, const String& text, const uint8_t* font = u8g2_font_fub14_tf);
  
  /**
   * Рисует текст между иконками питания и Wi-Fi
   * Автоматически рассчитывает отступы
   * @param y - вертикальная позиция
   * @param text - текст для отображения
   * @param powerIconVisible - видима ли иконка питания (влияет на отступ слева)
   * @param font - шрифт
   */
  void drawTextBetweenIcons(int y, const String& text, bool powerIconVisible, const uint8_t* font = u8g2_font_fub14_tf);
  
  /**
   * Отображает данные точки доступа (SSID/пароль) внизу экрана
   * @param state - текущее состояние системы (чтобы не показывать в неподходящих режимах)
   */
  void drawAPCredentials(SystemState state);
  
  // ==================== МЕТОДЫ ОТРИСОВКИ ЭКРАНОВ ====================
  
  /** Экран инициализации при запуске */
  void drawInitScreen();
  
  /** Экран ошибки */
  void drawErrorScreen(ErrorType error);
  
  /** Экран ожидания (основной рабочий экран) */
  void drawIdleScreen(bool kettlePresent, float waterVolume, bool powerRelayState);
  
  /** Экран процесса налива воды */
  void drawFillingScreen(float currentWater, float targetWaterVolume, 
                         float fillStartVolume, bool powerRelayState);
  
  /** Экран калибровки (пошаговая инструкция) */
  void drawCalibrationScreen(float currentWeight);
  
  /** Экран успешного завершения калибровки */
  //void drawCalibrationSuccessScreen();

  // ==================== УПРАВЛЕНИЕ НЕБЛОКИРУЮЩИМИ ЗАДЕРЖКАМИ ====================
  enum DisplayWaitState {
    WAIT_NONE,           // Нет ожидания
    WAIT_RESET_MESSAGE,  // Ожидание после сообщения о сбросе
    WAIT_CALIB_SUCCESS,  // Ожидание после успешной калибровки
    WAIT_CALIB_ERROR     // Ожидание после ошибки калибровки
  };
  
  DisplayWaitState waitState = WAIT_NONE;  // Текущее состояние ожидания
  unsigned long waitStartTime = 0;          // Время начала ожидания
  unsigned long waitDuration = 0;           // Длительность ожидания в мс
  
  // Указатель на StateMachine для обратного вызова после ожидания
  StateMachine* stateMachine = nullptr;

  void drawStringSafe(int x, int y, const String& text, const uint8_t* font, int maxWidth);
  void drawCupsWithIcon(int x, int y, const String& text, const uint8_t* font);
  int calculateCupsBlockWidth(const String& text, const uint8_t* font);
  int centerBlock(int blockWidth);
  
public:
  // ==================== КОНСТРУКТОР И ИНИЦИАЛИЗАЦИЯ ====================
  
  /** Конструктор, инициализирует объект дисплея */
  Display();
  
  /** Запускает дисплей и устанавливает начальные параметры */
  void begin();
  
  /**
   * Основной метод обновления дисплея
   * Вызывается каждый цикл loop() для отображения актуальной информации
   * @param state - состояние системы
   * @param error - код ошибки (если есть)
   * @param kettlePresent - наличие чайника на весах
   * @param currentWeight - текущий вес
   * @param targetWeight - целевой вес при наливе
   * @param fillStartVolume - начальный объем при наливе
   * @param powerRelayState - состояние реле питания
   * @param emptyWeight - вес пустого чайника (для расчета объема воды)
   */
  void update(SystemState state, ErrorType error, bool kettlePresent, 
              float currentWeight, float targetWeight, float fillStartVolume,
              bool powerRelayState, float emptyWeight);
  
  // ==================== УПРАВЛЕНИЕ РЕЖИМАМИ ====================
  
  /** Включить/выключить режим калибровки */
  void setCalibrationMode(bool active) { calibrationInProgress = active; }
  
  /** Установить флаг успешной калибровки */
  void setCalibrationSuccess(bool active) { calibrationSuccess = active; }
  
  // ==================== УПРАВЛЕНИЕ СТАТУСОМ Wi-Fi ====================
  
  /** Обновить статус Wi-Fi для отображения на дисплее */
  void setWiFiStatus(bool configured, bool connected) { 
    isWiFiConfigured = configured; 
    isWiFiConnected = connected;
  }
  
  // ==================== СПЕЦИАЛЬНЫЕ ЭКРАНЫ ====================
  
  /**
   * Показывает обратный отсчет перед сбросом
   * @param seconds - осталось секунд
   * @param isFullReset - true для полного сброса, false для сброса калибровки
   */
  void showResetCountdown(int seconds, bool isFullReset);
  
  /**
   * Показывает сообщение о выполнении сброса
   * @param isFullReset - true для полного сброса, false для сброса калибровки
   */
  //  void showResetMessage(bool isFullReset);
  
  // ==================== СТАТИЧЕСКИЕ УТИЛИТЫ ====================
  
  /**
   * Преобразует миллилитры в количество кружек
   * @param ml - объем в миллилитрах
   * @param cupVolume - объем одной кружки (по умолчанию из config.h)
   * @return количество полных кружек
   */
  static int mlToCups(float ml, int cupVolume = CUP_VOLUME);
  
  /**
   * Форматирует число кружек для отображения
   * @param cups - количество кружек
   * @return строка с числом
   */
  static String formatCupsNumber(int cups);
  
  /**
   * Вычисляет объем воды (текущий вес минус вес пустого чайника)
   * @param currentWeight - текущий вес
   * @param emptyWeight - вес пустого чайника
   * @return объем воды (не может быть отрицательным)
   */
  static float getWaterVolume(float currentWeight, float emptyWeight);
  
  /**
   * Вычисляет целевой объем воды для налива
   * @param targetWeight - целевой вес
   * @param emptyWeight - вес пустого чайника
   * @return целевой объем воды
   */
  static float getTargetWaterVolume(float targetWeight, float emptyWeight);

  /**
   * Обновление состояния ожидания (должен вызываться из loop)
   * @param sm - указатель на StateMachine для обратного вызова
   */
  void updateWaiting(StateMachine* sm);
  
/**
 * Показать сообщение о сбросе с неблокирующим ожиданием
 * @param isFullReset - тип сброса
 * @param sm - указатель на StateMachine (может быть nullptr)
 */
void showResetMessageNonBlocking(bool isFullReset, StateMachine* sm);

/**
 * Показать сообщение об успешной калибровке с неблокирующим ожиданием
 * @param sm - указатель на StateMachine (может быть nullptr)
 */
void showCalibrationSuccessNonBlocking(StateMachine* sm);

/**
 * Показать сообщение об ошибке калибровки с неблокирующим ожиданием
 * @param sm - указатель на StateMachine (может быть nullptr)
 */
void showCalibrationErrorNonBlocking(StateMachine* sm);

};

#endif