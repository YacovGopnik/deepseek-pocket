// Китайские иероглифы 16x16 (GB2312, 6763 знака) и китайская пунктуация.
// Источник: GNU Unifont. Каждый символ 32 байта: 16 строк по 2 байта, старший бит слева.
#pragma once
#include <stdint.h>
#include <pgmspace.h>

#define CJK_COUNT 6877
// Сами данные - в font_cjk.cpp (компилируется один раз и кэшируется - сборка быстрее).
extern const uint16_t CJK_CODES[CJK_COUNT];
extern const uint8_t CJK_BITMAPS[CJK_COUNT * 32];
