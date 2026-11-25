#pragma once
#include <Arduino.h>
#include <stdarg.h>
// Диагностический журнал: только системные события, без текста запросов и ключей.
constexpr int DEBUG_LOG_COUNT = 16;
constexpr int DEBUG_LOG_LEN = 88;
struct DebugEntry { uint32_t ms; char text[DEBUG_LOG_LEN]; };
static DebugEntry debugEntries[DEBUG_LOG_COUNT];
static uint32_t debugSequence = 0;
static portMUX_TYPE debugMux = portMUX_INITIALIZER_UNLOCKED;

void debugLog(const char* fmt, ...) {
  char line[160];
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  Serial.print(line);
  size_t n = strlen(line);
  while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
  if (n >= DEBUG_LOG_LEN) n = DEBUG_LOG_LEN - 1;
  line[n] = 0;
  uint32_t ms = millis();
  portENTER_CRITICAL(&debugMux);
  DebugEntry& e = debugEntries[debugSequence % DEBUG_LOG_COUNT];
  e.ms = ms;
  memcpy(e.text, line, n + 1);
  ++debugSequence;
  portEXIT_CRITICAL(&debugMux);
}


