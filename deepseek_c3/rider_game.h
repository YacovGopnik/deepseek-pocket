// Бесконечная трасса для «Райдера». Логика не зависит от дисплея и Arduino.
#pragma once
#include <math.h>
#include <stdint.h>

namespace rider {

constexpr int FPS = 30;
constexpr int SECTION = 448;
constexpr float PI_F = 3.14159265f;
constexpr float GRAVITY = 0.29f;

inline float clamp(float value, float low, float high) {
  return value < low ? low : value > high ? high : value;
}

inline float smooth(float t) { return t * t * (3.0f - 2.0f * t); }

inline uint32_t mix(uint32_t n) {
  n ^= n >> 16;
  n *= 0x7feb352du;
  n ^= n >> 15;
  n *= 0x846ca68bu;
  return n ^ (n >> 16);
}

struct Surface {
  float y;
  float grade;
};

// Аналитический наклон не пересекает край трамплина. Секции сходятся без ступеньки.
inline Surface surface(double x) {
  if (x <= 0) return {151, 0};
  const auto section = static_cast<uint64_t>(x / SECTION);
  const float u = static_cast<float>(x - static_cast<double>(section) * SECTION);
  const uint32_t pattern = mix(static_cast<uint32_t>(section) ^ 0x51f15e5du);
  const float difficulty = section < 24 ? static_cast<float>(section) / 24 : 1;
  const float lip = 225 + (pattern & 63);
  const float approach = 87 + ((pattern >> 6) & 31);
  const float height = 26 + ((pattern >> 11) & 7) + difficulty * 12;
  const float dip = 16 + ((pattern >> 14) & 15) + difficulty * 8;
  if (u < lip - approach) {
    const float length = lip - approach;
    const float t = u / length;
    const float wave = 4 * t * (1 - t);
    const float roller = section == 0 ? 0 : static_cast<int>((pattern >> 18) & 31) - 15;
    return {151 + roller * wave * wave, roller * 8 * wave * (1 - 2 * t) / length};
  }
  if (u < lip) {
    const float t = (u - lip + approach) / approach;
    return {151 - height * t * t, -2 * height * t / approach};
  }
  const float length = SECTION - lip;
  const float t = (u - lip) / length;
  return {151 + dip * (1 - smooth(t)), -dip * 6 * t * (1 - t) / length};
}

inline float ground(double x) { return surface(x).y; }

inline float wrapAngle(float a) {
  while (a > PI_F) a -= 2 * PI_F;
  while (a < -PI_F) a += 2 * PI_F;
  return a;
}

enum Event { Riding, Takeoff, Landing, Crash };

class Game {
 public:
  double x = 30;
  float y = 151;
  float v = 3.1f;
  float vx = 0, vy = 0;
  float angle = 0;
  bool air = false, over = false;
  uint32_t best = 0;
  uint32_t lastBonus = 0;
  unsigned lastFlips = 0, combo = 0;
  bool clean = false;

  void reset() {
    const uint32_t record = best;
    *this = Game{};
    best = record;
  }

  uint32_t distance() const {
    const double meters = (x - 30) / 10;
    return meters >= UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(meters);
  }

  uint32_t score() const {
    const uint64_t total = static_cast<uint64_t>(distance()) + trickPoints;
    return total >= UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(total);
  }

  Event step(bool gas) {
    if (over) return Crash;
    lastFlips = lastBonus = 0;
    const float topSpeed = 7.0f + clamp(static_cast<float>((x - 30) / 10000), 0, 1) * 1.5f;
    Event event = Riding;

    if (!air) {
      const Surface here = surface(x);
      const float slope = atanf(here.grade);
      v = clamp(v + (gas ? 0.15f : -0.04f) + GRAVITY * sinf(slope) * 0.50f,
                2.6f, topSpeed + (boostFrames ? 0.85f : 0.0f));
      if (boostFrames) --boostFrames;
      vx = v * cosf(slope);
      vy = v * sinf(slope);
      x += vx;
      const Surface next = surface(x);
      const float tangent = y + vy + GRAVITY * 0.5f;
      if (tangent < next.y - 0.45f) {
        air = true;
        angle = slope;
        y = tangent;
        vy += GRAVITY * 0.5f;
        spin = rotation = 0;
        event = Takeoff;
      } else {
        y = next.y;
        angle = atanf(next.grade);
      }
    } else {
      vy += GRAVITY;
      x += vx;
      y += vy;
      spin = gas ? clamp(spin - 0.055f, -0.30f, 0) : spin * 0.44f;
      rotation += spin;
      angle = wrapAngle(angle + spin);
      const Surface below = surface(x);
      const float slope = atanf(below.grade);
      if (!gas) angle = wrapAngle(angle + clamp(wrapAngle(slope - angle), -0.065f, 0.065f));

      if (y >= below.y) {
        y = below.y;
        const float impactAngle = fabsf(wrapAngle(angle - slope));
        if (impactAngle > 0.95f) {
          over = true;
          event = Crash;
        } else {
          clean = impactAngle <= 0.42f;
          lastFlips = static_cast<unsigned>(-rotation / (2 * PI_F));
          if (lastFlips && clean) {
            if (combo < 5) ++combo;
            boostFrames = 40;
          } else {
            combo = lastFlips ? 1 : 0;
          }
          lastBonus = lastFlips * 25u * combo;
          trickPoints += lastBonus;
          air = false;
          angle = slope;
          v = clamp((vx * cosf(slope) + vy * sinf(slope)) * (clean ? 0.98f : 0.78f)
                    + (boostFrames ? 0.85f : 0), 2.6f, topSpeed + 0.85f);
          event = Landing;
        }
      }
    }

    const uint32_t current = score();
    if (current > best) best = current;
    return event;
  }

 private:
  float spin = 0, rotation = 0;
  uint64_t trickPoints = 0;
  unsigned boostFrames = 0;
};

}  // namespace rider
