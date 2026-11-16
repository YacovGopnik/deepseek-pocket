// Математика для первого курса: корни уравнения, пределы, производная, интеграл, масштаб графика.
// Всё считается численно (приближённо). Чистая логика - тестируется на компьютере.
#pragma once
#include <math.h>
#include <stdio.h>
#include <algorithm>
#include <string>
#include <vector>

namespace mth {

typedef double (*Fn)(double x, bool& ok);       // ok = false, если в точке функция не определена

inline bool fin(double v) { return !isnan(v) && !isinf(v); }

inline double call(Fn f, double x, bool& ok) {
  ok = true;
  double v = f(x, ok);
  if (!fin(v)) ok = false;
  return v;
}

// ---------- корни f(x) = 0 на отрезке [a, b] ----------
inline void addRoot(std::vector<double>& r, double x) {
  if (fabs(x) < 1e-9) x = 0;
  for (double v : r)
    if (fabs(v - x) < 1e-6 * (1 + fabs(x))) return;
  if (r.size() < 12) r.push_back(x);
}

inline void findRoots(Fn f, double a, double b, int n, std::vector<double>& roots) {
  roots.clear();
  double h = (b - a) / n;
  bool ok0, ok1, ok2;
  double x0 = a, y0 = call(f, x0, ok0);
  double xp = 0, yp = 0;
  bool okp = false;
  for (int i = 1; i <= n; i++) {
    double x1 = a + h * i, y1 = call(f, x1, ok1);
    if (ok0 && y0 == 0) addRoot(roots, x0);
    if (ok0 && ok1 && ((y0 < 0 && y1 > 0) || (y0 > 0 && y1 < 0))) {       // знак сменился: делим отрезок пополам
      double lo = x0, hi = x1, flo = y0;
      for (int k = 0; k < 60; k++) {
        double m = (lo + hi) / 2, fm = call(f, m, ok2);
        if (!ok2) break;
        if ((flo < 0) == (fm < 0)) {
          lo = m;
          flo = fm;
        } else {
          hi = m;
        }
      }
      double r = (lo + hi) / 2, fr = call(f, r, ok2);
      double scale = fabs(y0) > fabs(y1) ? fabs(y0) : fabs(y1);
      if (ok2 && fabs(fr) <= 1e-6 * (1 + scale)) addRoot(roots, r);         // у разрыва (1/x, tg) знак тоже меняется, но значение огромное
    }
    // касание оси без смены знака: |f| в середине тройки меньше, чем по краям, и почти ноль
    if (okp && ok0 && ok1 && fabs(y0) < fabs(yp) && fabs(y0) <= fabs(y1) && fabs(y0) < 1e-3 * (1 + fabs(yp) + fabs(y1))) {
      double lo = xp, hi = x1;
      const double G = 0.6180339887;
      for (int k = 0; k < 80; k++) {
        double m1 = hi - G * (hi - lo), m2 = lo + G * (hi - lo);
        bool o1, o2;
        double f1 = fabs(call(f, m1, o1)), f2 = fabs(call(f, m2, o2));
        if (!o1 || !o2) break;
        if (f1 < f2) hi = m2;
        else lo = m1;
      }
      double r = (lo + hi) / 2, fr = call(f, r, ok2);
      if (ok2 && fabs(fr) < 1e-9 * (1 + fabs(yp) + fabs(y1))) addRoot(roots, r);
    }
    xp = x0;
    yp = y0;
    okp = ok0;
    x0 = x1;
    y0 = y1;
    ok0 = ok1;
  }
  if (ok0 && y0 == 0) addRoot(roots, x0);
  std::sort(roots.begin(), roots.end());
}

// ---------- пределы ----------
enum LimKind { L_VALUE, L_PINF, L_NINF, L_NONE };   // число, +бесконечность, -бесконечность, не определить
struct Lim {
  LimKind kind = L_NONE;
  double value = 0;
};

// Предел по последовательности точек xs, приближающихся к цели.
inline Lim limitSeq(Fn f, const double* xs, int n) {
  Lim out;
  double v[16];
  bool okv[16];
  if (n > 16) n = 16;
  for (int i = 0; i < n; i++) v[i] = call(f, xs[i], okv[i]);
  // уход в бесконечность: модуль растёт и уже большой, знак один (переполнение считаем очень большим числом)
  int grow = 0;
  bool big = false;
  double prev = 0;
  bool havePrev = false;
  for (int i = 0; i < n; i++) {
    bool infNow = isinf(v[i]);
    if (!okv[i] && !infNow) {                           // в точке не определена - цепочку не рвём
      continue;
    }
    double cur = infNow ? (v[i] > 0 ? 1e300 : -1e300) : v[i];
    if (havePrev) {
      bool sameSign = (cur > 0) == (prev > 0);
      if (sameSign && (fabs(cur) > fabs(prev) * 1.5 || (fabs(cur) >= 1e300 && fabs(prev) >= 1e300))) grow++;
      else grow = 0;
    }
    if (fabs(cur) > 1e6) big = true;
    prev = cur;
    havePrev = true;
  }
  if (grow >= 4 && big) {
    out.kind = prev > 0 ? L_PINF : L_NINF;
    return out;
  }
  // сходимость: ищем место, где соседние значения ближе всего
  int best = -1;
  double bestD = 1e300;
  for (int i = 2; i < n; i++) {
    if (!okv[i] || !okv[i - 1]) continue;
    double d = fabs(v[i] - v[i - 1]);
    if (d <= bestD) {
      bestD = d;
      best = i;
    }
  }
  if (best < 0) return out;
  double val = v[best];
  if (bestD > 1e-4 * (1 + fabs(val))) return out;                           // так и не сошлось (колебания)
  int near = 0;                                                             // и рядом значения должны быть к нему близки
  for (int i = 1; i < n; i++)
    if (okv[i] && fabs(v[i] - val) < 1e-2 * (1 + fabs(val))) near++;
  if (near < 3) return out;
  // значения убывают к нулю «в разы» на каждом шаге (как 1/x) - предел равен нулю
  if (fabs(val) < 1e-5) {
    int shrink = 0;
    for (int i = n - 1; i > 0 && okv[i] && okv[i - 1] && fabs(v[i]) <= fabs(v[i - 1]) * 0.5; i--) shrink++;
    if (shrink >= 4) val = 0;
  }
  out.kind = L_VALUE;
  out.value = fabs(val) < 1e-9 ? 0 : val;
  return out;
}

// side: -1 слева, +1 справа от точки x0.
inline Lim limitSide(Fn f, double x0, int side) {
  double xs[9], h = 0.1;
  for (int i = 0; i < 9; i++, h /= 10) xs[i] = x0 + side * h * (1 + fabs(x0));
  return limitSeq(f, xs, 8);
}

// sign: +1 при x -> +бесконечность, -1 при x -> -бесконечность.
inline Lim limitInf(Fn f, int sign) {
  double xs[10], x = 10;
  for (int i = 0; i < 10; i++, x *= 10) xs[i] = sign * x;
  return limitSeq(f, xs, 9);
}

inline bool sameLim(const Lim& a, const Lim& b) {
  if (a.kind != b.kind || a.kind == L_NONE) return false;
  if (a.kind != L_VALUE) return true;
  return fabs(a.value - b.value) < 1e-5 * (1 + fabs(a.value));
}

// ---------- производная и интеграл ----------
inline double derivative(Fn f, double x0, bool& ok) {
  double h = 1e-3 * (1 + fabs(x0));
  bool a, b, c, d;
  double d1 = (call(f, x0 + h, a) - call(f, x0 - h, b)) / (2 * h);
  double d2 = (call(f, x0 + h / 2, c) - call(f, x0 - h / 2, d)) / h;
  ok = a && b && c && d;
  return (4 * d2 - d1) / 3;                           // уточнение Ричардсона
}

inline double integral(Fn f, double a, double b, bool& ok) {
  const int N = 600;                                  // чётное число отрезков (метод Симпсона)
  double h = (b - a) / N, s = 0, s2 = 0;
  ok = true;
  for (int i = 0; i <= N; i++) {
    bool o;
    double x = a + h * i;
    double v = call(f, x, o);
    if (!o && (i == 0 || i == N)) v = call(f, x + (i == N ? -h : h) * 1e-6, o);   // на самом краю можно чуть отступить
    if (!o) {                                         // внутри отрезка функция не определена
      ok = false;
      return 0;
    }
    s += v * (i == 0 || i == N ? 1 : (i & 1) ? 4 : 2);
    if ((i & 1) == 0) s2 += v * (i == 0 || i == N ? 1 : (i & 2) ? 4 : 2);         // то же с вдвое большим шагом
  }
  double r = s * h / 3, r2 = s2 * (2 * h) / 3;
  if (fabs(r - r2) > 1e-3 * (1 + fabs(r))) ok = false;                            // грубый и точный счёт расходятся: разрыв или слишком резкая функция
  return r;
}

// ---------- запись ответа ----------
// Подсказка, на что похоже число: дробь, доля пи, корень. Пустая строка - ни на что.
inline std::string hint(double v) {
  if (!fin(v) || fabs(v) < 1e-9 || fabs(v) > 1e4) return "";
  if (fabs(v - round(v)) < 1e-9) return "";
  char b[40];
  const double tol = 1e-7 * (1 + fabs(v));
  for (int q = 2; q <= 12; q++) {
    double p = round(v * q);
    if (fabs(p / q - v) < tol) {
      snprintf(b, sizeof(b), "%d/%d", (int)p, q);
      return b;
    }
  }
  for (int q = 1; q <= 12; q++) {
    double p = round(v * q / M_PI);
    if (p != 0 && fabs(p * M_PI / q - v) < tol) {
      int ip = (int)p;
      std::string num = ip == 1 ? "\xCF\x80" : ip == -1 ? "-\xCF\x80" : std::to_string(ip) + "\xCF\x80";
      return q == 1 ? num : num + "/" + std::to_string(q);
    }
  }
  for (int n = 2; n <= 50; n++) {
    double r = sqrt((double)n);
    if (fabs(r - round(r)) < 1e-9) continue;
    if (fabs(fabs(v) - r) < tol) return std::string(v < 0 ? "-" : "") + "\xE2\x88\x9A" + std::to_string(n);
  }
  if (fabs(v - M_E) < tol) return "e";
  if (fabs(v - 1 / M_E) < tol) return "1/e";
  return "";
}

// ---------- график: границы по вертикали ----------
// Берём значения без «выбросов» (у разрывов они улетают в бесконечность) и добавляем поля.
inline void yRange(std::vector<double> v, double& lo, double& hi) {
  std::vector<double> f;
  for (double x : v)
    if (fin(x)) f.push_back(x);
  if (f.empty()) {
    lo = -1;
    hi = 1;
    return;
  }
  std::sort(f.begin(), f.end());
  size_t n = f.size();
  double a = f[n * 4 / 100], b = f[n - 1 - n * 4 / 100];
  double full = f[n - 1] - f[0], mid = b - a;
  if (full <= mid * 3 + 1e-12) {                     // выбросов нет - показываем всё
    a = f[0];
    b = f[n - 1];
  }
  if (b - a < 1e-9) {
    a -= 1;
    b += 1;
  }
  double pad = (b - a) * 0.08;
  lo = a - pad;
  hi = b + pad;
}

}  // namespace mth
