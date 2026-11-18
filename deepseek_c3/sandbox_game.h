// «Песочница»: двумерный мир из блоков - копать, строить, мастерить инструменты, отбиваться от слизней ночью.
// Здесь логика (мир, физика, крафт, сохранение) - без экрана, проверяется на компьютере.
// Управление: ручка влево/вправо - идти, вверх - прыжок; направление ручки - куда целиться;
// нажатие - ударить по блоку (копать), поставить выбранный блок в пустое место или ударить слизня.
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace sbx {

const int W = 256, H = 96;          // мир в блоках
const int T = 8;                    // блок - 8x8 точек
const int TICKS = 30;               // шагов игры в секунду
const int DAY = TICKS * 60 * 8;     // сутки - 8 минут (5 день, 3 ночь)
const int NIGHT_FROM = TICKS * 60 * 5;
const int PW = 6, PH = 14;          // размер игрока в точках
const int MAX_HP = 10;              // половинки сердец
const int SLOTS = 16;
const int MAX_SLIMES = 5;

enum Block : uint8_t {
  AIR, DIRT, GRASS, STONE, LOG, LEAVES, PLANKS, COAL, IRON, GOLD, SAND, BEDROCK, TORCH, BENCH, BRICK, BLOCKS
};
// Предметы: блоки (их номера) и всё остальное
enum Item : uint8_t {
  I_NONE = 0,
  I_GEL = 32, I_PICK_WOOD, I_PICK_STONE, I_PICK_IRON, I_SWORD_WOOD, I_SWORD_IRON, I_CROWN
};
enum Event { E_MINE = 1, E_HIT = 2, E_PLACE = 4, E_HURT = 8, E_DEAD = 16, E_KILL = 32, E_ATTACK = 64, E_NOPE = 128 };
enum Aim : uint8_t { A_UP, A_RIGHT, A_DOWN, A_LEFT };

inline bool solidBlock(uint8_t b) { return b != AIR && b != TORCH && b != LEAVES; }
inline bool placeable(uint8_t it) { return it > AIR && it < BLOCKS && it != BEDROCK && it != GRASS; }

struct Slot {
  uint8_t item;
  uint16_t n;
};

struct Slime {
  float x, y, vx, vy;
  int8_t hp;
  bool alive;
  int16_t wait;
};

struct Recipe {
  uint8_t out, outN, a, an, b, bn;
  bool bench;                        // нужен верстак рядом
};
static const Recipe RECIPES[] = {
    {PLANKS, 4, LOG, 1, 0, 0, false},
    {BENCH, 1, PLANKS, 6, 0, 0, false},
    {TORCH, 3, COAL, 1, PLANKS, 1, false},
    {TORCH, 3, I_GEL, 1, PLANKS, 1, false},
    {I_PICK_WOOD, 1, PLANKS, 8, 0, 0, true},
    {I_SWORD_WOOD, 1, PLANKS, 6, 0, 0, true},
    {I_PICK_STONE, 1, STONE, 6, PLANKS, 3, true},
    {BRICK, 2, STONE, 2, 0, 0, true},
    {I_PICK_IRON, 1, IRON, 6, PLANKS, 3, true},
    {I_SWORD_IRON, 1, IRON, 5, PLANKS, 2, true},
    {I_CROWN, 1, GOLD, 8, 0, 0, true},
};
const int RECIPE_COUNT = sizeof(RECIPES) / sizeof(RECIPES[0]);

struct Input {
  float mx = 0;                     // -1..1 идти
  bool jump = false;                // держат «вверх»
  int aim = -1;                     // куда сейчас наклонена ручка (A_*), -1 - никуда
  bool act = false;                 // нажали кнопку (один раз)
};

struct Game {
  uint8_t w[H][W];
  float px, py, vx, vy;
  bool ground = false;
  uint8_t aim = A_RIGHT;
  int8_t facing = 1;
  int hp = MAX_HP, hurtT = 0, regenT = 0;
  uint32_t tick = TICKS * 60;       // начинаем утром
  Slot inv[SLOTS];
  int sel = 0;
  int hitX = -1, hitY = -1, hits = 0;   // блок, по которому бьём, и сколько ударов уже
  Slime sl[MAX_SLIMES];
  uint32_t rng = 1;
  int spawnX = W / 2, spawnY = 0;
  bool won = false;
  int16_t surf[W];                  // верхний непрозрачный блок каждого столбца (для света)
  int16_t g0[W];                    // где была земля при создании мира: ниже - фон пещеры, выше - небо

  uint32_t rnd() {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
  }
  int rndi(int a, int b) { return a + (int)(rnd() % (uint32_t)(b - a + 1)); }

  uint8_t at(int x, int y) const {
    if (x < 0 || x >= W || y >= H) return BEDROCK;
    if (y < 0) return AIR;
    return w[y][x];
  }
  bool solid(int x, int y) const { return solidBlock(at(x, y)); }
  void set(int x, int y, uint8_t b) {
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    w[y][x] = b;
    updSurf(x);
  }
  void updSurf(int x) {
    int y = 0;
    while (y < H && (w[y][x] == AIR || w[y][x] == TORCH || w[y][x] == LEAVES || w[y][x] == LOG)) y++;   // деревья свет не заслоняют
    surf[x] = (int16_t)y;
  }

  // ---------- мир ----------
  float noise1(float x, uint32_t salt) const {      // гладкий шум по x
    int i = (int)floorf(x);
    float f = x - i;
    auto h = [&](int k) {
      uint32_t v = (uint32_t)k * 2654435761u ^ salt;
      v ^= v >> 15;
      v *= 2246822519u;
      v ^= v >> 13;
      return (float)(v & 0xFFFF) / 65535.0f;
    };
    float a = h(i), b = h(i + 1);
    float s = f * f * (3 - 2 * f);
    return a + (b - a) * s;
  }

  void carve(int cx, int cy, int r) {
    for (int y = cy - r; y <= cy + r; y++)
      for (int x = cx - r; x <= cx + r; x++)
        if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r + 1 && x > 0 && x < W - 1 && y > 0 && y < H - 2 &&
            w[y][x] != BEDROCK)
          w[y][x] = AIR;
  }

  void vein(int x, int y, uint8_t ore, int n) {
    for (int i = 0; i < n; i++) {
      if (x > 0 && x < W - 1 && y > 0 && y < H - 2 && w[y][x] == STONE) w[y][x] = ore;
      x += rndi(-1, 1);
      y += rndi(-1, 1);
    }
  }

  void generate(uint32_t seed) {
    rng = seed ? seed : 1;
    uint32_t salt = rnd();
    int hgt[W];
    for (int x = 0; x < W; x++) {
      float v = noise1(x / 24.0f, salt) * 12 + noise1(x / 9.0f, salt ^ 0x55) * 5 + noise1(x / 4.0f, salt ^ 0xAA) * 1.5f;
      hgt[x] = 28 + (int)v;                          // поверхность примерно на 28..46
    }
    for (int x = 0; x < W; x++) {
      int dirt = 3 + (int)(noise1(x / 6.0f, salt ^ 7) * 3);
      bool sandy = hgt[x] > 41;                      // в низинах - песок
      for (int y = 0; y < H; y++) {
        uint8_t b = AIR;
        if (y >= H - 2) b = BEDROCK;
        else if (y > hgt[x] + dirt) b = STONE;
        else if (y > hgt[x]) b = sandy ? SAND : DIRT;
        else if (y == hgt[x]) b = sandy ? SAND : GRASS;
        w[y][x] = b;
      }
      w[0][x] = AIR;
    }
    for (int y = 0; y < H; y++) w[y][0] = w[y][W - 1] = BEDROCK;   // края мира
    // пещеры: «червяки»
    for (int c = 0; c < 30; c++) {
      float x = rndi(4, W - 5), y = rndi(hgt[(int)x] + 6, H - 6);
      float dir = (rnd() % 628) / 100.0f;
      int len = rndi(30, 110), r = rndi(1, 2);
      for (int i = 0; i < len; i++) {
        carve((int)x, (int)y, r);
        dir += ((int)(rnd() % 100) - 50) / 120.0f;
        x += cosf(dir);
        y += sinf(dir) * 0.6f;
        if (x < 3 || x > W - 4 || y < 10 || y > H - 5) break;
        if (rnd() % 40 == 0) r = rndi(1, 2);
      }
    }
    // руда: уголь неглубоко, железо глубже, золото у самого дна
    for (int i = 0; i < 70; i++) vein(rndi(2, W - 3), rndi(34, H - 4), COAL, rndi(3, 7));
    for (int i = 0; i < 45; i++) vein(rndi(2, W - 3), rndi(50, H - 4), IRON, rndi(3, 6));
    for (int i = 0; i < 22; i++) vein(rndi(2, W - 3), rndi(72, H - 4), GOLD, rndi(2, 5));
    // деревья
    for (int x = 6; x < W - 6; x += rndi(5, 9)) {
      int y = hgt[x];
      if (w[y][x] != GRASS || (x > W / 2 - 6 && x < W / 2 + 6)) continue;
      int h = rndi(4, 7);
      for (int k = 1; k <= h; k++) w[y - k][x] = LOG;
      int top = y - h;
      for (int dy = -2; dy <= 1; dy++)
        for (int dx = -2; dx <= 2; dx++)
          if (abs(dx) + abs(dy) <= 3 && w[top + dy][x + dx] == AIR) w[top + dy][x + dx] = LEAVES;
    }
    for (int x = 0; x < W; x++) {
      updSurf(x);
      g0[x] = (int16_t)hgt[x];
    }
    spawnX = W / 2;
    spawnY = surf[spawnX];
    memset(inv, 0, sizeof(inv));
    memset(sl, 0, sizeof(sl));
    sel = 0;
    hp = MAX_HP;
    tick = TICKS * 60;
    won = false;
    respawn();
  }

  void respawn() {
    px = spawnX * T + 1;
    py = (surf[spawnX] - 2) * T - 1;
    while (py > 0 && boxHits(px, py)) py -= T;
    vx = vy = 0;
    hp = MAX_HP;
    hurtT = 40;
  }

  // ---------- инвентарь ----------
  int count(uint8_t it) const {
    int n = 0;
    for (const Slot& s : inv)
      if (s.item == it) n += s.n;
    return n;
  }
  bool add(uint8_t it, int n) {
    for (Slot& s : inv)
      if (s.item == it && s.n < 999) {
        s.n += n;
        return true;
      }
    for (Slot& s : inv)
      if (s.item == I_NONE) {
        s.item = it;
        s.n = n;
        return true;
      }
    return false;
  }
  void take(uint8_t it, int n) {
    for (Slot& s : inv)
      if (s.item == it && n > 0) {
        int k = s.n < n ? s.n : n;
        s.n -= k;
        n -= k;
        if (!s.n) s.item = I_NONE;
      }
  }
  int pickTier() const { return count(I_PICK_IRON) ? 3 : count(I_PICK_STONE) ? 2 : count(I_PICK_WOOD) ? 1 : 0; }
  int swordDmg() const { return count(I_SWORD_IRON) ? 4 : count(I_SWORD_WOOD) ? 2 : 1; }

  bool nearBench() const {
    int cx = (int)(px + PW / 2) / T, cy = (int)(py + PH / 2) / T;
    for (int y = cy - 4; y <= cy + 4; y++)
      for (int x = cx - 5; x <= cx + 5; x++)
        if (at(x, y) == BENCH) return true;
    return false;
  }
  // 0 - можно, 1 - не хватает материалов, 2 - нужен верстак рядом
  int canCraft(int r) const {
    const Recipe& c = RECIPES[r];
    if (count(c.a) < c.an || (c.b && count(c.b) < c.bn)) return 1;
    if (c.bench && !nearBench()) return 2;
    return 0;
  }
  bool craft(int r) {
    if (canCraft(r) != 0) return false;
    const Recipe& c = RECIPES[r];
    take(c.a, c.an);
    if (c.b) take(c.b, c.bn);
    if (!add(c.out, c.outN)) return false;
    if (c.out == I_CROWN) won = true;
    return true;
  }

  // ---------- блоки ----------
  // Сколько ударов нужно (0 - этим не сломать).
  int hardness(uint8_t b) const {
    int t = pickTier();
    switch (b) {
      case DIRT: case GRASS: case SAND: case LEAVES: case TORCH: return 1;
      case LOG: case PLANKS: case BENCH: return t ? 1 : 2;
      case STONE: case COAL: case BRICK: return t == 0 ? 0 : t == 1 ? 3 : t == 2 ? 2 : 1;
      case IRON: return t < 2 ? 0 : t == 2 ? 3 : 2;
      case GOLD: return t < 3 ? 0 : 3;
      default: return 0;
    }
  }
  uint8_t drop(uint8_t b) const { return b == GRASS ? (uint8_t)DIRT : b == LEAVES ? (uint8_t)I_NONE : b; }

  bool boxHits(float x, float y) const {
    // занятые точки: от floor(x) до ceil(x+PW)-1 (дробное положение тоже касается соседнего блока)
    int x0 = (int)floorf(x) / T, x1 = ((int)ceilf(x + PW) - 1) / T;
    int y0 = (int)floorf(y) / T, y1 = ((int)ceilf(y + PH) - 1) / T;
    if (y < 0) y0 = -1;
    for (int ty = y0; ty <= y1; ty++)
      for (int tx = x0; tx <= x1; tx++)
        if (solid(tx, ty)) return true;
    return false;
  }

  // Блок, в который целимся: сбоку - сначала на уровне головы, потом ног.
  void target(int& tx, int& ty, bool forPlace) const {
    int cx = (int)(px + PW / 2) / T;
    int head = (int)(py + 2) / T, feet = (int)(py + PH - 1) / T;
    if (aim == A_UP) { tx = cx; ty = (int)(py - 1) / T; return; }
    if (aim == A_DOWN) { tx = cx; ty = (int)(py + PH) / T; return; }
    tx = aim == A_RIGHT ? (int)(px + PW) / T : (int)(px - 1) / T;
    if (forPlace) ty = at(tx, feet) == AIR ? feet : head;
    else ty = solidOrTorch(tx, head) ? head : feet;
  }
  bool solidOrTorch(int x, int y) const { uint8_t b = at(x, y); return b != AIR; }

  // Слизень в той стороне, куда целимся (на расстоянии удара). -1 - нет.
  int slimeInReach() const {
    float cx = px + PW / 2, cy = py + PH / 2;
    float ax = aim == A_RIGHT ? 12 : aim == A_LEFT ? -12 : 0, ay = aim == A_DOWN ? 12 : aim == A_UP ? -12 : 0;
    for (int i = 0; i < MAX_SLIMES; i++) {
      if (!sl[i].alive) continue;
      float dx = sl[i].x + 4 - (cx + ax), dy = sl[i].y + 3 - (cy + ay);
      if (dx * dx + dy * dy < 13 * 13) return i;
    }
    return -1;
  }

  int act() {
    int s = slimeInReach();
    if (s >= 0) {
      Slime& m = sl[s];
      m.hp -= (int8_t)swordDmg();
      m.vx = (m.x > px ? 1 : -1) * 2.2f;
      m.vy = -2.2f;
      if (m.hp <= 0) {
        m.alive = false;
        add(I_GEL, 1 + (int)(rnd() % 2));
        return E_KILL | E_ATTACK;
      }
      return E_ATTACK;
    }
    int tx, ty;
    target(tx, ty, false);
    uint8_t b = at(tx, ty);
    if (b != AIR && ty >= 0) {
      int hard = hardness(b);
      if (!hard) return E_NOPE;
      if (hitX != tx || hitY != ty) {
        hitX = tx;
        hitY = ty;
        hits = 0;
      }
      if (++hits < hard) return E_HIT;
      hitX = hitY = -1;
      set(tx, ty, AIR);
      uint8_t d = drop(b);
      if (d) add(d, 1);
      // дерево: срубили ствол - всё, что стоит сверху (ствол и листья), падает
      if (b == LOG)
        for (int y = ty - 1; y >= 0 && (at(tx, y) == LOG || at(tx, y) == LEAVES); y--) {
          if (at(tx, y) == LOG) add(LOG, 1);
          set(tx, y, AIR);
        }
      // песок сверху осыпается
      for (int y = ty - 1; y >= 0 && at(tx, y) == SAND && at(tx, y + 1) == AIR; y--) {
        set(tx, y + 1, SAND);
        set(tx, y, AIR);
      }
      return E_MINE;
    }
    // пусто - ставим выбранный блок
    target(tx, ty, true);
    uint8_t it = inv[sel].item;
    if (!placeable(it) || inv[sel].n == 0 || at(tx, ty) != AIR || ty < 0) return E_NOPE;
    // нельзя поставить блок внутрь себя или слизня; факел можно куда угодно
    if (solidBlock(it)) {
      float bx = tx * T, by = ty * T;
      if (bx < px + PW && bx + T > px && by < py + PH && by + T > py) return E_NOPE;
      for (const Slime& m : sl)
        if (m.alive && bx < m.x + 8 && bx + T > m.x && by < m.y + 6 && by + T > m.y) return E_NOPE;
    } else if (it == TORCH && !solid(tx, ty + 1) && !solid(tx - 1, ty) && !solid(tx + 1, ty) && at(tx, ty - 1) == AIR) {
      return E_NOPE;                                 // факелу нужна опора
    }
    set(tx, ty, it);
    if (--inv[sel].n == 0) inv[sel].item = I_NONE;
    return E_PLACE;
  }

  // ---------- время и свет ----------
  bool night() const { return tick % DAY >= (uint32_t)NIGHT_FROM; }
  // Свет неба 1..4: днём 4, в сумерках меньше, ночью 1.
  int skyLight() const {
    uint32_t t = tick % DAY;
    const uint32_t dusk = TICKS * 30;
    if (t < dusk) return 2 + (int)(2 * t / dusk);                                   // рассвет
    if (t < (uint32_t)NIGHT_FROM - dusk) return 4;
    if (t < (uint32_t)NIGHT_FROM) return 2 + (int)(2 * ((uint32_t)NIGHT_FROM - t) / dusk);   // закат
    return 1;
  }

  // ---------- слизни ----------
  void slimeStep(Slime& m, int& ev) {
    if (m.wait > 0) m.wait--;
    bool onG = slimeHits(m.x, m.y + 1);
    if (onG && m.wait == 0) {                        // прыжок в сторону игрока
      float dir = px > m.x ? 1 : -1;
      m.vx = dir * (1.2f + (rnd() % 60) / 100.0f);
      m.vy = -3.2f - (rnd() % 100) / 100.0f;
      m.wait = (int16_t)rndi(40, 90);
    }
    m.vy += 0.3f;
    if (m.vy > 6) m.vy = 6;
    float nx = m.x + m.vx;
    if (!slimeHits(nx, m.y)) m.x = nx;
    else m.vx = -m.vx * 0.3f;
    float ny = m.y + m.vy;
    if (!slimeHits(m.x, ny)) m.y = ny;
    else {
      if (m.vy > 0) m.vx *= 0.5f;
      m.vy = 0;
    }
    if (onG) m.vx *= 0.8f;
    // задел игрока
    if (hurtT == 0 && m.x < px + PW && m.x + 8 > px && m.y < py + PH && m.y + 6 > py) {
      hurt(night() ? 2 : 1, m.x + 4 < px + PW / 2 ? 1 : -1);
      ev |= E_HURT;
    }
    float dx = m.x - px, dy = m.y - py;
    if (dx * dx + dy * dy > (float)(50 * T) * (50 * T)) m.alive = false;     // далеко - исчезает
  }
  bool slimeHits(float x, float y) const {
    int x0 = (int)floorf(x) / T, x1 = ((int)ceilf(x + 8) - 1) / T, y0 = (int)floorf(y) / T, y1 = ((int)ceilf(y + 6) - 1) / T;
    for (int ty = y0; ty <= y1; ty++)
      for (int tx = x0; tx <= x1; tx++)
        if (solid(tx, ty)) return true;
    return false;
  }
  void trySpawn() {
    int alive = 0;
    for (const Slime& m : sl) alive += m.alive;
    if (alive >= MAX_SLIMES - (night() ? 0 : 3)) return;
    int ptx = (int)px / T, pty = (int)py / T;
    bool deep = pty > surf[ptx < 0 ? 0 : ptx >= W ? W - 1 : ptx] + 8;
    int chance = deep ? 260 : night() ? 70 : 900;
    if (rnd() % (uint32_t)chance) return;
    int side = rnd() & 1 ? 1 : -1;
    int tx = ptx + side * rndi(16, 24);
    if (tx < 2 || tx > W - 3) return;
    int ty;
    if (deep) {                                      // в пещере рядом: пустое место над полом
      ty = pty + rndi(-6, 6);
      if (ty < 2 || ty > H - 4 || at(tx, ty) != AIR || !solid(tx, ty + 1) || ty <= surf[tx] + 4) return;
    } else {
      ty = surf[tx] - 1;
      if (at(tx, ty) != AIR) return;
    }
    for (Slime& m : sl)
      if (!m.alive) {
        m = Slime{(float)tx * T, (float)ty * T + 2, 0, 0, (int8_t)(deep ? 6 : night() ? 5 : 3), true, (int16_t)rndi(20, 60)};
        return;
      }
  }

  void hurt(int d, int dir) {
    hp -= d;
    hurtT = 30;
    regenT = 0;
    vx = dir * 2.5f;
    vy = -2.5f;
  }

  // ---------- шаг игры ----------
  int step(const Input& in) {
    int ev = 0;
    tick++;
    if (in.aim >= 0) aim = (uint8_t)in.aim;
    if (in.mx > 0.2f) facing = 1;
    if (in.mx < -0.2f) facing = -1;
    if (in.aim < 0 && (aim == A_LEFT || aim == A_RIGHT)) aim = facing > 0 ? A_RIGHT : A_LEFT;
    // движение
    float target = in.mx * 1.7f;
    if (hurtT > 20) target = vx;                     // отбросило - чуть не управляем
    vx += (target - vx) * (ground ? 0.5f : 0.25f);
    if (in.jump && ground) {
      vy = -3.9f;
      ground = false;
    }
    vy += 0.35f;
    if (vy > 7) vy = 7;
    float nx = px + vx;
    if (!boxHits(nx, py)) px = nx;
    else if (ground && !boxHits(nx, py - T)) {       // ступенька в один блок - заходим сами
      py -= T;
      px = nx;
    } else {
      vx = 0;
    }
    float ny = py + vy;
    bool wasAir = !ground;
    float fallV = vy;
    if (!boxHits(px, ny)) {
      py = ny;
      ground = false;
    } else {
      if (vy > 0) {
        ground = true;
        py = floorf((py + PH + vy) / T) * T - PH;
        while (boxHits(px, py)) py -= 1;
        if (wasAir && fallV > 6.6f) {                // больно упасть с высоты
          hp -= (int)((fallV - 6.6f) * 3) + 1;
          hurtT = 20;
          ev |= E_HURT;
        }
      }
      vy = 0;
    }
    if (ground && boxHits(px, py + 1) == false) ground = false;
    if (px < T) px = T;
    if (px > (W - 1) * T - PW) px = (W - 1) * T - PW;
    if (in.act) ev |= act();
    // слизни
    trySpawn();
    for (Slime& m : sl)
      if (m.alive) slimeStep(m, ev);
    if (hurtT > 0) hurtT--;
    if (hp < MAX_HP && ++regenT > TICKS * 4) {      // понемногу лечимся
      hp++;
      regenT = 0;
    }
    if (hp <= 0) {
      ev |= E_DEAD;
      for (Slime& m : sl) m.alive = false;
      respawn();
    }
    return ev;
  }

  // ---------- сохранение ----------
  static const uint32_t MAGIC = 0x53425831;          // "SBX1"
  struct Save {
    uint32_t magic;
    float px, py;
    int32_t hp, sel, spawnX;
    uint32_t tick, rng;
    uint8_t won;
    Slot inv[SLOTS];
  };
  static size_t saveSize() { return sizeof(Save) + (size_t)W * H + sizeof(int16_t) * W; }
  void saveTo(uint8_t* buf) const {
    Save s;
    memset(&s, 0, sizeof(s));
    s.magic = MAGIC;
    s.px = px;
    s.py = py;
    s.hp = hp;
    s.sel = sel;
    s.spawnX = spawnX;
    s.tick = tick;
    s.rng = rng;
    s.won = won;
    memcpy(s.inv, inv, sizeof(inv));
    memcpy(buf, &s, sizeof(s));
    memcpy(buf + sizeof(s), w, (size_t)W * H);
    memcpy(buf + sizeof(s) + (size_t)W * H, g0, sizeof(g0));
  }
  bool loadFrom(const uint8_t* buf, size_t n) {
    if (n != saveSize()) return false;
    Save s;
    memcpy(&s, buf, sizeof(s));
    if (s.magic != MAGIC) return false;
    memcpy(w, buf + sizeof(s), (size_t)W * H);
    memcpy(g0, buf + sizeof(s) + (size_t)W * H, sizeof(g0));
    px = s.px;
    py = s.py;
    hp = s.hp > 0 && s.hp <= MAX_HP ? s.hp : MAX_HP;
    sel = s.sel >= 0 && s.sel < SLOTS ? s.sel : 0;
    spawnX = s.spawnX > 0 && s.spawnX < W ? s.spawnX : W / 2;
    tick = s.tick;
    rng = s.rng ? s.rng : 1;
    won = s.won;
    memcpy(inv, s.inv, sizeof(inv));
    memset(sl, 0, sizeof(sl));
    vx = vy = 0;
    for (int x = 0; x < W; x++) updSurf(x);
    return true;
  }
};

}  // namespace sbx
