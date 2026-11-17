// Крупные иероглифы 24x24 (Noto Sans CJK SC), те же коды и порядок, что CJK_CODES в font_cjk.h.
// 24 строки по 3 байта на символ.
#pragma once
#include <stdint.h>
#include <pgmspace.h>
// Сами данные - в font_cjk24.cpp (компилируется один раз и кэшируется - сборка быстрее).
extern const uint8_t CJK24_BITMAPS[];
