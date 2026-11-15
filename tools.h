// Инструменты: потоковый ответ DeepSeek, красивые формулы, Википедия, погода, курсы ЦБ.
// Только разбор текста и адреса запросов - чистые функции, тестируются на компьютере.
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "pixabay.h"   // urlEncode, readString, field

namespace tools {

using pixabay::field;
using pixabay::readString;
using pixabay::urlEncode;

// ---------- простые помощники для JSON ----------
// Позиция значения после "key": (или npos), ищем в [from, to).
inline size_t valuePos(const std::string& j, size_t from, size_t to, const char* key) {
  std::string pat = std::string("\"") + key + "\"";
  size_t p = j.find(pat, from);
  if (p == std::string::npos || p >= to) return std::string::npos;
  p += pat.size();
  while (p < to && (j[p] == ' ' || j[p] == ':' || j[p] == '\n' || j[p] == '\r' || j[p] == '\t')) p++;
  return p < to ? p : std::string::npos;
}

inline bool numField(const std::string& j, size_t from, size_t to, const char* key, double& v) {
  size_t p = valuePos(j, from, to, key);
  if (p == std::string::npos) return false;
  if (j[p] == '"') p++;                                  // число в кавычках
  char* end = nullptr;
  v = strtod(j.c_str() + p, &end);
  return end != j.c_str() + p;
}

// Массив чисел "key":[1.5,-2,null,...]; null -> NAN.
inline std::vector<double> numArray(const std::string& j, size_t from, const char* key) {
  std::vector<double> out;
  size_t p = valuePos(j, from, j.size(), key);
  if (p == std::string::npos || j[p] != '[') return out;
  p++;
  while (p < j.size() && j[p] != ']') {
    while (p < j.size() && (j[p] == ' ' || j[p] == ',')) p++;
    if (p >= j.size() || j[p] == ']') break;
    if (j.compare(p, 4, "null") == 0) {
      out.push_back(NAN);
      p += 4;
      continue;
    }
    char* end = nullptr;
    double v = strtod(j.c_str() + p, &end);
    if (end == j.c_str() + p) break;
    out.push_back(v);
    p = (size_t)(end - j.c_str());
  }
  return out;
}

inline std::vector<std::string> strArray(const std::string& j, size_t from, const char* key) {
  std::vector<std::string> out;
  size_t p = valuePos(j, from, j.size(), key);
  if (p == std::string::npos || j[p] != '[') return out;
  p++;
  while (p < j.size() && j[p] != ']') {
    while (p < j.size() && (j[p] == ' ' || j[p] == ',')) p++;
    if (p >= j.size() || j[p] != '"') break;
    std::string s;
    p = readString(j, p, s);
    if (p == std::string::npos) break;
    out.push_back(s);
  }
  return out;
}

// Конец объекта, начинающегося с '{' в позиции a (с учётом вложенности и строк).
inline size_t objectEnd(const std::string& j, size_t a) {
  int depth = 0;
  bool inStr = false;
  for (size_t i = a; i < j.size(); i++) {
    char c = j[i];
    if (inStr) {
      if (c == '\\') i++;
      else if (c == '"') inStr = false;
    } else if (c == '"') inStr = true;
    else if (c == '{') depth++;
    else if (c == '}') {
      if (--depth == 0) return i + 1;
    }
  }
  return j.size();
}

// ======================= ПОТОКОВЫЙ ОТВЕТ (SSE) =======================
// Строка потока: «data: {...}» или «data: [DONE]». Возвращает true, если строка - данные.
// В piece - новый кусочек ответа (без «размышлений» reasoning_content).
inline bool sseLine(const std::string& line, std::string& piece, bool& done) {
  piece.clear();
  done = false;
  size_t p = 0;
  while (p < line.size() && line[p] == ' ') p++;
  if (line.compare(p, 5, "data:") != 0) return false;
  p += 5;
  while (p < line.size() && line[p] == ' ') p++;
  if (line.compare(p, 6, "[DONE]") == 0) {
    done = true;
    return true;
  }
  size_t d = line.find("\"delta\"", p);
  if (d == std::string::npos) return true;
  size_t a = line.find('{', d);
  if (a == std::string::npos) return true;
  size_t b = objectEnd(line, a);
  size_t c = valuePos(line, a, b, "content");
  if (c != std::string::npos && line[c] == '"') readString(line, c, piece);
  if (line.find("\"finish_reason\":\"", p) != std::string::npos) done = true;
  return true;
}

// ======================= ФОРМУЛЫ =======================
inline void replaceAll(std::string& s, const std::string& a, const std::string& b) {
  if (a.empty()) return;
  size_t p = 0;
  while ((p = s.find(a, p)) != std::string::npos) {
    s.replace(p, a.size(), b);
    p += b.size();
  }
}

inline const char* supDigit(char c) {
  static const char* sup[] = {"\xE2\x81\xB0", "\xC2\xB9", "\xC2\xB2", "\xC2\xB3", "\xE2\x81\xB4",
                              "\xE2\x81\xB5", "\xE2\x81\xB6", "\xE2\x81\xB7", "\xE2\x81\xB8", "\xE2\x81\xB9"};
  return sup[c - '0'];
}
inline const char* subDigit(char c) {
  static const char* sub[] = {"\xE2\x82\x80", "\xE2\x82\x81", "\xE2\x82\x82", "\xE2\x82\x83", "\xE2\x82\x84",
                              "\xE2\x82\x85", "\xE2\x82\x86", "\xE2\x82\x87", "\xE2\x82\x88", "\xE2\x82\x89"};
  return sub[c - '0'];
}

// Содержимое {…} начиная с позиции открывающей скобки; pos - после закрывающей.
inline std::string braceArg(const std::string& s, size_t open, size_t& pos) {
  int depth = 0;
  for (size_t i = open; i < s.size(); i++) {
    if (s[i] == '{') depth++;
    else if (s[i] == '}' && --depth == 0) {
      pos = i + 1;
      return s.substr(open + 1, i - open - 1);
    }
  }
  pos = s.size();
  return s.substr(open + 1);
}

inline bool simpleArg(const std::string& a) {          // не нужны скобки: число или одно слово
  for (char c : a)
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.')) return false;
  return !a.empty();
}

// Степень: только цифры (и минус) - надстрочными знаками, иначе ^(…).
inline std::string supText(const std::string& a) {
  bool ok = !a.empty();
  for (size_t i = 0; i < a.size(); i++)
    if (!((a[i] >= '0' && a[i] <= '9') || (a[i] == '-' && i == 0 && a.size() > 1) || (a[i] == 'n' && a.size() == 1))) ok = false;
  if (!ok) {
    bool num = !a.empty();                             // число или одна буква - без скобок
    for (char c : a)
      if (!((c >= '0' && c <= '9') || c == '.')) num = false;
    return (num || a.size() == 1) ? "^" + a : "^(" + a + ")";
  }
  std::string o;
  for (char c : a) o += c == '-' ? "\xE2\x81\xBB" : c == 'n' ? "\xE2\x81\xBF" : supDigit(c);
  return o;
}

// Превращает LaTeX и «программистскую» запись в читаемые символы, убирает markdown.
inline std::string prettify(std::string s) {
  // markdown
  replaceAll(s, "**", "");
  replaceAll(s, "__", "");
  replaceAll(s, "```", "");
  replaceAll(s, "`", "");
  {                                                  // заголовки «### Текст» в начале строк
    std::string o;
    size_t i = 0;
    while (i < s.size()) {
      size_t e = s.find('\n', i);
      if (e == std::string::npos) e = s.size();
      std::string line = s.substr(i, e - i);
      size_t k = 0;
      while (k < line.size() && line[k] == '#') k++;
      if (k > 0 && k < line.size() && line[k] == ' ') line = line.substr(k + 1);
      if (line.compare(0, 2, "* ") == 0 || line.compare(0, 2, "- ") == 0) line = "\xE2\x80\xA2 " + line.substr(2);   // •
      o += line;
      if (e < s.size()) o += '\n';
      i = e + 1;
    }
    s = o;
  }
  // разделители формул LaTeX
  replaceAll(s, "\\[", "");
  replaceAll(s, "\\]", "");
  replaceAll(s, "\\(", "");
  replaceAll(s, "\\)", "");
  replaceAll(s, "$$", "");
  replaceAll(s, "$", "");
  // \frac{a}{b} -> a/b, \sqrt{x} -> √x, \text{..} -> ..
  for (int guard = 0; guard < 50; guard++) {
    size_t p = s.find("\\frac{");
    if (p == std::string::npos) p = s.find("\\dfrac{");
    if (p == std::string::npos) break;
    size_t open = s.find('{', p), q;
    std::string a = braceArg(s, open, q);
    std::string b;
    if (q < s.size() && s[q] == '{') b = braceArg(s, q, q);
    std::string r = (simpleArg(a) ? a : "(" + a + ")") + "/" + (simpleArg(b) ? b : "(" + b + ")");
    s.replace(p, q - p, r);
  }
  for (int guard = 0; guard < 50; guard++) {
    size_t p = s.find("\\sqrt{");
    if (p == std::string::npos) break;
    size_t q;
    std::string a = braceArg(s, p + 5, q);
    s.replace(p, q - p, std::string("\xE2\x88\x9A") + (simpleArg(a) ? a : "(" + a + ")"));
  }
  for (const char* cmd : {"\\text{", "\\mathrm{", "\\mathbf{", "\\operatorname{", "\\textbf{"}) {
    for (int guard = 0; guard < 50; guard++) {
      size_t p = s.find(cmd);
      if (p == std::string::npos) break;
      size_t q;
      std::string a = braceArg(s, p + strlen(cmd) - 1, q);
      s.replace(p, q - p, a);
    }
  }
  static const char* const MAP[][2] = {
    {"\\cdot", "\xC2\xB7"}, {"\\times", "\xC3\x97"}, {"\\div", "\xC3\xB7"}, {"\\pm", "\xC2\xB1"},
    {"\\leq", "\xE2\x89\xA4"}, {"\\geq", "\xE2\x89\xA5"}, {"\\neq", "\xE2\x89\xA0"}, {"\\le", "\xE2\x89\xA4"},
    {"\\ge", "\xE2\x89\xA5"}, {"\\ne", "\xE2\x89\xA0"}, {"\\approx", "\xE2\x89\x88"}, {"\\infty", "\xE2\x88\x9E"},
    {"\\Rightarrow", "\xE2\x87\x92"}, {"\\rightarrow", "\xE2\x86\x92"}, {"\\to", "\xE2\x86\x92"},
    {"\\leftarrow", "\xE2\x86\x90"}, {"\\Leftrightarrow", "\xE2\x87\x94"}, {"\\degree", "\xC2\xB0"},
    {"\\circ", "\xC2\xB0"}, {"\\pi", "\xCF\x80"}, {"\\alpha", "\xCE\xB1"}, {"\\beta", "\xCE\xB2"},
    {"\\gamma", "\xCE\xB3"}, {"\\delta", "\xCE\xB4"}, {"\\Delta", "\xCE\x94"}, {"\\epsilon", "\xCE\xB5"},
    {"\\varepsilon", "\xCE\xB5"}, {"\\theta", "\xCE\xB8"}, {"\\lambda", "\xCE\xBB"}, {"\\mu", "\xCE\xBC"},
    {"\\rho", "\xCF\x81"}, {"\\sigma", "\xCF\x83"}, {"\\Sigma", "\xCE\xA3"}, {"\\sum", "\xE2\x88\x91"},
    {"\\prod", "\xE2\x88\x8F"}, {"\\tau", "\xCF\x84"}, {"\\phi", "\xCF\x86"}, {"\\varphi", "\xCF\x86"},
    {"\\omega", "\xCF\x89"}, {"\\Omega", "\xCE\xA9"}, {"\\int", "\xE2\x88\xAB"}, {"\\partial", "\xE2\x88\x82"},
    {"\\in", "\xE2\x88\x88"}, {"\\forall", "\xE2\x88\x80"}, {"\\exists", "\xE2\x88\x83"},
    {"\\emptyset", "\xE2\x88\x85"}, {"\\cap", "\xE2\x88\xA9"}, {"\\cup", "\xE2\x88\xAA"},
    {"\\subset", "\xE2\x8A\x82"}, {"\\angle", "\xE2\x88\xA0"}, {"\\perp", "\xE2\x8A\xA5"},
    {"\\equiv", "\xE2\x89\xA1"}, {"\\propto", "\xE2\x88\x9D"}, {"\\sin", "sin"}, {"\\cos", "cos"},
    {"\\tan", "tan"}, {"\\log", "log"}, {"\\ln", "ln"}, {"\\lim", "lim"}, {"\\left", ""}, {"\\right", ""},
    {"\\,", " "}, {"\\;", " "}, {"\\quad", " "}, {"\\!", ""}, {"\\{", "{"}, {"\\}", "}"},
    {"\\%", "%"},
  };
  // длинные команды раньше коротких: \leq раньше \le, \left раньше \le и т.п.
  const size_t N = sizeof(MAP) / sizeof(MAP[0]);
  bool used[sizeof(MAP) / sizeof(MAP[0])] = {};
  for (size_t k = 0; k < N; k++) {
    size_t best = N;
    for (size_t i = 0; i < N; i++)
      if (!used[i] && (best == N || strlen(MAP[i][0]) > strlen(MAP[best][0]))) best = i;
    used[best] = true;
    replaceAll(s, MAP[best][0], MAP[best][1]);
  }
  replaceAll(s, "^{\xC2\xB0}", "\xC2\xB0");          // 30^{\circ} -> 30°
  replaceAll(s, "^\xC2\xB0", "\xC2\xB0");
  // степени и индексы: x^2, x^{10}, x^{-1}, a_1, a_{12}
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    char c = s[i];
    if ((c == '^' || c == '_') && i + 1 < s.size()) {
      std::string a;
      size_t q;
      if (s[i + 1] == '{') a = braceArg(s, i + 1, q);
      else {
        q = i + 1;
        while (q < s.size() && s[q] >= '0' && s[q] <= '9') q++;
        if (q == i + 1) {                                 // x^n
          if (c == '^' && s[q] == 'n') q++;
          else {
            o += c;
            continue;
          }
        }
        a = s.substr(i + 1, q - i - 1);
      }
      if (c == '^') {
        o += supText(a);
      } else {
        bool digits = !a.empty();
        for (char d : a)
          if (d < '0' || d > '9') digits = false;
        if (digits)
          for (char d : a) o += subDigit(d);
        else
          o += "_" + a;
      }
      i = q - 1;
      continue;
    }
    o += c;
  }
  s = o;
  // sqrt(x) -> √(x), " * " -> " · ", >= <= != -> ≥ ≤ ≠
  replaceAll(s, "sqrt(", "\xE2\x88\x9A(");
  replaceAll(s, ">=", "\xE2\x89\xA5");
  replaceAll(s, "<=", "\xE2\x89\xA4");
  replaceAll(s, "!=", "\xE2\x89\xA0");
  replaceAll(s, " * ", " \xC2\xB7 ");
  replaceAll(s, "->", "\xE2\x86\x92");
  return s;
}

// ======================= ВИКИПЕДИЯ =======================
// Один запрос: найти самую подходящую статью и взять её вступление простым текстом.
inline std::string wikiUrl(const std::string& lang, const std::string& query) {
  return "https://" + lang +
         ".wikipedia.org/w/api.php?action=query&format=json&formatversion=2&redirects=1"
         "&generator=search&gsrlimit=1&gsrsearch=" +
         urlEncode(query) + "&prop=extracts&exintro=1&explaintext=1&exchars=1500";
}

inline bool parseWiki(const std::string& j, std::string& title, std::string& text) {
  size_t p = j.find("\"pages\"");
  if (p == std::string::npos) return false;
  field(j, p, j.size(), "title", title);
  if (!field(j, p, j.size(), "extract", text)) return false;
  // убрать пустые строки подряд
  std::string o;
  for (char c : text) {
    if (c == '\n' && o.size() >= 1 && o.back() == '\n') continue;
    o += c;
  }
  text = o;
  return !text.empty();
}

// ======================= ПОГОДА (Open-Meteo) =======================
inline std::string geoUrl(const std::string& city, const std::string& lang) {
  return "https://geocoding-api.open-meteo.com/v1/search?count=1&format=json&language=" + lang +
         "&name=" + urlEncode(city);
}

struct Place {
  std::string name, country;
  double lat = 0, lon = 0;
};

inline bool parseGeo(const std::string& j, Place& pl) {
  size_t p = j.find("\"results\"");
  if (p == std::string::npos) return false;
  size_t a = j.find('{', p);
  if (a == std::string::npos) return false;
  size_t b = objectEnd(j, a);
  field(j, a, b, "name", pl.name);
  field(j, a, b, "country", pl.country);
  return numField(j, a, b, "latitude", pl.lat) && numField(j, a, b, "longitude", pl.lon);
}

inline std::string fmtCoord(double v) {
  char b[24];
  snprintf(b, sizeof(b), "%.3f", v);
  return b;
}

inline std::string forecastUrl(double lat, double lon) {
  return "https://api.open-meteo.com/v1/forecast?latitude=" + fmtCoord(lat) + "&longitude=" + fmtCoord(lon) +
         "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m"
         "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
         "&wind_speed_unit=ms&timezone=auto&forecast_days=4";
}

struct Forecast {
  double temp = NAN, feels = NAN, hum = NAN, wind = NAN;
  int code = -1;
  std::vector<std::string> days;
  std::vector<double> tmax, tmin, rain, dcode;
};

inline bool parseForecast(const std::string& j, Forecast& f) {
  size_t c = j.find("\"current\"");
  if (c == std::string::npos) return false;
  size_t a = j.find('{', c);
  size_t b = objectEnd(j, a);
  double code = -1;
  numField(j, a, b, "temperature_2m", f.temp);
  numField(j, a, b, "apparent_temperature", f.feels);
  numField(j, a, b, "relative_humidity_2m", f.hum);
  numField(j, a, b, "wind_speed_10m", f.wind);
  numField(j, a, b, "weather_code", code);
  f.code = (int)code;
  size_t d = j.find("\"daily\"");
  if (d != std::string::npos) {
    d = j.find('{', d);
    f.days = strArray(j, d, "time");
    f.dcode = numArray(j, d, "weather_code");
    f.tmax = numArray(j, d, "temperature_2m_max");
    f.tmin = numArray(j, d, "temperature_2m_min");
    f.rain = numArray(j, d, "precipitation_probability_max");
  }
  return !isnan(f.temp);
}

// Код погоды WMO -> номер описания (0 ясно, 1 облачно, 2 туман, 3 морось, 4 дождь, 5 снег, 6 ливень, 7 гроза).
inline int weatherKind(int code) {
  if (code <= 1) return 0;
  if (code <= 3) return 1;
  if (code == 45 || code == 48) return 2;
  if (code >= 51 && code <= 57) return 3;
  if ((code >= 61 && code <= 67)) return 4;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return 5;
  if (code >= 80 && code <= 82) return 6;
  if (code >= 95) return 7;
  return 1;
}

// ======================= КУРСЫ ЦБ =======================
inline const char* cbrUrl() { return "https://www.cbr-xml-daily.ru/daily_json.js"; }

struct Rate {
  std::string code, name;
  double nominal = 1, value = 0, prev = 0;
};

// Курсы в порядке: сначала самые нужные, потом остальные по алфавиту кода.
inline bool parseCbr(const std::string& j, std::vector<Rate>& rates, std::string& date) {
  rates.clear();
  field(j, 0, j.size(), "Date", date);
  if (date.size() >= 10) date = date.substr(8, 2) + "." + date.substr(5, 2) + "." + date.substr(0, 4);
  size_t v = j.find("\"Valute\"");
  if (v == std::string::npos) return false;
  size_t p = j.find('{', v);
  size_t end = objectEnd(j, p);
  p++;
  while (p < end) {
    size_t a = j.find('{', p);
    if (a == std::string::npos || a >= end) break;
    size_t b = objectEnd(j, a);
    Rate r;
    field(j, a, b, "CharCode", r.code);
    field(j, a, b, "Name", r.name);
    numField(j, a, b, "Nominal", r.nominal);
    numField(j, a, b, "Value", r.value);
    numField(j, a, b, "Previous", r.prev);
    if (!r.code.empty() && r.value > 0) rates.push_back(r);
    p = b;
  }
  static const char* top[] = {"USD", "EUR", "CNY", "BYN", "KZT", "TRY", "GBP", "JPY"};
  auto rank = [&](const Rate& r) {
    for (int i = 0; i < 8; i++)
      if (r.code == top[i]) return i;
    return 100;
  };
  for (size_t x = 1; x < rates.size(); x++)       // устойчивая сортировка вставками
    for (size_t y = x; y > 0; y--) {
      int ra = rank(rates[y - 1]), rb = rank(rates[y]);
      if (ra > rb || (ra == rb && ra == 100 && rates[y - 1].code > rates[y].code)) std::swap(rates[y - 1], rates[y]);
      else break;
    }
  return !rates.empty();
}

}  // namespace tools
