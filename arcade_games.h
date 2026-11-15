// Лабиринт, Память, Пинг-понг, Летящая птичка: чистая логика без экрана.
// Тестируется на компьютере; скетч только рисует и читает джойстик.
#pragma once
#include <stdint.h>

namespace arcade {

typedef uint32_t (*RandFn)();

inline uint32_t fallbackRand() {
  static uint32_t s = 2024;
  s = s * 1103515245u + 12345u;
  return s >> 8;
}

// ============================ ЛАБИРИНТ ============================
// Стены клетки - биты: 1 - сверху, 2 - справа, 4 - снизу, 8 - слева.
namespace maze {
const int MAXN = 22;
const uint8_t WN = 1, WE = 2, WS = 4, WW = 8;

class Game {
 public:
  uint8_t wall[MAXN][MAXN];
  int n = 6;               // размер (n x n)
  int px = 0, py = 0;      // игрок
  int level = 1;
  int best = 0;            // лучший пройденный уровень
  RandFn rnd = nullptr;

  static int sizeForLevel(int lv) {
    static const int S[] = {6, 8, 10, 12, 15, 18, 22};
    return S[lv - 1 < 6 ? lv - 1 : 6];
  }

  // Новый случайный лабиринт (поиск в глубину без рекурсии): от любой клетки есть путь к любой.
  void generate() {
    n = sizeForLevel(level);
    for (int y = 0; y < n; y++)
      for (int x = 0; x < n; x++) wall[y][x] = WN | WE | WS | WW;
    static bool seen[MAXN][MAXN];
    static uint16_t stack[MAXN * MAXN];
    for (int y = 0; y < n; y++)
      for (int x = 0; x < n; x++) seen[y][x] = false;
    int sp = 0;
    stack[sp++] = 0;
    seen[0][0] = true;
    while (sp > 0) {
      int cur = stack[sp - 1], cx = cur % n, cy = cur / n;
      int dirs[4], k = 0;
      if (cy > 0 && !seen[cy - 1][cx]) dirs[k++] = 0;
      if (cx < n - 1 && !seen[cy][cx + 1]) dirs[k++] = 1;
      if (cy < n - 1 && !seen[cy + 1][cx]) dirs[k++] = 2;
      if (cx > 0 && !seen[cy][cx - 1]) dirs[k++] = 3;
      if (!k) {
        sp--;
        continue;
      }
      int d = dirs[r() % (uint32_t)k];
      int nx = cx + (d == 1) - (d == 3), ny = cy + (d == 2) - (d == 0);
      static const uint8_t here[4] = {WN, WE, WS, WW}, there[4] = {WS, WW, WN, WE};
      wall[cy][cx] &= (uint8_t)~here[d];
      wall[ny][nx] &= (uint8_t)~there[d];
      seen[ny][nx] = true;
      stack[sp++] = (uint16_t)(ny * n + nx);
    }
    px = py = 0;
  }

  // dir: 0 вверх, 1 вправо, 2 вниз, 3 влево. true - если шаг сделан.
  bool move(int dir) {
    static const uint8_t bit[4] = {WN, WE, WS, WW};
    if (wall[py][px] & bit[dir]) return false;
    px += (dir == 1) - (dir == 3);
    py += (dir == 2) - (dir == 0);
    return true;
  }

  bool atExit() const { return px == n - 1 && py == n - 1; }

  void nextLevel() {
    if (level > best) best = level;
    level++;
    generate();
  }

 private:
  uint32_t r() { return rnd ? rnd() : fallbackRand(); }
};
}  // namespace maze

// ============================ ПАМЯТЬ ============================
// 16 карточек, 8 пар. Открываешь две: совпали - остаются открытыми.
namespace memory {
const int CARDS = 16;
enum State : uint8_t { HIDDEN = 0, SHOWN = 1, MATCHED = 2 };
enum Pick { INVALID = 0, FIRST = 1, MATCH = 2, MISMATCH = 3 };

class Game {
 public:
  uint8_t card[CARDS];     // номер рисунка 0..7
  uint8_t state[CARDS];
  int first = -1, second = -1;   // открытые сейчас (до совпадения/закрытия)
  int moves = 0;           // сколько пар открыто
  int pairsLeft = 8;
  RandFn rnd = nullptr;

  void reset() {
    for (int i = 0; i < CARDS; i++) {
      card[i] = (uint8_t)(i / 2);
      state[i] = HIDDEN;
    }
    for (int i = CARDS - 1; i > 0; i--) {
      int j = (int)(r() % (uint32_t)(i + 1));
      uint8_t t = card[i];
      card[i] = card[j];
      card[j] = t;
    }
    first = second = -1;
    moves = 0;
    pairsLeft = 8;
  }

  // Открыть карточку i.
  int pick(int i) {
    if (i < 0 || i >= CARDS || state[i] != HIDDEN || second >= 0) return INVALID;
    state[i] = SHOWN;
    if (first < 0) {
      first = i;
      return FIRST;
    }
    second = i;
    moves++;
    if (card[first] == card[second]) {
      state[first] = state[second] = MATCHED;
      first = second = -1;
      pairsLeft--;
      return MATCH;
    }
    return MISMATCH;
  }

  // Закрыть несовпавшую пару (после паузы, чтобы игрок успел посмотреть).
  void hideMismatch() {
    if (second < 0) return;
    state[first] = state[second] = HIDDEN;
    first = second = -1;
  }

  bool done() const { return pairsLeft == 0; }

 private:
  uint32_t r() { return rnd ? rnd() : fallbackRand(); }
};
}  // namespace memory

// ============================ ПИНГ-ПОНГ ============================
// Поле 240 x 220 (y от 20 до 240). Слева ракетка игрока, справа - платы. До 7 очков.
namespace pong {
const float FX0 = 0, FX1 = 240, FY0 = 20, FY1 = 240;
const float PW = 6, PH = 40, BALL = 6;
const float PLAYER_X = 4, CPU_X = 230;
const int WIN = 7;
const float PADDLE_SPEED = 4.5f;   // ракетка игрока быстрее самого крутого мяча - любой мяч можно взять

class Game {
 public:
  float py = 110, cy = 110;      // верх ракеток
  float bx = 117, by = 127, vx = 0, vy = 0;
  int playerScore = 0, cpuScore = 0;
  float cpuSpeed = 3.2f;         // максимальная скорость бота за кадр
  float cpuErr = 0;              // куда бот промахивается (меняется после каждого вашего удара)
  int cpuErrRange = 24;          // разброс ошибки бота, пикселей (подобрано: средний игрок выигрывает ~2 из 3)
  float cpuReactX = 80;          // с какого места поля бот начинает следить за мячом
  bool over = false;
  RandFn rnd = nullptr;

  void reset() {
    playerScore = cpuScore = 0;
    over = false;
    py = cy = (FY0 + FY1 - PH) / 2;
    serve(1);
  }

  // Подача от центра в сторону dir (1 - к боту, -1 - к игроку).
  void serve(int dir) {
    bx = (FX0 + FX1 - BALL) / 2;
    by = (FY0 + FY1 - BALL) / 2;
    float a = ((int)(r() % 100) - 50) / 100.0f;      // -0.5..0.5
    vx = 2.6f * dir;
    vy = 2.0f * a;
  }

  // Один кадр. input: -1 вверх, 1 вниз, 0 стоять. Возвращает: 0, 1 - очко игроку, 2 - очко боту.
  int step(int input) {
    if (over) return 0;
    py += input * PADDLE_SPEED;
    if (py < FY0) py = FY0;
    if (py > FY1 - PH) py = FY1 - PH;
    // бот следит за мячом, только когда мяч летит к нему и уже за серединой; целится с ошибкой
    float target = (vx > 0 && bx > cpuReactX) ? by + BALL / 2 - PH / 2 + cpuErr : (FY0 + FY1 - PH) / 2;
    float d = target - cy;
    if (d > cpuSpeed) d = cpuSpeed;
    if (d < -cpuSpeed) d = -cpuSpeed;
    cy += d;
    if (cy < FY0) cy = FY0;
    if (cy > FY1 - PH) cy = FY1 - PH;

    bx += vx;
    by += vy;
    if (by < FY0) {
      by = FY0;
      vy = -vy;
    }
    if (by > FY1 - BALL) {
      by = FY1 - BALL;
      vy = -vy;
    }
    // ракетка игрока
    if (vx < 0 && bx <= PLAYER_X + PW && bx >= PLAYER_X - BALL && by + BALL >= py && by <= py + PH) {
      bounce(py);
      bx = PLAYER_X + PW;
      cpuErr = (float)((int)(r() % (uint32_t)(2 * cpuErrRange + 1)) - cpuErrRange);   // иногда бот не дотягивается
    }
    // ракетка бота
    if (vx > 0 && bx + BALL >= CPU_X && bx <= CPU_X + PW && by + BALL >= cy && by <= cy + PH) {
      bounce(cy);
      bx = CPU_X - BALL;
    }
    if (bx < FX0 - BALL) {
      cpuScore++;
      if (cpuScore >= WIN) over = true;
      serve(-1);
      return 2;
    }
    if (bx > FX1) {
      playerScore++;
      if (playerScore >= WIN) over = true;
      serve(1);
      return 1;
    }
    return 0;
  }

  bool playerWon() const { return playerScore >= WIN; }

 private:
  uint32_t r() { return rnd ? rnd() : fallbackRand(); }

  // Отскок: угол зависит от того, куда попал мяч на ракетке; каждый удар чуть быстрее.
  void bounce(float paddleY) {
    float rel = ((by + BALL / 2) - (paddleY + PH / 2)) / (PH / 2);   // -1..1
    if (rel > 1) rel = 1;
    if (rel < -1) rel = -1;
    float speed = (vx < 0 ? -vx : vx) * 1.05f;
    if (speed > 6.0f) speed = 6.0f;
    vx = vx < 0 ? speed : -speed;
    vy = rel * speed * 0.6f;                 // не круче 3,6 пикс/кадр по вертикали
  }
};
}  // namespace pong

// ============================ ЛЕТЯЩАЯ ПТИЧКА ============================
// Небо y 20..240. Нажатие - взмах вверх. Пролетаешь между трубами - очко.
namespace flappy {
const float Y0 = 20, Y1 = 240;
const float BIRD_X = 60, BW = 14, BH = 12;
const float PIPE_W = 30, GAP = 74, SPACING = 130, SPEED = 2.0f;
const float GRAVITY = 0.32f, FLAP = -4.6f;
const int MAXP = 4;

struct Pipe {
  float x;
  int gapY;      // верх проёма
  bool passed;
  bool used;
};

class Game {
 public:
  float y = 120, vy = 0;
  Pipe pipe[MAXP];
  uint32_t score = 0, best = 0;
  bool over = false;
  bool started = false;
  RandFn rnd = nullptr;

  void reset() {
    y = (Y0 + Y1) / 2;
    vy = 0;
    score = 0;
    over = false;
    started = false;
    for (int i = 0; i < MAXP; i++) pipe[i].used = false;
    float x = 260;
    for (int i = 0; i < 3; i++, x += SPACING) addPipe(x);
  }

  void flap() {
    if (over) return;
    started = true;
    vy = FLAP;
  }

  // Один кадр. Возвращает true, если только что набрали очко.
  bool step() {
    if (over || !started) return false;
    vy += GRAVITY;
    if (vy > 6) vy = 6;
    y += vy;
    bool scored = false;
    float maxX = 0;
    for (int i = 0; i < MAXP; i++) {
      Pipe& p = pipe[i];
      if (!p.used) continue;
      p.x -= SPEED;
      if (p.x + PIPE_W < 0) {
        p.used = false;
        continue;
      }
      if (p.x > maxX) maxX = p.x;
      if (!p.passed && p.x + PIPE_W < BIRD_X) {
        p.passed = true;
        score++;
        if (score > best) best = score;
        scored = true;
      }
      // столкновение с трубой
      if (BIRD_X + BW > p.x && BIRD_X < p.x + PIPE_W && (y < p.gapY || y + BH > p.gapY + GAP)) over = true;
    }
    if (maxX < 240) addPipe(maxX + SPACING);       // справа за экраном всегда ждёт следующая труба
    if (y < Y0) {
      y = Y0;
      vy = 0;
    }
    if (y + BH > Y1) {
      y = Y1 - BH;
      over = true;
    }
    return scored;
  }

 private:
  uint32_t r() { return rnd ? rnd() : fallbackRand(); }

  void addPipe(float x) {
    for (int i = 0; i < MAXP; i++)
      if (!pipe[i].used) {
        int lo = (int)Y0 + 18, hi = (int)(Y1 - GAP) - 18;
        pipe[i] = {x, lo + (int)(r() % (uint32_t)(hi - lo + 1)), false, true};
        return;
      }
  }
};
}  // namespace flappy

}  // namespace arcade
