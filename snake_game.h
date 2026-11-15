// Змейка: поле 20x18, яблоко удлиняет змейку, удар о стену или себя - конец игры.
// Чистая логика без экрана - тестируется на компьютере.
#pragma once
#include <stdint.h>

namespace snake {

const int W = 20, H = 18;
enum Dir { UP = 0, DOWN = 1, LEFT = 2, RIGHT = 3 };

struct Pt {
  int8_t x, y;
};

class Game {
 public:
  Pt body[W * H];          // body[0] - голова
  int len = 0;
  int dir = RIGHT;
  Pt food = {0, 0};
  uint32_t score = 0;
  uint32_t best = 0;
  bool over = false;
  uint32_t (*rnd)() = nullptr;

  void reset() {
    len = 3;
    for (int i = 0; i < len; i++) body[i] = {(int8_t)(W / 2 - i), (int8_t)(H / 2)};
    dir = nextDir = RIGHT;
    score = 0;
    over = false;
    placeFood();
  }

  // Поворот применяется на следующем шаге; развернуться назад нельзя.
  void turn(int d) {
    if ((dir == UP && d == DOWN) || (dir == DOWN && d == UP) || (dir == LEFT && d == RIGHT) ||
        (dir == RIGHT && d == LEFT))
      return;
    nextDir = d;
  }

  // Шаг: 0 - просто ход, 1 - съели яблоко, -1 - конец игры.
  int step() {
    if (over) return -1;
    dir = nextDir;
    Pt h = body[0];
    if (dir == UP) h.y--;
    if (dir == DOWN) h.y++;
    if (dir == LEFT) h.x--;
    if (dir == RIGHT) h.x++;
    if (h.x < 0 || h.y < 0 || h.x >= W || h.y >= H) {
      over = true;
      return -1;
    }
    bool eat = h.x == food.x && h.y == food.y;
    int check = eat ? len : len - 1;        // хвост уходит - в его клетку можно
    for (int i = 0; i < check; i++)
      if (body[i].x == h.x && body[i].y == h.y) {
        over = true;
        return -1;
      }
    int newLen = eat ? len + 1 : len;
    for (int i = newLen - 1; i > 0; i--) body[i] = body[i - 1];
    body[0] = h;
    len = newLen;
    if (eat) {
      score++;
      if (score > best) best = score;
      if (len >= W * H) {                     // всё поле - змейка: победа
        over = true;
        return 1;
      }
      placeFood();
      return 1;
    }
    return 0;
  }

  bool occupied(int x, int y) const {
    for (int i = 0; i < len; i++)
      if (body[i].x == x && body[i].y == y) return true;
    return false;
  }

  // Задержка между шагами: быстрее с ростом очков.
  uint32_t stepMs() const {
    uint32_t ms = 220 - score * 4;
    return score * 4 > 140 ? 80 : ms;
  }

 private:
  int nextDir = RIGHT;
  uint32_t seed = 777;
  uint32_t next() {
    if (rnd) return rnd();
    seed = seed * 1103515245u + 12345u;
    return seed >> 8;
  }

  void placeFood() {
    int freeCells = W * H - len;
    if (freeCells <= 0) return;
    int k = (int)(next() % (uint32_t)freeCells);
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++)
        if (!occupied(x, y) && k-- == 0) {
          food = {(int8_t)x, (int8_t)y};
          return;
        }
  }
};

}  // namespace snake
