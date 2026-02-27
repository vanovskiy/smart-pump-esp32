// файл: debug.h
// Простая система отладки с флагом DEBUG
// ВСЕ СООБЩЕНИЯ НА РУССКОМ ЯЗЫКЕ

#ifndef DEBUG_H
#define DEBUG_H

#include <Arduino.h>
#include "config.h"

// Если DEBUG определен, включаем отладку
#ifdef DEBUG
  #define DPRINT(...) Serial.print(__VA_ARGS__)
  #define DPRINTLN(...) Serial.println(__VA_ARGS__)
  #define DPRINTF(...) Serial.printf(__VA_ARGS__)
  
  #define DENTER(func) DPRINTLN("➡️ Вход в: " func)
  #define DEXIT(func) DPRINTLN("⬅️ Выход из: " func)
  #define DSTATE(state) DPRINTF("📊 Состояние: %s\n", state)
  #define DVAL(name, val) DPRINTF("  📌 %s: ", name); DPRINTLN(val)
  #define DVALF(name, val) DPRINTF("  📌 %s: %f\n", name, val)
  #define DVALD(name, val) DPRINTF("  📌 %s: %d\n", name, val)
  #define DVALX(name, val) DPRINTF("  📌 %s: 0x%X\n", name, val)
  #define DVALUL(name, val) DPRINTF("  📌 %s: %lu\n", name, val)
#else
  #define DPRINT(...)
  #define DPRINTLN(...)
  #define DPRINTF(...)
  
  #define DENTER(func)
  #define DEXIT(func)
  #define DSTATE(state)
  #define DVAL(name, val)
  #define DVALF(name, val)
  #define DVALD(name, val)
  #define DVALX(name, val)
  #define DVALUL(name, val)
#endif

// Критические сообщения всегда выводятся (НА РУССКОМ)
#define LOG_ERROR(...) Serial.print("❌ ОШИБКА: "); Serial.println(__VA_ARGS__)
#define LOG_WARN(...) Serial.print("⚠️ ПРЕДУПРЕЖДЕНИЕ: "); Serial.println(__VA_ARGS__)
#define LOG_INFO(...) Serial.print("ℹ️ ИНФО: "); Serial.println(__VA_ARGS__)
#define LOG_OK(...) Serial.print("✅ "); Serial.println(__VA_ARGS__)

#endif