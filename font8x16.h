// Растровый шрифт 8x16: ASCII 32..126 + кириллица (А-Я, Ё, а-я, ё) + дополнительные символы (см. textutil.h).
// 161 символ по 16 байт, каждый байт = одна строка символа, старший бит слева.
#pragma once
#include <stdint.h>
#include <pgmspace.h>

#define GLYPH_W 8
#define GLYPH_H 16
#define GLYPH_COUNT (161 + 203)   // + знаки формул, греческие буквы, стрелки, Latin-1
// Сами данные - в font8x16.cpp (компилируется один раз и кэшируется - сборка быстрее).
extern const uint8_t FONT8x16[GLYPH_COUNT * GLYPH_H];
