// Игры с DeepSeek: запросы и разбор ответов (Викторина, Приключение, «Угадай, кто я»).
// Метки формата (Q:, A), ANSWER: ...) всегда латиницей - текст на языке интерфейса.
// Чистые функции на std::string - тестируются на компьютере.
#pragma once
#include <string>
#include <vector>

namespace aigames {

// ---------- вспомогательное ----------
inline std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '*' || s[a] == '#')) a++;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '*')) b--;
  return s.substr(a, b - a);
}

inline std::vector<std::string> lines(const std::string& s) {
  std::vector<std::string> out;
  size_t p = 0;
  while (p <= s.size()) {
    size_t e = s.find('\n', p);
    if (e == std::string::npos) e = s.size();
    out.push_back(trim(s.substr(p, e - p)));
    p = e + 1;
  }
  return out;
}

inline bool startsWithNoCase(const std::string& s, const char* pre) {
  size_t i = 0;
  for (; pre[i]; i++) {
    if (i >= s.size()) return false;
    char a = s[i], b = pre[i];
    if (a >= 'a' && a <= 'z') a = (char)(a - 32);
    if (b >= 'a' && b <= 'z') b = (char)(b - 32);
    if (a != b) return false;
  }
  return true;
}

// Значение после метки «TAG:» (или «TAG：» китайским двоеточием).
inline bool tagValue(const std::string& line, const char* tag, std::string& out) {
  if (!startsWithNoCase(line, tag)) return false;
  size_t p = 0;
  while (tag[p]) p++;
  if (line.compare(p, 3, "\xEF\xBC\x9A") == 0) p += 3;          // ：
  else if (p < line.size() && line[p] == ':') p++;
  else return false;
  out = trim(line.substr(p));
  return true;
}

// Вариант ответа «A) текст», «A. текст», «A: текст»; кириллические А/В/С тоже понимаем.
inline int optionLetter(const std::string& line, std::string& text) {
  int idx = -1;
  size_t len = 0;
  if (!line.empty() && line[0] >= 'A' && line[0] <= 'D') { idx = line[0] - 'A'; len = 1; }
  else if (!line.empty() && line[0] >= 'a' && line[0] <= 'd') { idx = line[0] - 'a'; len = 1; }
  else if (line.compare(0, 2, "\xD0\x90") == 0) { idx = 0; len = 2; }   // А
  else if (line.compare(0, 2, "\xD0\x92") == 0) { idx = 1; len = 2; }   // В
  else if (line.compare(0, 2, "\xD0\xA1") == 0) { idx = 2; len = 2; }   // С
  else if (line.compare(0, 2, "\xD0\x94") == 0) { idx = 3; len = 2; }   // Д
  if (idx < 0 || line.size() <= len) return -1;
  char sep = line[len];
  if (sep != ')' && sep != '.' && sep != ':') return -1;
  text = trim(line.substr(len + 1));
  return text.empty() ? -1 : idx;
}

// Номер варианта «1) текст» / «1. текст».
inline int optionNumber(const std::string& line, std::string& text) {
  if (line.size() < 3 || line[0] < '1' || line[0] > '9') return -1;
  if (line[1] != ')' && line[1] != '.') return -1;
  text = trim(line.substr(2));
  return text.empty() ? -1 : line[0] - '1';
}

// ======================= ВИКТОРИНА =======================
inline std::string quizSystem(const std::string& lang, const std::string& topic) {
  return "You are a quiz host. Write all text in " + lang +
         ". Create ONE new multiple-choice question on the topic: " + topic +
         ". It must be different from all previous questions in this conversation and have exactly one correct "
         "answer. Use exactly this format with the tags in English:\n"
         "Q: question (max 120 characters)\nA) option\nB) option\nC) option\nD) option\n"
         "ANSWER: letter A, B, C or D\nWHY: short explanation (max 200 characters)\n"
         "Options max 40 characters each. Plain text only, no markdown, no emoji.";
}

struct Quiz {
  std::string question, why;
  std::string option[4];
  int answer = -1;
};

inline bool parseQuiz(const std::string& text, Quiz& q) {
  q = Quiz();
  int got = 0;
  std::string v;
  for (const std::string& l : lines(text)) {
    if (l.empty()) continue;
    if (tagValue(l, "Q", v) || tagValue(l, "QUESTION", v)) {
      q.question = v;
      continue;
    }
    if (tagValue(l, "ANSWER", v)) {
      std::string t;
      q.answer = optionLetter(v.substr(0, (!v.empty() && (v[0] & 0x80)) ? 2 : 1) + ")x", t);   // первая буква
      continue;
    }
    if (tagValue(l, "WHY", v)) {
      q.why = v;
      continue;
    }
    int i = optionLetter(l, v);
    if (i >= 0 && q.option[i].empty()) {
      q.option[i] = v;
      got++;
    }
  }
  return !q.question.empty() && got == 4 && q.answer >= 0 && q.answer < 4;
}

// ======================= ПРИКЛЮЧЕНИЕ =======================
inline std::string storySystem(const std::string& lang, const std::string& genre) {
  return "You are the narrator of an interactive text adventure in the genre: " + genre +
         ". Write all text in " + lang +
         ". Each reply: a short scene of 2-4 sentences (max 350 characters) and then exactly 3 choices for the "
         "player. Use exactly this format with the tags in English:\n"
         "STORY: scene text\n1) choice (max 40 characters)\n2) choice\n3) choice\n"
         "Keep the story consistent with previous turns. After about 12-15 turns, or if the hero dies or wins, "
         "write the final scene as STORY: ... and then a line END instead of choices. "
         "Plain text only, no markdown, no emoji.";
}

struct StoryTurn {
  std::string story;
  std::vector<std::string> choices;
  bool end = false;
};

inline bool parseStory(const std::string& text, StoryTurn& t) {
  t = StoryTurn();
  bool inStory = false;
  std::string v;
  for (const std::string& l : lines(text)) {
    if (l.empty()) continue;
    if (startsWithNoCase(l, "END") && l.size() <= 5) {
      t.end = true;
      inStory = false;
      continue;
    }
    if (tagValue(l, "STORY", v)) {
      t.story = v;
      inStory = true;
      continue;
    }
    int n = optionNumber(l, v);
    if (n >= 0) {
      inStory = false;
      if ((size_t)n == t.choices.size()) t.choices.push_back(v);
      continue;
    }
    if (inStory) t.story += " " + l;                     // сцена на нескольких строках
  }
  if (t.story.empty()) {                                 // без метки - берём текст до вариантов
    for (const std::string& l : lines(text)) {
      if (optionNumber(l, v) >= 0) break;
      if (!l.empty() && !(startsWithNoCase(l, "END") && l.size() <= 5)) t.story += (t.story.empty() ? "" : " ") + l;
    }
  }
  if (t.end) t.choices.clear();
  return !t.story.empty() && (t.end || t.choices.size() >= 2);
}

// ======================= УГАДАЙ, КТО Я =======================
inline std::string guessSystem(const std::string& lang, const std::string& category) {
  return "You run a guessing game. Write all text in " + lang + ". Secretly pick one well-known " + category +
         " that most people would know. Reply in exactly this format with the tags in English:\n"
         "SECRET: name\nOPTIONS: four names separated by | (the secret is one of them, at a random position)\n"
         "HINT: hint 1 (vague)\nHINT: hint 2\nHINT: hint 3\nHINT: hint 4\nHINT: hint 5 (almost gives it away)\n"
         "Each hint max 90 characters and must not contain the name. Plain text only, no markdown.";
}

struct Guess {
  std::string secret;
  std::vector<std::string> options;
  std::vector<std::string> hints;
  int answer = -1;     // номер правильного варианта
};

inline std::string lowerAscii(const std::string& s) {
  std::string o = s;
  for (char& c : o)
    if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
  return o;
}

inline bool parseGuess(const std::string& text, Guess& g, unsigned salt = 0) {
  g = Guess();
  std::string v;
  for (const std::string& l : lines(text)) {
    if (l.empty()) continue;
    if (tagValue(l, "SECRET", v)) g.secret = v;
    else if (tagValue(l, "OPTIONS", v)) {
      size_t p = 0;
      while (p <= v.size()) {
        size_t e = v.find('|', p);
        if (e == std::string::npos) e = v.size();
        std::string o = trim(v.substr(p, e - p));
        if (!o.empty()) g.options.push_back(o);
        p = e + 1;
      }
    } else if (tagValue(l, "HINT", v) && !v.empty()) g.hints.push_back(v);
  }
  if (g.secret.empty() || g.hints.size() < 3) return false;
  if (g.options.size() > 4) g.options.resize(4);
  for (size_t i = 0; i < g.options.size(); i++)
    if (lowerAscii(g.options[i]) == lowerAscii(g.secret)) g.answer = (int)i;
  if (g.answer < 0) {                                    // загаданного нет среди вариантов - ставим сами
    if (g.options.size() < 4) g.options.push_back(g.secret);
    else g.options[salt % 4] = g.secret;
    for (size_t i = 0; i < g.options.size(); i++)
      if (g.options[i] == g.secret) g.answer = (int)i;
  }
  if (g.hints.size() > 5) g.hints.resize(5);
  return g.options.size() >= 2 && g.answer >= 0;
}

// Очки за раунд: чем меньше подсказок понадобилось, тем больше.
inline int guessPoints(int hintsUsed, bool correct) { return correct ? 6 - hintsUsed : 0; }

}  // namespace aigames
