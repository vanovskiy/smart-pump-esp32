// файл: Display.cpp
// Реализация класса для управления OLED-дисплеем SSD1306
// Содержит всю логику отрисовки экранов интерфейса
// Добавлена поддержка OTA экранов с прогресс-баром

#include "Display.h"
#include <WiFi.h>
#include "StateMachine.h"

// ==================== ИКОНКИ (ДАННЫЕ) ====================
const unsigned char icon_power_16x16[] PROGMEM = {
  0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x98, 0x19,
  0x8c, 0x31, 0x86, 0x61, 0x82, 0x41, 0x83, 0xc1,
  0x03, 0xc0, 0x03, 0xc0, 0x03, 0xc0, 0x02, 0x40,
  0x06, 0x60, 0x0c, 0x30, 0x78, 0x1e, 0xe0, 0x07
};

const unsigned char icon_wifi_16x16[] PROGMEM = {
  0x00, 0x00, 0xC0, 0x03, 0xF8, 0x1F,
  0x0C, 0x30, 0x06, 0x60, 0xC3, 0xC3,
  0x70, 0x0E, 0x18, 0x18, 0x0C, 0x30,
  0x80, 0x01, 0xE0, 0x07, 0x30, 0x0C,
  0x00, 0x00, 0x80, 0x01, 0x80, 0x01,
  0x00, 0x00
};

const unsigned char icon_no_wifi_16x16[] PROGMEM = {
  0x00, 0x00, 0xC6, 0x03, 0xFE, 0x1E,
  0x1C, 0x30, 0x3A, 0x60, 0x73, 0xC3,
  0xe0, 0x0e, 0xd8, 0x19, 0x8c, 0x33,
  0x00, 0x07, 0x70, 0x0e, 0x38, 0x1c,
  0x00, 0x38, 0x80, 0x71, 0x80, 0x61,
  0x00, 0x00
};

const unsigned char cup_20x20[] PROGMEM = {
  0xfe, 0x3f, 0xf0, 0xff, 0x7f, 0xf0,
  0x03, 0x60, 0xf0, 0x03, 0xe0, 0xf3,
  0x03, 0x60, 0xf7, 0x03, 0x60, 0xf4,
  0x03, 0x60, 0xfc, 0x03, 0x60, 0xfc,
  0x03, 0x60, 0xfc, 0x03, 0x60, 0xfc,
  0x03, 0x60, 0xfc, 0x03, 0x60, 0xfc,
  0x03, 0x60, 0xfc, 0x03, 0x60, 0xf6,
  0x03, 0xe0, 0xf7, 0x03, 0xe0, 0xf3,
  0x07, 0x70, 0xf0, 0xfe, 0x3f, 0xf0,
  0xfc, 0x1f, 0xf0, 0x00, 0x00, 0xf0
};

// ==================== КОНСТРУКТОР ====================
Display::Display() : oled(U8G2_R0, /* reset=*/U8X8_PIN_NONE) {}

// ==================== ИНИЦИАЛИЗАЦИЯ ====================
void Display::begin() {
  oled.begin();
  oled.setFont(u8g2_font_6x10_tf);
  oled.setFontRefHeightExtendedText();
  oled.setDrawColor(1);
  oled.setFontPosTop();
  oled.setFontDirection(0);
}

// ==================== OTA ЭКРАНЫ ====================

void Display::showOTAScreen(int progress) {
    oled.clearBuffer();
    
    if (progress < 0) {
        // Начальный экран OTA
        oled.setFont(u8g2_font_10x20_tf);
        int titleWidth = oled.getStrWidth("OTA ОБНОВЛЕНИЕ");
        oled.setCursor((128 - titleWidth) / 2, 10);
        oled.print("OTA ОБНОВЛЕНИЕ");
        
        oled.setFont(u8g2_font_6x10_tf);
        String line1 = "НЕ ВЫКЛЮЧАЙТЕ!";
        String line2 = "Идет загрузка...";
        
        int width1 = oled.getStrWidth(line1.c_str());
        int width2 = oled.getStrWidth(line2.c_str());
        
        oled.setCursor((128 - width1) / 2, 30);
        oled.print(line1);
        oled.setCursor((128 - width2) / 2, 45);
        oled.print(line2);
        
        // Рисуем анимацию ожидания
        static int dotCount = 0;
        dotCount = (dotCount + 1) % 4;
        String dots = "";
        for (int i = 0; i < dotCount; i++) dots += ".";
        int dotsWidth = oled.getStrWidth(dots.c_str());
        oled.setCursor((128 - dotsWidth) / 2, 55);
        oled.print(dots);
    } else {
        // Экран с прогрессом
        oled.setFont(u8g2_font_10x20_tf);
        int titleWidth = oled.getStrWidth("OTA ОБНОВЛЕНИЕ");
        oled.setCursor((128 - titleWidth) / 2, 5);
        oled.print("OTA ОБНОВЛЕНИЕ");
        
        // Рисуем прогресс-бар
        int barWidth = 100;
        int barX = (128 - barWidth) / 2;
        int barY = 25;
        int barHeight = 15;
        
        // Рамка прогресс-бара
        oled.drawFrame(barX, barY, barWidth, barHeight);
        
        // Заполнение прогресс-бара
        if (progress > 0) {
            int fillWidth = (barWidth - 4) * progress / 100;
            oled.drawBox(barX + 2, barY + 2, fillWidth, barHeight - 4);
        }
        
        // Показываем проценты крупно
        oled.setFont(u8g2_font_fub20_tf);
        String percentStr = String(progress) + "%";
        int percentWidth = oled.getStrWidth(percentStr.c_str());
        oled.setCursor((128 - percentWidth) / 2, 45);
        oled.print(percentStr);
        
        // Подсказка
        oled.setFont(u8g2_font_5x7_tf);
        String hint = "Не выключайте питание!";
        int hintWidth = oled.getStrWidth(hint.c_str());
        oled.setCursor((128 - hintWidth) / 2, 58);
        oled.print(hint);
    }
    
    oled.sendBuffer();
}

void Display::showOTACompleteScreen() {
    oled.clearBuffer();
    
    oled.setFont(u8g2_font_10x20_tf);
    int titleWidth = oled.getStrWidth("ОБНОВЛЕНИЕ");
    oled.setCursor((128 - titleWidth) / 2, 15);
    oled.print("ОБНОВЛЕНИЕ");
    
    titleWidth = oled.getStrWidth("ЗАВЕРШЕНО");
    oled.setCursor((128 - titleWidth) / 2, 30);
    oled.print("ЗАВЕРШЕНО");
    
    // Рисуем галочку
    oled.drawLine(50, 45, 60, 55);
    oled.drawLine(60, 55, 80, 35);
    
    oled.setFont(u8g2_font_6x10_tf);
    String line = "Перезагрузка...";
    int lineWidth = oled.getStrWidth(line.c_str());
    oled.setCursor((128 - lineWidth) / 2, 55);
    oled.print(line);
    
    oled.sendBuffer();
}

// ==================== ОСНОВНОЙ МЕТОД ОБНОВЛЕНИЯ ====================
void Display::update(SystemState state, ErrorType error, bool kettlePresent,
                     float currentWeight, float targetWeight, float fillStartVolume,
                     bool powerRelayState, float emptyWeight) {
  
  if (waitState != WAIT_NONE) {
    return;
  }
  
  oled.clearBuffer();

  if (calibrationSuccess) {
    showCalibrationSuccessNonBlocking(nullptr);
  } else if (calibrationInProgress) {
    drawCalibrationScreen(currentWeight);
  } else {
    switch (state) {
      case ST_INIT:
        drawInitScreen();
        break;
      case ST_ERROR:
        drawErrorScreen(error);
        break;
      case ST_IDLE:
        drawIdleScreen(kettlePresent, getWaterVolume(currentWeight, emptyWeight), powerRelayState);
        break;
      case ST_FILLING:
        drawFillingScreen(getWaterVolume(currentWeight, emptyWeight),
                          getTargetWaterVolume(targetWeight, emptyWeight),
                          fillStartVolume, powerRelayState);
        break;
      case ST_CALIBRATION:
        drawCalibrationScreen(currentWeight);
        break;
    }
  }

  oled.sendBuffer();
}

// ==================== ПУБЛИЧНЫЙ МЕТОД ЦЕНТРИРОВАННОГО ТЕКСТА ====================
void Display::drawCenteredText(int y, const String& text, const uint8_t* font) {
  if (font != nullptr) {
    oled.setFont(font);
  }
  int textWidth = oled.getStrWidth(text.c_str());
  oled.setCursor((128 - textWidth) / 2, y);
  oled.print(text);
}

// ==================== ЭКРАН ТОЧКИ ДОСТУПА ====================
void Display::drawAPCredentials(SystemState state) {
    bool isAPMode = (WiFi.getMode() & WIFI_AP) != 0;
    if (isAPMode && state != ST_FILLING && state != ST_INIT && state != ST_ERROR) {
        String apInfo = "Smart_Pump_AP/12345678";
        int textWidth = oled.getStrWidth(apInfo.c_str());
        drawStringSafe((128 - textWidth) / 2, 54, apInfo, u8g2_font_5x7_tf, 110);
    }
}

// ==================== ЭКРАН СБРОСА ====================
void Display::showResetCountdown(int seconds, bool isFullReset) {
  oled.clearBuffer();

  oled.setFont(u8g2_font_fub14_tf);
  String title = "СБРОС ЧЕРЕЗ:";
  int titleWidth = oled.getStrWidth(title.c_str());
  oled.setCursor((128 - titleWidth) / 2, 5);
  oled.print(title);

  oled.setFont(u8g2_font_fub20_tf);
  String countdownStr = String(seconds);
  int countdownWidth = oled.getStrWidth(countdownStr.c_str());
  oled.setCursor((128 - countdownWidth) / 2, 25);
  oled.print(countdownStr);

  oled.setFont(u8g2_font_6x10_tf);
  String label = isFullReset ? "ПОЛНЫЙ" : "КАЛИБР.";
  int labelWidth = oled.getStrWidth(label.c_str());
  oled.setCursor((128 - labelWidth) / 2, 45);
  oled.print(label);

  oled.sendBuffer();
}

// ==================== ЭКРАН КАЛИБРОВКИ ====================
void Display::drawCalibrationScreen(float currentWeight) {
  oled.setFont(u8g2_font_10x20_tf);
  String title = "КАЛИБРОВКА";
  int titleWidth = oled.getStrWidth(title.c_str());
  oled.setCursor((128 - titleWidth) / 2, 5);
  oled.print(title);

  oled.setFont(u8g2_font_6x10_tf);
  String line1 = "1. Уберите чайник";
  String line2 = "2. ПУСТОЙ чайник";
  String line3 = "3. Кнопка 3 раза";
  
  int width1 = oled.getStrWidth(line1.c_str());
  int width2 = oled.getStrWidth(line2.c_str());
  int width3 = oled.getStrWidth(line3.c_str());
  
  oled.setCursor((128 - width1) / 2, 25);
  oled.print(line1);
  oled.setCursor((128 - width2) / 2, 35);
  oled.print(line2);
  oled.setCursor((128 - width3) / 2, 45);
  oled.print(line3);

  String weightStr = "Вес: " + String(currentWeight, 0) + "г";
  int weightWidth = oled.getStrWidth(weightStr.c_str());
  oled.setCursor((128 - weightWidth) / 2, 55);
  oled.print(weightStr);

  drawWiFiIcon();
}

// ==================== ЭКРАН ИНИЦИАЛИЗАЦИИ ====================
void Display::drawInitScreen() {
  bool isAPMode = (WiFi.getMode() & WIFI_AP) != 0;

  if (isAPMode) {
    oled.setFont(u8g2_font_10x20_tf);
    drawCenteredText(20, "УМНАЯ ПОМПА", u8g2_font_10x20_tf);
    drawCenteredText(45, "НАСТРОЙКА", u8g2_font_10x20_tf);

    static bool wifiVisible = true;
    static unsigned long lastBlink = 0;

    if (millis() - lastBlink > 500) {
      wifiVisible = !wifiVisible;
      lastBlink = millis();
    }

    if (wifiVisible) {
      oled.drawXBMP(112, 0, 16, 16, icon_wifi_16x16);
    }
  } else {
    oled.setFont(u8g2_font_10x20_tf);
    drawCenteredText(20, "УМНАЯ ПОМПА", u8g2_font_10x20_tf);
    drawCenteredText(45, "ЗАГРУЗКА...", u8g2_font_10x20_tf);
  }
}

// ==================== ЭКРАН ОШИБКИ ====================
void Display::drawErrorScreen(ErrorType error) {
  drawCenteredText(10, "ОШИБКА", u8g2_font_fub20_tf);

  String errorText;
  switch (error) {
    case ERR_HX711_TIMEOUT:
      errorText = "ДАТЧИК ВЕСА";
      break;
    case ERR_NO_FLOW:
      errorText = "НЕТ ВОДЫ";
      break;
    case ERR_FILL_TIMEOUT:
      errorText = "ТАЙМАУТ";
      break;
    default:
      errorText = "НЕИЗВЕСТНО";
      break;
  }

  drawCenteredText(35, errorText, u8g2_font_9x15_tf);
  oled.setFont(u8g2_font_5x7_tf);
  drawCenteredText(55, "ТРЕБУЕТСЯ ПЕРЕЗАГРУЗКА", u8g2_font_5x7_tf);
}

// ==================== ЭКРАН ОЖИДАНИЯ ====================
void Display::drawIdleScreen(bool kettlePresent, float waterVolume, bool powerRelayState) {
    drawPowerIcon(powerRelayState);
    drawWiFiIcon();

    String statusStr = kettlePresent ? "ГОТОВ" : "НЕТ ЧАЙНИКА";
    drawTextBetweenIcons(0, statusStr, powerRelayState, u8g2_font_fub14_tf);

    int cups = mlToCups(waterVolume);
    String cupsStr = String(cups);

    int blockWidth = calculateCupsBlockWidth(cupsStr, u8g2_font_fub20_tf);
    int startX = centerBlock(blockWidth);
    
    drawCupsWithIcon(startX, 28, cupsStr, u8g2_font_fub20_tf);

    if (!isWiFiConfigured && kettlePresent) {
        String hint = "Удерживайте для настройки";
        int hintWidth = oled.getStrWidth(hint.c_str());
        drawStringSafe(centerBlock(min(110, hintWidth)), 55, 
                      hint, u8g2_font_5x7_tf, 110);
    }
}

// ==================== ЭКРАН НАЛИВА ====================
void Display::drawFillingScreen(float currentWater, float targetWaterVolume,
                                float fillStartVolume, bool powerRelayState) {
    drawPowerIcon(powerRelayState);
    drawWiFiIcon();

    String statusText = "НАЛИВ...";
    drawTextBetweenIcons(0, statusText, powerRelayState, u8g2_font_fub14_tf);

    int currentCups = mlToCups(currentWater);
    int targetCups = mlToCups(targetWaterVolume);
    String cupsStr = String(currentCups) + " -> " + String(targetCups);

    int blockWidth = calculateCupsBlockWidth(cupsStr, u8g2_font_fub14_tf);
    int startX = centerBlock(blockWidth);
    
    drawCupsWithIcon(startX, 24, cupsStr, u8g2_font_fub14_tf);

    int progress = 0;
    if (targetWaterVolume > fillStartVolume) {
        progress = map(constrain(currentWater, fillStartVolume, targetWaterVolume),
                       fillStartVolume, targetWaterVolume, 0, 100);
        progress = constrain(progress, 0, 100);
    }

    drawProgressBar(14, 48, 65, 10, progress);

    oled.setFont(u8g2_font_fub14_tf);
    String percentStr = String(progress) + "%";
    oled.setCursor(85, 44);
    oled.print(percentStr);

    String stopHint = "Удерживайте для остановки";
    int stopWidth = oled.getStrWidth(stopHint.c_str());
    drawStringSafe(128 - stopWidth - 2, 58, stopHint, u8g2_font_5x7_tf, stopWidth);
}

// ==================== ВСПОМОГАТЕЛЬНЫЕ МЕТОДЫ ОТРИСОВКИ ====================
void Display::drawProgressBar(int x, int y, int w, int h, int p) {
  oled.drawFrame(x, y, w, h);
  oled.drawBox(x + 1, y + 1, (w - 2) * p / 100, h - 2);
}

void Display::drawWiFiIcon() {
  bool wifiConnected = isWiFiConnected;
  bool isAPMode = !wifiConnected && (WiFi.getMode() & WIFI_AP);
  bool showWifiIcon = true;
  bool useNoWifiIcon = false;
  bool blinkIcon = false;

  if (wifiConnected) {
    useNoWifiIcon = false;
    blinkIcon = false;
  } else {
    if (isAPMode) {
      useNoWifiIcon = false;
      blinkIcon = true;
    } else {
      useNoWifiIcon = true;
      blinkIcon = false;
    }
  }

  if (blinkIcon) {
    if (millis() - lastBlinkTime > 500) {
      wifiIconVisible = !wifiIconVisible;
      lastBlinkTime = millis();
    }
    showWifiIcon = showWifiIcon && wifiIconVisible;
  } else {
    wifiIconVisible = true;
  }

  if (showWifiIcon) {
    if (useNoWifiIcon) {
      oled.drawXBMP(112, 0, 16, 16, icon_no_wifi_16x16);
    } else {
      oled.drawXBMP(112, 0, 16, 16, icon_wifi_16x16);
    }
  }
}

void Display::drawPowerIcon(bool isOn) {
  if (isOn) {
    oled.drawXBMP(0, 0, 16, 16, icon_power_16x16);
  }
}

void Display::drawTextBetweenIcons(int y, const String& text, bool powerIconVisible, const uint8_t* font) {
  if (font != nullptr) {
    oled.setFont(font);
  }

  int textWidth = oled.getStrWidth(text.c_str());
  int leftMargin = powerIconVisible ? 18 : 0;
  int rightMargin = 114;
  int availableWidth = rightMargin - leftMargin;

  if (textWidth <= availableWidth) {
    int textX = leftMargin + (availableWidth - textWidth) / 2;
    oled.setCursor(textX, y);
  } else {
    oled.setCursor((128 - textWidth) / 2, y);
  }

  oled.print(text);
}

// ==================== НЕБЛОКИРУЮЩИЕ ОЖИДАНИЯ ====================
void Display::updateWaiting(StateMachine* sm) {
    if (waitState == WAIT_NONE) return;
    
    if (sm == nullptr) {
        unsigned long now = millis();
        if ((long)(now - waitStartTime) >= (long)waitDuration) {
            switch (waitState) {
                case WAIT_CALIB_SUCCESS:
                    calibrationSuccess = false;
                    break;
                default:
                    break;
            }
            waitState = WAIT_NONE;
        }
        return;
    }

    unsigned long now = millis();
    if ((long)(now - waitStartTime) >= (long)waitDuration) {
        switch (waitState) {
            case WAIT_RESET_MESSAGE:
                break;
            case WAIT_CALIB_SUCCESS:
                calibrationSuccess = false;
                sm->toIdle();
                break;
            case WAIT_CALIB_ERROR:
                break;
            default:
                break;
        }
        waitState = WAIT_NONE;
    }
}

void Display::showResetMessageNonBlocking(bool isFullReset, StateMachine* sm) {
    oled.clearBuffer();
    oled.setFont(u8g2_font_fub14_tf);

    if (isFullReset) {
        drawCenteredText(15, "ПОЛНЫЙ", u8g2_font_fub14_tf);
        drawCenteredText(35, "СБРОС", u8g2_font_fub14_tf);
        oled.setFont(u8g2_font_6x10_tf);
        drawCenteredText(50, "WiFi и калибровка удалены", u8g2_font_6x10_tf);
    } else {
        drawCenteredText(20, "СБРОС", u8g2_font_fub14_tf);
        drawCenteredText(40, "КАЛИБРОВКИ", u8g2_font_fub14_tf);
        oled.setFont(u8g2_font_6x10_tf);
        drawCenteredText(55, "Настройки WiFi сохранены", u8g2_font_6x10_tf);
    }

    oled.sendBuffer();
    stateMachine = sm;
    waitState = WAIT_RESET_MESSAGE;
    waitStartTime = millis();
    waitDuration = 2000;
}

void Display::showCalibrationSuccessNonBlocking(StateMachine* sm) {
    oled.setFont(u8g2_font_10x20_tf);
    drawCenteredText(20, "КАЛИБРОВКА", u8g2_font_10x20_tf);
    drawCenteredText(40, "ЗАВЕРШЕНА", u8g2_font_10x20_tf);
    drawWiFiIcon();
    oled.sendBuffer();

    stateMachine = sm;
    waitState = WAIT_CALIB_SUCCESS;
    waitStartTime = millis();
    waitDuration = 2000;
}

void Display::showCalibrationErrorNonBlocking(StateMachine* sm) {
    oled.clearBuffer();
    oled.setFont(u8g2_font_10x20_tf);
    drawCenteredText(20, "ОШИБКА", u8g2_font_10x20_tf);
    drawCenteredText(40, "КАЛИБРОВКИ", u8g2_font_10x20_tf);
    oled.setFont(u8g2_font_6x10_tf);
    drawCenteredText(55, "Вес должен быть 100-5000г", u8g2_font_6x10_tf);
    oled.sendBuffer();

    stateMachine = sm;
    waitState = WAIT_CALIB_ERROR;
    waitStartTime = millis();
    waitDuration = 2000;
}

void Display::drawStringSafe(int x, int y, const String& text, const uint8_t* font, int maxWidth) {
    oled.setFont(font);
    int textWidth = oled.getStrWidth(text.c_str());
    
    if (textWidth <= maxWidth) {
        oled.setCursor(x, y);
        oled.print(text);
    } else {
        String shortText = text.substring(0, text.length() - 3) + "...";
        oled.setCursor(x, y);
        oled.print(shortText);
    }
}

void Display::drawCupsWithIcon(int x, int y, const String& text, const uint8_t* font) {
    oled.setFont(font);
    int textWidth = oled.getStrWidth(text.c_str());
    
    oled.setCursor(x, y);
    oled.print(text);
    oled.drawXBMP(x + textWidth + 4, y + 1, 20, 20, cup_20x20);
}

int Display::calculateCupsBlockWidth(const String& text, const uint8_t* font) {
    oled.setFont(font);
    return oled.getStrWidth(text.c_str()) + 20 + 4;
}

int Display::centerBlock(int blockWidth) {
    return (128 - blockWidth) / 2;
}

// ==================== СТАТИЧЕСКИЕ УТИЛИТЫ ====================
int Display::mlToCups(float ml, int cupVolume) {
  if (ml <= 0 || cupVolume <= 0) return 0;
  if (ml > 10000) return 0;
  return (int)(ml / cupVolume);
}

String Display::formatCupsNumber(int cups) {
  return String(cups);
}

float Display::getWaterVolume(float currentWeight, float emptyWeight) {
  float vol = currentWeight - emptyWeight;
  return (vol < 0) ? 0 : vol;
}

float Display::getTargetWaterVolume(float targetWeight, float emptyWeight) {
  float vol = targetWeight - emptyWeight;
  return (vol < 0) ? 0 : vol;
}