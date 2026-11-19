// Расписание занятий с сайта вуза (страница группы, сделанная в Word: таблица на каждую неделю).
// Разбор идёт по кусочкам, прямо из сети: вся страница (~200 КБ) в память не загружается.
// Результат - короткий текст, строки через '\n', поля через '\t':
//   G <группа>
//   T <время 1-й пары> ... <время 7-й пары>
//   W <номер недели> <дд.мм.гггг начала> <дд.мм.гггг конца>
//   D <день, как на сайте: «Пнд,16 ноября»>
//   P <номер пары> <предмет> <преподаватель и аудитория>
// Здесь нет ничего от Arduino - разбор проверяется на компьютере.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

namespace rasp {

// windows-1251 -> UTF-8 (страницы Word 97 в этой кодировке)
inline void cp1251(uint8_t c, std::string& out) {
  if (c < 0x80) {
    out += (char)c;
    return;
  }
  uint16_t u;
  if (c >= 0xC0) u = 0x0410 + (c - 0xC0);           // А..я
  else if (c == 0xA8) u = 0x0401;                    // Ё
  else if (c == 0xB8) u = 0x0451;                    // ё
  else if (c == 0xA0) u = ' ';
  else if (c == 0xB9) u = 0x2116;                    // №
  else if (c == 0x96 || c == 0x97) u = '-';
  else if (c == 0xAB || c == 0xBB || c == 0x93 || c == 0x94) u = '"';
  else u = '?';
  if (u < 0x80) {
    out += (char)u;
  } else if (u < 0x800) {
    out += (char)(0xC0 | (u >> 6));
    out += (char)(0x80 | (u & 0x3F));
  } else {
    out += (char)(0xE0 | (u >> 12));
    out += (char)(0x80 | ((u >> 6) & 0x3F));
    out += (char)(0x80 | (u & 0x3F));
  }
}

inline std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\n' || s[a] == '\t')) a++;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\n' || s[b - 1] == '\t')) b--;
  return s.substr(a, b - a);
}
// Схлопнуть пробелы; '\n' оставить (это <br>)
inline std::string squash(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '\t' || c == '\r') c = ' ';
    if (c == ' ' && (o.empty() || o.back() == ' ' || o.back() == '\n')) continue;
    if (c == '\n') {
      while (!o.empty() && o.back() == ' ') o.pop_back();
      if (o.empty() || o.back() == '\n') continue;
    }
    o += c;
  }
  return trim(o);
}

struct Parser {
  std::string out;                   // готовый результат (вызывающий забирает и очищает)
  std::string text;                  // текст вне ячеек (там «Неделя: 12-я (...)»)
  std::string cell;                  // текущая ячейка
  std::vector<std::string> row;
  std::string tag, ent;
  bool inTag = false, inCell = false, inComment = false, inEnt = false;
  std::string commentTail;
  bool timesDone = false, groupDone = false;
  int weeks = 0;

  void addText(const std::string& s) {
    if (inCell) cell += s;
    else {
      text += s;
      if (text.size() > 600) text.erase(0, text.size() - 300);
    }
  }

  void onTag() {
    std::string t;
    for (char c : tag) {
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') break;
      t += (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    if (t == "td" || t == "th") {
      inCell = true;
      cell.clear();
    } else if (t == "/td" || t == "/th") {
      if (inCell) row.push_back(squash(cell));
      inCell = false;
    } else if (t == "br" || t == "br/" || t == "/p" || t == "p") {
      addText("\n");
    } else if (t == "/tr") {
      onRow();
      row.clear();
    } else if (t == "table") {
      onTableStart();
    }
  }

  // Перед каждой таблицей: «Расписание занятий учебной группы: ЭЭ-11 / Неделя: 12-я (16.11.2026-22.11.2026)»
  void onTableStart() {
    std::string s = squash(text);
    text.clear();
    size_t g = s.find("группы:");
    if (!groupDone && g != std::string::npos) {
      size_t a = g + strlen("группы:");
      while (a < s.size() && (s[a] == ' ' || s[a] == '\n')) a++;
      size_t b = a;
      while (b < s.size() && s[b] != ' ' && s[b] != '\n') b++;
      out += "G\t" + s.substr(a, b - a) + "\n";
      groupDone = true;
    }
    size_t w = s.find("Неделя:");
    if (w == std::string::npos) return;
    size_t a = w + strlen("Неделя:");
    while (a < s.size() && s[a] == ' ') a++;
    int num = 0;
    while (a < s.size() && s[a] >= '0' && s[a] <= '9') num = num * 10 + (s[a++] - '0');
    size_t p = s.find('(', a), q = s.find(')', a);
    std::string d1, d2;
    if (p != std::string::npos && q != std::string::npos && q > p) {
      std::string r = s.substr(p + 1, q - p - 1);
      size_t m = r.find('-');
      if (m != std::string::npos) {
        d1 = trim(r.substr(0, m));
        d2 = trim(r.substr(m + 1));
      }
    }
    out += "W\t" + std::to_string(num) + "\t" + d1 + "\t" + d2 + "\n";
    weeks++;
  }

  void onRow() {
    if (row.empty()) return;
    std::string first = row[0];
    for (char& c : first)
      if (c == '\n') c = ' ';
    if (first.rfind("Пары", 0) == 0) return;
    if (first.rfind("Время", 0) == 0) {
      if (timesDone) return;
      std::string l = "T";
      for (size_t i = 1; i < row.size() && i <= 7; i++) l += "\t" + row[i];
      out += l + "\n";
      timesDone = true;
      return;
    }
    // строка дня: «Пнд,16 ноября» и семь ячеек пар
    std::string day = squash(first);
    size_t comma = day.find(',');
    if (comma != std::string::npos && comma + 1 < day.size() && day[comma + 1] == ' ') day.erase(comma + 1, 1);
    out += "D\t" + day + "\n";
    for (size_t i = 1; i < row.size() && i <= 7; i++) {
      std::string c = row[i];
      if (c.empty()) continue;
      size_t nl = c.find('\n');
      std::string subj = trim(nl == std::string::npos ? c : c.substr(0, nl));
      std::string rest = nl == std::string::npos ? "" : c.substr(nl + 1);
      for (char& ch : rest)
        if (ch == '\n') ch = ' ';
      out += "P\t" + std::to_string(i) + "\t" + subj + "\t" + squash(rest) + "\n";
    }
  }

  void onEntity() {
    if (ent == "nbsp") addText(" ");
    else if (ent == "amp") addText("&");
    else if (ent == "quot") addText("\"");
    else if (ent == "lt") addText("<");
    else if (ent == "gt") addText(">");
    else if (!ent.empty() && ent[0] == '#') addText(" ");
  }

  void feed(const uint8_t* p, size_t n) {
    std::string u;
    for (size_t i = 0; i < n; i++) {
      uint8_t c = p[i];
      if (inComment) {                               // <!-- ... -->
        commentTail += (char)c;
        if (commentTail.size() > 3) commentTail.erase(0, commentTail.size() - 3);
        if (commentTail == "-->") inComment = false;
        continue;
      }
      if (inTag) {
        if (c == '>') {
          inTag = false;
          onTag();
        } else {
          if (tag.size() < 64) tag += (char)c;
          if (tag == "!--") {
            inTag = false;
            inComment = true;
            commentTail.clear();
          }
        }
        continue;
      }
      if (inEnt) {
        if (c == ';' || ent.size() > 8) {
          inEnt = false;
          onEntity();
        } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '#') {
          ent += (char)c;
        } else {
          inEnt = false;
          onEntity();
          i--;                                       // этот символ - уже обычный текст
        }
        continue;
      }
      if (c == '<') {
        inTag = true;
        tag.clear();
        continue;
      }
      if (c == '&') {
        inEnt = true;
        ent.clear();
        continue;
      }
      if (c == '\r' || c == '\n') c = ' ';
      u.clear();
      cp1251(c, u);
      addText(u);
    }
  }
};

// ---------- чтение готового текста ----------
struct Pair {
  int num;
  std::string subj, who;
};
struct Day {
  int week, y, m, d;                 // дата
  int wd;                            // 0 - понедельник
  std::string label;
  std::vector<Pair> pairs;
};
struct Schedule {
  std::string group;
  std::string times[8];              // [1..7]
  std::vector<Day> days;
};

inline bool parseDate(const std::string& s, int& d, int& m, int& y) {
  return sscanf(s.c_str(), "%d.%d.%d", &d, &m, &y) == 3;
}
// Номер дня от 1 января 1970 (для сравнения дат и сдвига на N дней)
inline long dayNum(int y, int m, int d) {
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  long yoe = y - era * 400;
  long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}
inline void fromDayNum(long z, int& y, int& m, int& d) {
  z += 719468;
  long era = (z >= 0 ? z : z - 146096) / 146097;
  long doe = z - era * 146097;
  long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  long yy = yoe + era * 400;
  long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  long mp = (5 * doy + 2) / 153;
  d = (int)(doy - (153 * mp + 2) / 5 + 1);
  m = (int)(mp < 10 ? mp + 3 : mp - 9);
  y = (int)(yy + (m <= 2));
}

inline int weekdayOf(const std::string& label) {
  static const char* const W[] = {"Пнд", "Втр", "Срд", "Чтв", "Птн", "Сбт", "Вск"};
  for (int i = 0; i < 7; i++)
    if (label.rfind(W[i], 0) == 0) return i;
  return -1;
}

// Разобрать готовый текст. fromDay - пропустить недели, закончившиеся раньше этого дня (0 - взять все).
inline void load(const std::string& txt, Schedule& s, long fromDay) {
  s.days.clear();
  int week = 0;
  long wStart = 0;
  bool skip = false;
  size_t pos = 0;
  while (pos < txt.size()) {
    size_t e = txt.find('\n', pos);
    if (e == std::string::npos) e = txt.size();
    std::string line = txt.substr(pos, e - pos);
    pos = e + 1;
    std::vector<std::string> f;
    size_t a = 0;
    for (;;) {
      size_t t = line.find('\t', a);
      f.push_back(line.substr(a, t == std::string::npos ? std::string::npos : t - a));
      if (t == std::string::npos) break;
      a = t + 1;
    }
    if (f[0] == "G" && f.size() > 1) s.group = f[1];
    else if (f[0] == "T") {
      for (size_t i = 1; i < f.size() && i <= 7; i++) s.times[i] = f[i];
    } else if (f[0] == "W" && f.size() >= 4) {
      week = atoi(f[1].c_str());
      int d, m, y, d2, m2, y2;
      wStart = parseDate(f[2], d, m, y) ? dayNum(y, m, d) : 0;
      long wEnd = parseDate(f[3], d2, m2, y2) ? dayNum(y2, m2, d2) : wStart + 6;
      skip = fromDay && wEnd < fromDay;
    } else if (f[0] == "D" && f.size() >= 2) {
      if (skip) continue;
      Day d;
      d.week = week;
      d.label = f[1];
      d.wd = weekdayOf(f[1]);
      long dn = wStart + (d.wd >= 0 ? d.wd : 0);
      fromDayNum(dn, d.y, d.m, d.d);
      s.days.push_back(d);
    } else if (f[0] == "P" && f.size() >= 3 && !s.days.empty() && !skip) {
      Pair p;
      p.num = atoi(f[1].c_str());
      p.subj = f[2];
      p.who = f.size() > 3 ? f[3] : "";
      s.days.back().pairs.push_back(p);
    }
  }
}

}  // namespace rasp
