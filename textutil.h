// Работа с текстом: UTF-8 <-> коды символов, соответствие шрифту, перенос строк.
// Не зависит от Arduino, поэтому проверяется обычными тестами на компьютере.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>

// Дополнительные символы шрифтов (после кириллицы), по возрастанию: формулы, греческие, стрелки, Latin-1.
static const uint16_t EXTRA_GLYPHS[] = {
  0x00A1, 0x00A2, 0x00A3, 0x00A4, 0x00A5, 0x00A6, 0x00A7, 0x00A8, 0x00A9, 0x00AA, 0x00AB, 0x00AC,
  0x00AD, 0x00AE, 0x00AF, 0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5, 0x00B6, 0x00B7, 0x00B8,
  0x00B9, 0x00BA, 0x00BB, 0x00BC, 0x00BD, 0x00BE, 0x00BF, 0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C4,
  0x00C5, 0x00C6, 0x00C7, 0x00C8, 0x00C9, 0x00CA, 0x00CB, 0x00CC, 0x00CD, 0x00CE, 0x00CF, 0x00D0,
  0x00D1, 0x00D2, 0x00D3, 0x00D4, 0x00D5, 0x00D6, 0x00D7, 0x00D8, 0x00D9, 0x00DA, 0x00DB, 0x00DC,
  0x00DD, 0x00DE, 0x00DF, 0x00E0, 0x00E1, 0x00E2, 0x00E3, 0x00E4, 0x00E5, 0x00E6, 0x00E7, 0x00E8,
  0x00E9, 0x00EA, 0x00EB, 0x00EC, 0x00ED, 0x00EE, 0x00EF, 0x00F0, 0x00F1, 0x00F2, 0x00F3, 0x00F4,
  0x00F5, 0x00F6, 0x00F7, 0x00F8, 0x00F9, 0x00FA, 0x00FB, 0x00FC, 0x00FD, 0x00FE, 0x00FF, 0x0127,
  0x02E3, 0x0393, 0x0394, 0x0398, 0x039B, 0x039E, 0x03A0, 0x03A3, 0x03A6, 0x03A8, 0x03A9, 0x03B1,
  0x03B2, 0x03B3, 0x03B4, 0x03B5, 0x03B6, 0x03B7, 0x03B8, 0x03B9, 0x03BA, 0x03BB, 0x03BC, 0x03BD,
  0x03BE, 0x03C0, 0x03C1, 0x03C3, 0x03C4, 0x03C6, 0x03C7, 0x03C8, 0x03C9, 0x1D50, 0x2013, 0x2014,
  0x2018, 0x2019, 0x201C, 0x201D, 0x201E, 0x2022, 0x2026, 0x2030, 0x2032, 0x2033, 0x2070, 0x2074,
  0x2075, 0x2076, 0x2077, 0x2078, 0x2079, 0x207A, 0x207B, 0x207F, 0x2080, 0x2081, 0x2082, 0x2083,
  0x2084, 0x2085, 0x2086, 0x2087, 0x2088, 0x2089, 0x2093, 0x2099, 0x20AC, 0x20BD, 0x2116, 0x2190,
  0x2191, 0x2192, 0x2193, 0x2194, 0x21D2, 0x21D4, 0x2200, 0x2202, 0x2203, 0x2205, 0x2208, 0x220F,
  0x2211, 0x2213, 0x221A, 0x221B, 0x221D, 0x221E, 0x2220, 0x2229, 0x222A, 0x222B, 0x2248, 0x2260,
  0x2261, 0x2264, 0x2265, 0x2282, 0x22A5, 0x2600, 0x2601, 0x2602, 0x2605, 0x26A1, 0x2744,
};
const int EXTRA_GLYPH_COUNT = sizeof(EXTRA_GLYPHS) / sizeof(EXTRA_GLYPHS[0]);

// Номер глифа в font8x16.h по коду символа Юникода, или -1, если такого нет.
// Порядок в шрифте: ASCII 32..126 (95 шт.), затем А-Е, Ё, Ж-Я, а-е, ё, ж-я.
inline int glyphIndex(uint32_t cp) {
  if (cp >= 32 && cp <= 126) return (int)(cp - 32);
  const int base = 95;
  if (cp >= 0x410 && cp <= 0x415) return base + (int)(cp - 0x410);       // А-Е
  if (cp == 0x401) return base + 6;                                      // Ё
  if (cp >= 0x416 && cp <= 0x42F) return base + 7 + (int)(cp - 0x416);   // Ж-Я
  if (cp >= 0x430 && cp <= 0x435) return base + 33 + (int)(cp - 0x430);  // а-е
  if (cp == 0x451) return base + 39;                                     // ё
  if (cp >= 0x436 && cp <= 0x44F) return base + 40 + (int)(cp - 0x436);  // ж-я
  if (cp == 0xA0) return 0;                                              // неразрывный пробел
  int lo = 0, hi = EXTRA_GLYPH_COUNT - 1;                                // остальное - двоичный поиск
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (EXTRA_GLYPHS[mid] == cp) return 161 + mid;
    if (EXTRA_GLYPHS[mid] < cp) lo = mid + 1;
    else hi = mid - 1;
  }
  return -1;
}

// Широкий символ (иероглиф, китайская пунктуация) занимает две позиции.
inline bool isWide(uint32_t cp) {
  return (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
         (cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
         (cp >= 0xFFE0 && cp <= 0xFFE6);
}

inline int cellWidth(uint32_t cp) { return isWide(cp) ? 2 : 1; }

// Знаки, с которых не должна начинаться строка (китайская и обычная пунктуация).
inline bool isNoStart(uint32_t cp) {
  switch (cp) {
    case 0x3001: case 0x3002: case 0xFF0C: case 0xFF0E: case 0xFF01: case 0xFF1F:
    case 0xFF1A: case 0xFF1B: case 0xFF09: case 0x300B: case 0x300D: case 0x300F:
    case 0x3011: case 0xFF3D: case 0xFF5D: case 0x3009:
    case ',': case '.': case '!': case '?': case ':': case ';': case ')':
      return true;
    default:
      return false;
  }
}

// Знаки, на которых не должна заканчиваться строка (открывающие скобки и кавычки).
inline bool isNoEnd(uint32_t cp) {
  switch (cp) {
    case 0xFF08: case 0x300A: case 0x300C: case 0x300E: case 0x3010: case 0xFF3B:
    case 0xFF5B: case 0x3008: case '(': case '[':
      return true;
    default:
      return false;
  }
}

// Можно ли перенести строку перед символом cps[j] (между иероглифами).
inline bool canBreakCjk(const std::vector<uint16_t>& cps, size_t j) {
  return (isWide(cps[j]) || isWide(cps[j - 1])) && !isNoStart(cps[j]) && !isNoEnd(cps[j - 1]);
}

// Добавляет символ в строку. Знаки, которых нет в шрифтах, заменяются похожими.
inline void pushCp(uint32_t cp, std::vector<uint16_t>& out) {
  switch (cp) {
    case '\r': return;
    case '\n': out.push_back('\n'); return;
    case '\t': case 0xA0: case 0x2009: case 0x202F: out.push_back(' '); return;
    case 0x2010: case 0x2011: case 0x2212: out.push_back('-'); return;   // дефисы и минус
    case 0x2217: case 0x22C5: out.push_back(0xB7); return;               // знаки умножения -> ·
    case 0x2264: case 0x2A7D: out.push_back(0x2264); return;
    case 0x2265: case 0x2A7E: out.push_back(0x2265); return;
    case 0x21D2: case 0x27F9: out.push_back(0x21D2); return;
    case 0x2192: case 0x27F6: out.push_back(0x2192); return;
    default: break;
  }
  if (cp < 32) cp = ' ';          // прочие управляющие символы
  if (cp > 0xFFFF) cp = '?';      // эмодзи и т.п.
  out.push_back((uint16_t)cp);
}

// Разбирает строку UTF-8 в массив кодов символов.
inline void utf8ToCps(const char* s, std::vector<uint16_t>& out) {
  const unsigned char* p = (const unsigned char*)s;
  while (*p) {
    unsigned char c = *p++;
    uint32_t cp;
    int extra;
    if (c < 0x80) { cp = c; extra = 0; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
    else { cp = '?'; extra = 0; }
    for (int i = 0; i < extra && (*p & 0xC0) == 0x80; i++) cp = (cp << 6) | (*p++ & 0x3F);
    pushCp(cp, out);
  }
}

// Собирает строку UTF-8 из кодов символов (для отправки в API).
inline std::string cpsToUtf8(const uint16_t* cps, size_t n) {
  std::string s;
  for (size_t i = 0; i < n; i++) {
    uint32_t c = cps[i];
    if (c < 0x80) {
      s += (char)c;
    } else if (c < 0x800) {
      s += (char)(0xC0 | (c >> 6));
      s += (char)(0x80 | (c & 0x3F));
    } else {
      s += (char)(0xE0 | (c >> 12));
      s += (char)(0x80 | ((c >> 6) & 0x3F));
      s += (char)(0x80 | (c & 0x3F));
    }
  }
  return s;
}

struct Line {
  uint16_t start;
  uint16_t len;
};

// Перенос на строки шириной cols позиций (иероглиф = 2 позиции).
// Обычный текст переносится по пробелам, китайский - между любыми иероглифами,
// но не перед знаками препинания. Слишком длинные слова режутся.
inline void wrapCps(const std::vector<uint16_t>& cps, int cols, std::vector<Line>& lines) {
  const size_t n = cps.size();
  const size_t NONE = (size_t)-1;
  size_t i = 0;
  while (i < n) {
    if (cps[i] == '\n') {                       // пустая строка (абзац)
      lines.push_back({(uint16_t)i, 0});
      i++;
      continue;
    }
    while (i < n && cps[i] == ' ') i++;         // пробелы в начале строки не нужны
    if (i >= n) break;
    if (cps[i] == '\n') continue;

    const size_t start = i;
    size_t j = i, brk = NONE, brkSkip = 0;
    int w = 0;
    while (j < n && cps[j] != '\n') {
      int cw = cellWidth(cps[j]);
      if (w + cw > cols) break;
      if (j > start) {
        if (cps[j] == ' ') { brk = j; brkSkip = 1; }
        else if (canBreakCjk(cps, j)) { brk = j; brkSkip = 0; }
      }
      w += cw;
      j++;
    }

    if (j >= n || cps[j] == '\n') {             // остаток абзаца помещается целиком
      lines.push_back({(uint16_t)start, (uint16_t)(j - start)});
      i = j;
      if (i < n && cps[i] == '\n') i++;
      continue;
    }
    // Не поместился символ cps[j]: ищем, где оборвать строку.
    if (cps[j] == ' ') {                        // слово закончилось ровно на границе
      lines.push_back({(uint16_t)start, (uint16_t)(j - start)});
      i = j + 1;
    } else if (j > start && canBreakCjk(cps, j)) {
      lines.push_back({(uint16_t)start, (uint16_t)(j - start)});
      i = j;                                    // перенос между иероглифами
    } else if (brk != NONE && brk > start) {
      lines.push_back({(uint16_t)start, (uint16_t)(brk - start)});
      i = brk + brkSkip;
    } else {                                    // слово длиннее строки - режем
      if (j == start) j = start + 1;
      lines.push_back({(uint16_t)start, (uint16_t)(j - start)});
      i = j;
    }
  }
}
