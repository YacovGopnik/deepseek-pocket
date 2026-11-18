// Крупный шрифт 12x24 (DejaVu Sans Mono): ASCII + кириллица, порядок как в font8x16.h.
// 24 строки по 2 байта на символ, 12 бит прижаты влево.
#pragma once
#include <stdint.h>
#include <pgmspace.h>

#define BIG_W 12
#define BIG_H 24
// Сами данные - в font12x24.cpp (компилируется один раз и кэшируется - сборка быстрее).
extern const uint8_t FONT12x24[];
