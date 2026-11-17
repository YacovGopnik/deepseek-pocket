// Решение уравнений с одной неизвестной x для калькулятора.
// Многочлены и одночлены (до 8-й степени): точные рациональные корни, кратность, комплексные корни.
// Всё остальное (sin x = 0,5, 1/x = 2, ...): численный поиск корней на отрезке.
// Чистая логика, без Arduino - тестируется на компьютере.
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <algorithm>
#include "calc.h"

namespace calc {

// ---------- разбор записи ----------

// Есть ли в записи переменная x (слово exp не считается).
inline bool hasVar(const std::string& s) {
  for (size_t i = 0; i < s.size(); i++) {
    if (s.compare(i, 3, "exp") == 0) {
      i += 2;
      continue;
    }
    if (s[i] == 'x') return true;
  }
  return false;
}

inline bool hasEq(const std::string& s) { return s.find('=') != std::string::npos; }

inline bool hasTrig(const std::string& s) {
  return s.find("sin") != std::string::npos || s.find("cos") != std::string::npos ||
         s.find("tan") != std::string::npos || s.find("tg") != std::string::npos;
}

// ---------- многочлен ----------

const int MAX_DEG = 8;

struct Poly {
  double c[MAX_DEG + 1];   // c[i] - коэффициент при x^i
  int deg = 0;
  Poly() {
    for (double& v : c) v = 0;
  }
};

// Подгоняет многочлен под f(x) по значениям в 9 точках и проверяет в ещё четырёх.
// Результат: 0 - многочлен, 1 - не многочлен, 2 - ошибка записи (err).
inline int fitPoly(Parser& P, const std::string& s, Poly& out, Err& err) {
  const int N = MAX_DEG + 1;
  double xs[N], d[N];
  for (int k = 0; k < N; k++) {
    xs[k] = k - 4;
    Err e;
    d[k] = P.evalAt(s, xs[k], e);
    if (e == E_SYNTAX) {
      err = e;
      return 2;
    }
    if (e != E_NONE) return 1;
  }
  double scale = 0;
  for (int k = 0; k < N; k++) scale = std::max(scale, fabs(d[k]));
  double nd[N];                                    // разделённые разности
  for (int k = 0; k < N; k++) nd[k] = d[k];
  for (int lvl = 1; lvl < N; lvl++)
    for (int k = N - 1; k >= lvl; k--) nd[k] = (nd[k] - nd[k - 1]) / (xs[k] - xs[k - lvl]);
  double c[N] = {0};                               // схема Горнера для формы Ньютона
  int len = 1;
  c[0] = nd[N - 1];
  for (int k = N - 2; k >= 0; k--) {
    double nc[N + 1] = {0};                        // c * (x - xs[k]) + nd[k]
    for (int i = 0; i < len; i++) {
      nc[i + 1] += c[i];
      nc[i] -= c[i] * xs[k];
    }
    nc[0] += nd[k];
    len++;
    for (int i = 0; i < len && i < N; i++) c[i] = nc[i];
  }
  double mx = 0;
  for (int i = 0; i < N; i++) mx = std::max(mx, fabs(c[i]));
  for (int i = 0; i < N; i++) {                    // убрать шум и «приклеить» к простым дробям
    if (fabs(c[i]) < 1e-9 * std::max(mx, 1e-300)) {
      c[i] = 0;
      continue;
    }
    for (int m = 1; m <= 100; m++) {
      double t = c[i] * m;
      if (fabs(t - round(t)) < 1e-8 * std::max(1.0, fabs(t))) {
        c[i] = round(t) / m;
        break;
      }
    }
  }
  static const double test[5] = {0.5, -2.5, 5.3, 17.7, -37.1};   // и далеко от узлов: sin(x°) вблизи нуля почти многочлен   // это действительно многочлен?
  for (double tx : test) {
    Err e;
    double v = P.evalAt(s, tx, e);
    if (e != E_NONE) return 1;
    double pv = 0;
    for (int i = N - 1; i >= 0; i--) pv = pv * tx + c[i];
    if (fabs(pv - v) > 1e-7 * std::max(scale, fabs(v))) return 1;
  }
  out = Poly();
  for (int i = 0; i < N; i++) {
    out.c[i] = c[i];
    if (c[i] != 0) out.deg = i;
  }
  return 0;
}

// ---------- корни ----------

struct Root {
  double re = 0, im = 0;   // im > 0: пара re ± i·im
  int mult = 1;
};

struct Solution {
  int kind = 0;            // 0 - корни, 1 - любое x, 2 - корней нет, 3 - ошибка записи
  bool poly = true;        // false - численный поиск
  int deg = 0;
  std::vector<Root> roots; // сначала действительные по возрастанию, потом комплексные
  Err err = E_NONE;
  double lo = 0, hi = 0;   // отрезок поиска (для численного)
  bool more = false;       // корней больше, чем показано
};

struct Cx {
  double r, i;
};
inline Cx cadd(Cx a, Cx b) { return {a.r + b.r, a.i + b.i}; }
inline Cx csub(Cx a, Cx b) { return {a.r - b.r, a.i - b.i}; }
inline Cx cmul(Cx a, Cx b) { return {a.r * b.r - a.i * b.i, a.r * b.i + a.i * b.r}; }
inline Cx cdiv(Cx a, Cx b) {
  double d = b.r * b.r + b.i * b.i;
  return {(a.r * b.r + a.i * b.i) / d, (a.i * b.r - a.r * b.i) / d};
}
inline double cabs2(Cx a) { return a.r * a.r + a.i * a.i; }

inline double polyAt(const std::vector<double>& a, double x) {
  double v = 0;
  for (size_t i = a.size(); i-- > 0;) v = v * x + a[i];
  return v;
}

inline Cx polyAtC(const std::vector<double>& a, Cx x) {
  Cx v = {0, 0};
  for (size_t i = a.size(); i-- > 0;) {
    v = cmul(v, x);
    v.r += a[i];
  }
  return v;
}

// Делит на (x - r), остаток отбрасывается.
inline void deflate(std::vector<double>& a, double r) {
  size_t n = a.size() - 1;
  std::vector<double> b(n);
  double carry = a[n];
  for (size_t k = n; k-- > 0;) {
    b[k] = carry;
    carry = a[k] + r * carry;
  }
  a = b;
}

inline void divisors(long n, std::vector<long>& out) {
  out.clear();
  for (long i = 1; i * i <= n; i++)
    if (n % i == 0) {
      out.push_back(i);
      if (i != n / i) out.push_back(n / i);
    }
}

inline long gcdl(long a, long b) {
  while (b) {
    long t = a % b;
    a = b;
    b = t;
  }
  return a;
}

// Метод Дюрана - Кернера: все комплексные корни многочлена a (старший коэффициент не 0).
inline void durandKerner(const std::vector<double>& a, std::vector<Cx>& z) {
  size_t n = a.size() - 1;
  std::vector<double> m(a.size());
  for (size_t i = 0; i <= n; i++) m[i] = a[i] / a[n];
  double R = 0;
  for (size_t i = 0; i < n; i++) R = std::max(R, pow(fabs(m[i]), 1.0 / (double)(n - i)));
  R = std::max(R, 1e-3);
  z.assign(n, Cx{0, 0});
  Cx w = {1, 0};
  for (size_t k = 0; k < n; k++) {
    z[k] = {w.r * R, w.i * R};
    w = cmul(w, Cx{0.4, 0.9});
    double s = sqrt(cabs2(w));
    w = {w.r / s, w.i / s};
  }
  for (int it = 0; it < 500; it++) {
    double maxStep = 0;
    for (size_t k = 0; k < n; k++) {
      Cx den = {1, 0};
      for (size_t j = 0; j < n; j++)
        if (j != k) den = cmul(den, csub(z[k], z[j]));
      if (cabs2(den) < 1e-300) den = {1e-150, 1e-150};
      Cx step = cdiv(polyAtC(m, z[k]), den);
      z[k] = csub(z[k], step);
      maxStep = std::max(maxStep, sqrt(cabs2(step)) / (1 + sqrt(cabs2(z[k]))));
    }
    if (maxStep < 1e-15) break;
  }
  std::vector<double> dm(n);                       // уточнение методом Ньютона
  for (size_t i = 1; i <= n; i++) dm[i - 1] = m[i] * (double)i;
  for (size_t k = 0; k < n; k++)
    for (int it = 0; it < 4; it++) {
      Cx d = polyAtC(dm, z[k]);
      if (cabs2(d) < 1e-24) break;
      Cx st = cdiv(polyAtC(m, z[k]), d);
      if (cabs2(st) > 1e-6 * (1 + cabs2(z[k]))) break;   // кратный корень: не трогаем
      z[k] = csub(z[k], st);
    }
}

inline void addRoot(Solution& s, double re, double im, int mult) {
  Root r;
  r.re = re;
  r.im = im;
  r.mult = mult;
  s.roots.push_back(r);
}

// Корни многочлена p.
inline void solvePoly(const Poly& p, Solution& sol) {
  sol.poly = true;
  sol.deg = p.deg;
  sol.roots.clear();
  if (p.deg == 0) {
    sol.kind = p.c[0] == 0 ? 1 : 2;
    return;
  }
  sol.kind = 0;
  std::vector<double> a(p.c, p.c + p.deg + 1);
  int zeros = 0;                                   // корень 0
  while (a.size() > 1 && a[0] == 0) {
    a.erase(a.begin());
    zeros++;
  }
  if (zeros) addRoot(sol, 0, 0, zeros);

  // рациональные корни p/q (теорема о рациональных корнях)
  if (a.size() > 2) {
    int mulK = 0;
    for (int m = 1; m <= 1000 && !mulK; m++) {
      bool ok = true;
      for (double v : a) {
        double t = v * m;
        if (fabs(t - round(t)) > 1e-7 * std::max(1.0, fabs(t)) || fabs(t) > 1e6) {
          ok = false;
          break;
        }
      }
      if (ok) mulK = m;
    }
    if (mulK) {
      long a0 = (long)llround(fabs(a[0] * mulK)), an = (long)llround(fabs(a.back() * mulK));
      std::vector<long> ps, qs;
      divisors(a0, ps);
      divisors(an, qs);
      std::sort(ps.begin(), ps.end());
      std::sort(qs.begin(), qs.end());
      int budget = 40000;
      std::vector<Root> found;
      for (long q : qs)
        for (long pp : ps) {
          if (a.size() <= 3) break;                // осталась квадратичная часть - решим формулой
          if (gcdl(pp, q) != 1) continue;
          for (int sg = -1; sg <= 1; sg += 2) {
            if (budget-- <= 0 || a.size() <= 3) break;
            double r = sg * (double)pp / (double)q;
            int mult = 0;
            for (;;) {
              if (a.size() <= 3) break;
              double scale = 0, pw = 1;
              for (double c : a) {
                scale += fabs(c) * pw;
                pw *= fabs(r);
              }
              if (fabs(polyAt(a, r)) > 1e-9 * scale) break;
              deflate(a, r);
              mult++;
            }
            if (mult) addRoot(sol, r, 0, mult);
          }
        }
    }
  }

  size_t n = a.size() - 1;
  if (n == 1) {
    addRoot(sol, -a[0] / a[1], 0, 1);
  } else if (n == 2) {
    double A = a[2], B = a[1], C = a[0];
    double D = B * B - 4 * A * C;
    if (fabs(D) < 1e-12 * std::max(B * B, fabs(4 * A * C))) {
      addRoot(sol, -B / (2 * A), 0, 2);
    } else if (D > 0) {
      double q = -0.5 * (B + (B >= 0 ? 1 : -1) * sqrt(D));   // без потери точности
      double r1 = q / A, r2 = C / q;
      if (r1 > r2) std::swap(r1, r2);
      addRoot(sol, r1, 0, 1);
      addRoot(sol, r2, 0, 1);
    } else {
      addRoot(sol, -B / (2 * A), sqrt(-D) / (2 * fabs(A)), 1);
    }
  } else if (n >= 3) {
    std::vector<Cx> z;
    durandKerner(a, z);
    std::vector<bool> used(z.size(), false);
    for (size_t i = 0; i < z.size(); i++) {
      if (used[i]) continue;
      Cx sum = z[i];
      int cnt = 1;
      used[i] = true;
      for (size_t j = i + 1; j < z.size(); j++) {
        if (used[j]) continue;
        double dr = z[i].r - z[j].r, di = z[i].i - z[j].i;
        if (sqrt(dr * dr + di * di) < 2e-5 * (1 + sqrt(cabs2(z[i])))) {
          sum.r += z[j].r;
          sum.i += z[j].i;
          cnt++;
          used[j] = true;
        }
      }
      double re = sum.r / cnt, im = sum.i / cnt;
      if (fabs(im) < 1e-7 * (1 + fabs(re))) addRoot(sol, re, 0, cnt);
      else if (im > 0) addRoot(sol, re, im, cnt);   // сопряжённый корень (im < 0) пропускаем
    }
  }
  std::stable_sort(sol.roots.begin(), sol.roots.end(), [](const Root& x, const Root& y) {
    if ((x.im != 0) != (y.im != 0)) return x.im == 0;
    if (x.im != 0) return x.re < y.re;
    return x.re < y.re;
  });
}

// ---------- численный поиск ----------

inline void solveNumeric(Parser& P, const std::string& s, double lo, double hi, double step, Solution& sol) {
  sol.poly = false;
  sol.kind = 0;
  sol.roots.clear();
  sol.lo = lo;
  sol.hi = hi;
  auto f = [&](double x, bool& ok) {
    Err e;
    double v = P.evalAt(s, x, e);
    ok = e == E_NONE;
    return v;
  };
  bool ok0;
  double xa = lo, fa = f(xa, ok0);
  for (double x = lo + step; x <= hi + step / 2; x += step) {
    if (sol.roots.size() >= 12) {
      sol.more = true;
      break;
    }
    bool okb;
    double fb = f(x, okb);
    if (ok0 && okb && ((fa < 0 && fb > 0) || (fa > 0 && fb < 0))) {
      double a = xa, b = x, fA = fa;
      for (int it = 0; it < 70; it++) {
        double m = 0.5 * (a + b);
        bool okm;
        double fm = f(m, okm);
        if (!okm) break;
        if ((fA < 0) == (fm < 0)) {
          a = m;
          fA = fm;
        } else {
          b = m;
        }
      }
      double r = 0.5 * (a + b);
      bool okr;
      double fr = f(r, okr);
      if (okr && fabs(fr) <= 1e-6 * (1 + std::min(fabs(fa), fabs(fb)))) {   // не разрыв (1/x)
        if (fabs(r) < 1e-9) r = 0;
        addRoot(sol, r, 0, 1);
      }
    } else if (okb && fb == 0) {
      addRoot(sol, x, 0, 1);
    }
    xa = x;
    fa = fb;
    ok0 = okb;
  }
  if (sol.roots.empty()) sol.kind = 2;
}

// Главная функция: s - запись «выражение» или «левая = правая».
inline void solve(Parser& P, const std::string& s, Solution& sol) {
  Poly p;
  Err err = E_NONE;
  int r = fitPoly(P, s, p, err);
  if (r == 2) {
    sol.kind = 3;
    sol.err = err;
    return;
  }
  if (r == 0) {
    solvePoly(p, sol);
    return;
  }
  bool deg = P.degrees && hasTrig(s);
  if (deg) solveNumeric(P, s, -720, 720, 0.5, sol);
  else if (hasTrig(s)) solveNumeric(P, s, -20, 20, 0.02, sol);
  else solveNumeric(P, s, -100, 100, 0.1, sol);
}

// ---------- вывод ----------

inline std::string numStr(double v) {
  if (fabs(v) < 1e-12) v = 0;
  std::string t = format(v);
  return t;
}

inline std::string subscript(int n) {
  static const char* sub[] = {"\xE2\x82\x80", "\xE2\x82\x81", "\xE2\x82\x82", "\xE2\x82\x83", "\xE2\x82\x84",
                              "\xE2\x82\x85", "\xE2\x82\x86", "\xE2\x82\x87", "\xE2\x82\x88", "\xE2\x82\x89"};
  std::string r;
  for (char d : std::to_string(n)) r += sub[d - '0'];
  return r;
}

inline std::string describe(const Solution& s, bool ru) {
  std::string o;
  if (s.kind == 3) return errName(s.err, ru);
  if (s.kind == 1) return ru ? "Подходит любое x." : "Any x works.";
  if (s.kind == 2) {
    if (s.poly) return ru ? "Корней нет." : "No solutions.";
    return ru ? "Корней на отрезке [" + numStr(s.lo) + "; " + numStr(s.hi) + "] не найдено."
              : "No roots found on [" + numStr(s.lo) + "; " + numStr(s.hi) + "].";
  }
  if (s.poly) {
    o += ru ? "Степень уравнения: " : "Degree: ";
    o += std::to_string(s.deg) + "\n";
  } else {
    o += ru ? "Приближённо, на отрезке [" : "Approximate, on [";
    o += numStr(s.lo) + "; " + numStr(s.hi) + "]\n";
  }
  int idx = 0;
  for (const Root& r : s.roots) {
    std::string mult = r.mult > 1 ? (ru ? " (кратн. " : " (x") + std::to_string(r.mult) + ")" : "";
    if (r.im == 0) {
      idx++;
      o += "\nx" + subscript(idx) + " = " + numStr(r.re) + mult;
    } else {
      bool noRe = fabs(r.re) < 1e-12;
      std::string im = numStr(r.im);
      if (im == "1") im = "";
      o += "\nx = " + (noRe ? std::string() : numStr(r.re) + " ") + "\xC2\xB1" + (noRe ? "" : " ") + im + "i" + mult;
    }
  }
  if (s.more) o += ru ? "\n\n... и другие" : "\n\n... and more";
  return o;
}

}  // namespace calc
