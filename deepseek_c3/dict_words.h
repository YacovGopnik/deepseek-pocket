// Базовые словари подсказок: частые слова, от самых частых к редким.
#pragma once
#include <pgmspace.h>
// Сами данные - в dict_words.cpp (компилируется один раз и кэшируется - сборка быстрее).
extern const char DICT_RU[];
extern const char DICT_EN[];
