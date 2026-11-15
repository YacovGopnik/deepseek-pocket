// «Падающие блоки»: стакан 10x20, фигуры из четырёх квадратов, заполненные строки исчезают.
// Чистая логика без экрана - тестируется на компьютере.
#pragma once
#include <stdint.h>

namespace fall {

const int W = 10, H = 20;
const int KINDS = 7;

// Фигуры в своей рамке n x n: клетки (x, y). Поворот - (x, y) -> (n-1-y, x).
struct Kind {
  uint8_t n;
  int8_t cells[4][2];
  uint16_t color;
};

const Kind KIND[KINDS] = {
  {4, {{0, 1}, {1, 1}, {2, 1}, {3, 1}}, 0x2EDD},   // палка
  {4, {{1, 1}, {2, 1}, {1, 2}, {2, 2}}, 0xFEA0},   // квадрат
  {3, {{1, 0}, {0, 1}, {1, 1}, {2, 1}}, 0xA27F},   // T
  {3, {{1, 0}, {2, 0}, {0, 1}, {1, 1}}, 0x5E89},   // S
  {3, {{0, 0}, {1, 0}, {1, 1}, {2, 1}}, 0xF9A6},   // Z
  {3, {{0, 0}, {0, 1}, {1, 1}, {2, 1}}, 0x3C1F},   // J
  {3, {{2, 0}, {0, 1}, {1, 1}, {2, 1}}, 0xFC40},   // L
};

class Game {
 public:
  uint16_t cell[H][W];     // 0 - пусто, иначе цвет
  int cur = 0, rot = 0, px = 0, py = 0;
  int nextKind = 0;
  uint32_t score = 0, lines = 0, best = 0;
  int level = 1;
  bool over = false;
  uint32_t (*rnd)() = nullptr;

  void reset() {
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++) cell[y][x] = 0;
    score = lines = 0;
    level = 1;
    over = false;
    bagPos = 7;
    nextKind = fromBag();
    spawn();
  }

  // Клетка i фигуры kind в повороте r (координаты внутри рамки).
  static void cellOf(int kind, int r, int i, int& x, int& y) {
    const Kind& k = KIND[kind];
    x = k.cells[i][0];
    y = k.cells[i][1];
    for (int t = 0; t < (r & 3); t++) {
      int nx = k.n - 1 - y, ny = x;
      x = nx;
      y = ny;
    }
  }

  bool fits(int kind, int r, int ox, int oy) const {
    for (int i = 0; i < 4; i++) {
      int x, y;
      cellOf(kind, r, i, x, y);
      x += ox;
      y += oy;
      if (x < 0 || x >= W || y >= H) return false;
      if (y >= 0 && cell[y][x]) return false;
    }
    return true;
  }

  bool moveH(int dx) {
    if (over || !fits(cur, rot, px + dx, py)) return false;
    px += dx;
    return true;
  }

  // Поворот по часовой с простыми «отскоками» от стен.
  bool rotate() {
    if (over) return false;
    static const int kicks[] = {0, -1, 1, -2, 2};
    int nr = (rot + 1) & 3;
    for (int k : kicks)
      if (fits(cur, nr, px + k, py)) {
        rot = nr;
        px += k;
        return true;
      }
    return false;
  }

  // Шаг вниз. false - фигура легла (и появилась новая). soft - очко за ускорение.
  bool down(bool soft) {
    if (over) return false;
    if (fits(cur, rot, px, py + 1)) {
      py++;
      if (soft) score++;
      return true;
    }
    lock();
    return false;
  }

  // Бросок до дна: по 2 очка за клетку.
  void drop() {
    if (over) return;
    int n = 0;
    while (fits(cur, rot, px, py + 1)) {
      py++;
      n++;
    }
    score += (uint32_t)n * 2;
    lock();
  }

  // Где фигура окажется, если её бросить (для подсказки).
  int ghostY() const {
    int y = py;
    while (fits(cur, rot, px, y + 1)) y++;
    return y;
  }

  uint32_t gravityMs() const {
    int ms = 800 - (level - 1) * 70;
    return (uint32_t)(ms < 90 ? 90 : ms);
  }

  int lastCleared = 0;     // сколько строк исчезло последним ходом

 private:
  int bag[7];
  int bagPos = 7;
  uint32_t seed = 99;
  uint32_t next() {
    if (rnd) return rnd();
    seed = seed * 1103515245u + 12345u;
    return seed >> 8;
  }

  // «Мешок»: все 7 фигур в случайном порядке, потом новый мешок - без долгих засух.
  int fromBag() {
    if (bagPos >= 7) {
      for (int i = 0; i < 7; i++) bag[i] = i;
      for (int i = 6; i > 0; i--) {
        int j = (int)(next() % (uint32_t)(i + 1));
        int t = bag[i];
        bag[i] = bag[j];
        bag[j] = t;
      }
      bagPos = 0;
    }
    return bag[bagPos++];
  }

  void spawn() {
    cur = nextKind;
    nextKind = fromBag();
    rot = 0;
    px = (W - KIND[cur].n) / 2;
    py = cur == 0 ? -1 : 0;
    if (!fits(cur, rot, px, py)) over = true;
  }

  void lock() {
    for (int i = 0; i < 4; i++) {
      int x, y;
      cellOf(cur, rot, i, x, y);
      x += px;
      y += py;
      if (y < 0) {                       // фигура не влезла в стакан
        over = true;
        continue;
      }
      cell[y][x] = KIND[cur].color;
    }
    // убрать заполненные строки
    int n = 0;
    for (int y = H - 1; y >= 0;) {
      bool full = true;
      for (int x = 0; x < W; x++)
        if (!cell[y][x]) full = false;
      if (!full) {
        y--;
        continue;
      }
      n++;
      for (int yy = y; yy > 0; yy--)
        for (int x = 0; x < W; x++) cell[yy][x] = cell[yy - 1][x];
      for (int x = 0; x < W; x++) cell[0][x] = 0;
    }
    lastCleared = n;
    static const uint32_t pts[5] = {0, 100, 300, 500, 800};
    score += pts[n] * (uint32_t)level;
    lines += (uint32_t)n;
    level = 1 + (int)(lines / 10);
    if (score > best) best = score;
    if (!over) spawn();
    if (score > best) best = score;
  }
};

}  // namespace fall
