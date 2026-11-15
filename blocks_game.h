// Игра «Блоки»: поле 8x8, три фигуры на выбор, заполненные ряды и столбцы сгорают.
// Чистая логика без экрана и Arduino - тестируется на компьютере.
#pragma once
#include <stdlib.h>
#include <string>
#include <stdint.h>

namespace blocks {

const int N = 8;          // размер поля
const int SLOTS = 3;      // фигур в лотке

struct Piece {
  uint8_t w, h;
  uint32_t mask;          // клетки по строкам, бит (r*5 + c)
  uint16_t color;         // цвет RGB565
};

// Фигуры: строки через '/', '#' - клетка.
struct ShapeDef {
  const char* rows;
  uint16_t color;
};

const uint16_t COL_RED = 0xF9A6, COL_ORANGE = 0xFC40, COL_YELLOW = 0xFEA0, COL_GREEN = 0x5E89,
               COL_CYAN = 0x2EDD, COL_BLUE = 0x3C1F, COL_PURPLE = 0xA27F, COL_PINK = 0xF97B;

const ShapeDef SHAPES[] = {
  {"#", COL_YELLOW},
  {"##", COL_ORANGE}, {"#/#", COL_ORANGE},
  {"###", COL_RED}, {"#/#/#", COL_RED},
  {"####", COL_CYAN}, {"#/#/#/#", COL_CYAN},
  {"#####", COL_BLUE}, {"#/#/#/#/#", COL_BLUE},
  {"##/##", COL_YELLOW},
  {"###/###/###", COL_GREEN},
  {"###/###", COL_PURPLE}, {"##/##/##", COL_PURPLE},
  {"#./##", COL_PINK}, {".#/##", COL_PINK}, {"##/#.", COL_PINK}, {"##/.#", COL_PINK},
  {"#../#../###", COL_BLUE}, {"..#/..#/###", COL_BLUE}, {"###/#../#..", COL_BLUE}, {"###/..#/..#", COL_BLUE},
  {"#./#./##", COL_ORANGE}, {".#/.#/##", COL_ORANGE}, {"##/#./#.", COL_ORANGE}, {"##/.#/.#", COL_ORANGE},
  {"###/#..", COL_CYAN}, {"###/..#", COL_CYAN}, {"#../###", COL_CYAN}, {"..#/###", COL_CYAN},
  {"###/.#.", COL_PURPLE}, {".#./###", COL_PURPLE}, {"#./##/#.", COL_PURPLE}, {".#/##/.#", COL_PURPLE},
  {".##/##.", COL_GREEN}, {"##./.##", COL_RED}, {"#./##/.#", COL_GREEN}, {".#/##/#.", COL_RED},
};
const int SHAPE_COUNT = sizeof(SHAPES) / sizeof(SHAPES[0]);

inline Piece parseShape(const ShapeDef& d) {
  Piece p = {0, 0, 0, d.color};
  int r = 0, c = 0;
  for (const char* s = d.rows;; s++) {
    if (*s == '/' || *s == 0) {
      if (c > p.w) p.w = (uint8_t)c;
      r++;
      c = 0;
      if (*s == 0) break;
      continue;
    }
    if (*s == '#') p.mask |= 1u << (r * 5 + c);
    c++;
  }
  p.h = (uint8_t)r;
  return p;
}

inline bool pieceCell(const Piece& p, int r, int c) { return (p.mask >> (r * 5 + c)) & 1u; }

inline int pieceSize(const Piece& p) {
  int n = 0;
  for (uint32_t m = p.mask; m; m &= m - 1) n++;
  return n;
}

class Game {
 public:
  uint16_t cell[N][N];      // 0 - пусто, иначе цвет клетки
  int tray[SLOTS];          // номер фигуры в SHAPES или -1, если уже поставлена
  uint32_t score = 0;
  uint32_t best = 0;
  int streak = 0;           // сколько ходов подряд что-то сгорало
  bool over = false;
  uint32_t (*rnd)() = nullptr;

  // Что сгорело последним ходом (для анимации): биты рядов и столбцов.
  uint8_t clearedRows = 0, clearedCols = 0;

  void reset() {
    for (int r = 0; r < N; r++)
      for (int c = 0; c < N; c++) cell[r][c] = 0;
    score = 0;
    streak = 0;
    over = false;
    clearedRows = clearedCols = 0;
    refill();
  }

  static Piece piece(int idx) { return parseShape(SHAPES[idx]); }

  bool canPlace(int idx, int row, int col) const {
    if (idx < 0) return false;
    Piece p = piece(idx);
    if (row < 0 || col < 0 || row + p.h > N || col + p.w > N) return false;
    for (int r = 0; r < p.h; r++)
      for (int c = 0; c < p.w; c++)
        if (pieceCell(p, r, c) && cell[row + r][col + c]) return false;
    return true;
  }

  bool fitsAnywhere(int idx) const {
    if (idx < 0) return false;
    for (int r = 0; r < N; r++)
      for (int c = 0; c < N; c++)
        if (canPlace(idx, r, c)) return true;
    return false;
  }

  // Ставит фигуру из лотка. Возвращает число сгоревших линий или -1, если нельзя.
  int place(int slot, int row, int col) {
    if (over || slot < 0 || slot >= SLOTS || !canPlace(tray[slot], row, col)) return -1;
    Piece p = piece(tray[slot]);
    for (int r = 0; r < p.h; r++)
      for (int c = 0; c < p.w; c++)
        if (pieceCell(p, r, c)) cell[row + r][col + c] = p.color;
    tray[slot] = -1;
    score += (uint32_t)pieceSize(p);

    // Ищем полные ряды и столбцы до очистки - чтобы перекрестие считалось за обе линии.
    clearedRows = clearedCols = 0;
    for (int i = 0; i < N; i++) {
      bool fullR = true, fullC = true;
      for (int j = 0; j < N; j++) {
        if (!cell[i][j]) fullR = false;
        if (!cell[j][i]) fullC = false;
      }
      if (fullR) clearedRows |= (uint8_t)(1u << i);
      if (fullC) clearedCols |= (uint8_t)(1u << i);
    }
    int lines = 0;
    for (int i = 0; i < N; i++) {
      if (clearedRows & (1u << i)) lines++;
      if (clearedCols & (1u << i)) lines++;
    }
    for (int r = 0; r < N; r++)
      for (int c = 0; c < N; c++)
        if ((clearedRows & (1u << r)) || (clearedCols & (1u << c))) cell[r][c] = 0;

    if (lines) {
      streak++;
      score += (uint32_t)(lines * lines * 10 + (streak - 1) * 10);   // 1 линия - 10, 2 - 40, 3 - 90; серия +10
    } else {
      streak = 0;
    }
    if (score > best) best = score;

    bool empty = true;
    for (int i = 0; i < SLOTS; i++) if (tray[i] >= 0) empty = false;
    if (empty) refill();
    over = !anyMove();
    return lines;
  }

  bool anyMove() const {
    for (int i = 0; i < SLOTS; i++) if (fitsAnywhere(tray[i])) return true;
    return false;
  }

  // ---- Сохранение партии (чтобы продолжить после выключения) ----
  static const uint8_t SAVE_MAGIC = 0xB1;
  static const size_t SAVE_SIZE = 1 + N * N * 2 + SLOTS + 4 + 2;   // 138 байт

  size_t saveTo(uint8_t* out) const {
    size_t k = 0;
    out[k++] = SAVE_MAGIC;
    for (int r = 0; r < N; r++)
      for (int c = 0; c < N; c++) {
        out[k++] = (uint8_t)(cell[r][c] >> 8);
        out[k++] = (uint8_t)(cell[r][c] & 0xFF);
      }
    for (int i = 0; i < SLOTS; i++) out[k++] = (uint8_t)(int8_t)tray[i];
    for (int b = 3; b >= 0; b--) out[k++] = (uint8_t)(score >> (8 * b));
    out[k++] = (uint8_t)(streak >> 8);
    out[k++] = (uint8_t)(streak & 0xFF);
    return k;
  }

  // Загружает сохранённую партию; false - если данные битые (тогда игра не меняется).
  bool loadFrom(const uint8_t* in, size_t n) {
    if (n != SAVE_SIZE || in[0] != SAVE_MAGIC) return false;
    int t[SLOTS];
    for (int i = 0; i < SLOTS; i++) {
      t[i] = (int8_t)in[1 + N * N * 2 + i];
      if (t[i] < -1 || t[i] >= SHAPE_COUNT) return false;
    }
    size_t k = 1;
    for (int r = 0; r < N; r++)
      for (int c = 0; c < N; c++) {
        cell[r][c] = (uint16_t)((in[k] << 8) | in[k + 1]);
        k += 2;
      }
    k += SLOTS;
    for (int i = 0; i < SLOTS; i++) tray[i] = t[i];
    score = 0;
    for (int b = 0; b < 4; b++) score = (score << 8) | in[k++];
    streak = (in[k] << 8) | in[k + 1];
    bool empty = true;
    for (int i = 0; i < SLOTS; i++) if (tray[i] >= 0) empty = false;
    if (empty) refill();
    if (score > best) best = score;
    clearedRows = clearedCols = 0;
    over = !anyMove();
    return true;
  }

  // Новые три фигуры. Стараемся, чтобы хотя бы одна помещалась (несколько попыток).
  void refill() {
    for (int attempt = 0; attempt < 6; attempt++) {
      for (int i = 0; i < SLOTS; i++) tray[i] = (int)(next() % (uint32_t)SHAPE_COUNT);
      if (anyMove()) return;
    }
  }

 private:
  uint32_t seed = 12345;
  uint32_t next() {
    if (rnd) return rnd();
    seed = seed * 1103515245u + 12345u;   // запасной генератор
    return seed >> 8;
  }
};

// ============================ СОВЕТЧИК ============================
// Перебирает все возможные ходы и оценивает их: сгоревшие линии, останется ли место другим фигурам,
// насколько «дырявым» станет поле. Лучшие ходы можно показать игроку или отдать DeepSeek на выбор.
struct Advice {
  int slot, row, col;   // какую фигуру лотка и куда (левый верхний угол)
  int lines;            // сколько рядов и столбцов сгорит
  int stuck;            // сколько оставшихся фигур после этого некуда будет поставить
  int freeCells;        // пустых клеток после хода
  int score;
};

inline bool gridFits(const bool g[N][N], const Piece& p, int row, int col) {
  if (row + p.h > N || col + p.w > N) return false;
  for (int r = 0; r < p.h; r++)
    for (int c = 0; c < p.w; c++)
      if (pieceCell(p, r, c) && g[row + r][col + c]) return false;
  return true;
}

inline bool gridFitsAnywhere(const bool g[N][N], const Piece& p) {
  for (int r = 0; r < N; r++)
    for (int c = 0; c < N; c++)
      if (gridFits(g, p, r, c)) return true;
  return false;
}

inline Advice evalMove(const Game& gm, int slot, int row, int col) {
  Advice a = {slot, row, col, 0, 0, 0, 0};
  bool g[N][N];
  for (int r = 0; r < N; r++)
    for (int c = 0; c < N; c++) g[r][c] = gm.cell[r][c] != 0;
  Piece p = Game::piece(gm.tray[slot]);
  for (int r = 0; r < p.h; r++)
    for (int c = 0; c < p.w; c++)
      if (pieceCell(p, r, c)) g[row + r][col + c] = true;
  bool fullR[N], fullC[N];
  for (int i = 0; i < N; i++) {
    fullR[i] = fullC[i] = true;
    for (int k = 0; k < N; k++) {
      if (!g[i][k]) fullR[i] = false;
      if (!g[k][i]) fullC[i] = false;
    }
    a.lines += (fullR[i] ? 1 : 0) + (fullC[i] ? 1 : 0);
  }
  for (int r = 0; r < N; r++)
    for (int c = 0; c < N; c++)
      if (fullR[r] || fullC[c]) g[r][c] = false;
  int borders = 0, lonely = 0;                       // «дырявость»: границы занято/пусто и одиночные пустые клетки
  for (int r = 0; r < N; r++)
    for (int c = 0; c < N; c++) {
      if (!g[r][c]) {
        a.freeCells++;
        bool up = r == 0 || g[r - 1][c], dn = r == N - 1 || g[r + 1][c], lf = c == 0 || g[r][c - 1], rt = c == N - 1 || g[r][c + 1];
        if (up && dn && lf && rt) lonely++;
      }
      if (c + 1 < N && g[r][c] != g[r][c + 1]) borders++;
      if (r + 1 < N && g[r][c] != g[r + 1][c]) borders++;
    }
  for (int i = 0; i < SLOTS; i++)
    if (i != slot && gm.tray[i] >= 0 && !gridFitsAnywhere(g, Game::piece(gm.tray[i]))) a.stuck++;
  Piece big = parseShape(ShapeDef{"###/###/###", 0});
  int room = gridFitsAnywhere(g, big) ? 1 : 0;       // остаётся ли место под самую большую фигуру
  a.score = a.lines * 120 + (a.lines >= 2 ? 80 : 0) - a.stuck * 400 - borders * 4 - lonely * 25 + room * 40;
  return a;
}

// Лучшие ходы по убыванию оценки (не больше maxN; для каждой фигуры лотка - не больше двух).
inline int bestMoves(const Game& gm, Advice* out, int maxN) {
  int n = 0;
  for (int pass = 0; pass < 2; pass++)               // сначала лучший ход каждой фигуры, потом вторые по качеству
    for (int s = 0; s < SLOTS; s++) {
      if (gm.tray[s] < 0) continue;
      Advice best = {s, 0, 0, 0, 0, 0, -1000000};
      bool found = false;
      for (int r = 0; r < N; r++)
        for (int c = 0; c < N; c++) {
          if (!gm.canPlace(gm.tray[s], r, c)) continue;
          bool taken = false;
          for (int k = 0; k < n; k++)
            if (out[k].slot == s && out[k].row == r && out[k].col == c) taken = true;
          if (taken) continue;
          Advice a = evalMove(gm, s, r, c);
          if (!found || a.score > best.score) {
            best = a;
            found = true;
          }
        }
      if (found && n < maxN) out[n++] = best;
    }
  for (int i = 1; i < n; i++)                         // по убыванию оценки
    for (int j = i; j > 0 && out[j - 1].score < out[j].score; j--) {
      Advice t = out[j];
      out[j] = out[j - 1];
      out[j - 1] = t;
    }
  return n;
}

// Описание позиции и вариантов для DeepSeek.
inline std::string adviceQuestion(const Game& gm, const Advice* adv, int n) {
  std::string q = "Board (# filled, . empty), rows 1-8 top to bottom:\n";
  for (int r = 0; r < N; r++) {
    for (int c = 0; c < N; c++) q += gm.cell[r][c] ? '#' : '.';
    q += '\n';
  }
  q += "Pieces to place:";
  for (int s = 0; s < SLOTS; s++)
    if (gm.tray[s] >= 0) q += std::string(" ") + (char)('A' + s) + "=" + SHAPES[gm.tray[s]].rows;
  q += "\nCandidate moves:\n";
  for (int i = 0; i < n; i++) {
    q += std::to_string(i + 1) + ") piece " + (char)('A' + adv[i].slot) + " at row " + std::to_string(adv[i].row + 1) +
         ", column " + std::to_string(adv[i].col + 1) + ": clears " + std::to_string(adv[i].lines) + " lines, leaves " +
         std::to_string(adv[i].freeCells) + " empty cells";
    if (adv[i].stuck) q += ", but " + std::to_string(adv[i].stuck) + " remaining piece(s) will not fit";
    q += '\n';
  }
  return q;
}

inline std::string adviceSystem(const std::string& lang) {
  return "You advise a player in a block puzzle: pieces are placed on an 8x8 board, full rows and columns are "
         "cleared, the game ends when a piece cannot be placed. Choose the best candidate move. Reply in exactly "
         "this format:\nPICK: number\nWHY: reason in " + lang + ", at most 26 characters\nNo other text.";
}

// Разбор ответа: номер варианта 1..n (0 - не понял) и короткая причина.
inline int parseAdvice(const std::string& text, int n, std::string& why) {
  why.clear();
  int pick = 0;
  size_t p = text.find("PICK");
  if (p != std::string::npos) {
    p += 4;
    while (p < text.size() && (text[p] < '0' || text[p] > '9') && text[p] != '\n') p++;
    if (p < text.size() && text[p] >= '0' && text[p] <= '9') pick = atoi(text.c_str() + p);
  }
  size_t w = text.find("WHY");
  if (w != std::string::npos) {
    w += 3;
    while (w < text.size() && (text[w] == ':' || text[w] == ' ')) w++;
    if (text.compare(w, 3, "\xEF\xBC\x9A") == 0) w += 3;
    size_t e = text.find('\n', w);
    why = text.substr(w, e == std::string::npos ? std::string::npos : e - w);
    while (!why.empty() && (why.back() == ' ' || why.back() == '.' || why.back() == '\r')) why.pop_back();
  }
  return pick >= 1 && pick <= n ? pick : 0;
}

}  // namespace blocks
