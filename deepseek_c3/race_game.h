// Псевдо-3D гонка: бесконечная трасса с поворотами и холмами, поток машин, чекпойнты на время.
// Логика и отрисовка в кадровый буфер 120x110 (потом выводится на экран с увеличением x2).
// Без Arduino - тестируется на компьютере.
#pragma once
#include <stdint.h>
#include <math.h>

namespace race {

const int W = 120, H = 110;              // размер кадра (на экране - 240x220)
const float SEG = 200;                   // длина отрезка трассы
const float ROAD_W = 2000;               // половина ширины дороги
const float CAM_H = 1000;                // высота камеры
const float DEPTH = 0.84f;               // 1 / tan(угол обзора / 2), угол ~100 градусов
const int DRAW = 70;                     // сколько отрезков видно вперёд
const int SEGS = 1600;                   // длина трассы в отрезках (дальше по кругу)
const float TRACK_LEN = SEGS * SEG;
const float DT = 1.0f / 30;              // шаг физики
const float MAXS = SEG / DT;             // максимальная скорость (6000 = 216 км/ч)
const float ACCEL = MAXS / 5, BRAKE = -MAXS, DECEL = -MAXS / 5;
const float OFFROAD_DECEL = -MAXS / 2, OFFROAD_LIMIT = MAXS / 4;
const float CENTRIFUGAL = 0.3f;
const float PLAYER_Z = CAM_H * DEPTH;    // машина игрока впереди камеры
const float CAR_W = 260;                 // полуширина машины в мире (на экране как машина игрока)
const int NCARS = 8;
const float START_TIME = 35, CHECK_DIST = 400 * SEG;   // бонус за чекпойнт уменьшается: 15 с, 14,5 с ... до 8 с

// Цвета RGB565
const uint16_t SKY_TOP = 0x3C9F, SKY_LOW = 0x9EFF, HILLS = 0x3A6B, HILLS_D = 0x2965;
const uint16_t GRASS_L = 0x3666, GRASS_D = 0x2D24, RUMBLE_L = 0xFFFF, RUMBLE_D = 0xC800;
const uint16_t ROAD_L = 0x6B4D, ROAD_D = 0x630C, LANE = 0xFFFF;

struct Seg {
  float curve;
  float y;               // высота начала отрезка
};

struct Car {
  float z;               // положение на трассе
  float x;               // смещение от центра: -1..1 - дорога
  float speed;
  uint16_t color;
};

struct Proj {
  float x, y, w, scale, cz;
};

typedef uint32_t (*RandFn)();

class Game {
 public:
  Seg seg[SEGS];
  Car car[NCARS];
  float pos = 0;          // положение камеры на трассе
  float playerX = 0;      // -1..1 - на дороге
  float speed = 0;
  float dist = 0;         // пройдено всего
  float timeLeft = START_TIME;
  float nextCheck = CHECK_DIST;
  float checkMsg = 0;     // сколько ещё показывать «Чекпойнт»
  int checks = 0;         // пройдено чекпойнтов
  float lastBonus = 0;
  float bgOffset = 0;     // сдвиг дальних гор на поворотах
  float steerShown = 0;   // наклон машины игрока на экране
  bool over = false;
  bool crashed = false;   // последний шаг - столкновение (для тряски)
  RandFn rnd = nullptr;

  void reset() {
    buildTrack();
    pos = 0;
    playerX = 0;
    speed = 0;
    dist = 0;
    timeLeft = START_TIME;
    nextCheck = CHECK_DIST;
    checkMsg = 0;
    checks = 0;
    bgOffset = 0;
    over = false;
    static const uint16_t cols[] = {0xF800, 0x041F, 0xFFE0, 0x07E0, 0xF81F, 0xFD20, 0xFFFF, 0x07FF};
    for (int i = 0; i < NCARS; i++) {
      car[i].z = (float)(20 + i * 30 + (int)(r() % 15)) * SEG;
      car[i].x = lane((int)(r() % 3));
      car[i].speed = MAXS * (0.35f + (float)(r() % 30) / 100.0f);
      car[i].color = cols[i % 8];
    }
  }

  float kmh() const { return speed * 0.036f; }
  uint32_t meters() const { return (uint32_t)(dist / 100); }

  // Шаг физики. steer: -1..1 (руль), brake: тормоз. Возвращает true, если только что прошли чекпойнт.
  bool step(float steer, bool brake) {
    if (over) return false;
    crashed = false;
    int pseg = segIndex(pos + PLAYER_Z);
    float sp = speed / MAXS;
    float dx = DT * 2 * sp;
    playerX += steer * dx;
    playerX -= dx * sp * seg[pseg].curve * CENTRIFUGAL;
    steerShown += (steer - steerShown) * 0.3f;
    speed += (brake ? BRAKE : ACCEL) * DT;
    if ((playerX < -1 || playerX > 1) && speed > OFFROAD_LIMIT) speed += OFFROAD_DECEL * DT;
    if (playerX < -2) playerX = -2;
    if (playerX > 2) playerX = 2;
    if (speed < 0) speed = 0;
    if (speed > MAXS) speed = MAXS;
    bgOffset += seg[pseg].curve * sp * 0.6f;

    // машины в потоке
    float pz = wrap(pos + PLAYER_Z);
    for (int i = 0; i < NCARS; i++) {
      Car& c = car[i];
      c.z = wrap(c.z + c.speed * DT);
      float d = rel(c.z, pz);                       // насколько машина впереди игрока
      if (d > -SEG * 0.6f && d < SEG * 0.6f && fabsf(playerX - c.x) < 0.28f && speed > c.speed) {
        speed = c.speed * 0.5f;                     // врезались сзади: потеря скорости
        pos = wrap(c.z - PLAYER_Z - SEG * 0.7f);
        crashed = true;
      }
      if (d < -SEG * 30) {                          // далеко позади - переносим вперёд
        c.z = wrap(pz + SEG * (float)(DRAW + (int)(r() % 40)));
        c.x = lane((int)(r() % 3));
        c.speed = MAXS * (0.35f + (float)(r() % 30) / 100.0f);
      }
    }

    pos = wrap(pos + speed * DT);
    dist += speed * DT;
    timeLeft -= DT;
    if (checkMsg > 0) checkMsg -= DT;
    bool check = false;
    if (dist >= nextCheck) {
      lastBonus = 15 - 0.5f * (float)(checks < 14 ? checks : 14);
      timeLeft += lastBonus;
      checks++;
      nextCheck += CHECK_DIST;
      checkMsg = 2.0f;
      check = true;
    }
    if (timeLeft <= 0) {
      timeLeft = 0;
      over = true;
    }
    return check;
  }

  // ---------------- Отрисовка в буфер W x H ----------------
  void render(uint16_t* fb) {
    drawBackground(fb);
    int base = segIndex(pos);
    float pct = fmodf(pos, SEG) / SEG;
    float camZ = pos;
    float playerY = lerp(seg[base].y, seg[(base + 1) % SEGS].y, pct);
    float camY = playerY + CAM_H, camX = playerX * ROAD_W;
    float x = 0, dx = -seg[base].curve * pct;
    float maxy = H;
    static Proj p1s[DRAW];
    static float clips[DRAW];
    float z0 = (float)(int)(pos / SEG) * SEG;          // начало базового отрезка (без зацикливания)
    for (int n = 0; n < DRAW; n++) {
      int i = (base + n) % SEGS, i2 = (i + 1) % SEGS;
      Proj a = project(x, seg[i].y, z0 + n * SEG, camX, camY, camZ);
      Proj b = project(x + dx, seg[i2].y, z0 + (n + 1) * SEG, camX, camY, camZ);
      x += dx;
      dx += seg[i].curve;
      p1s[n] = a;
      clips[n] = maxy;
      if (a.cz <= DEPTH || b.y >= a.y || b.y >= maxy) continue;
      int yTop = b.y < 0 ? 0 : (int)ceilf(b.y);
      int yBot = a.y < maxy ? (int)ceilf(a.y) : (int)ceilf(maxy);
      if (yBot > H) yBot = H;
      bool light = (i / 3) % 2 == 0;
      for (int y = yTop; y < yBot; y++) {
        float t = (y - b.y) / (a.y - b.y);
        drawRow(fb, y, b.x + (a.x - b.x) * t, b.w + (a.w - b.w) * t, light);
      }
      if (b.y < maxy) maxy = b.y;
    }
    // машины: от дальних к ближним, с обрезкой за холмами
    float pz = wrap(pos + PLAYER_Z);
    for (int n = DRAW - 1; n >= 1; n--) {
      int i = (base + n) % SEGS;
      for (int k = 0; k < NCARS; k++) {
        if (segIndex(car[k].z) != i) continue;
        const Proj& a = p1s[n];
        if (a.cz <= DEPTH) continue;
        if (rel(car[k].z, pz) < -SEG) continue;      // уже позади игрока
        float half = a.scale * CAR_W * W / 2;
        float cx = a.x + a.scale * car[k].x * ROAD_W * W / 2;
        drawCar(fb, cx, a.y, half, car[k].color, clips[n]);
      }
    }
    // машина игрока
    float bounce = (speed > 0 && ((int)(dist / 300)) % 2) ? 1 : 0;
    if (crashed) bounce = 3;
    drawCar(fb, W / 2 + steerShown * 4, H - 4 - bounce, 15, 0xF800, H);
  }

 private:
  uint32_t seedv = 31337;
  uint32_t r() {
    if (rnd) return rnd();
    seedv = seedv * 1103515245u + 12345u;
    return seedv >> 8;
  }

  static float lerp(float a, float b, float t) { return a + (b - a) * t; }
  static float lane(int k) { return -0.66f + 0.66f * (float)k; }
  static float wrap(float z) {
    while (z >= TRACK_LEN) z -= TRACK_LEN;
    while (z < 0) z += TRACK_LEN;
    return z;
  }
  // расстояние от b до a вперёд по кругу, в диапазоне -TRACK_LEN/2..TRACK_LEN/2
  static float rel(float a, float b) {
    float d = a - b;
    if (d > TRACK_LEN / 2) d -= TRACK_LEN;
    if (d < -TRACK_LEN / 2) d += TRACK_LEN;
    return d;
  }
  static int segIndex(float z) { return ((int)(wrap(z) / SEG)) % SEGS; }

  static Proj project(float wx, float wy, float wz, float camX, float camY, float camZ) {
    Proj p;
    p.cz = wz - camZ;
    float cz = p.cz < 0.01f ? 0.01f : p.cz;
    p.scale = DEPTH / cz;
    p.x = W / 2.0f + p.scale * (wx - camX) * W / 2;
    p.y = H / 2.0f - p.scale * (wy - camY) * H / 2;
    p.w = p.scale * ROAD_W * W / 2;
    return p;
  }

  // ---- трасса ----
  static float easeIn(float a, float b, float t) { return a + (b - a) * t * t; }
  static float easeInOut(float a, float b, float t) { return a + (b - a) * ((-cosf(t * 3.14159265f) / 2) + 0.5f); }

  int built = 0;
  float lastY = 0;
  void addSeg(float curve, float y) {
    if (built >= SEGS) return;
    seg[built].curve = curve;
    seg[built].y = y;
    built++;
  }
  // въезд, середина, выезд; curve - кривизна; hill - перепад высоты (в отрезках)
  void addRoad(int enter, int hold, int leave, float curve, float hill) {
    float startY = lastY, endY = startY + hill * SEG;
    int total = enter + hold + leave;
    for (int n = 0; n < enter; n++) addSeg(easeIn(0, curve, (float)n / enter), easeInOut(startY, endY, (float)n / total));
    for (int n = 0; n < hold; n++) addSeg(curve, easeInOut(startY, endY, (float)(enter + n) / total));
    for (int n = 0; n < leave; n++)
      addSeg(easeInOut(curve, 0, (float)n / leave), easeInOut(startY, endY, (float)(enter + hold + n) / total));
    lastY = endY;
  }

  void buildTrack() {
    built = 0;
    lastY = 0;
    addRoad(10, 40, 10, 0, 0);                         // старт - прямая
    while (built < SEGS - 160) {
      int t = (int)(r() % 5);
      int len = 20 + (int)(r() % 30);
      float curve = (float)(2 + (int)(r() % 4)) * ((r() % 2) ? 1 : -1);
      float hill = (float)((int)(r() % 61) - 30);
      if (lastY + hill * SEG > 60 * SEG || lastY + hill * SEG < -60 * SEG) hill = -hill;   // не уходим слишком высоко
      if (t == 0) addRoad(len / 2, len, len / 2, 0, 0);             // прямая
      else if (t == 1) addRoad(len / 2, len, len / 2, curve, 0);    // поворот
      else if (t == 2) addRoad(len / 2, len / 2, len / 2, 0, hill); // холм
      else addRoad(len / 2, len, len / 2, curve, hill);             // поворот на холме
    }
    float back = -lastY / SEG;                         // плавно возвращаемся к началу (трасса по кругу)
    int rest = SEGS - built;
    addRoad(rest / 3, rest - 2 * (rest / 3), rest / 3, 0, back);
    while (built < SEGS) addSeg(0, 0);
  }

  // ---- рисование ----
  static void fillRow(uint16_t* fb, int y, int x0, int x1, uint16_t c) {
    if (x0 < 0) x0 = 0;
    if (x1 > W) x1 = W;
    uint16_t* p = fb + y * W;
    for (int x = x0; x < x1; x++) p[x] = c;
  }

  void drawBackground(uint16_t* fb) {
    // небо с переходом к горизонту
    for (int y = 0; y < H; y++) {
      float t = (float)y / (H / 2);
      if (t > 1) t = 1;
      uint16_t c = t < 0.5f ? SKY_TOP : SKY_LOW;
      fillRow(fb, y, 0, W, c);
    }
    // дальние горы: сдвигаются на поворотах
    int off = (int)bgOffset;
    for (int x = 0; x < W; x++) {
      int u = x + off;
      float h = 10 + 6 * sinf(u * 0.07f) + 4 * sinf(u * 0.19f + 1.3f);
      int top = H / 2 - (int)h;
      for (int y = top; y < H; y++) fb[y * W + x] = (y < H / 2 - (int)(h * 0.5f)) ? HILLS : HILLS_D;
    }
  }

  static void drawRow(uint16_t* fb, int y, float cx, float w, bool light) {
    fillRow(fb, y, 0, W, light ? GRASS_L : GRASS_D);
    float rw = w * 1.15f;
    fillRow(fb, y, (int)(cx - rw), (int)(cx + rw + 1), light ? RUMBLE_L : RUMBLE_D);
    fillRow(fb, y, (int)(cx - w), (int)(cx + w + 1), light ? ROAD_L : ROAD_D);
    if (light) {                                       // разметка между полосами
      float lw = w / 32 + 0.5f;
      for (int k = -1; k <= 1; k += 2) {
        float lx = cx + k * w / 3;
        fillRow(fb, y, (int)(lx - lw), (int)(lx + lw + 1), LANE);
      }
    }
  }

  static uint16_t dark(uint16_t c) { return (c >> 1) & 0x7BEF; }

  // Машина: низ в (cx, by), полуширина half; рисуем только строки выше clip.
  static void drawCar(uint16_t* fb, float cx, float by, float half, uint16_t col, float clip) {
    if (half < 0.6f) return;
    int w = (int)(half * 2 + 0.5f);
    int h = (int)(half * 1.1f + 0.5f);
    if (h < 1) h = 1;
    int x0 = (int)(cx - half), y0 = (int)by - h;
    int limit = (int)clip < H ? (int)clip : H;
    for (int j = 0; j < h; j++) {
      int y = y0 + j;
      if (y < 0 || y >= limit) continue;
      float t = (float)j / h;
      uint16_t c;
      int inset = 0;
      if (t < 0.45f) {                                 // крыша и заднее стекло
        inset = (int)(w * 0.18f);
        c = t < 0.12f ? dark(col) : 0x2945;
      } else if (t < 0.85f) {
        c = col;                                       // кузов
      } else {
        c = 0x0000;                                    // колёса/тень
      }
      fillRow(fb, y, x0 + inset, x0 + w - inset, c);
      if (t >= 0.55f && t < 0.7f && w >= 6) {          // задние фонари
        int lw = w / 6 > 1 ? w / 6 : 1;
        fillRow(fb, y, x0, x0 + lw, 0xF800);
        fillRow(fb, y, x0 + w - lw, x0 + w, 0xF800);
      }
    }
  }
};

}  // namespace race
