// ИК-пульт: разбор файлов .ir (формат Flipper Zero, база Flipper-IRDB) и превращение команды
// в последовательность «вспышка - пауза» в микросекундах. Плюс разбор списка папок GitHub.
// Чистая логика без железа - тестируется на компьютере.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <vector>

namespace ir {

struct Signal {
  bool hasType = false;
  bool raw = false;
  std::string name, protocol;
  uint32_t address = 0, command = 0;
  uint32_t freq = 38000;
  std::vector<uint32_t> data;          // для raw: вспышка, пауза, вспышка...
};

inline std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) b--;
  return s.substr(a, b - a);
}

// «ключ: значение». false - строка не такого вида (комментарий, пустая).
inline bool splitLine(const std::string& line, std::string& key, std::string& val) {
  std::string t = trim(line);
  if (t.empty() || t[0] == '#') return false;
  size_t c = t.find(':');
  if (c == std::string::npos || c == 0) return false;
  key = trim(t.substr(0, c));
  val = trim(t.substr(c + 1));
  return true;
}

// «04 00 00 00» -> число (младший байт первым).
inline uint32_t hexBytes(const std::string& v) {
  uint32_t r = 0;
  int n = 0;
  size_t i = 0;
  while (i < v.size() && n < 4) {
    while (i < v.size() && v[i] == ' ') i++;
    if (i >= v.size()) break;
    r |= (uint32_t)(strtoul(v.substr(i, 2).c_str(), nullptr, 16) & 0xFF) << (8 * n);
    n++;
    while (i < v.size() && v[i] != ' ') i++;
  }
  return r;
}

// Очередная строка описания кнопки (после name).
inline void feedLine(Signal& s, const std::string& line) {
  std::string k, v;
  if (!splitLine(line, k, v)) return;
  if (k == "name") s.name = v;
  else if (k == "type") {
    s.hasType = true;
    s.raw = v == "raw";
  } else if (k == "protocol") s.protocol = v;
  else if (k == "address") s.address = hexBytes(v);
  else if (k == "command") s.command = hexBytes(v);
  else if (k == "frequency") s.freq = (uint32_t)strtoul(v.c_str(), nullptr, 10);
  else if (k == "data") {
    const char* p = v.c_str();
    while (*p && s.data.size() < 2048) {
      char* e;
      unsigned long t = strtoul(p, &e, 10);
      if (e == p) break;
      s.data.push_back((uint32_t)t);
      p = e;
    }
  }
}

// ---------- кодирование ----------
struct Frame {
  std::vector<uint32_t> t;             // вспышка, пауза, вспышка... (начинается со вспышки)
  uint32_t freq = 38000;               // несущая частота, Гц
  int repeats = 1;                     // сколько раз послать кадр
  uint32_t period = 0;                 // от начала кадра до начала следующего, мкс (для повторов)
};

// Собираем сигнал из отрезков «уровень, длительность»: соседние одинаковые склеиваются.
class Builder {
 public:
  std::vector<uint32_t> t;
  void add(bool on, uint32_t us) {
    if (t.empty() && !on) return;                        // пауза в самом начале не нужна
    bool lastOn = (t.size() & 1) == 1;                   // нечётное число отрезков - последний был вспышкой
    if (!t.empty() && lastOn == on) t.back() += us;
    else t.push_back(us);
  }
  void mark(uint32_t us) { add(true, us); }
  void space(uint32_t us) { add(false, us); }
  // Биты «длиной паузы»: вспышка одинаковая, пауза длинная (1) или короткая (0).
  void bitsLsb(uint64_t v, int n, uint32_t m, uint32_t one, uint32_t zero) {
    for (int i = 0; i < n; i++) {
      mark(m);
      space((v >> i) & 1 ? one : zero);
    }
  }
  void bitsMsb(uint64_t v, int n, uint32_t m, uint32_t one, uint32_t zero) {
    for (int i = n - 1; i >= 0; i--) {
      mark(m);
      space((v >> i) & 1 ? one : zero);
    }
  }
  void finish() {
    if (!t.empty() && (t.size() & 1) == 0) t.pop_back();  // пауза в конце не нужна
  }
};

inline uint32_t total(const std::vector<uint32_t>& t) {
  uint32_t s = 0;
  for (uint32_t v : t) s += v;
  return s;
}

// Команда -> кадр. false - протокол неизвестен или данных нет.
inline bool encode(const Signal& s, Frame& f) {
  f = Frame();
  if (s.raw) {
    if (s.data.size() < 2) return false;
    f.t = s.data;
    if ((f.t.size() & 1) == 0) f.t.pop_back();
    f.freq = s.freq >= 20000 && s.freq <= 60000 ? s.freq : 38000;
    return true;
  }
  const std::string& p = s.protocol;
  uint32_t a = s.address, c = s.command;
  Builder b;
  if (p == "NEC" || p == "NECext" || p == "NEC42" || p == "NEC42ext" || p == "Pioneer") {
    bool pio = p == "Pioneer";
    uint32_t m = pio ? 500 : 560, one = pio ? 1500 : 1690, zero = pio ? 500 : 560;
    b.mark(pio ? 8500 : 9000);
    b.space(pio ? 4225 : 4500);
    if (p == "NEC42") {
      uint64_t v = (uint64_t)(a & 0x1FFF) | ((uint64_t)(~a & 0x1FFF) << 13) | ((uint64_t)(c & 0xFF) << 26) | ((uint64_t)(~c & 0xFF) << 34);
      b.bitsLsb(v, 42, m, one, zero);
    } else if (p == "NEC42ext") {
      uint64_t v = (uint64_t)(a & 0x3FFFFFF) | ((uint64_t)(c & 0xFFFF) << 26);
      b.bitsLsb(v, 42, m, one, zero);
    } else if (p == "NECext") {
      b.bitsLsb((a & 0xFFFF) | ((uint64_t)(c & 0xFFFF) << 16), 32, m, one, zero);
    } else {
      b.bitsLsb((a & 0xFF) | ((~a & 0xFF) << 8) | ((uint64_t)(c & 0xFF) << 16) | ((uint64_t)(~c & 0xFF) << 24), 32, m, one, zero);
    }
    b.mark(m);
    f.freq = pio ? 40000 : 38000;
  } else if (p == "Samsung32") {
    b.mark(4500);
    b.space(4500);
    b.bitsLsb((a & 0xFF) | ((a & 0xFF) << 8) | ((uint64_t)(c & 0xFF) << 16) | ((uint64_t)(~c & 0xFF) << 24), 32, 550, 1650, 550);
    b.mark(550);
  } else if (p == "SIRC" || p == "SIRC15" || p == "SIRC20") {
    int abits = p == "SIRC" ? 5 : p == "SIRC15" ? 8 : 13;
    uint32_t v = (c & 0x7F) | ((a & ((1u << abits) - 1)) << 7);
    b.mark(2400);
    b.space(600);
    for (int i = 0; i < 7 + abits; i++) {                // у Sony бит - длина вспышки
      b.mark((v >> i) & 1 ? 1200 : 600);
      b.space(600);
    }
    f.freq = 40000;
    f.repeats = 3;                                       // техника Sony ждёт команду минимум трижды
    f.period = 45000;
  } else if (p == "RC5" || p == "RC5X") {
    const uint32_t T = 889;
    bool s2 = p == "RC5" ? true : !(c & 0x40);
    uint32_t v = (1u << 13) | ((s2 ? 1u : 0u) << 12) | ((a & 0x1F) << 6) | (c & 0x3F);   // старт, второй старт, флажок 0, адрес, команда
    for (int i = 13; i >= 0; i--) {
      bool one = (v >> i) & 1;                           // 1: пауза-вспышка, 0: вспышка-пауза
      b.add(!one, T);
      b.add(one, T);
    }
    f.freq = 36000;
  } else if (p == "RC6") {
    const uint32_t T = 444;
    b.mark(6 * T);
    b.space(2 * T);
    uint32_t v = (1u << 20) | ((a & 0xFF) << 8) | (c & 0xFF);   // старт 1, режим 000, флажок 0, адрес, команда = 21 бит
    for (int i = 20; i >= 0; i--) {
      bool one = (v >> i) & 1;                           // 1: вспышка-пауза, 0: пауза-вспышка
      uint32_t w = i == 16 ? 2 * T : T;                  // флажок вдвое длиннее
      b.add(one, w);
      b.add(!one, w);
    }
    f.freq = 36000;
  } else if (p == "Kaseikyo") {
    const uint32_t T = 432;
    uint8_t id = (a >> 24) & 3;
    uint16_t vendor = (a >> 8) & 0xFFFF;
    uint8_t g1 = (a >> 4) & 0x0F, g2 = a & 0x0F;
    uint8_t d[6];
    d[0] = vendor & 0xFF;
    d[1] = vendor >> 8;
    uint8_t vp = d[0] ^ d[1];
    vp = (vp & 0x0F) ^ (vp >> 4);
    d[2] = (uint8_t)((vp & 0x0F) | (g1 << 4));
    d[3] = (uint8_t)((g2 & 0x0F) | ((c & 0x0F) << 4));
    d[4] = (uint8_t)((id << 6) | ((c >> 4) & 0x3F));
    d[5] = d[2] ^ d[3] ^ d[4];
    b.mark(8 * T);
    b.space(4 * T);
    for (int i = 0; i < 6; i++) b.bitsLsb(d[i], 8, T, 3 * T, T);
    b.mark(T);
    f.freq = 37000;
  } else if (p == "RCA") {
    b.mark(4000);
    b.space(4000);
    uint32_t v = ((a & 0x0F) << 20) | ((c & 0xFF) << 12) | ((~a & 0x0F) << 8) | (~c & 0xFF);
    b.bitsMsb(v, 24, 500, 2000, 1000);
    b.mark(500);
  } else {
    return false;
  }
  b.finish();
  f.t = b.t;
  return !f.t.empty();
}

// ---------- список папок GitHub (ответ api.github.com/repos/.../contents/...) ----------
// Ответ большой (сотни килобайт), поэтому разбирается по одной букве, без хранения целиком.
// Нужны только пары "name" и "type" каждого элемента. Папки получают «/» на конце.
class DirScan {
 public:
  std::vector<std::string> items;
  size_t maxItems = 1200;

  void feed(char ch) {
    if (inStr) {
      if (esc) {
        esc = false;
        if (cur.size() < 160) cur += ch == 'n' ? ' ' : ch;
      } else if (ch == '\\') {
        esc = true;
      } else if (ch == '"') {
        inStr = false;
        strDone = true;
        afterColon = pendingColon;
        pendingColon = false;
      } else if (cur.size() < 160) {
        cur += ch;
      }
      return;
    }
    if (ch == '"') {
      if (strDone && !afterColon) lastKey.clear();       // прошлая строка не была ни ключом, ни значением
      inStr = true;
      cur.clear();
      return;
    }
    if (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') return;
    if (ch == ':') {
      if (strDone && !afterColon) {                      // строка перед двоеточием - ключ
        lastKey = cur;
        pendingColon = true;
      }
      strDone = false;
      return;
    }
    if (strDone && afterColon) value();                  // строка после двоеточия - значение
    strDone = false;
    afterColon = false;
    pendingColon = false;
    if (ch == '{') depth++;
    if (ch == '}') depth--;
  }

  void finish() {
    if (strDone && afterColon) value();
    strDone = false;
  }

 private:
  bool inStr = false, esc = false, strDone = false, afterColon = false, pendingColon = false;
  int depth = 0;
  std::string cur, lastKey, name;

  void value() {
    if (depth == 1 && lastKey == "name") name = cur;
    else if (depth == 1 && lastKey == "type" && !name.empty()) {
      if (items.size() < maxItems) {
        if (cur == "dir") items.push_back(name + "/");
        else if (name.size() > 3 && name.compare(name.size() - 3, 3, ".ir") == 0) items.push_back(name);
      }
      name.clear();
    }
    lastKey.clear();
  }
};

// Путь в адресе: пробелы и прочее - через %XX, «/» остаётся.
inline std::string urlPath(const std::string& p) {
  static const char* HEXD = "0123456789ABCDEF";
  std::string o;
  for (unsigned char c : p) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') o += (char)c;
    else {
      o += '%';
      o += HEXD[c >> 4];
      o += HEXD[c & 15];
    }
  }
  return o;
}

// Название пульта из пути к файлу: «TVs/Samsung/Samsung_BN59.ir» -> «Samsung BN59».
inline std::string titleFromPath(const std::string& path) {
  size_t sl = path.find_last_of("/\\");
  std::string n = sl == std::string::npos ? path : path.substr(sl + 1);
  if (n.size() > 3 && n.compare(n.size() - 3, 3, ".ir") == 0) n.erase(n.size() - 3);
  for (char& c : n)
    if (c == '_' || (unsigned char)c < 32) c = ' ';
  n = trim(n);
  if (n.size() > 40) n.erase(40);
  return n.empty() ? std::string("Remote") : n;
}

}  // namespace ir
