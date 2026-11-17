// «Райдер»: мотоцикл едет по холмам. Кнопка - газ; в воздухе газ крутит мотоцикл назад (сальто).
// Заезд длится минуту (каждое сальто добавляет 2 секунды): кто дальше уедет и больше накрутит.
// Приземлиться надо на колёса. Трасса у всех одинаковая, поэтому рекорды разных игроков можно сравнивать.
// Чистая логика без экрана - тестируется на компьютере.
#pragma once
#include <math.h>
#include <stdint.h>

namespace rider {

const float G = 0.22f;            // тяжесть, точек на кадр^2
const float V_MIN = 1.6f;         // мотоцикл не останавливается совсем
const float V_MAX = 6.5f;
const float ACCEL = 0.09f;        // газ
const float DRAG = 0.012f;        // накат без газа
const float SPIN = 0.19f;         // вращение в воздухе при газе, радиан за кадр
const float LAND_OK = 1.05f;      // допустимый угол к склону при посадке (~60 градусов)
const float LEVEL = 0.05f;        // как быстро мотоцикл сам выравнивается в воздухе без газа
const float PI_F = 3.14159265f;
const int FPS = 30;
const int RUN_FRAMES = 60 * FPS;  // длина заезда
const int FLIP_BONUS = 2 * FPS;   // прибавка времени за сальто

// Трамплин: каждые 520 точек дорога плавно поднимается на 30 точек и обрывается.
const float RAMP_FROM = 420, RAMP_EVERY = 520, RAMP_LEN = 90, RAMP_H = 30;
inline float ramp(float x) {
  if (x < RAMP_FROM) return 0;
  float u = fmodf(x - RAMP_FROM, RAMP_EVERY);
  if (u >= RAMP_LEN) return 0;
  float t = u / RAMP_LEN;
  return RAMP_H * t * t;                               // всё круче к краю
}

// Высота земли в точке x (экранная: больше - ниже). Холмы растут с расстоянием.
inline float ground(float x) {
  if (x < 0) x = 0;
  float grow = x / 3000.0f;
  if (grow > 1.0f) grow = 1.0f;
  float a = 1.0f + grow;
  float start = x < 220 ? x / 220.0f : 1.0f;          // первые метры - ровная дорога
  return 150.0f - ramp(x) - start * a * (13.0f * sinf(x / 62.0f) + 6.0f * sinf(x / 23.0f + 1.3f) + 5.0f * sinf(x / 131.0f + 0.7f));
}

// Угол склона (вниз по экрану - плюс). У самого края трамплина берём наклон перед обрывом.
inline float slopeAt(float x) {
  float a = x - 3, b = x + 3;
  if (x >= RAMP_FROM) {
    float u = fmodf(x - RAMP_FROM, RAMP_EVERY);
    if (u < RAMP_LEN && u + 3 >= RAMP_LEN) b = x + (RAMP_LEN - u) - 0.01f;      // не заглядываем за край
    if (u >= RAMP_LEN && u - 3 < RAMP_LEN) a = x - (u - RAMP_LEN);              // и не оглядываемся на него
  }
  if (b - a < 0.5f) a = b - 0.5f;
  return atan2f(ground(b) - ground(a), b - a);
}

inline float wrapAngle(float a) {
  while (a > PI_F) a -= 2 * PI_F;
  while (a < -PI_F) a += 2 * PI_F;
  return a;
}

class Game {
 public:
  float x = 0, y = 0;              // точка касания (середина между колёсами)
  float v = 3.0f;                  // скорость вдоль склона (на земле)
  float vx = 0, vy = 0;            // скорость в воздухе
  float angle = 0;                 // наклон мотоцикла (0 - ровно; минус - нос вверх)
  float spun = 0;                  // сколько накручено в этом прыжке
  bool air = false, over = false;
  bool crashed = false;            // заезд кончился аварией (а не по времени)
  int timeLeft = RUN_FRAMES;       // осталось кадров
  int flips = 0;                   // всего сальто
  int lastFlips = 0;               // сальто в последнем прыжке (для надписи)
  uint32_t best = 0;

  void reset() {
    x = 30;
    y = ground(x);
    v = 3.0f;
    vx = vy = 0;
    angle = slopeAt(x);
    spun = 0;
    air = over = crashed = false;
    flips = lastFlips = 0;
    timeLeft = RUN_FRAMES;
  }

  uint32_t score() const { return (uint32_t)(x / 10.0f) + (uint32_t)flips * 25u; }

  // Один кадр. gas - кнопка нажата. Возвращает: 0 - едем, 1 - прыжок начался, 2 - приземлились, 3 - авария, 4 - время вышло.
  int step(bool gas) {
    if (over) return 0;
    lastFlips = 0;
    if (--timeLeft <= 0) {
      timeLeft = 0;
      over = true;
      if (score() > best) best = score();
      return 4;
    }
    if (!air) {
      float s = slopeAt(x);
      v += (gas ? ACCEL : -DRAG) + G * sinf(s) * 0.55f;        // под гору разгоняет, в гору тормозит
      if (v < V_MIN) v = V_MIN;
      if (v > V_MAX) v = V_MAX;
      float nx = x + v * cosf(s);
      float fy = y + v * sinf(s) + G * 0.5f;                    // куда ушли бы по прямой
      float gy = ground(nx);
      if (fy < gy - 0.15f) {                                    // земля уходит из-под колёс быстрее, чем падает мотоцикл - летим
        air = true;
        vx = v * cosf(s);
        vy = v * sinf(s);
        x = nx;
        y = fy;
        angle = s;
        spun = 0;
        return 1;
      }
      x = nx;
      y = gy;
      angle = slopeAt(x);
      return 0;
    }
    vy += G;
    x += vx;
    y += vy;
    if (gas) {                                                  // газ в воздухе - сальто назад
      angle -= SPIN;
      spun += SPIN;
    } else {                                                    // без газа нос сам понемногу опускается к земле
      float want = slopeAt(x + vx * 6);
      float d = wrapAngle(want - angle);
      angle += d > LEVEL ? LEVEL : d < -LEVEL ? -LEVEL : d;
    }
    float gy = ground(x);
    if (y < gy) return 0;
    y = gy;
    float s = slopeAt(x);
    float diff = wrapAngle(angle - s);
    if (diff > LAND_OK || diff < -LAND_OK) {                    // упали не на колёса
      over = true;
      crashed = true;
      if (score() > best) best = score();
      return 3;
    }
    lastFlips = (int)((spun + 0.9f) / (2 * PI_F));
    flips += lastFlips;
    timeLeft += lastFlips * FLIP_BONUS;
    air = false;
    v = vx * cosf(s) + vy * sinf(s);                            // скорость вдоль склона
    if (v < V_MIN) v = V_MIN;
    if (v > V_MAX) v = V_MAX;
    angle = s;
    if (score() > best) best = score();
    return 2;
  }
};

}  // namespace rider
