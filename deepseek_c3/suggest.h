// Подсказки слов: базовый частотный словарь + слова, выученные из вопросов и ответов.
// Не зависит от Arduino (сохранение в файл делает скетч), поэтому тестируется на компьютере.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include "textutil.h"

// Буква (латиница или кириллица) - из таких символов состоят слова.
inline bool isLetterCp(uint32_t c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= 0x410 && c <= 0x44F) || c == 0x401 || c == 0x451;
}

inline bool isUpperCp(uint32_t c) {
  return (c >= 'A' && c <= 'Z') || (c >= 0x410 && c <= 0x42F) || c == 0x401;
}

inline uint16_t toLowerCp(uint16_t c) {
  if (c >= 'A' && c <= 'Z') return c + 32;
  if (c >= 0x410 && c <= 0x42F) return c + 0x20;
  if (c == 0x401) return 0x451;
  return c;
}

inline uint16_t toUpperCp(uint16_t c) {
  if (c >= 'a' && c <= 'z') return c - 32;
  if (c >= 0x430 && c <= 0x44F) return c - 0x20;
  if (c == 0x451) return 0x401;
  return c;
}

// Недописанное слово в конце текста: сколько последних символов - буквы.
inline size_t partialWordLen(const std::vector<uint16_t>& text) {
  size_t n = 0;
  while (n < text.size() && isLetterCp(text[text.size() - 1 - n])) n++;
  return n;
}

class Suggester {
 public:
  struct Learned {
    std::vector<uint16_t> w;   // слово строчными буквами
    uint16_t count;
  };

  static const size_t MAX_LEARNED = 1200;
  static const size_t MAX_WORD = 24;

  // dicts: строки вида "слово\nслово\n...", слова идут от самых частых к редким.
  void addDict(const char* words) { dicts_.push_back(words); }

  // Варианты для недописанного слова prefix (как набрано, с учётом регистра).
  std::vector<std::vector<uint16_t>> suggest(const std::vector<uint16_t>& prefix, size_t maxN = 3) const {
    std::vector<std::vector<uint16_t>> out;
    if (prefix.empty()) return out;
    std::vector<uint16_t> low(prefix.size());
    for (size_t i = 0; i < prefix.size(); i++) low[i] = toLowerCp(prefix[i]);

    std::vector<Cand> cands;
    // Базовые словари: чем раньше слово в списке, тем выше оценка (100..1000).
    std::vector<uint16_t> w;
    for (const char* d : dicts_) {
      size_t total = countWords(d), rank = 0;
      const char* p = d;
      while (*p) {
        const char* e = p;
        while (*e && *e != '\n') e++;
        w.clear();
        std::string tmp(p, e);
        utf8ToCps(tmp.c_str(), w);
        if (startsWith(w, low) && w.size() > low.size())
          addCand(cands, w, 100 + (int)((total - rank) * 900 / (total ? total : 1)));
        rank++;
        p = *e ? e + 1 : e;
      }
    }
    // Выученные слова: каждое употребление добавляет 300 (3 раза - выше самых частых слов).
    for (const Learned& l : learned_)
      if (startsWith(l.w, low) && l.w.size() > low.size()) addCand(cands, l.w, (int)l.count * 300);

    // Лучшие maxN по оценке.
    for (size_t k = 0; k < maxN; k++) {
      int best = -1;
      for (size_t i = 0; i < cands.size(); i++)
        if (!cands[i].used && (best < 0 || cands[i].score > cands[(size_t)best].score)) best = (int)i;
      if (best < 0) break;
      cands[(size_t)best].used = true;
      std::vector<uint16_t> s = cands[(size_t)best].w;
      // Регистр как у набранного: Слово -> Подсказка, СЛОВО -> ПОДСКАЗКА.
      bool allUp = prefix.size() > 1;
      for (uint16_t c : prefix) if (!isUpperCp(c)) allUp = false;
      if (allUp) for (auto& c : s) c = toUpperCp(c);
      else if (isUpperCp(prefix[0])) s[0] = toUpperCp(s[0]);
      out.push_back(s);
    }
    return out;
  }

  // Запоминает все слова из текста (вопроса или ответа). Возвращает true, если что-то изменилось.
  bool learnText(const char* utf8) {
    std::vector<uint16_t> cps;
    utf8ToCps(utf8, cps);
    bool changed = false;
    std::vector<uint16_t> w;
    for (size_t i = 0; i <= cps.size(); i++) {
      if (i < cps.size() && isLetterCp(cps[i])) {
        w.push_back(toLowerCp(cps[i]));
      } else {
        if (w.size() >= 3 && w.size() <= MAX_WORD) {
          learnWord(w);
          changed = true;
        }
        w.clear();
      }
    }
    return changed;
  }

  void learnWord(const std::vector<uint16_t>& w, uint16_t add = 1) {
    for (Learned& l : learned_)
      if (l.w == w) {
        l.count = (uint16_t)(l.count + add > 60000 ? 60000 : l.count + add);
        return;
      }
    if (learned_.size() >= MAX_LEARNED) {   // вытесняем самое редкое (из самых старых)
      size_t worst = 0;
      for (size_t i = 1; i < learned_.size(); i++)
        if (learned_[i].count < learned_[worst].count) worst = i;
      learned_.erase(learned_.begin() + (long)worst);
    }
    learned_.push_back({w, add});
  }

  // Сохранение: строки "число слово".
  std::string serialize() const {
    std::string s;
    for (const Learned& l : learned_) {
      s += std::to_string(l.count);
      s += ' ';
      s += cpsToUtf8(l.w.data(), l.w.size());
      s += '\n';
    }
    return s;
  }

  void deserialize(const char* data) {
    learned_.clear();
    const char* p = data;
    while (*p) {
      const char* e = p;
      while (*e && *e != '\n') e++;
      std::string line(p, e);
      size_t sp = line.find(' ');
      if (sp != std::string::npos && sp > 0) {
        int cnt = atoi(line.substr(0, sp).c_str());
        std::vector<uint16_t> w;
        utf8ToCps(line.substr(sp + 1).c_str(), w);
        bool ok = cnt > 0 && w.size() >= 2 && w.size() <= MAX_WORD;
        for (uint16_t c : w) if (!isLetterCp(c)) ok = false;
        if (ok && learned_.size() < MAX_LEARNED) learned_.push_back({w, (uint16_t)(cnt > 60000 ? 60000 : cnt)});
      }
      p = *e ? e + 1 : e;
    }
  }

  size_t learnedCount() const { return learned_.size(); }
  void clearLearned() { learned_.clear(); }

 private:
  struct Cand {
    std::vector<uint16_t> w;
    int score;
    bool used;
  };
  std::vector<const char*> dicts_;
  std::vector<Learned> learned_;

  static size_t countWords(const char* d) {
    size_t n = 0;
    for (const char* p = d; *p; p++) if (*p == '\n') n++;
    return n + 1;
  }

  static bool startsWith(const std::vector<uint16_t>& w, const std::vector<uint16_t>& pre) {
    if (w.size() < pre.size()) return false;
    for (size_t i = 0; i < pre.size(); i++) if (w[i] != pre[i]) return false;
    return true;
  }

  static void addCand(std::vector<Cand>& c, const std::vector<uint16_t>& w, int score) {
    for (Cand& x : c)
      if (x.w == w) {
        x.score += score;
        return;
      }
    c.push_back({w, score, false});
  }
};
