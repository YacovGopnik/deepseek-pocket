// Игра 2048: поле 4x4, плитки сдвигаются в одну сторону, одинаковые складываются.
// Чистая логика без экрана - тестируется на компьютере.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace g2048 {

const int N = 4;
enum Dir { UP = 0, DOWN = 1, LEFT = 2, RIGHT = 3 };

class Game {
 public:
  uint8_t cell[N][N];      // степень двойки: 0 - пусто, 1 - «2», 2 - «4», ... 11 - «2048»
  uint32_t score = 0;
  uint32_t best = 0;
  bool over = false;
  bool won = false;        // собрали 2048 (играть можно дальше)
  uint32_t (*rnd)() = nullptr;

  void reset() {
    for (int r = 0; r < N; r++)
      for (int c = 0; c < N; c++) cell[r][c] = 0;
    score = 0;
    over = won = false;
    spawn();
    spawn();
  }

  static uint32_t value(uint8_t e) { return e ? (1u << e) : 0; }

  // Сдвиг. true - если что-то сдвинулось (тогда появляется новая плитка).
  bool move(int dir) {
    if (over) return false;
    bool moved = false;
    for (int line = 0; line < N; line++) {
      uint8_t a[N], out[N] = {0, 0, 0, 0};
      for (int k = 0; k < N; k++) a[k] = *at(dir, line, k);
      int m = 0;
      bool justMerged = false;
      for (int k = 0; k < N; k++) {
        if (!a[k]) continue;
        if (m > 0 && !justMerged && out[m - 1] == a[k]) {
          out[m - 1]++;
          score += value(out[m - 1]);
          if (out[m - 1] >= 11) won = true;
          justMerged = true;
        } else {
          out[m++] = a[k];
          justMerged = false;
        }
      }
      for (int k = 0; k < N; k++) {
        if (*at(dir, line, k) != out[k]) moved = true;
        *at(dir, line, k) = out[k];
      }
    }
    if (moved) spawn();
    if (score > best) best = score;
    over = !canMove();
    return moved;
  }

  bool canMove() const {
    for (int r = 0; r < N; r++)
      for (int c = 0; c < N; c++) {
        if (!cell[r][c]) return true;
        if (c + 1 < N && cell[r][c] == cell[r][c + 1]) return true;
        if (r + 1 < N && cell[r][c] == cell[r + 1][c]) return true;
      }
    return false;
  }

  uint8_t maxTile() const {
    uint8_t m = 0;
    for (int r = 0; r < N; r++)
      for (int c = 0; c < N; c++) if (cell[r][c] > m) m = cell[r][c];
    return m;
  }

  // ---- Сохранение ----
  static const uint8_t SAVE_MAGIC = 0x48;
  static const size_t SAVE_SIZE = 1 + N * N + 4 + 1;

  size_t saveTo(uint8_t* out) const {
    size_t k = 0;
    out[k++] = SAVE_MAGIC;
    for (int r = 0; r < N; r++)
      for (int c = 0; c < N; c++) out[k++] = cell[r][c];
    for (int b = 3; b >= 0; b--) out[k++] = (uint8_t)(score >> (8 * b));
    out[k++] = won ? 1 : 0;
    return k;
  }

  bool loadFrom(const uint8_t* in, size_t n) {
    if (n != SAVE_SIZE || in[0] != SAVE_MAGIC) return false;
    for (int i = 0; i < N * N; i++)
      if (in[1 + i] > 17) return false;
    size_t k = 1;
    for (int r = 0; r < N; r++)
      for (int c = 0; c < N; c++) cell[r][c] = in[k++];
    score = 0;
    for (int b = 0; b < 4; b++) score = (score << 8) | in[k++];
    won = in[k] != 0;
    if (score > best) best = score;
    over = !canMove();
    return true;
  }

 private:
  uint32_t seed = 4242;
  uint32_t next() {
    if (rnd) return rnd();
    seed = seed * 1103515245u + 12345u;
    return seed >> 8;
  }

  // k-я клетка линии line, если считать от края, к которому едут плитки.
  uint8_t* at(int dir, int line, int k) {
    switch (dir) {
      case LEFT:  return &cell[line][k];
      case RIGHT: return &cell[line][N - 1 - k];
      case UP:    return &cell[k][line];
      default:    return &cell[N - 1 - k][line];
    }
  }

  void spawn() {
    int empty = 0;
    for (int r = 0; r < N; r++)
      for (int c = 0; c < N; c++) if (!cell[r][c]) empty++;
    if (!empty) return;
    int k = (int)(next() % (uint32_t)empty);
    uint8_t v = (next() % 10) == 0 ? 2 : 1;   // 10% - «4», иначе «2»
    for (int r = 0; r < N; r++)
      for (int c = 0; c < N; c++)
        if (!cell[r][c] && k-- == 0) {
          cell[r][c] = v;
          return;
        }
  }
};

}  // namespace g2048
