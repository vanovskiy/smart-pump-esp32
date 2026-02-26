// файл: Display.cpp
// Реализация класса для управления OLED-дисплеем SSD1306
// Содержит всю логику отрисовки экранов интерфейса

#include "Display.h"
#include <WiFi.h>
#include "StateMachine.h"

// ==================== ИКОНКИ (ДАННЫЕ) ====================
// Все иконки хранятся в PROGMEM для экономии оперативной памяти
// Формат: монохромное изображение, 1 бит на пиксель

/**
 * Иконка питания (включенное реле)
 * 16x16 пикселей, стилизованная молния/включение
 */
const unsigned char icon_power_16x16[] PROGMEM = {
  0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x98, 0x19,
  0x8c, 0x31, 0x86, 0x61, 0x82, 0x41, 0x83, 0xc1,
  0x03, 0xc0, 0x03, 0xc0, 0x03, 0xc0, 0x02, 0x40,
  0x06, 0x60, 0x0c, 0x30, 0x78, 0x1e, 0xe0, 0x07
};

/**
 * Иконка Wi-Fi (есть подключение)
 * 16x16 пикселей, стилизованные волны сигнала
 */
const unsigned char icon_wifi_16x16[] PROGMEM = {
  0x00, 0x00, 0xC0, 0x03, 0xF8, 0x1F,
  0x0C, 0x30, 0x06, 0x60, 0xC3, 0xC3,
  0x70, 0x0E, 0x18, 0x18, 0x0C, 0x30,
  0x80, 0x01, 0xE0, 0x07, 0x30, 0x0C,
  0x00, 0x00, 0x80, 0x01, 0x80, 0x01,
  0x00, 0x00
};

/**
 * Иконка отсутствия Wi-Fi
 * 16x16 пикселей, перечеркнутые волны
 */
const unsigned char icon_no_wifi_16x16[] PROGMEM = {
  0x00, 0x00, 0xC6, 0x03, 0xFE, 0x1E,
  0x1C, 0x30, 0x3A, 0x60, 0x73, 0xC3,
  0xe0, 0x0e, 0xd8, 0x19, 0x8c, 0x33,
  0x00, 0x07, 0x70, 0x0e, 0x38, 0x1c,
  0x00, 0x38, 0x80, 0x71, 0x80, 0x61,
  0x00, 0x00
};

/**
 * Иконка кружки для воды
 * 20x20 пикселей, стилизованное изображение чашки
 */
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

/**
 * Конструктор класса Display
 * Инициализирует объект U8g2 для работы с дисплеем SSD1306 по I2C
 * Параметры: U8G2_R0 - без поворота, reset - не используется (PIN_NONE)
 */
Display::Display()
  : oled(U8G2_R0, /* reset=*/U8X8_PIN_NONE) {
  // Тело конструктора пустое, вся инициализация в begin()
}

// ==================== ИНИЦИАЛИЗАЦИЯ ====================

/**
 * Запускает дисплей и устанавливает базовые параметры
 * Должен вызываться один раз в setup()
 */
void Display::begin() {
  oled.begin();                          // Инициализация аппаратной части
  oled.setFont(u8g2_font_6x10_tf);        // Шрифт по умолчанию (6x10 пикселей)
  oled.setFontRefHeightExtendedText();    // Корректная высота для шрифта
  oled.setDrawColor(1);                   // Цвет рисования: белый (1) на черном (0)
  oled.setFontPosTop();                    // Отсчет координат от верхнего края символов
  oled.setFontDirection(0);                 // Направление текста: слева направо
}

// ==================== ОСНОВНОЙ МЕТОД ОБНОВЛЕНИЯ ====================

/**
 * Главный метод обновления содержимого дисплея
 * Вызывается из loop() с периодичностью ~200 мс
 * Очищает буфер, выбирает нужный экран и отправляет данные на дисплей
 */
void Display::update(SystemState state, ErrorType error, bool kettlePresent,
                     float currentWeight, float targetWeight, float fillStartVolume,
                     bool powerRelayState, float emptyWeight) {
  
  // Если есть активное ожидание - не обновляем обычные экраны
  if (waitState != WAIT_NONE) {
    return;
  }
  
  oled.clearBuffer();

  // Проверка на экран успешной калибровки
  if (calibrationSuccess) {
    // Вместо прямого вызова drawCalibrationSuccessScreen,
    // показываем сообщение через неблокирующий метод
    if (stateMachine) {
      showCalibrationSuccessNonBlocking(stateMachine);
    }
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

// ==================== ЭКРАН ТОЧКИ ДОСТУПА ====================

/**
 * Отображает информацию о точке доступа внизу экрана
 * Показывает SSID и пароль для подключения к режиму настройки
 */
void Display::drawAPCredentials(SystemState state) {
  bool isAPMode = (WiFi.getMode() & WIFI_AP) != 0;

  if (isAPMode) {
    // Не показываем данные точки доступа во время налива/инициализации/ошибки
    if (state == ST_FILLING || state == ST_INIT || state == ST_ERROR) {
      return;
    }

    // Используем самый узкий шрифт (5x7), чтобы поместилась длинная строка
    oled.setFont(u8g2_font_5x7_tf);
    String apInfo = "Smart_Pump_AP/12345678";
    int textWidth = oled.getStrWidth(apInfo.c_str());
    int xPos = (128 - textWidth) / 2;
    int yPos = 54;

    oled.setCursor(xPos, yPos);
    oled.print(apInfo);
  }
}

// ==================== ЭКРАН СБРОСА ====================

/**
 * Показывает обратный отсчет перед выполнением сброса
 * Вызывается при удержании кнопки более 5 секунд
 * 
 * @param seconds - осталось секунд до сброса
 * @param isFullReset - true: полный сброс, false: только калибровка
 */
void Display::showResetCountdown(int seconds, bool isFullReset) {
  oled.clearBuffer();

  // Заголовок (крупный шрифт)
  oled.setFont(u8g2_font_fub14_tf);
  String title = "СБРОС ЧЕРЕЗ:";
  int titleWidth = oled.getStrWidth(title.c_str());
  oled.setCursor((128 - titleWidth) / 2, 5);
  oled.print(title);

  // Отсчет секунд (очень крупный шрифт)
  oled.setFont(u8g2_font_fub20_tf);
  String countdownStr = String(seconds);
  int countdownWidth = oled.getStrWidth(countdownStr.c_str());
  oled.setCursor((128 - countdownWidth) / 2, 25);
  oled.print(countdownStr);

  // Подпись: что именно будет сброшено
  oled.setFont(u8g2_font_6x10_tf);
  String label = isFullReset ? "ПОЛНЫЙ" : "КАЛИБР.";
  int labelWidth = oled.getStrWidth(label.c_str());
  oled.setCursor((128 - labelWidth) / 2, 45);
  oled.print(label);

  oled.sendBuffer();
}

/**
 * Показывает сообщение о выполнении сброса
 * Вызывается после отпускания кнопки, если удержание было достаточным
 * 
 * @param isFullReset - true: полный сброс, false: только калибровка
 */
/* Display::showResetMessage(bool isFullReset) {
  oled.clearBuffer();

  oled.setFont(u8g2_font_fub14_tf);

  if (isFullReset) {
    // Сообщение о полном сбросе (WiFi + калибровка)
    drawCenteredText(15, "ПОЛНЫЙ", u8g2_font_fub14_tf);
    drawCenteredText(35, "СБРОС", u8g2_font_fub14_tf);
    oled.setFont(u8g2_font_6x10_tf);
    drawCenteredText(50, "WiFi и калибровка удалены", u8g2_font_6x10_tf);
  } else {
    // Сообщение о сбросе только калибровки
    drawCenteredText(20, "СБРОС", u8g2_font_fub14_tf);
    drawCenteredText(40, "КАЛИБРОВКИ", u8g2_font_fub14_tf);
    oled.setFont(u8g2_font_6x10_tf);
    drawCenteredText(55, "Настройки WiFi сохранены", u8g2_font_6x10_tf);
  }

  oled.sendBuffer();
  delay(2000);  // Задержка, чтобы пользователь успел прочитать
}
*/
// ==================== ЭКРАН КАЛИБРОВКИ ====================

/**
 * Экран пошаговой инструкции по калибровке весов
 * Показывает текущий вес для контроля
 * 
 * @param currentWeight - текущий вес на весах
 */
void Display::drawCalibrationScreen(float currentWeight) {
  oled.setFont(u8g2_font_10x20_tf);

  // Заголовок
  String title = "КАЛИБРОВКА";
  int titleWidth = oled.getStrWidth(title.c_str());
  oled.setCursor((128 - titleWidth) / 2, 5);
  oled.print(title);

  oled.setFont(u8g2_font_6x10_tf);

  // Шаг 1: убрать чайник
  String line1 = "1. Уберите чайник";
  int width1 = oled.getStrWidth(line1.c_str());
  oled.setCursor((128 - width1) / 2, 25);
  oled.print(line1);

  // Шаг 2: поставить пустой чайник
  String line2 = "2. ПУСТОЙ чайник";
  int width2 = oled.getStrWidth(line2.c_str());
  oled.setCursor((128 - width2) / 2, 35);
  oled.print(line2);

  // Шаг 3: нажать кнопку 3 раза
  String line3 = "3. Кнопка 3 раза";
  int width3 = oled.getStrWidth(line3.c_str());
  oled.setCursor((128 - width3) / 2, 45);
  oled.print(line3);

  // Отображение текущего веса (для обратной связи)
  String weightStr = "Вес: " + String(currentWeight, 0) + "г";
  int weightWidth = oled.getStrWidth(weightStr.c_str());
  oled.setCursor((128 - weightWidth) / 2, 55);
  oled.print(weightStr);

  drawWiFiIcon();  // Иконка Wi-Fi в углу
}

// ==================== ЭКРАН УСПЕШНОЙ КАЛИБРОВКИ ====================

/**
 * Экран, показываемый после успешного завершения калибровки
 * Отображается 2 секунды, затем переход в основной режим
 */
/*void Display::drawCalibrationSuccessScreen() {
  oled.setFont(u8g2_font_10x20_tf);
  drawCenteredText(20, "КАЛИБРОВКА", u8g2_font_10x20_tf);
  drawCenteredText(40, "ЗАВЕРШЕНА", u8g2_font_10x20_tf);

  drawWiFiIcon();
}*/

// ==================== ЭКРАН ИНИЦИАЛИЗАЦИИ ====================

/**
 * Экран при запуске системы
 * Показывает логотип и статус загрузки/режима настройки
 */
void Display::drawInitScreen() {
  bool isAPMode = (WiFi.getMode() & WIFI_AP) != 0;

  if (isAPMode) {
    // Режим точки доступа (настройка Wi-Fi)
    oled.setFont(u8g2_font_10x20_tf);
    drawCenteredText(20, "УМНАЯ ПОМПА", u8g2_font_10x20_tf);
    drawCenteredText(45, "НАСТРОЙКА", u8g2_font_10x20_tf);

    // Мигающая Wi-Fi иконка для привлечения внимания
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
    // Обычный режим загрузки (подключение к Wi-Fi)
    oled.setFont(u8g2_font_10x20_tf);
    drawCenteredText(20, "УМНАЯ ПОМПА", u8g2_font_10x20_tf);
    drawCenteredText(45, "ЗАГРУЗКА...", u8g2_font_10x20_tf);
  }
}

// ==================== ЭКРАН ОШИБКИ ====================

/**
 * Экран отображения критических ошибок
 * 
 * @param error - тип ошибки из перечисления ErrorType
 */
void Display::drawErrorScreen(ErrorType error) {
  drawCenteredText(10, "ОШИБКА", u8g2_font_fub20_tf);

  // Выбор текста ошибки в зависимости от кода
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

  // Инструкция по выходу из ошибки
  oled.setFont(u8g2_font_5x7_tf);
  drawCenteredText(55, "ТРЕБУЕТСЯ ПЕРЕЗАГРУЗКА", u8g2_font_5x7_tf);
}

// ==================== ЭКРАН ОЖИДАНИЯ ====================

/**
 * Основной рабочий экран в режиме IDLE
 * Показывает статус, количество воды, иконки состояния
 * 
 * @param kettlePresent - наличие чайника
 * @param waterVolume - объем воды в мл
 * @param powerRelayState - состояние реле питания чайника
 */
void Display::drawIdleScreen(bool kettlePresent, float waterVolume, bool powerRelayState) {
  // Рисуем значки в верхних углах
  drawPowerIcon(powerRelayState);
  drawWiFiIcon();

  // Статус системы (ГОТОВ / НЕТ ЧАЙНИКА)
  String statusStr = kettlePresent ? "ГОТОВ" : "НЕТ ЧАЙНИКА";
  drawTextBetweenIcons(0, statusStr, powerRelayState, u8g2_font_fub14_tf);

  // Количество кружек (только цифра)
  int cups = mlToCups(waterVolume);
  String cupsStr = String(cups);

  oled.setFont(u8g2_font_fub20_tf);
  int cupsWidth = oled.getStrWidth(cupsStr.c_str());
  int totalWidth = cupsWidth + 20 + 4;  // Ширина цифры + иконка + отступ
  int startX = (128 - totalWidth) / 2;  // Центрирование блока
  int textBaselineY = 28;
  int iconY = textBaselineY + 1;

  oled.setCursor(startX, textBaselineY);
  oled.print(cupsStr);
  oled.drawXBMP(startX + cupsWidth + 4, iconY, 20, 20, cup_20x20);

  // Подсказка для запуска режима настройки (если Wi-Fi не настроен)
  if (!isWiFiConfigured && kettlePresent) {
    oled.setFont(u8g2_font_5x7_tf);
    String hint = "Удерживайте для настройки";
    int hintWidth = oled.getStrWidth(hint.c_str());
    oled.setCursor((128 - hintWidth) / 2, 55);
    oled.print(hint);
  }
}

// ==================== ЭКРАН НАЛИВА ====================

/**
 * Экран процесса налива воды
 * Показывает прогресс, текущее и целевое количество кружек
 * 
 * @param currentWater - текущий объем воды
 * @param targetWaterVolume - целевой объем
 * @param fillStartVolume - объем на момент начала налива
 * @param powerRelayState - состояние реле питания
 */
void Display::drawFillingScreen(float currentWater, float targetWaterVolume,
                                float fillStartVolume, bool powerRelayState) {
  // Рисуем значки
  drawPowerIcon(powerRelayState);
  drawWiFiIcon();

  // Статус процесса
  String statusText = "НАЛИВ...";
  drawTextBetweenIcons(0, statusText, powerRelayState, u8g2_font_fub14_tf);

  // Отображение прогресса: текущие кружки -> целевые кружки
  int currentCups = mlToCups(currentWater);
  int targetCups = mlToCups(targetWaterVolume);
  String cupsStr = String(currentCups) + " -> " + String(targetCups);

  oled.setFont(u8g2_font_fub14_tf);
  int cupsWidth = oled.getStrWidth(cupsStr.c_str());
  int totalWidth = cupsWidth + 20 + 4;
  int startX = (128 - totalWidth) / 2;
  int textBaselineY = 24;
  int iconY = textBaselineY + 1;

  oled.setCursor(startX, textBaselineY);
  oled.print(cupsStr);
  oled.drawXBMP(startX + cupsWidth + 4, iconY, 20, 20, cup_20x20);

  // Прогресс-бар
  int progress = 0;
  if (targetWaterVolume > fillStartVolume) {
    progress = map(constrain(currentWater, fillStartVolume, targetWaterVolume),
                   fillStartVolume, targetWaterVolume, 0, 100);
    progress = constrain(progress, 0, 100);
  }

  drawProgressBar(14, 48, 65, 10, progress);

  // Отображение процентов
  oled.setFont(u8g2_font_fub14_tf);
  String percentStr = String(progress) + "%";
  oled.setCursor(85, 44);
  oled.print(percentStr);

  // Подсказка для экстренной остановки
  oled.setFont(u8g2_font_5x7_tf);
  String stopHint = "Удерживайте для остановки";
  int stopWidth = oled.getStrWidth(stopHint.c_str());
  oled.setCursor(128 - stopWidth - 2, 58);
  oled.print(stopHint);
}

// ==================== ВСПОМОГАТЕЛЬНЫЕ МЕТОДЫ ОТРИСОВКИ ====================

/**
 * Рисует прогресс-бар с заданными параметрами
 * 
 * @param x - координата X
 * @param y - координата Y
 * @param w - ширина
 * @param h - высота
 * @param p - процент заполнения (0-100)
 */
void Display::drawProgressBar(int x, int y, int w, int h, int p) {
  oled.drawFrame(x, y, w, h);                // Рамка
  oled.drawBox(x + 1, y + 1, (w - 2) * p / 100, h - 2);  // Заполнение
}

/**
 * Рисует иконку Wi-Fi с учетом текущего состояния
 * Поддерживает режимы: подключено, нет подключения, мигание в режиме AP
 */
void Display::drawWiFiIcon() {
  bool wifiConnected = isWiFiConnected;
  bool isAPMode = !wifiConnected && (WiFi.getMode() & WIFI_AP);
  bool showWifiIcon = true;
  bool useNoWifiIcon = false;
  bool blinkIcon = false;

  // Определяем режим отображения
  if (wifiConnected) {
    useNoWifiIcon = false;
    blinkIcon = false;
  } else {
    if (isAPMode) {
      useNoWifiIcon = false;
      blinkIcon = true;      // В режиме AP иконка мигает
    } else {
      useNoWifiIcon = true;
      blinkIcon = false;     // Нет подключения - перечеркнутая иконка
    }
  }

  // Обработка мигания
  if (blinkIcon) {
    if (millis() - lastBlinkTime > 500) {
      wifiIconVisible = !wifiIconVisible;
      lastBlinkTime = millis();
    }
    showWifiIcon = showWifiIcon && wifiIconVisible;
  } else {
    wifiIconVisible = true;
  }

  // Отрисовка иконки
  if (showWifiIcon) {
    if (useNoWifiIcon) {
      oled.drawXBMP(112, 0, 16, 16, icon_no_wifi_16x16);
    } else {
      oled.drawXBMP(112, 0, 16, 16, icon_wifi_16x16);
    }
  }
}

/**
 * Рисует иконку питания в левом верхнем углу
 * 
 * @param isOn - состояние реле (true = включено, false = выключено)
 */
void Display::drawPowerIcon(bool isOn) {
  if (isOn) {
    oled.drawXBMP(0, 0, 16, 16, icon_power_16x16);
  }
}

/**
 * Рисует текст, центрированный по горизонтали
 * 
 * @param y - вертикальная позиция
 * @param text - текст для отображения
 * @param font - указатель на шрифт (может быть nullptr для текущего)
 */
void Display::drawCenteredText(int y, const String& text, const uint8_t* font) {
  if (font != nullptr) {
    oled.setFont(font);
  }
  int textWidth = oled.getStrWidth(text.c_str());
  oled.setCursor((128 - textWidth) / 2, y);
  oled.print(text);
}

/**
 * Рисует текст между иконками питания и Wi-Fi
 * Автоматически рассчитывает отступы для центрирования
 * 
 * @param y - вертикальная позиция
 * @param text - текст
 * @param powerIconVisible - видима ли иконка питания (влияет на левый отступ)
 * @param font - шрифт
 */
void Display::drawTextBetweenIcons(int y, const String& text, bool powerIconVisible, const uint8_t* font) {
  if (font != nullptr) {
    oled.setFont(font);
  }

  int textWidth = oled.getStrWidth(text.c_str());

  // Левая граница: если иконка питания видна, начинаем после нее (18 пикселей)
  int leftMargin = powerIconVisible ? 18 : 0;
  // Правая граница: перед иконкой Wi-Fi (114 пикселей)
  int rightMargin = 114;
  int availableWidth = rightMargin - leftMargin;

  // Если текст помещается, центрируем в доступной области
  if (textWidth <= availableWidth) {
    int textX = leftMargin + (availableWidth - textWidth) / 2;
    oled.setCursor(textX, y);
  } else {
    // Если не помещается, центрируем относительно всего экрана
    oled.setCursor((128 - textWidth) / 2, y);
  }

  oled.print(text);
}

// ==================== НЕБЛОКИРУЮЩИЕ ОЖИДАНИЯ ====================

/**
 * Обновление состояния ожидания
 * Вызывается из основного loop() для обработки таймеров
 */
void Display::updateWaiting(StateMachine* sm) {
  if (waitState == WAIT_NONE) return;  // Нет активного ожидания
  
  unsigned long now = millis();
  
  // Проверяем, истекло ли время ожидания
  if ((long)(now - waitStartTime) >= (long)waitDuration) {
    // Время истекло - выполняем действие в зависимости от состояния
    switch (waitState) {
      case WAIT_RESET_MESSAGE:
        // После сообщения о сбросе ничего не делаем
        // Устройство уже перезагружается
        break;
        
      case WAIT_CALIB_SUCCESS:
        // После успешной калибровки - возвращаемся в IDLE
        calibrationSuccess = false;
        if (sm) sm->toIdle();
        break;
        
      case WAIT_CALIB_ERROR:
        // После ошибки калибровки - остаемся на том же шаге
        // Состояние уже обработано в CalibrationState
        break;
        
      default:
        break;
    }
    
    waitState = WAIT_NONE;  // Сбрасываем состояние ожидания
  }
}

/**
 * Показать сообщение о сбросе с неблокирующим ожиданием
 */
void Display::showResetMessageNonBlocking(bool isFullReset, StateMachine* sm) {
  // Отрисовываем сообщение
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
  
  // Устанавливаем неблокирующее ожидание
  waitState = WAIT_RESET_MESSAGE;
  waitStartTime = millis();
  waitDuration = 2000;  // 2 секунды
  stateMachine = sm;
}

/**
 * Показать сообщение об успешной калибровке с неблокирующим ожиданием
 */
void Display::showCalibrationSuccessNonBlocking(StateMachine* sm) {
  // Показываем экран успеха
  oled.setFont(u8g2_font_10x20_tf);
  drawCenteredText(20, "КАЛИБРОВКА", u8g2_font_10x20_tf);
  drawCenteredText(40, "ЗАВЕРШЕНА", u8g2_font_10x20_tf);
  drawWiFiIcon();
  oled.sendBuffer();
  
  // Устанавливаем неблокирующее ожидание
  waitState = WAIT_CALIB_SUCCESS;
  waitStartTime = millis();
  waitDuration = 2000;  // 2 секунды
  stateMachine = sm;
}

/**
 * Показать сообщение об ошибке калибровки с неблокирующим ожиданием
 */
void Display::showCalibrationErrorNonBlocking(StateMachine* sm) {
  // Показываем сообщение об ошибке
  oled.clearBuffer();
  oled.setFont(u8g2_font_10x20_tf);
  drawCenteredText(20, "ОШИБКА", u8g2_font_10x20_tf);
  drawCenteredText(40, "КАЛИБРОВКИ", u8g2_font_10x20_tf);
  oled.setFont(u8g2_font_6x10_tf);
  drawCenteredText(55, "Вес должен быть 100-5000г", u8g2_font_6x10_tf);
  oled.sendBuffer();
  
  // Устанавливаем неблокирующее ожидание
  waitState = WAIT_CALIB_ERROR;
  waitStartTime = millis();
  waitDuration = 2000;  // 2 секунды
  stateMachine = sm;
}

// ==================== СТАТИЧЕСКИЕ УТИЛИТЫ ====================

/**
 * Преобразует миллилитры в количество полных кружек
 * 
 * @param ml - объем в миллилитрах
 * @param cupVolume - объем одной кружки
 * @return количество полных кружек (целое число)
 */
int Display::mlToCups(float ml, int cupVolume) {
  if (ml <= 0) return 0;
  return (int)(ml / cupVolume);
}

/**
 * Форматирует число кружек для отображения
 * В текущей реализации просто возвращает строку с числом
 * 
 * @param cups - количество кружек
 * @return строковое представление числа
 */
String Display::formatCupsNumber(int cups) {
  return String(cups);
}

/**
 * Вычисляет объем воды (текущий вес минус вес пустого чайника)
 * 
 * @param currentWeight - текущий вес
 * @param emptyWeight - вес пустого чайника
 * @return объем воды (никогда не отрицательный)
 */
float Display::getWaterVolume(float currentWeight, float emptyWeight) {
  float vol = currentWeight - emptyWeight;
  return (vol < 0) ? 0 : vol;
}

/**
 * Вычисляет целевой объем воды для налива
 * 
 * @param targetWeight - целевой вес
 * @param emptyWeight - вес пустого чайника
 * @return целевой объем воды
 */
float Display::getTargetWaterVolume(float targetWeight, float emptyWeight) {
  float vol = targetWeight - emptyWeight;
  return (vol < 0) ? 0 : vol;
}