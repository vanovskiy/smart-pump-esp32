// файл: Display.h
// Заголовочный файл класса для управления OLED-дисплеем SSD1306
// Отвечает за отрисовку всех экранов интерфейса пользователя
// Добавлена поддержка OTA экранов с прогресс-баром

#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <U8g2lib.h>
#include "config.h"

// ==================== ПРЕДВАРИТЕЛЬНОЕ ОБЪЯВЛЕНИЕ ====================
class StateMachine;

// ==================== ИКОНКИ ДЛЯ ДИСПЛЕЯ ====================
extern const unsigned char icon_power_16x16[];
extern const unsigned char icon_wifi_16x16[];
extern const unsigned char icon_no_wifi_16x16[];
extern const unsigned char cup_20x20[];

/**
 * Класс Display управляет выводом информации на OLED-экран
 * Реализует отрисовку всех состояний системы
 */
class Display {
private:
  // ==================== ОСНОВНЫЕ ОБЪЕКТЫ ====================
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled;
  
  // ==================== СОСТОЯНИЯ ДИСПЛЕЯ ====================
  bool calibrationInProgress = false;
  bool calibrationSuccess = false;
  
  // ==================== УПРАВЛЕНИЕ МИГАНИЕМ ====================
  unsigned long lastBlinkTime = 0;
  bool wifiIconVisible = true;
  
  // ==================== СТАТУС Wi-Fi ====================
  bool isWiFiConfigured = false;
  bool isWiFiConnected = false;
  
  // ==================== ПРИВАТНЫЕ МЕТОДЫ ОТРИСОВКИ ====================
  void drawProgressBar(int x, int y, int w, int h, int p);
  void drawWiFiIcon();
  void drawPowerIcon(bool isOn);
  void drawAPCredentials(SystemState state);
  void drawStringSafe(int x, int y, const String& text, const uint8_t* font, int maxWidth);
  void drawCupsWithIcon(int x, int y, const String& text, const uint8_t* font);
  void drawTextBetweenIcons(int y, const String& text, bool powerIconVisible, const uint8_t* font = u8g2_font_fub14_tf);
  int calculateCupsBlockWidth(const String& text, const uint8_t* font);
  int centerBlock(int blockWidth);
  
  // ==================== МЕТОДЫ ОТРИСОВКИ ЭКРАНОВ ====================
  void drawInitScreen();
  void drawErrorScreen(ErrorType error);
  void drawIdleScreen(bool kettlePresent, float waterVolume, bool powerRelayState);
  void drawFillingScreen(float currentWater, float targetWaterVolume, 
                         float fillStartVolume, bool powerRelayState);
  void drawCalibrationScreen(float currentWeight);
  
  // ==================== УПРАВЛЕНИЕ НЕБЛОКИРУЮЩИМИ ЗАДЕРЖКАМИ ====================
  enum DisplayWaitState {
    WAIT_NONE,
    WAIT_RESET_MESSAGE,
    WAIT_CALIB_SUCCESS,
    WAIT_CALIB_ERROR
  };
  
  DisplayWaitState waitState = WAIT_NONE;
  unsigned long waitStartTime = 0;
  unsigned long waitDuration = 0;
  StateMachine* stateMachine = nullptr;

public:
  // ==================== КОНСТРУКТОР И ИНИЦИАЛИЗАЦИЯ ====================
  Display();
  void begin();
  
  // ==================== ПУБЛИЧНЫЕ МЕТОДЫ ОТРИСОВКИ ====================
  /**
   * Рисует текст, центрированный по горизонтали
   * @param y - вертикальная позиция
   * @param text - текст для отображения
   * @param font - указатель на шрифт (может быть nullptr для текущего)
   */
  void drawCenteredText(int y, const String& text, const uint8_t* font = u8g2_font_fub14_tf);
  
  /**
   * Основной метод обновления дисплея
   */
  void update(SystemState state, ErrorType error, bool kettlePresent, 
              float currentWeight, float targetWeight, float fillStartVolume,
              bool powerRelayState, float emptyWeight);
  
  // ==================== УПРАВЛЕНИЕ РЕЖИМАМИ ====================
  void setCalibrationMode(bool active) { calibrationInProgress = active; }
  void setCalibrationSuccess(bool active) { calibrationSuccess = active; }
  
  // ==================== УПРАВЛЕНИЕ СТАТУСОМ Wi-Fi ====================
  void setWiFiStatus(bool configured, bool connected) { 
    isWiFiConfigured = configured; 
    isWiFiConnected = connected;
  }
  
  // ==================== OTA ЭКРАНЫ ====================
  /**
   * Показать экран OTA обновления
   * @param progress - прогресс в процентах (0-100), -1 для начального экрана
   */
  void showOTAScreen(int progress = -1);
  
  /**
   * Показать экран завершения OTA
   */
  void showOTACompleteScreen();
  
  // ==================== СПЕЦИАЛЬНЫЕ ЭКРАНЫ ====================
  void showResetCountdown(int seconds, bool isFullReset);
  void showResetMessageNonBlocking(bool isFullReset, StateMachine* sm);
  void showCalibrationSuccessNonBlocking(StateMachine* sm);
  void showCalibrationErrorNonBlocking(StateMachine* sm);
  
  // ==================== ОБНОВЛЕНИЕ СОСТОЯНИЙ ====================
  void updateWaiting(StateMachine* sm);
  
  // ==================== СТАТИЧЕСКИЕ УТИЛИТЫ ====================
  static int mlToCups(float ml, int cupVolume = CUP_VOLUME);
  static String formatCupsNumber(int cups);
  static float getWaterVolume(float currentWeight, float emptyWeight);
  static float getTargetWaterVolume(float targetWeight, float emptyWeight);
};

#endif