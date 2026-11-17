// Поиск картинок Pixabay: адрес запроса и разбор ответа без больших JSON-библиотек.
// Чистые функции на std::string - тестируются на компьютере.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

namespace pixabay {

struct Hit {
  std::string url;     // картинка высотой 180 пикселей (JPEG)
  std::string tags;
  std::string user;
};

// Кодирование для адреса: буквы/цифры как есть, остальное - %XX (UTF-8 по байтам).
inline std::string urlEncode(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string o;
  for (unsigned char c : s) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      o += (char)c;
    } else {
      o += '%';
      o += hex[c >> 4];
      o += hex[c & 15];
    }
  }
  return o;
}

inline std::string searchUrl(const std::string& key, const std::string& query, const std::string& lang) {
  return "https://pixabay.com/api/?key=" + urlEncode(key) + "&q=" + urlEncode(query) + "&lang=" + lang +
         "&image_type=photo&safesearch=true&per_page=20";
}

inline void appendUtf8(std::string& o, uint32_t cp) {
  if (cp < 0x80) o += (char)cp;
  else if (cp < 0x800) {
    o += (char)(0xC0 | (cp >> 6));
    o += (char)(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    o += (char)(0xE0 | (cp >> 12));
    o += (char)(0x80 | ((cp >> 6) & 0x3F));
    o += (char)(0x80 | (cp & 0x3F));
  } else {
    o += (char)(0xF0 | (cp >> 18));
    o += (char)(0x80 | ((cp >> 12) & 0x3F));
    o += (char)(0x80 | ((cp >> 6) & 0x3F));
    o += (char)(0x80 | (cp & 0x3F));
  }
}

inline int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Читает JSON-строку, начиная с открывающей кавычки в позиции p. Возвращает позицию после закрывающей.
inline size_t readString(const std::string& j, size_t p, std::string& out) {
  out.clear();
  if (p >= j.size() || j[p] != '"') return std::string::npos;
  p++;
  while (p < j.size()) {
    char c = j[p];
    if (c == '"') return p + 1;
    if (c != '\\') {
      out += c;
      p++;
      continue;
    }
    if (p + 1 >= j.size()) break;
    char e = j[p + 1];
    p += 2;
    switch (e) {
      case 'n': out += '\n'; break;
      case 't': out += ' '; break;
      case 'r': break;
      case 'b': case 'f': break;
      case 'u': {
        if (p + 4 > j.size()) return std::string::npos;
        uint32_t cp = 0;
        for (int k = 0; k < 4; k++) {
          int h = hexVal(j[p + k]);
          if (h < 0) return std::string::npos;
          cp = (cp << 4) | (uint32_t)h;
        }
        p += 4;
        if (cp >= 0xD800 && cp <= 0xDBFF && p + 6 <= j.size() && j[p] == '\\' && j[p + 1] == 'u') {   // пара суррогатов
          uint32_t lo = 0;
          bool ok = true;
          for (int k = 0; k < 4; k++) {
            int h = hexVal(j[p + 2 + k]);
            if (h < 0) ok = false;
            lo = (lo << 4) | (uint32_t)(h < 0 ? 0 : h);
          }
          if (ok && lo >= 0xDC00 && lo <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            p += 6;
          }
        }
        appendUtf8(out, cp);
        break;
      }
      default: out += e; break;   // \" \\ \/
    }
  }
  return std::string::npos;
}

// Значение строкового поля "name": "..." внутри фрагмента [from, to).
inline bool field(const std::string& j, size_t from, size_t to, const char* name, std::string& out) {
  std::string pat = std::string("\"") + name + "\"";
  size_t p = j.find(pat, from);
  if (p == std::string::npos || p >= to) return false;
  p += pat.size();
  while (p < to && (j[p] == ' ' || j[p] == ':' || j[p] == '\t' || j[p] == '\n' || j[p] == '\r')) p++;
  return readString(j, p, out) != std::string::npos;
}

// Разбор ответа: все элементы массива "hits". Каждый элемент - объект с полями без вложенных объектов.
inline std::vector<Hit> parseHits(const std::string& j) {
  std::vector<Hit> hits;
  size_t p = j.find("\"hits\"");
  if (p == std::string::npos) return hits;
  p = j.find('[', p);
  if (p == std::string::npos) return hits;
  p++;
  while (p < j.size()) {
    size_t a = j.find_first_of("{]", p);
    if (a == std::string::npos || j[a] == ']') break;
    // конец объекта: ищем '}' вне строк
    size_t b = a + 1;
    bool inStr = false;
    for (; b < j.size(); b++) {
      char c = j[b];
      if (inStr) {
        if (c == '\\') b++;
        else if (c == '"') inStr = false;
      } else if (c == '"') {
        inStr = true;
      } else if (c == '}') {
        break;
      }
    }
    if (b >= j.size()) break;
    Hit h;
    if (field(j, a, b, "webformatURL", h.url) && !h.url.empty()) {
      size_t k = h.url.find("_640");
      if (k != std::string::npos) h.url.replace(k, 4, "_180");   // маленькая версия - высотой 180 px
      field(j, a, b, "tags", h.tags);
      field(j, a, b, "user", h.user);
      hits.push_back(h);
    }
    p = b + 1;
  }
  return hits;
}

// Сообщение об ошибке, если Pixabay вернул не JSON (например, "[ERROR 400] "key" is invalid").
inline std::string errorText(const std::string& body) {
  if (body.empty() || body[0] == '{') return "";
  return body.substr(0, 80);
}

}  // namespace pixabay

// ---------- Wikimedia Commons: без ключа, доступен из России ----------
namespace wiki {

using pixabay::Hit;

// Поиск файлов-картинок; миниатюра сразу под область экрана (вписывается в box w x h).
inline std::string searchUrl(const std::string& query, int w, int h) {
  return "https://commons.wikimedia.org/w/api.php?action=query&format=json&formatversion=2"
         "&generator=search&gsrnamespace=6&gsrlimit=20&gsrsearch=" +
         pixabay::urlEncode(query + " filetype:bitmap") +
         "&prop=imageinfo&iiprop=url%7Cmime%7Cuser&iiurlwidth=" + std::to_string(w) +
         "&iiurlheight=" + std::to_string(h);
}

inline int intField(const std::string& j, size_t from, size_t to, const char* name, int def) {
  std::string pat = std::string("\"") + name + "\"";
  size_t p = j.find(pat, from);
  if (p == std::string::npos || p >= to) return def;
  p += pat.size();
  while (p < to && (j[p] == ' ' || j[p] == ':')) p++;
  int v = 0;
  bool any = false;
  while (p < to && j[p] >= '0' && j[p] <= '9') {
    v = v * 10 + (j[p] - '0');
    p++;
    any = true;
  }
  return any ? v : def;
}

// «File:Red_cat_on_grass.jpg» -> «Red cat on grass»
inline std::string niceTitle(std::string t) {
  size_t c = t.find(':');
  if (c != std::string::npos && c < 12) t = t.substr(c + 1);
  size_t d = t.rfind('.');
  if (d != std::string::npos && t.size() - d <= 5) t = t.substr(0, d);
  for (char& ch : t)
    if (ch == '_') ch = ' ';
  return t;
}

// Разбор ответа: страницы в порядке релевантности (поле index), только JPEG.
inline std::vector<Hit> parseHits(const std::string& j) {
  std::vector<std::pair<int, Hit>> tmp;
  std::vector<size_t> starts;
  for (size_t p = j.find("\"pageid\""); p != std::string::npos; p = j.find("\"pageid\"", p + 1)) starts.push_back(p);
  for (size_t i = 0; i < starts.size(); i++) {
    size_t a = starts[i], b = i + 1 < starts.size() ? starts[i + 1] : j.size();
    // поля страницы могут стоять и до "pageid" - начинаем с открывающей скобки объекта
    size_t ob = j.rfind('{', a);
    if (ob != std::string::npos && (i == 0 || ob > starts[i - 1])) a = ob;
    Hit h;
    std::string mime, title;
    if (!pixabay::field(j, a, b, "thumburl", h.url) || h.url.empty()) continue;
    pixabay::field(j, a, b, "mime", mime);
    if (mime != "image/jpeg") continue;                 // PNG/GIF/TIFF декодер не умеет
    pixabay::field(j, a, b, "title", title);
    pixabay::field(j, a, b, "user", h.user);
    h.tags = niceTitle(title);
    tmp.push_back({intField(j, a, b, "index", 1000 + (int)i), h});
  }
  for (size_t x = 1; x < tmp.size(); x++)                // сортировка вставками по index
    for (size_t y = x; y > 0 && tmp[y - 1].first > tmp[y].first; y--) std::swap(tmp[y - 1], tmp[y]);
  std::vector<Hit> out;
  for (auto& t : tmp) out.push_back(t.second);
  return out;
}

}  // namespace wiki
