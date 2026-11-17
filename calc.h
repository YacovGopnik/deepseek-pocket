// Инженерный калькулятор: разбор и вычисление выражения.
// Понимает: + - × ÷ * / ^ ² ⁻¹ ! %, скобки (незакрытые закрываются сами), π, e, Ans,
// 2E5 (экспонента), неявное умножение (2π, 3(4), 2sin(30)), функции:
// sin cos tan asin acos atan sinh cosh tanh ln log lg log2 exp abs sqrt √ ∛.
// Переменная x (для функций и уравнений): evalAt(f, x) считает f в точке x;
// запись «левая = правая» понимается как левая − правая.
// Чистая логика на std::string - тестируется на компьютере.
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>

namespace calc {

enum Err { E_NONE = 0, E_SYNTAX, E_MATH, E_DIV0, E_BIG };

class Parser {
 public:
  bool degrees = true;   // углы в градусах
  double ans = 0;        // результат прошлого вычисления
  double xv = 0;         // значение переменной x

  // Значение функции в точке x. «L = R» даёт L − R (удобно для уравнений).
  double evalAt(const std::string& s, double x, Err& err) {
    xv = x;
    size_t eq = s.find('=');
    if (eq == std::string::npos) return eval(s, err);
    double l = eval(s.substr(0, eq), err);
    if (err != E_NONE) return l;
    double r = eval(s.substr(eq + 1), err);
    return l - r;
  }

  double eval(const std::string& s, Err& err) {
    src = s;
    p = 0;
    err_ = E_NONE;
    double v = expr();
    skip();
    while (err_ == E_NONE && p < src.size() && src[p] == ')') {   // лишняя закрывающая скобка
      err_ = E_SYNTAX;
    }
    if (err_ == E_NONE && p < src.size()) err_ = E_SYNTAX;
    if (err_ == E_NONE && isnan(v)) err_ = E_MATH;
    if (err_ == E_NONE && isinf(v)) err_ = E_BIG;
    err = err_;
    return v;
  }

 private:
  std::string src;
  size_t p = 0;
  Err err_ = E_NONE;

  void skip() {
    while (p < src.size() && src[p] == ' ') p++;
  }
  bool at(const char* t) {          // совпадает ли текст в позиции p (без сдвига)
    skip();
    size_t n = strlen(t);
    return src.compare(p, n, t) == 0;
  }
  bool eat(const char* t) {
    if (!at(t)) return false;
    p += strlen(t);
    return true;
  }
  void fail(Err e) {
    if (err_ == E_NONE) err_ = e;
  }

  // Можно ли начать множитель с текущей позиции (для неявного умножения)?
  bool startsFactor() {
    skip();
    if (p >= src.size()) return false;
    char c = src[p];
    if ((c >= '0' && c <= '9') || c == '.' || c == '(') return true;
    if ((c >= 'a' && c <= 'z') || c == 'A') return true;
    return at("\xCF\x80") || at("\xE2\x88\x9A") || at("\xE2\x88\x9B");   // π √ ∛
  }

  double expr() {
    double v = term();
    for (;;) {
      if (eat("+")) v += term();
      else if (eat("-") || eat("\xE2\x88\x92")) v -= term();   // - или −
      else return v;
    }
  }

  double term() {
    double v = unary();
    for (;;) {
      if (eat("*") || eat("\xC3\x97") || eat("\xC2\xB7")) {     // * × ·
        v *= unary();
      } else if (eat("/") || eat("\xC3\xB7")) {                  // / ÷
        double d = unary();
        if (d == 0) fail(E_DIV0);
        v /= d;
      } else if (err_ == E_NONE && startsFactor()) {                // 2π, 3(4), 2sin(30)
        v *= power();
      } else {
        return v;
      }
    }
  }

  double unary() {
    if (eat("-") || eat("\xE2\x88\x92")) return -unary();
    if (eat("+")) return unary();
    return power();
  }

  double power() {
    double b = postfix();
    if (eat("^")) {
      double e = unary();                      // правоассоциативно: 2^3^2 = 2^9
      return powr(b, e);
    }
    return b;
  }

  double powr(double b, double e) {
    if (b < 0 && e != floor(e)) {              // корень нечётной степени из отрицательного: (-8)^(1/3)
      double inv = 1.0 / e;
      if (fabs(inv - round(inv)) < 1e-9 && ((long)round(inv)) % 2 != 0) return -pow(-b, e);
      fail(E_MATH);
      return NAN;
    }
    if (b == 0 && e < 0) fail(E_DIV0);
    return pow(b, e);
  }

  double postfix() {
    double v = primary();
    for (;;) {
      if (eat("!")) v = fact(v);
      else if (eat("\xC2\xB2")) v = v * v;                      // ²
      else if (eat("\xC2\xB3")) v = v * v * v;                  // ³
      else if (eat("\xE2\x81\xBB\xC2\xB9")) {                   // ⁻¹
        if (v == 0) fail(E_DIV0);
        v = 1.0 / v;
      } else if (eat("%")) v /= 100.0;
      else return v;
    }
  }

  double fact(double v) {
    if (v < 0 || v != floor(v)) {
      fail(E_MATH);
      return NAN;
    }
    if (v > 170) {
      fail(E_BIG);
      return INFINITY;
    }
    double r = 1;
    for (int i = 2; i <= (int)v; i++) r *= i;
    return r;
  }

  double number() {
    size_t s = p;
    while (p < src.size() && ((src[p] >= '0' && src[p] <= '9') || src[p] == '.')) p++;
    if (p < src.size() && src[p] == 'E') {     // 2E5, 2E-5
      size_t q = p + 1;
      if (q < src.size() && (src[q] == '-' || src[q] == '+')) q++;
      if (q < src.size() && src[q] >= '0' && src[q] <= '9') {
        p = q;
        while (p < src.size() && src[p] >= '0' && src[p] <= '9') p++;
      } else {
        fail(E_SYNTAX);
      }
    }
    std::string t = src.substr(s, p - s);
    int dots = 0;
    for (char c : t)
      if (c == '.') dots++;
    if (dots > 1 || t == ".") fail(E_SYNTAX);
    return atof(t.c_str());
  }

  // Аргумент функции: в скобках (закрывающую можно не ставить) или следующий множитель (sin 30, √2).
  double arg() {
    if (eat("(")) {
      double v = expr();
      if (!eat(")")) {
        skip();
        if (p < src.size()) fail(E_SYNTAX);   // посреди выражения скобка обязана закрыться
      }
      return v;
    }
    return postfix();
  }

  double toRad(double a) { return degrees ? a * M_PI / 180.0 : a; }
  double fromRad(double a) { return degrees ? a * 180.0 / M_PI : a; }

  // sin(180°) должен давать 0, а не 1.2e-16.
  static double clean(double v) { return fabs(v) < 1e-12 ? 0 : v; }

  double trig(int kind, double a) {
    if (degrees) {                                    // точные значения для «круглых» углов
      double m = fmod(a, 360.0);
      if (m < 0) m += 360;
      if (kind == 2 && (fabs(m - 90) < 1e-9 || fabs(m - 270) < 1e-9)) {
        fail(E_MATH);
        return NAN;
      }
    }
    double r = toRad(a);
    if (kind == 0) return clean(sin(r));
    if (kind == 1) return clean(cos(r));
    double c = cos(r);
    if (fabs(c) < 1e-15) {
      fail(E_MATH);
      return NAN;
    }
    return clean(tan(r));
  }

  double primary() {
    skip();
    if (p >= src.size()) {
      fail(E_SYNTAX);
      return NAN;
    }
    char c = src[p];
    if ((c >= '0' && c <= '9') || c == '.') return number();
    if (eat("(")) {
      double v = expr();
      if (!eat(")")) {                        // незакрытая скобка в конце - закрываем сами
        skip();
        if (p < src.size()) fail(E_SYNTAX);
      }
      return v;
    }
    if (eat("\xCF\x80") || eat("pi")) return M_PI;               // π
    if (eat("Ans")) return ans;
    if (eat("\xE2\x88\x9A")) {                                   // √
      double v = arg();
      if (v < 0) fail(E_MATH);
      return sqrt(v);
    }
    if (eat("\xE2\x88\x9B")) return cbrt(arg());                 // ∛
    // функции: длинные имена раньше коротких (asin раньше sin, sinh раньше sin)
    if (eat("asin")) {
      double v = arg();
      if (v < -1 || v > 1) fail(E_MATH);
      return fromRad(asin(v));
    }
    if (eat("acos")) {
      double v = arg();
      if (v < -1 || v > 1) fail(E_MATH);
      return fromRad(acos(v));
    }
    if (eat("atan")) return fromRad(atan(arg()));
    if (eat("sinh")) return sinh(arg());
    if (eat("cosh")) return cosh(arg());
    if (eat("tanh")) return tanh(arg());
    if (eat("sin")) return trig(0, arg());
    if (eat("cos")) return trig(1, arg());
    if (eat("tan") || eat("tg")) return trig(2, arg());
    if (eat("sqrt")) {
      double v = arg();
      if (v < 0) fail(E_MATH);
      return sqrt(v);
    }
    if (eat("abs")) return fabs(arg());
    if (eat("exp")) return exp(arg());
    if (eat("ln")) {
      double v = arg();
      if (v <= 0) fail(E_MATH);
      return log(v);
    }
    if (eat("log2")) {
      double v = arg();
      if (v <= 0) fail(E_MATH);
      return log2(v);
    }
    if (eat("log") || eat("lg")) {
      double v = arg();
      if (v <= 0) fail(E_MATH);
      return log10(v);
    }
    if (eat("x")) return xv;
    if (eat("e")) return M_E;
    fail(E_SYNTAX);
    p = src.size();
    return NAN;
  }
};

// Красивая запись числа: до 10 значащих цифр, большие и маленькие - как 1.5×10¹².
inline std::string format(double v) {
  if (v == 0) return "0";
  char b[40];
  double a = fabs(v);
  if (a >= 1e10 || a < 1e-6) {
    snprintf(b, sizeof(b), "%.8e", v);              // 1.50000000e+12
    std::string s(b);
    size_t e = s.find('e');
    std::string mant = s.substr(0, e), ex = s.substr(e + 1);
    if (mant.find('.') != std::string::npos) {      // убрать хвостовые нули мантиссы
      while (mant.back() == '0') mant.pop_back();
      if (mant.back() == '.') mant.pop_back();
    }
    int n = atoi(ex.c_str());
    static const char* sup[] = {"\xE2\x81\xB0", "\xC2\xB9", "\xC2\xB2", "\xC2\xB3", "\xE2\x81\xB4",
                                "\xE2\x81\xB5", "\xE2\x81\xB6", "\xE2\x81\xB7", "\xE2\x81\xB8", "\xE2\x81\xB9"};
    std::string es;
    if (n < 0) es += "\xE2\x81\xBB";                // ⁻
    std::string digits = std::to_string(n < 0 ? -n : n);
    for (char d : digits) es += sup[d - '0'];
    return mant + "\xC3\x97" "10" + es;             // ×10ⁿ
  }
  snprintf(b, sizeof(b), "%.10g", v);
  std::string s(b);
  if (s == "-0") s = "0";
  return s;
}

inline const char* errName(Err e, bool ru) {
  switch (e) {
    case E_SYNTAX: return ru ? "Ошибка в записи" : "Syntax error";
    case E_MATH: return ru ? "Не определено" : "Math error";
    case E_DIV0: return ru ? "Деление на 0" : "Division by 0";
    case E_BIG: return ru ? "Слишком большое" : "Overflow";
    default: return "";
  }
}

}  // namespace calc
