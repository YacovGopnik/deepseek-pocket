// Шахматы: правила (все ходы, рокировка, взятие на проходе, превращение, шах/мат/пат)
// и простой «мозг» платы (перебор с альфа-бета отсечением).
// Чистая логика без экрана - тестируется на компьютере (в том числе подсчётом позиций «perft»).
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <string>

namespace chess {

// Фигуры: белые - положительные, чёрные - отрицательные.
enum : int8_t { EMPTY_SQ = 0, PAWN = 1, KNIGHT = 2, BISHOP = 3, ROOK = 4, QUEEN = 5, KING = 6 };
// Клетка = ряд * 8 + колонка; ряд 0 - первая горизонталь (белые внизу), колонка 0 - вертикаль «a».
typedef uint16_t Move;                     // от (6 бит) | до (6 бит) | превращение (3 бита: 0 или KNIGHT..QUEEN)
const int MAX_MOVES = 128;
const int MAX_PLY = 12;

inline int mFrom(Move m) { return m & 63; }
inline int mTo(Move m) { return (m >> 6) & 63; }
inline int mPromo(Move m) { return (m >> 12) & 7; }
inline Move mk(int from, int to, int promo) { return (Move)(from | (to << 6) | (promo << 12)); }

struct Undo {
  int8_t captured;
  int8_t capSq;
  uint8_t castle;
  int8_t ep;
  uint8_t half;
};

enum Result { PLAYING = 0, MATE_WHITE_WINS, MATE_BLACK_WINS, STALEMATE, DRAW_50, DRAW_MATERIAL };

class Board {
 public:
  int8_t sq[64];
  bool white = true;       // чей ход
  uint8_t castle = 15;     // 1 - белые короткая, 2 - белые длинная, 4 - чёрные короткая, 8 - чёрные длинная
  int8_t ep = -1;          // клетка, на которую можно взять на проходе
  uint8_t half = 0;        // ходов без взятий и ходов пешками (правило 50 ходов)
  uint16_t full = 1;

  void reset() {
    static const int8_t back[8] = {ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK};
    memset(sq, 0, sizeof(sq));
    for (int f = 0; f < 8; f++) {
      sq[f] = back[f];
      sq[8 + f] = PAWN;
      sq[48 + f] = -PAWN;
      sq[56 + f] = (int8_t)-back[f];
    }
    white = true;
    castle = 15;
    ep = -1;
    half = 0;
    full = 1;
  }

  int kingSq(bool w) const {
    int8_t k = w ? KING : -KING;
    for (int i = 0; i < 64; i++)
      if (sq[i] == k) return i;
    return -1;
  }

  // Бьёт ли сторона byWhite клетку s?
  bool attacked(int s, bool byWhite) const {
    int r = s >> 3, f = s & 7;
    int sg = byWhite ? 1 : -1;
    // пешки
    int pr = r - sg;
    if (pr >= 0 && pr < 8) {
      if (f > 0 && sq[pr * 8 + f - 1] == sg * PAWN) return true;
      if (f < 7 && sq[pr * 8 + f + 1] == sg * PAWN) return true;
    }
    static const int8_t KN[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
    for (auto& d : KN) {
      int rr = r + d[0], ff = f + d[1];
      if (rr >= 0 && rr < 8 && ff >= 0 && ff < 8 && sq[rr * 8 + ff] == sg * KNIGHT) return true;
    }
    static const int8_t DIR[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    for (int d = 0; d < 8; d++) {
      int rr = r + DIR[d][0], ff = f + DIR[d][1];
      bool first = true;
      while (rr >= 0 && rr < 8 && ff >= 0 && ff < 8) {
        int8_t p = sq[rr * 8 + ff];
        if (p) {
          if (p * sg > 0) {
            int a = p * sg;
            if (a == QUEEN || (d < 4 && a == ROOK) || (d >= 4 && a == BISHOP) || (first && a == KING)) return true;
          }
          break;
        }
        first = false;
        rr += DIR[d][0];
        ff += DIR[d][1];
      }
    }
    return false;
  }

  bool inCheck(bool w) const {
    int k = kingSq(w);
    return k >= 0 && attacked(k, !w);
  }

  // Все ходы без проверки «король под боем» (её делает legal/поиск). Возвращает количество.
  int gen(Move* list, bool capturesOnly) const {
    int n = 0;
    int sg = white ? 1 : -1;
    for (int s = 0; s < 64; s++) {
      int8_t p = sq[s];
      if (p * sg <= 0) continue;
      int a = p * sg, r = s >> 3, f = s & 7;
      if (a == PAWN) {
        int nr = r + sg;
        if (nr < 0 || nr > 7) continue;
        bool promo = nr == (white ? 7 : 0);
        for (int df = -1; df <= 1; df += 2) {            // взятия
          int nf = f + df;
          if (nf < 0 || nf > 7) continue;
          int t = nr * 8 + nf;
          if (sq[t] * sg < 0 || t == ep) {
            if (promo) {
              for (int pp = QUEEN; pp >= KNIGHT; pp--) list[n++] = mk(s, t, pp);
            } else {
              list[n++] = mk(s, t, 0);
            }
          }
        }
        int t = nr * 8 + f;
        if (!sq[t]) {                                    // вперёд
          if (promo) {
            for (int pp = QUEEN; pp >= KNIGHT; pp--) list[n++] = mk(s, t, pp);   // превращение считаем «шумным» ходом
          } else if (!capturesOnly) {
            list[n++] = mk(s, t, 0);
            if (r == (white ? 1 : 6) && !sq[t + sg * 8]) list[n++] = mk(s, t + sg * 8, 0);
          }
        }
        continue;
      }
      if (a == KNIGHT || a == KING) {
        static const int8_t KN[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
        static const int8_t KG[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
        const int8_t (*D)[2] = a == KNIGHT ? KN : KG;
        for (int d = 0; d < 8; d++) {
          int rr = r + D[d][0], ff = f + D[d][1];
          if (rr < 0 || rr > 7 || ff < 0 || ff > 7) continue;
          int t = rr * 8 + ff;
          if (sq[t] * sg > 0) continue;
          if (capturesOnly && !sq[t]) continue;
          list[n++] = mk(s, t, 0);
        }
        if (a == KING && !capturesOnly) {                // рокировка
          int home = white ? 4 : 60;
          if (s == home && !attacked(home, !white)) {
            uint8_t ks = white ? 1 : 4, qs = white ? 2 : 8;
            if ((castle & ks) && !sq[home + 1] && !sq[home + 2] && sq[home + 3] == sg * ROOK &&
                !attacked(home + 1, !white) && !attacked(home + 2, !white))
              list[n++] = mk(home, home + 2, 0);
            if ((castle & qs) && !sq[home - 1] && !sq[home - 2] && !sq[home - 3] && sq[home - 4] == sg * ROOK &&
                !attacked(home - 1, !white) && !attacked(home - 2, !white))
              list[n++] = mk(home, home - 2, 0);
          }
        }
        continue;
      }
      static const int8_t DIR[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
      int d0 = a == BISHOP ? 4 : 0, d1 = a == ROOK ? 4 : 8;
      for (int d = d0; d < d1; d++) {
        int rr = r + DIR[d][0], ff = f + DIR[d][1];
        while (rr >= 0 && rr < 8 && ff >= 0 && ff < 8) {
          int t = rr * 8 + ff;
          if (sq[t] * sg > 0) break;
          if (sq[t] || !capturesOnly) list[n++] = mk(s, t, 0);
          if (sq[t]) break;
          rr += DIR[d][0];
          ff += DIR[d][1];
        }
        if (n > MAX_MOVES - 12) return n;                // запас: больше 128 ходов в реальных позициях не бывает
      }
    }
    return n;
  }

  void make(Move m, Undo& u) {
    int from = mFrom(m), to = mTo(m);
    int8_t p = sq[from];
    int sg = white ? 1 : -1;
    int a = p * sg;
    u.castle = castle;
    u.ep = ep;
    u.half = half;
    u.captured = sq[to];
    u.capSq = (int8_t)to;
    if (a == PAWN && to == ep && !sq[to]) {              // взятие на проходе
      u.capSq = (int8_t)(to - sg * 8);
      u.captured = sq[u.capSq];
      sq[u.capSq] = 0;
    }
    sq[to] = mPromo(m) ? (int8_t)(sg * mPromo(m)) : p;
    sq[from] = 0;
    if (a == KING && (to - from == 2 || from - to == 2)) {   // рокировка: переставить ладью
      if (to > from) {
        sq[from + 1] = sq[from + 3];
        sq[from + 3] = 0;
      } else {
        sq[from - 1] = sq[from - 4];
        sq[from - 4] = 0;
      }
    }
    ep = -1;
    if (a == PAWN && (to - from == 16 || from - to == 16)) ep = (int8_t)((from + to) / 2);
    if (a == KING) castle &= white ? ~3 : ~12;
    if (from == 0 || to == 0) castle &= ~2;
    if (from == 7 || to == 7) castle &= ~1;
    if (from == 56 || to == 56) castle &= ~8;
    if (from == 63 || to == 63) castle &= ~4;
    half = (a == PAWN || u.captured) ? 0 : (uint8_t)(half < 250 ? half + 1 : half);
    if (!white) full++;
    white = !white;
  }

  void unmake(Move m, const Undo& u) {
    white = !white;
    if (!white) full--;
    int from = mFrom(m), to = mTo(m);
    int sg = white ? 1 : -1;
    int8_t p = sq[to];
    if (mPromo(m)) p = (int8_t)(sg * PAWN);
    sq[from] = p;
    sq[to] = 0;
    if (u.captured) sq[u.capSq] = u.captured;
    if (p * sg == KING && (to - from == 2 || from - to == 2)) {
      if (to > from) {
        sq[from + 3] = sq[from + 1];
        sq[from + 1] = 0;
      } else {
        sq[from - 4] = sq[from - 1];
        sq[from - 1] = 0;
      }
    }
    castle = u.castle;
    ep = u.ep;
    half = u.half;
  }

  // Только разрешённые ходы (свой король не остаётся под боем).
  int legal(Move* list) {
    Move tmp[MAX_MOVES];
    int n = gen(tmp, false), k = 0;
    for (int i = 0; i < n; i++) {
      Undo u;
      bool me = white;
      make(tmp[i], u);
      if (!inCheck(me)) list[k++] = tmp[i];
      unmake(tmp[i], u);
    }
    return k;
  }

  Result result() {
    Move l[MAX_MOVES];
    if (legal(l) == 0) {
      if (inCheck(white)) return white ? MATE_BLACK_WINS : MATE_WHITE_WINS;
      return STALEMATE;
    }
    if (half >= 100) return DRAW_50;
    int minor = 0, other = 0;                            // король против короля (или с одной лёгкой фигурой)
    for (int i = 0; i < 64; i++) {
      int a = sq[i] < 0 ? -sq[i] : sq[i];
      if (a == KNIGHT || a == BISHOP) minor++;
      else if (a && a != KING) other++;
    }
    if (!other && minor <= 1) return DRAW_MATERIAL;
    return PLAYING;
  }

  // ---------- запись позиции (FEN) ----------
  std::string fen() const {
    static const char* L = ".pnbrqk";
    std::string s;
    for (int r = 7; r >= 0; r--) {
      int empty = 0;
      for (int f = 0; f < 8; f++) {
        int8_t p = sq[r * 8 + f];
        if (!p) {
          empty++;
          continue;
        }
        if (empty) s += (char)('0' + empty);
        empty = 0;
        char c = L[p < 0 ? -p : p];
        s += p > 0 ? (char)(c - 32) : c;
      }
      if (empty) s += (char)('0' + empty);
      if (r) s += '/';
    }
    s += white ? " w " : " b ";
    if (!castle) s += '-';
    if (castle & 1) s += 'K';
    if (castle & 2) s += 'Q';
    if (castle & 4) s += 'k';
    if (castle & 8) s += 'q';
    s += ' ';
    if (ep < 0) s += '-';
    else {
      s += (char)('a' + (ep & 7));
      s += (char)('1' + (ep >> 3));
    }
    s += ' ' + std::to_string((int)half) + ' ' + std::to_string((int)full);
    return s;
  }

  bool setFen(const std::string& s) {
    memset(sq, 0, sizeof(sq));
    size_t i = 0;
    int r = 7, f = 0, kings = 0;
    for (; i < s.size() && s[i] != ' '; i++) {
      char c = s[i];
      if (c == '/') {
        r--;
        f = 0;
        continue;
      }
      if (c >= '1' && c <= '8') {
        f += c - '0';
        continue;
      }
      const char* L = "pnbrqk";
      char lc = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
      const char* at = strchr(L, lc);
      if (!at || r < 0 || f > 7) return false;
      int8_t p = (int8_t)(at - L + 1);
      if (p == KING) kings++;
      sq[r * 8 + f++] = lc == c ? (int8_t)-p : p;
    }
    if (kings != 2 || i + 1 >= s.size()) return false;
    white = s[i + 1] == 'w';
    i += 3;
    castle = 0;
    for (; i < s.size() && s[i] != ' '; i++) {
      if (s[i] == 'K') castle |= 1;
      if (s[i] == 'Q') castle |= 2;
      if (s[i] == 'k') castle |= 4;
      if (s[i] == 'q') castle |= 8;
    }
    i++;
    ep = -1;
    if (i + 1 < s.size() && s[i] >= 'a' && s[i] <= 'h') ep = (int8_t)((s[i] - 'a') + (s[i + 1] - '1') * 8);
    while (i < s.size() && s[i] != ' ') i++;
    half = (uint8_t)atoi(s.c_str() + (i < s.size() ? i + 1 : i));
    i++;
    while (i < s.size() && s[i] != ' ') i++;
    int fm = i < s.size() ? atoi(s.c_str() + i + 1) : 1;
    full = (uint16_t)(fm > 0 ? fm : 1);
    return true;
  }
};

// Запись хода: e2e4, e7e8q.
inline std::string moveStr(Move m) {
  std::string s;
  s += (char)('a' + (mFrom(m) & 7));
  s += (char)('1' + (mFrom(m) >> 3));
  s += (char)('a' + (mTo(m) & 7));
  s += (char)('1' + (mTo(m) >> 3));
  if (mPromo(m)) s += ".pnbrq"[mPromo(m)];
  return s;
}

// Найти в тексте первый ход из списка разрешённых (понимает e2e4, E2-E4, e7e8=Q). 0 - не найден.
inline Move findMove(const std::string& text, const Move* list, int n) {
  std::string t;
  for (char c : text) {
    if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    if (c == '-' || c == '=' || c == 'x') continue;
    t += c;
  }
  for (size_t i = 0; i + 4 <= t.size(); i++) {
    if (t[i] < 'a' || t[i] > 'h' || t[i + 1] < '1' || t[i + 1] > '8' || t[i + 2] < 'a' || t[i + 2] > 'h' ||
        t[i + 3] < '1' || t[i + 3] > '8')
      continue;
    int from = (t[i] - 'a') + (t[i + 1] - '1') * 8, to = (t[i + 2] - 'a') + (t[i + 3] - '1') * 8;
    char pc = i + 4 < t.size() ? t[i + 4] : ' ';
    int promo = pc == 'q' ? QUEEN : pc == 'r' ? ROOK : pc == 'b' ? BISHOP : pc == 'n' ? KNIGHT : 0;
    Move best = 0;
    for (int k = 0; k < n; k++) {
      if (mFrom(list[k]) != from || mTo(list[k]) != to) continue;
      if (mPromo(list[k]) == promo) return list[k];
      if (mPromo(list[k]) == QUEEN && !promo) best = list[k];      // превращение без буквы - в ферзя
    }
    if (best) return best;
  }
  return 0;
}

// ============================ «МОЗГ» ПЛАТЫ ============================
class Engine {
 public:
  uint32_t nodes = 0;
  uint32_t budget = 40000;            // предел перебора: дальше оцениваем позицию как есть
  void (*pause)() = nullptr;          // вызывается время от времени (чтобы плата не «зависала»)
  uint32_t (*rnd)() = nullptr;

  static int value(int a) {
    static const int16_t V[7] = {0, 100, 320, 330, 500, 900, 0};
    return V[a];
  }

  // Оценка позиции с точки зрения того, чей ход.
  static int eval(const Board& b) {
    int score = 0, queens = 0, pieces = 0;
    for (int i = 0; i < 64; i++) {
      int a = b.sq[i] < 0 ? -b.sq[i] : b.sq[i];
      if (a == QUEEN) queens++;
      if (a >= KNIGHT && a <= QUEEN) pieces++;
    }
    bool endgame = queens == 0 || pieces <= 4;
    for (int i = 0; i < 64; i++) {
      int8_t p = b.sq[i];
      if (!p) continue;
      int sg = p > 0 ? 1 : -1, a = p * sg;
      int r = i >> 3, f = i & 7;
      int rr = sg > 0 ? r : 7 - r;                       // ряд «от своей стороны»
      int cf = f < 4 ? f : 7 - f, cr = r < 4 ? r : 7 - r;
      int centre = cf < cr ? cf : cr;                    // 0 у края .. 3 в центре
      int v = value(a);
      switch (a) {
        case PAWN: {
          static const int8_t ADV[8] = {0, 0, 2, 6, 12, 22, 40, 0};
          v += ADV[rr] + ((f == 3 || f == 4) && rr >= 3 ? 12 : 0);
          break;
        }
        case KNIGHT: v += centre * 12 - 18; break;
        case BISHOP: v += centre * 6 - (rr == 0 ? 8 : 0); break;
        case ROOK: v += rr == 6 ? 15 : 0; break;
        case QUEEN: v += centre * 3; break;
        case KING: v += endgame ? centre * 14 : (rr == 0 ? 12 : 0) - centre * 18 + ((f <= 2 || f >= 6) && rr == 0 ? 20 : 0); break;
      }
      score += sg * v;
    }
    return b.white ? score : -score;
  }

  // Лучший ход для стороны, чей ход. depth: 1 - легко, 2 - нормально, 3 - сложно.
  // noise - случайная добавка к оценке хода (чтобы лёгкий уровень ошибался, а партии отличались).
  Move best(Board& b, int depth, int noise) {
    nodes = 0;
    Move list[MAX_MOVES];
    int n = b.legal(list);
    if (!n) return 0;
    order(b, list, n);
    Move bestM = list[0];
    int bestV = -32000;
    for (int i = 0; i < n; i++) {
      Undo u;
      b.make(list[i], u);
      int v = -search(b, depth - 1, -32000, 32000, 1);
      b.unmake(list[i], u);
      if (noise) v += (int)((rnd ? rnd() : (uint32_t)(nodes * 2654435761u >> 8)) % (uint32_t)(2 * noise + 1)) - noise;
      if (v > bestV) {
        bestV = v;
        bestM = list[i];
      }
    }
    return bestM;
  }

 private:
  Move buf[MAX_PLY][MAX_MOVES];

  void tick() {
    nodes++;
    if (pause && (nodes & 1023) == 0) pause();
  }

  // Сначала взятия: чем ценнее жертва и дешевле нападающий, тем раньше.
  void order(const Board& b, Move* l, int n) {
    int16_t key[MAX_MOVES];
    for (int i = 0; i < n; i++) {
      int8_t victim = b.sq[mTo(l[i])], att = b.sq[mFrom(l[i])];
      int v = victim < 0 ? -victim : victim, a = att < 0 ? -att : att;
      key[i] = (int16_t)(v ? value(v) * 10 - value(a) : 0);
      if (mPromo(l[i])) key[i] = (int16_t)(key[i] + 8000 + mPromo(l[i]));
    }
    for (int i = 1; i < n; i++) {
      Move m = l[i];
      int16_t k = key[i];
      int j = i;
      while (j > 0 && key[j - 1] < k) {
        l[j] = l[j - 1];
        key[j] = key[j - 1];
        j--;
      }
      l[j] = m;
      key[j] = k;
    }
  }

  int quiesce(Board& b, int alpha, int beta, int ply) {
    tick();
    int stand = eval(b);
    if (stand >= beta || ply >= MAX_PLY - 1 || nodes > budget) return stand;
    if (stand > alpha) alpha = stand;
    Move* l = buf[ply];
    int n = b.gen(l, true);
    order(b, l, n);
    bool me = b.white;
    for (int i = 0; i < n; i++) {
      Undo u;
      b.make(l[i], u);
      if (b.inCheck(me)) {
        b.unmake(l[i], u);
        continue;
      }
      int v = -quiesce(b, -beta, -alpha, ply + 1);
      b.unmake(l[i], u);
      if (v >= beta) return v;
      if (v > alpha) alpha = v;
    }
    return alpha;
  }

  int search(Board& b, int depth, int alpha, int beta, int ply) {
    if (depth <= 0) return quiesce(b, alpha, beta, ply);
    tick();
    if (nodes > budget) return eval(b);
    Move* l = buf[ply];
    int n = b.gen(l, false);
    order(b, l, n);
    bool me = b.white;
    int legalCount = 0;
    for (int i = 0; i < n; i++) {
      Undo u;
      b.make(l[i], u);
      if (b.inCheck(me)) {
        b.unmake(l[i], u);
        continue;
      }
      legalCount++;
      int v = -search(b, depth - 1, -beta, -alpha, ply + 1);
      b.unmake(l[i], u);
      if (v >= beta) return v;
      if (v > alpha) alpha = v;
    }
    if (!legalCount) return b.inCheck(me) ? -30000 + ply : 0;   // мат или пат
    if (b.half >= 100) return 0;
    return alpha;
  }
};

}  // namespace chess
