// Повтори, Сапёр, Арканоид, Космические пришельцы, Крестики-нолики: чистая логика без экрана.
// Тестируется на компьютере; скетч только рисует и читает джойстик.
#pragma once
#include <math.h>
#include <stdint.h>

namespace games2 {

typedef uint32_t (*RandFn)();

inline uint32_t fallbackRand() {
  static uint32_t s = 777;
  s = s * 1103515245u + 12345u;
  return s >> 8;
}

// ============================ ПОВТОРИ ============================
// Четыре цветные кнопки = четыре направления джойстика (0 вверх, 1 вправо, 2 вниз, 3 влево).
namespace simon {
const int MAXLEN = 100;

class Game {
 public:
  uint8_t seq[MAXLEN];
  int len = 0;          // длина последовательности
  int pos = 0;          // сколько игрок уже повторил
  bool over = false;
  RandFn rnd = nullptr;

  void reset() {
    len = 0;
    over = false;
    addStep();
  }
  void addStep() {
    if (len < MAXLEN) seq[len++] = (uint8_t)(r() % 4);
    pos = 0;
  }
  // Ответ игрока. 1 - верно, раунд не закончен; 2 - раунд пройден (добавлен шаг); 0 - ошибка.
  int press(int d) {
    if (over) return 0;
    if (seq[pos] != d) {
      over = true;
      return 0;
    }
    pos++;
    if (pos >= len) {
      addStep();
      return 2;
    }
    return 1;
  }
  int score() const { return over ? len - 1 : len - 1; }   // сколько раундов пройдено
  // Скорость показа: чем длиннее, тем быстрее.
  int showMs() const {
    int ms = 600 - len * 25;
    return ms < 220 ? 220 : ms;
  }

 private:
  uint32_t r() { return rnd ? rnd() : fallbackRand(); }
};
}  // namespace simon

// ============================ САПЁР ============================
namespace mines {
const int W = 9, H = 9, MINES = 10;
const uint8_t MINE = 9;

class Game {
 public:
  uint8_t num[H][W];      // 0..8 - мин вокруг, 9 - мина
  bool open[H][W];
  bool flag[H][W];
  bool placed = false;    // мины ставятся после первого хода (первый ход всегда безопасен)
  bool over = false, won = false;
  int opened = 0, flags = 0;
  int boomX = -1, boomY = -1;
  RandFn rnd = nullptr;

  void reset() {
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++) {
        num[y][x] = 0;
        open[y][x] = flag[y][x] = false;
      }
    placed = over = won = false;
    opened = flags = 0;
    boomX = boomY = -1;
  }

  void place(int sx, int sy) {
    int n = 0;
    while (n < MINES) {
      int x = (int)(r() % W), y = (int)(r() % H);
      if (num[y][x] == MINE) continue;
      if (iabs(x - sx) <= 1 && iabs(y - sy) <= 1) continue;   // вокруг первого хода - без мин
      num[y][x] = MINE;
      n++;
    }
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++) {
        if (num[y][x] == MINE) continue;
        int c = 0;
        for (int dy = -1; dy <= 1; dy++)
          for (int dx = -1; dx <= 1; dx++) {
            int nx = x + dx, ny = y + dy;
            if (nx >= 0 && nx < W && ny >= 0 && ny < H && num[ny][nx] == MINE) c++;
          }
        num[y][x] = (uint8_t)c;
      }
    placed = true;
  }

  // Открыть клетку. Открытое число с нужным количеством флажков вокруг - открыть соседей.
  // Возвращает число новых открытых клеток.
  int reveal(int x, int y) {
    if (over || flag[y][x]) return 0;
    if (!placed) place(x, y);
    if (open[y][x]) return chord(x, y);
    return flood(x, y);
  }

  bool toggleFlag(int x, int y) {
    if (over || open[y][x]) return false;
    flag[y][x] = !flag[y][x];
    flags += flag[y][x] ? 1 : -1;
    return true;
  }

 private:
  int flood(int x, int y) {
    static int16_t stack[W * H * 2];
    int sp = 0, n = 0;
    stack[sp++] = (int16_t)(y * W + x);
    while (sp) {
      int c = stack[--sp], cx = c % W, cy = c / W;
      if (open[cy][cx] || flag[cy][cx]) continue;
      open[cy][cx] = true;
      n++;
      opened++;
      if (num[cy][cx] == MINE) {
        over = true;
        boomX = cx;
        boomY = cy;
        return n;
      }
      if (num[cy][cx] == 0)
        for (int dy = -1; dy <= 1; dy++)
          for (int dx = -1; dx <= 1; dx++) {
            int nx = cx + dx, ny = cy + dy;
            if (nx >= 0 && nx < W && ny >= 0 && ny < H && !open[ny][nx] && sp < W * H * 2)
              stack[sp++] = (int16_t)(ny * W + nx);
          }
    }
    if (opened == W * H - MINES) {
      over = won = true;
    }
    return n;
  }

  int chord(int x, int y) {
    int f = 0;
    for (int dy = -1; dy <= 1; dy++)
      for (int dx = -1; dx <= 1; dx++) {
        int nx = x + dx, ny = y + dy;
        if (nx >= 0 && nx < W && ny >= 0 && ny < H && flag[ny][nx]) f++;
      }
    if (num[y][x] == 0 || f != num[y][x]) return 0;
    int n = 0;
    for (int dy = -1; dy <= 1; dy++)
      for (int dx = -1; dx <= 1; dx++) {
        int nx = x + dx, ny = y + dy;
        if (nx >= 0 && nx < W && ny >= 0 && ny < H && !open[ny][nx] && !flag[ny][nx]) n += flood(nx, ny);
        if (over) return n;
      }
    return n;
  }

  static int iabs(int v) { return v < 0 ? -v : v; }
  uint32_t r() { return rnd ? rnd() : fallbackRand(); }
};
}  // namespace mines

// ============================ АРКАНОИД ============================
namespace breakout {
const int FW = 240, FH = 220;              // поле (экран ниже полосы счёта)
const int NCOLS = 8, NROWS = 6;        // не ROWS/COLS: в скетче это макросы шрифта
const int BW = 28, BH = 10, BX0 = 4, BY0 = 24, BGAP = 1;   // кирпичи: (28+1)*8+4 = 236
const int PW = 44, PH = 6, PY = 206;       // ракетка
const int BALL = 6;

class Game {
 public:
  uint8_t brick[NROWS][NCOLS];   // 0 - нет, иначе прочность
  int left = 0;
  float px = 98, bx = 117, by = 190, vx = 0, vy = 0;
  bool stuck = true;           // мяч лежит на ракетке до нажатия
  int lives = 3, level = 1;
  uint32_t score = 0;
  bool over = false;
  int hitR = -1, hitC = -1;    // какой кирпич разбит на этом шаге (для перерисовки)
  RandFn rnd = nullptr;

  void reset() {
    lives = 3;
    level = 1;
    score = 0;
    over = false;
    fill();
    serve();
  }

  void fill() {
    left = 0;
    for (int r = 0; r < NROWS; r++)
      for (int c = 0; c < NCOLS; c++) {
        // с 3-го уровня верхние ряды прочнее
        brick[r][c] = (uint8_t)((level >= 3 && r < 2) ? 2 : 1);
        left++;
      }
  }

  void serve() {
    stuck = true;
    bx = px + PW / 2 - BALL / 2;
    by = PY - BALL - 1;
    vx = vy = 0;
  }

  float speed() const { return 3.0f + 0.35f * (level - 1); }

  void launch() {
    if (!stuck) return;
    stuck = false;
    float s = speed();
    float a = ((int)(r() % 60) - 30) * 3.14159f / 180.0f;   // от -30 до 30 градусов от вертикали
    vx = s * sinf(a);
    vy = -s * cosf(a);
  }

  // Брусок в позиции клетки (для отрисовки).
  static int brickX(int c) { return BX0 + c * (BW + BGAP); }
  static int brickY(int r) { return BY0 + r * (BH + BGAP); }

  // Шаг. move: -1..1 (ось джойстика), fire - запуск мяча.
  // Возвращает: 0 - ничего, 1 - разбит кирпич, 2 - потеря мяча, 3 - уровень пройден.
  int step(float move, bool fire) {
    hitR = hitC = -1;
    if (over) return 0;
    px += move * 5.0f;
    if (px < 0) px = 0;
    if (px > FW - PW) px = FW - PW;
    if (stuck) {
      bx = px + PW / 2 - BALL / 2;
      if (fire) launch();
      return 0;
    }
    // движение мелкими шагами, чтобы мяч не проскакивал сквозь кирпичи
    int n = (int)(fmaxf(fabsf(vx), fabsf(vy)) / 2.0f) + 1;
    int res = 0;
    for (int i = 0; i < n && res == 0; i++) res = sub(vx / n, vy / n);
    return res;
  }

 private:
  int sub(float dx, float dy) {
    bx += dx;
    by += dy;
    if (bx < 0) { bx = 0; vx = fabsf(vx); }
    if (bx > FW - BALL) { bx = FW - BALL; vx = -fabsf(vx); }
    if (by < 0) { by = 0; vy = fabsf(vy); }
    // ракетка: угол отскока зависит от места удара
    if (vy > 0 && by + BALL >= PY && by + BALL <= PY + PH + 4 && bx + BALL >= px && bx <= px + PW) {
      float hit = ((bx + BALL / 2.0f) - (px + PW / 2.0f)) / (PW / 2.0f);   // -1..1
      if (hit < -1) hit = -1;
      if (hit > 1) hit = 1;
      float s = speed();
      float a = hit * 60.0f * 3.14159f / 180.0f;
      vx = s * sinf(a);
      vy = -s * cosf(a);
      by = PY - BALL;
    }
    if (by > FH) {                                   // мяч упал
      lives--;
      if (lives <= 0) {
        over = true;
        lives = 0;
      } else {
        serve();
      }
      return 2;
    }
    // кирпичи: проверяем клетку под центром мяча
    int cx = (int)(bx + BALL / 2.0f), cy = (int)(by + BALL / 2.0f);
    for (int r = 0; r < NROWS; r++)
      for (int c = 0; c < NCOLS; c++) {
        if (!brick[r][c]) continue;
        int x0 = brickX(c), y0 = brickY(r);
        if (bx + BALL > x0 && bx < x0 + BW && by + BALL > y0 && by < y0 + BH) {
          // откуда прилетели: по меньшему перекрытию
          float ox = fminf(bx + BALL - x0, x0 + BW - bx), oy = fminf(by + BALL - y0, y0 + BH - by);
          if (ox < oy) vx = -vx;
          else vy = -vy;
          (void)cx;
          (void)cy;
          brick[r][c]--;
          hitR = r;
          hitC = c;
          if (!brick[r][c]) {
            left--;
            score += (uint32_t)(10 * (NROWS - r) * level);
          }
          if (left == 0) {
            level++;
            fill();
            serve();
            return 3;
          }
          return 1;
        }
      }
    return 0;
  }
  uint32_t r() { return rnd ? rnd() : fallbackRand(); }
};
}  // namespace breakout

// ============================ ПРИШЕЛЬЦЫ ============================
namespace invaders {
const int FW = 240, FH = 220;
const int AC = 8, AR = 4;                  // пришельцев: 8 x 4
const int AW = 16, AH = 10, ASX = 24, ASY = 18;   // размер и шаг сетки
const int SHIPW = 18, SHIPY = 204, SHIPH = 10;
const int MAXSHOTS = 4;

struct Shot {
  int x, y;
  bool on;
};

class Game {
 public:
  bool alive[AR][AC];
  int alienCount = 0;
  int gx = 10, gy = 24, dir = 1;           // сдвиг всей сетки
  int shipX = 111;
  Shot shot;                               // выстрел игрока (один на экране)
  Shot bomb[MAXSHOTS];                     // бомбы пришельцев
  int lives = 3, wave = 1;
  uint32_t score = 0;
  bool over = false;
  int tick = 0;
  bool gridMoved = false;                  // сетка сдвинулась на этом шаге (перерисовать)
  RandFn rnd = nullptr;

  void reset() {
    lives = 3;
    wave = 1;
    score = 0;
    over = false;
    newWave();
  }

  void newWave() {
    for (int r = 0; r < AR; r++)
      for (int c = 0; c < AC; c++) alive[r][c] = true;
    alienCount = AR * AC;
    gx = 10;
    gy = 24 + (wave - 1) * 6 > 60 ? 60 : 24 + (wave - 1) * 6;
    dir = 1;
    shot.on = false;
    for (auto& b : bomb) b.on = false;
    tick = 0;
  }

  int alienX(int c) const { return gx + c * ASX; }
  int alienY(int r) const { return gy + r * ASY; }

  // Как часто двигается сетка: чем меньше пришельцев, тем быстрее.
  int moveEvery() const {
    int m = 2 + alienCount * 22 / (AR * AC) - (wave - 1);
    return m < 2 ? 2 : m;
  }

  // Шаг. move: -1/0/1, fire - выстрел. 0 - ничего, 1 - попадание, 2 - по игроку попали, 3 - волна пройдена.
  int step(int move, bool fire) {
    gridMoved = false;
    if (over) return 0;
    tick++;
    shipX += move * 4;
    if (shipX < 0) shipX = 0;
    if (shipX > FW - SHIPW) shipX = FW - SHIPW;
    if (fire && !shot.on) {
      shot.on = true;
      shot.x = shipX + SHIPW / 2 - 1;
      shot.y = SHIPY - 6;
    }
    int res = 0;
    if (shot.on) {
      shot.y -= 7;
      if (shot.y < 18) shot.on = false;
      else
        for (int r = 0; r < AR && shot.on; r++)
          for (int c = 0; c < AC; c++) {
            if (!alive[r][c]) continue;
            int x = alienX(c), y = alienY(r);
            if (shot.x + 2 > x && shot.x < x + AW && shot.y < y + AH && shot.y + 6 > y) {
              alive[r][c] = false;
              alienCount--;
              shot.on = false;
              score += (uint32_t)((AR - r) * 10);
              res = 1;
              break;
            }
          }
      if (alienCount == 0) {
        wave++;
        newWave();
        return 3;
      }
    }
    if (tick % moveEvery() == 0) {                 // сетка идёт вбок, у края - вниз
      int minC = AC, maxC = -1, maxR = -1;
      for (int r = 0; r < AR; r++)
        for (int c = 0; c < AC; c++)
          if (alive[r][c]) {
            if (c < minC) minC = c;
            if (c > maxC) maxC = c;
            if (r > maxR) maxR = r;
          }
      int nx = gx + dir * 4;
      if (alienX(maxC) - gx + nx + AW > FW - 2 || alienX(minC) - gx + nx < 2) {
        dir = -dir;
        gy += 8;
      } else {
        gx = nx;
      }
      gridMoved = true;
      if (alienY(maxR) + AH >= SHIPY) {            // дошли до игрока
        over = true;
        lives = 0;
        return 2;
      }
    }
    // бомбы
    int bombEvery = 40 - wave * 4 < 14 ? 14 : 40 - wave * 4;
    if (tick % bombEvery == 0) {
      int c = (int)(r() % AC);
      for (int rr = AR - 1; rr >= 0; rr--)
        if (alive[rr][c]) {
          for (auto& b : bomb)
            if (!b.on) {
              b.on = true;
              b.x = alienX(c) + AW / 2 - 1;
              b.y = alienY(rr) + AH;
              break;
            }
          break;
        }
    }
    for (auto& b : bomb) {
      if (!b.on) continue;
      b.y += 3;
      if (b.y > FH) {
        b.on = false;
        continue;
      }
      if (b.y + 6 >= SHIPY && b.y <= SHIPY + SHIPH && b.x + 2 >= shipX && b.x <= shipX + SHIPW) {
        b.on = false;
        lives--;
        for (auto& o : bomb) o.on = false;
        if (lives <= 0) {
          lives = 0;
          over = true;
        }
        return 2;
      }
    }
    return res;
  }

 private:
  uint32_t r() { return rnd ? rnd() : fallbackRand(); }
};
}  // namespace invaders

// ============================ КРЕСТИКИ-НОЛИКИ ============================
namespace ttt {
// Клетки 0..8, значения: 0 - пусто, 1 - крестик (игрок), 2 - нолик (плата).
inline int winner(const uint8_t b[9]) {
  static const uint8_t L[8][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6}, {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6}};
  for (auto& l : L)
    if (b[l[0]] && b[l[0]] == b[l[1]] && b[l[1]] == b[l[2]]) return b[l[0]];
  for (int i = 0; i < 9; i++)
    if (!b[i]) return 0;
  return 3;   // ничья
}

// Выигрышная линия (для подсветки): индексы в out, true если есть.
inline bool winLine(const uint8_t b[9], int out[3]) {
  static const uint8_t L[8][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6}, {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6}};
  for (auto& l : L)
    if (b[l[0]] && b[l[0]] == b[l[1]] && b[l[1]] == b[l[2]]) {
      for (int k = 0; k < 3; k++) out[k] = l[k];
      return true;
    }
  return false;
}

// Минимакс: оценка позиции для игрока who, который ходит сейчас.
inline int minimax(uint8_t b[9], int who, int depth) {
  int w = winner(b);
  if (w == 2) return 10 - depth;
  if (w == 1) return depth - 10;
  if (w == 3) return 0;
  int best = who == 2 ? -100 : 100;
  for (int i = 0; i < 9; i++) {
    if (b[i]) continue;
    b[i] = (uint8_t)who;
    int v = minimax(b, 3 - who, depth + 1);
    b[i] = 0;
    if (who == 2 ? v > best : v < best) best = v;
  }
  return best;
}

// Ход платы. level: 0 - легко (часто ошибается), 1 - нормально, 2 - без ошибок.
inline int cpuMove(uint8_t b[9], int level, RandFn rnd) {
  uint32_t rv = rnd ? rnd() : fallbackRand();
  int mistake = level == 0 ? 45 : level == 1 ? 15 : 0;   // процент случайных ходов
  int freeCells[9], nf = 0;
  for (int i = 0; i < 9; i++)
    if (!b[i]) freeCells[nf++] = i;
  if (!nf) return -1;
  if ((int)(rv % 100) < mistake) return freeCells[(rv / 100) % (uint32_t)nf];
  int best = -100, bestCells[9], nb = 0;
  for (int k = 0; k < nf; k++) {
    int i = freeCells[k];
    b[i] = 2;
    int v = minimax(b, 1, 1);
    b[i] = 0;
    if (v > best) {
      best = v;
      nb = 0;
    }
    if (v == best) bestCells[nb++] = i;
  }
  return bestCells[(rv / 7) % (uint32_t)nb];   // среди равных - случайно, чтобы игры отличались
}
}  // namespace ttt

}  // namespace games2
