// «Подземелье 3D»: стрелялка от первого лица в духе игр 90-х (стены рисуются «бросанием лучей»).
// Ручка: вперёд/назад и поворот, кнопка - выстрел. Найти портал - выход на следующий уровень.
// Здесь логика игры, уровни, картинки и расчёт лучей - без экрана, тестируется на компьютере.
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace fps3d {

const int MAP = 16;                 // уровень 16x16 клеток
const int TICKS = 20;               // шагов игры в секунду
const int LEVELS = 3;
const int MAX_THINGS = 28;
const float PI_F = 3.14159265f;
const float MOVE = 0.085f;          // клеток за шаг
const float TURN = 0.085f;          // радиан за шаг
const float RADIUS = 0.22f;

enum Kind : uint8_t { K_GRUNT = 0, K_MAGE = 1, K_MED = 2, K_AMMO = 3, K_EXIT = 4 };
enum Event { EV_SHOT = 1, EV_HIT = 2, EV_KILL = 4, EV_HURT = 8, EV_PICK = 16, EV_LEVEL = 32, EV_DEAD = 64, EV_EMPTY = 128 };

// Уровни: 1-3 - стены, P - старт, E - портал, a - громила, b - колдун, h - аптечка, m - патроны.
static const char* const LEVEL_MAPS[LEVELS][MAP] = {
    {"1111111111111111",
     "1P....1........1",
     "1.....1...a....1",
     "1.....1........1",
     "1..m..1111.11111",
     "1.........a....1",
     "111.1111.......1",
     "1.....21....h..1",
     "1..a..21111.1111",
     "1.....2........1",
     "1.....2..a.....1",
     "122.222........1",
     "1........3333.31",
     "1..h.....3..b..1",
     "1....a...3...E.1",
     "1111111111111111"},
    {"2222222222222222",
     "2P...2....a....2",
     "2....2.........2",
     "2.m..2..2222.222",
     "2....2..2.b....2",
     "22.222..2....h.2",
     "2.......2......2",
     "2..a....222.2222",
     "2..........a...2",
     "2222.2222......2",
     "2.....m.2..33.32",
     "2..b....2..3..a2",
     "2.......2..3.b.2",
     "2..h.a.....3...2",
     "2.......2..3.E.2",
     "2222222222222222"},
    {"3333333333333333",
     "3P..3...b...3.E3",
     "3...3.......3..3",
     "3.m.3..33...3b.3",
     "3......33.a.3..3",
     "3...3.......3..3",
     "33.3333.333331.3",
     "3..a...h...b.1.3",
     "3.......11...1.3",
     "3.11....11.a...3",
     "3.11.b.........3",
     "3......1111.1113",
     "3.a....1.m...a.3",
     "3......1..b....3",
     "3..h...1.....h.3",
     "3333333333333333"},
};

// Картинки 16x16: точка - прозрачно, буква - цвет (см. artColor).
static const char* const ART_GRUNT[16] = {
    "................",
    "....GG....GG....",
    "....GgG..GgG....",
    ".....GggggG.....",
    "....GggggggG....",
    "...GgwkggwkgG...",
    "...GgwkggwkgG...",
    "...GggggggggG...",
    "...GgrrrrrrgG...",
    "..GGgrwrwrrgGG..",
    ".GgGggrrrrggGgG.",
    ".GgGggggggggGgG.",
    ".Gg.GggggggG.gG.",
    ".....GgGGgG.....",
    "....GggG.GggG...",
    "...GGGG..GGGG..."};
static const char* const ART_MAGE[16] = {
    "................",
    "......PPPP......",
    ".....PppppP.....",
    "....PppppppP....",
    "....PpyyyypP....",
    "...PppykkyppP...",
    "...PppyyyyppP...",
    "...PppppppppP...",
    "..PPppppppppPP..",
    ".PpPppppppppPpP.",
    ".Pp.PppppppP.pP.",
    ".y..PppppppP..y.",
    "....PppppppP....",
    ".....PppppP.....",
    ".....Pp..pP.....",
    "......P..P......"};
static const char* const ART_MED[16] = {
    "................",
    "................",
    "................",
    "................",
    "..kkkkkkkkkkkk..",
    "..kwwwwwwwwwwk..",
    "..kwwwwrrwwwwk..",
    "..kwwwwrrwwwwk..",
    "..kwwrrrrrrwwk..",
    "..kwwrrrrrrwwk..",
    "..kwwwwrrwwwwk..",
    "..kwwwwrrwwwwk..",
    "..kwwwwwwwwwwk..",
    "..kkkkkkkkkkkk..",
    "................",
    "................"};
static const char* const ART_AMMO[16] = {
    "................",
    "................",
    "................",
    "................",
    "....y..y..y.....",
    "...yyyyyyyyy....",
    "...ysysysysy....",
    "..kkkkkkkkkkkk..",
    "..kYYYYYYYYYYk..",
    "..kYkkYkkYkkYk..",
    "..kYYYYYYYYYYk..",
    "..kYYYYYYYYYYk..",
    "..kYYYYYYYYYYk..",
    "..kkkkkkkkkkkk..",
    "................",
    "................"};
static const char* const ART_EXIT[16] = {
    ".....bbbbbb.....",
    "...bbccccccbb...",
    "..bcccwwwwcccb..",
    ".bccww....wwccb.",
    ".bcw........wcb.",
    "bccw........wccb",
    "bcw..........wcb",
    "bcw..........wcb",
    "bcw..........wcb",
    "bcw..........wcb",
    "bccw........wccb",
    ".bcw........wcb.",
    ".bccww....wwccb.",
    "..bcccwwwwcccb..",
    "...bbccccccbb...",
    ".....bbbbbb....."};
static const char* const ART_SPLAT[16] = {
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "......r...r.....",
    "...r.rrr.rr..r..",
    "..rrrrRrrrRrrr..",
    ".rrRRrrrRRrrrRr.",
    "..rrrrrrrrrrrr.."};
static const char* const ART_GUN[16] = {
    "................",
    "................",
    "................",
    ".......kk.......",
    "......kssk......",
    "......kssk......",
    "......kSSk......",
    ".....kkSSkk.....",
    ".....ksSSsk.....",
    ".....ksSSsk.....",
    "....hhsSSshh....",
    "...hhhkSSkhhh...",
    "...hhhhkkhhhh...",
    "....hhhhhhhh....",
    ".....hhhhhh.....",
    ".....hhhhhh....."};

// Цвет точки картинки (RGB565); 0 - прозрачно (чёрный задаётся как 0x0841).
inline uint16_t artColor(char ch) {
  switch (ch) {
    case 'g': return 0x4E86;   // зелёный
    case 'G': return 0x1B02;   // тёмно-зелёный
    case 'p': return 0x91D6;   // фиолетовый
    case 'P': return 0x488C;   // тёмно-фиолетовый
    case 'w': return 0xFFFF;
    case 'k': return 0x0841;
    case 'r': return 0xE0C3;   // красный
    case 'R': return 0x8000;   // тёмно-красный
    case 'y': return 0xFFE0;   // жёлтый
    case 'Y': return 0xB4A0;   // охра
    case 's': return 0xAD75;   // светлый металл
    case 'S': return 0x630C;   // тёмный металл
    case 'h': return 0xE5B0;   // рука
    case 'b': return 0x11D9;   // синий
    case 'c': return 0x2E9F;   // голубой
    default: return 0;
  }
}

struct Thing {
  float x, y;
  uint8_t kind;
  int8_t hp;
  bool alive;        // враг жив / предмет ещё лежит
  bool awake;        // враг заметил игрока
  uint8_t hurt;      // вспышка после попадания, шагов
  uint8_t cool;      // пауза до следующей атаки, шагов
  uint8_t deadT;     // сколько ещё показывать лужу
  uint8_t attack;    // враг только что атаковал (для картинки), шагов
};

inline float wrapAngle(float a) {
  while (a > PI_F) a -= 2 * PI_F;
  while (a < -PI_F) a += 2 * PI_F;
  return a;
}

class Game {
 public:
  uint8_t map[MAP * MAP];          // 0 - пол, 1..3 - стена
  Thing things[MAX_THINGS];
  int nThings = 0;
  float px = 1.5f, py = 1.5f, pa = 0;
  int hp = 100, ammo = 30, kills = 0;
  int level = 0;                   // сколько уровней пройдено
  uint32_t score = 0, best = 0;
  bool over = false;
  int hurtFlash = 0, shotFlash = 0, fireCool = 0;
  uint32_t rng = 12345;

  uint32_t rnd() {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
  }

  void reset(uint32_t seed) {
    rng = seed ? seed : 1;
    hp = 100;
    ammo = 30;
    kills = 0;
    level = 0;
    score = 0;
    over = false;
    hurtFlash = shotFlash = fireCool = 0;
    loadLevel(0);
  }

  int depth() const { return level / LEVELS; }       // сколько раз пройдены все уровни: враги крепче

  void loadLevel(int n) {
    const char* const* rows = LEVEL_MAPS[n % LEVELS];
    nThings = 0;
    for (int y = 0; y < MAP; y++)
      for (int x = 0; x < MAP; x++) {
        char c = rows[y][x];
        map[y * MAP + x] = (c >= '1' && c <= '3') ? (uint8_t)(c - '0') : 0;
        int kind = c == 'a' ? K_GRUNT : c == 'b' ? K_MAGE : c == 'h' ? K_MED : c == 'm' ? K_AMMO : c == 'E' ? K_EXIT : -1;
        if (c == 'P') {
          px = x + 0.5f;
          py = y + 0.5f;
          pa = 0;
        }
        if (kind >= 0 && nThings < MAX_THINGS) {
          Thing& t = things[nThings++];
          t.x = x + 0.5f;
          t.y = y + 0.5f;
          t.kind = (uint8_t)kind;
          t.hp = (int8_t)((kind == K_GRUNT ? 2 : kind == K_MAGE ? 3 : 1) + (kind <= K_MAGE ? depth() : 0));
          t.alive = true;
          t.awake = false;
          t.hurt = t.cool = t.deadT = t.attack = 0;
        }
      }
    // развернуть игрока в сторону свободного коридора
    static const int DX[4] = {1, 0, -1, 0}, DY[4] = {0, 1, 0, -1};
    int bestLen = -1;
    for (int d = 0; d < 4; d++) {
      int len = 0, x = (int)px, y = (int)py;
      while (!wall(x + DX[d], y + DY[d])) {
        x += DX[d];
        y += DY[d];
        len++;
      }
      if (len > bestLen) {
        bestLen = len;
        pa = d * PI_F / 2;
      }
    }
  }

  bool wall(int x, int y) const {
    if (x < 0 || y < 0 || x >= MAP || y >= MAP) return true;
    return map[y * MAP + x] != 0;
  }

  bool canStand(float x, float y, float r) const {
    return !wall((int)(x - r), (int)(y - r)) && !wall((int)(x + r), (int)(y - r)) && !wall((int)(x - r), (int)(y + r)) &&
           !wall((int)(x + r), (int)(y + r));
  }

  // Видно ли из одной точки другую (нет стены между ними).
  bool los(float x0, float y0, float x1, float y1) const {
    float dx = x1 - x0, dy = y1 - y0;
    float d = sqrtf(dx * dx + dy * dy);
    int n = (int)(d / 0.12f) + 1;
    for (int i = 1; i < n; i++) {
      float t = (float)i / n;
      if (wall((int)(x0 + dx * t), (int)(y0 + dy * t))) return false;
    }
    return true;
  }

  int enemiesLeft() const {
    int n = 0;
    for (int i = 0; i < nThings; i++)
      if (things[i].kind <= K_MAGE && things[i].alive) n++;
    return n;
  }

  // Один шаг. fwd, turn: -1..1 (ручка), fire - нажата кнопка. Возвращает набор событий Event.
  int step(float fwd, float turn, bool fire) {
    if (over) return 0;
    int ev = 0;
    if (hurtFlash) hurtFlash--;
    if (shotFlash) shotFlash--;
    if (fireCool) fireCool--;

    pa = wrapAngle(pa + turn * TURN);
    float c = cosf(pa), s = sinf(pa);
    float nx = px + c * fwd * MOVE, ny = py + s * fwd * MOVE;
    if (canStand(nx, py, RADIUS)) px = nx;            // вдоль стены можно скользить
    if (canStand(px, ny, RADIUS)) py = ny;

    if (fire && fireCool == 0) {
      fireCool = 6;
      bool gun = ammo > 0;
      if (gun) {
        ammo--;
        shotFlash = 2;
        ev |= EV_SHOT;
      } else {
        ev |= EV_EMPTY;                               // патронов нет - бьём ножом вплотную
      }
      int target = -1;
      float bestD = gun ? 14.0f : 1.3f;
      for (int i = 0; i < nThings; i++) {
        Thing& t = things[i];
        if (t.kind > K_MAGE || !t.alive) continue;
        float dx = t.x - px, dy = t.y - py;
        float d = sqrtf(dx * dx + dy * dy);
        if (d >= bestD) continue;
        float off = wrapAngle(atan2f(dy, dx) - pa);
        float lim = gun ? atan2f(0.34f, d) : 0.7f;
        if (off > lim || off < -lim) continue;
        if (!los(px, py, t.x, t.y)) continue;
        bestD = d;
        target = i;
      }
      if (target >= 0) {
        Thing& t = things[target];
        t.hp--;
        t.hurt = 3;
        t.awake = true;
        ev |= EV_HIT;
        if (t.hp <= 0) {
          t.alive = false;
          t.deadT = 60;
          kills++;
          score += t.kind == K_MAGE ? 15 : 10;
          ev |= EV_KILL;
        }
      }
    }

    for (int i = 0; i < nThings; i++) {
      Thing& t = things[i];
      float dx = px - t.x, dy = py - t.y;
      float d = sqrtf(dx * dx + dy * dy);
      if (t.kind > K_MAGE) {                           // предметы и портал
        if (!t.alive) continue;
        if (t.kind == K_MED && d < 0.55f && hp < 100) {
          hp = hp + 25 > 100 ? 100 : hp + 25;
          t.alive = false;
          ev |= EV_PICK;
        } else if (t.kind == K_AMMO && d < 0.55f) {
          ammo = ammo + 12 > 99 ? 99 : ammo + 12;
          t.alive = false;
          ev |= EV_PICK;
        } else if (t.kind == K_EXIT && d < 0.6f) {
          level++;
          score += 100;
          hp = hp + 20 > 100 ? 100 : hp + 20;          // передышка между уровнями
          ammo = ammo + 8 > 99 ? 99 : ammo + 8;
          if (score > best) best = score;
          loadLevel(level);
          return ev | EV_LEVEL;
        }
        continue;
      }
      if (!t.alive) {
        if (t.deadT) t.deadT--;
        continue;
      }
      if (t.attack) t.attack--;
      if (t.cool) t.cool--;
      if (t.hurt) {                                    // после попадания враг на миг замирает
        t.hurt--;
        continue;
      }
      bool sees = d < 9.0f && los(t.x, t.y, px, py);
      if (sees) t.awake = true;
      if (!t.awake) continue;
      float keep = t.kind == K_GRUNT ? 0.7f : 3.5f;    // колдун держится на расстоянии
      float sp = (t.kind == K_GRUNT ? 0.045f : 0.03f) + 0.004f * depth();
      if (d > keep && d > 0.01f) {
        float mx = t.x + dx / d * sp, my = t.y + dy / d * sp;
        if (canStand(mx, t.y, 0.25f)) t.x = mx;
        if (canStand(t.x, my, 0.25f)) t.y = my;
      }
      if (t.cool) continue;
      if (t.kind == K_GRUNT && d < 0.95f) {
        t.cool = 16;
        t.attack = 5;
        hp -= 8 + 2 * depth();
        hurtFlash = 3;
        ev |= EV_HURT;
      } else if (t.kind == K_MAGE && sees && d < 8.0f) {
        t.cool = 30;
        t.attack = 5;
        int chance = 75 - (int)(d * 7);                // издалека колдун чаще мажет
        if ((int)(rnd() % 100) < chance) {
          hp -= 6 + depth();
          hurtFlash = 3;
          ev |= EV_HURT;
        }
      }
    }
    if (hp <= 0) {
      hp = 0;
      over = true;
      if (score > best) best = score;
      ev |= EV_DEAD;
    }
    return ev;
  }
};

// ---------- бросание лучей (целые числа: у процессора нет быстрых дробных) ----------
struct Hit {
  int32_t dist;      // расстояние до стены поперёк взгляда, 16.16
  uint8_t type;      // вид стены 1..3
  uint8_t side;      // 0 - грань вдоль оси Y, 1 - вдоль X (рисуется темнее)
  uint8_t u;         // столбец рисунка стены 0..15
};

// px,py - где стоим; rdx,rdy - направление луча; всё в формате 16.16.
inline void castRay(const uint8_t* map, int32_t px, int32_t py, int32_t rdx, int32_t rdy, Hit& h) {
  const int32_t BIG = 0x7FFFFFFF;
  int mx = px >> 16, my = py >> 16;
  int32_t ax = rdx < 0 ? -rdx : rdx, ay = rdy < 0 ? -rdy : rdy;
  int32_t ddx = ax < 8 ? BIG : (int32_t)(((int64_t)1 << 32) / ax);
  int32_t ddy = ay < 8 ? BIG : (int32_t)(((int64_t)1 << 32) / ay);
  int32_t fx = px & 0xFFFF, fy = py & 0xFFFF;
  int32_t sdx = ddx == BIG ? BIG : (int32_t)(((int64_t)(rdx < 0 ? fx : 0x10000 - fx) * ddx) >> 16);
  int32_t sdy = ddy == BIG ? BIG : (int32_t)(((int64_t)(rdy < 0 ? fy : 0x10000 - fy) * ddy) >> 16);
  int stx = rdx < 0 ? -1 : 1, sty = rdy < 0 ? -1 : 1;
  int side = 0;
  uint8_t type = 1;
  for (int i = 0; i < 64; i++) {
    if (sdx < sdy) {
      sdx += ddx;
      mx += stx;
      side = 0;
    } else {
      sdy += ddy;
      my += sty;
      side = 1;
    }
    if (mx < 0 || my < 0 || mx >= MAP || my >= MAP) break;
    if (map[my * MAP + mx]) {
      type = map[my * MAP + mx];
      break;
    }
  }
  int32_t d = side == 0 ? sdx - ddx : sdy - ddy;
  if (d < 2000) d = 2000;
  int32_t w = side == 0 ? py + (int32_t)(((int64_t)d * rdy) >> 16) : px + (int32_t)(((int64_t)d * rdx) >> 16);
  h.dist = d;
  h.type = type;
  h.side = (uint8_t)side;
  h.u = (uint8_t)((w >> 12) & 15);
}

// Рисунок стены: точка (u,v) 0..15 для вида 1..3.
inline uint16_t wallTexel(int type, int u, int v) {
  if (type == 1) {                                       // кирпич
    int row = v / 4;
    int uu = (u + (row & 1) * 4) & 7;
    if (v % 4 == 3 || uu == 7) return 0x4A49;            // швы
    return ((u * 7 + row * 13) & 7) < 2 ? 0x9A45 : 0xB2E6;
  }
  if (type == 2) {                                       // каменные блоки
    if (v == 0 || v == 8 || (v < 8 ? u == 0 : u == 8)) return 0x31A6;
    return ((u * 5 + v * 3) % 11) < 3 ? 0x6B6D : 0x7BEF;
  }
  if (u == 0 || u == 15 || v == 0 || v == 15) return 0x1A27;   // железные панели
  if ((u == 3 || u == 12) && (v == 3 || v == 12)) return 0xC618;  // заклёпки
  if (v >= 6 && v <= 9 && u >= 5 && u <= 10) return (u + v) & 1 ? 0xFD20 : 0x8200;   // жёлто-чёрная вставка
  return 0x3B8E;
}

}  // namespace fps3d
