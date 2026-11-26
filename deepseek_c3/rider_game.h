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

constexpr float ROAD_Y = 151;
constexpr float SAW_RADIUS = 10;
constexpr float SPIKE_HEIGHT = 13;
constexpr int SPIKE_COUNT = 3;
constexpr int SPIKE_WIDTH = 8;
constexpr float SPIKE_HALF = SPIKE_COUNT * SPIKE_WIDTH / 2.0f;

enum class Terrain { Jump, Spikes, Saw, Loop, Rollers };

struct Section {
  uint64_t id;
  uint32_t pattern;
  Terrain type;

  explicit Section(double x)
      : id(x > 0 ? static_cast<uint64_t>(x / SECTION) : 0),
        pattern(mix(static_cast<uint32_t>(id) ^ 0x51f15e5du)),
        type(Terrain::Jump) {
    if (id != 0) {
      // Shuffle the order of five different sections in each group.
      uint32_t group = mix(static_cast<uint32_t>((id - 1) / 5) ^ 0x286e8a3du);
      unsigned slot = (id - 1) % 5;
      type = static_cast<Terrain>((slot * (1 + group % 4) + (group >> 8) % 5) % 5);
    }
  }

  double start() const { return static_cast<double>(id) * SECTION; }
  float lip() const { return 225 + (pattern & 63); }
  float radius() const { return 34 + ((pattern >> 4) & 7); }
  double featureX() const {
    if (type == Terrain::Loop) return start() + lip();
    if (type == Terrain::Saw) return start() + lip() + 52 + ((pattern >> 19) & 15);
    return start() + lip() + 103 + ((pattern >> 19) & 15);
  }
  float sawY() const { return 113 + ((pattern >> 23) & 7); }
};

struct Surface {
  float y;
  float grade;
};

// Аналитический наклон не пересекает край трамплина. Секции сходятся без ступеньки.
inline Surface surface(double x) {
  if (x <= 0) return {ROAD_Y, 0};
  const Section section(x);
  const float u = static_cast<float>(x - section.start());
  const uint32_t pattern = section.pattern;
  if (section.type == Terrain::Loop) return {ROAD_Y, 0};
  if (section.type == Terrain::Rollers) {
    const int humps = 2 + (pattern & 1);
    const float length = static_cast<float>(SECTION) / humps;
    const float t = (u - static_cast<int>(u / length) * length) / length;
    const float wave = 4 * t * (1 - t);
    const float height = 14 + ((pattern >> 8) & 15);
    return {ROAD_Y - height * wave * wave, -height * 8 * wave * (1 - 2 * t) / length};
  }
  const float difficulty = section.id < 24 ? static_cast<float>(section.id) / 24 : 1;
  const float lip = section.lip();
  const float approach = 87 + ((pattern >> 6) & 31);
  const float height = 26 + ((pattern >> 11) & 7) + difficulty * 12;
  const float dip = 16 + ((pattern >> 14) & 15) + difficulty * 8;
  if (u < lip - approach) {
    const float length = lip - approach;
    const float t = u / length;
    const float wave = 4 * t * (1 - t);
    const float roller = section.id == 0 ? 0 : static_cast<int>((pattern >> 18) & 31) - 15;
    return {ROAD_Y + roller * wave * wave, roller * 8 * wave * (1 - 2 * t) / length};
  }
  if (u < lip) {
    const float t = (u - lip + approach) / approach;
    return {ROAD_Y - height * t * t, -2 * height * t / approach};
  }
  const float length = SECTION - lip;
  const float t = (u - lip) / length;
  return {ROAD_Y + dip * (1 - smooth(t)), -dip * 6 * t * (1 - t) / length};
}

inline float ground(double x) { return surface(x).y; }

inline float wrapAngle(float a) {
  while (a > PI_F) a -= 2 * PI_F;
  while (a < -PI_F) a += 2 * PI_F;
  return a;
}

enum Event { Riding, Takeoff, Landing, Crash, LoopClear };
enum class CrashCause { Landing, Spikes, Saw, Loop };

inline bool outsideCircle(float dx, float dy, float radius) {
  return dx * dx + dy * dy >= radius * radius;
}

class Game {
 public:
  double x = 30;
  float y = 151;
  float v = 3.1f;
  float vx = 0, vy = 0;
  float angle = 0;
  bool air = false, over = false;
  CrashCause crashCause = CrashCause::Landing;

  double bikeX() const {
    return loopRadius ? x + loopRadius * sinf(loopPhase) : x;
  }
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
    const double meters = (x - 30 + loopDistance) / 10;
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

    if (loopRadius) {
      event = stepLoop(gas, topSpeed);
    } else if (!air) {
      const Surface here = surface(x);
      const float slope = atanf(here.grade);
      v = clamp(v + (gas ? 0.15f : -0.04f) + GRAVITY * sinf(slope) * 0.50f,
                2.6f, topSpeed + (boostFrames ? 0.85f : 0.0f));
      if (boostFrames) --boostFrames;
      vx = v * cosf(slope);
      vy = v * sinf(slope);
      const Section hereSection(x);
      const double entry = hereSection.featureX();
      if (hereSection.type == Terrain::Loop && x <= entry && x + vx > entry) {
        x = entry;
        y = ROAD_Y;
        angle = 0;
        loopRadius = hereSection.radius();
        loopPhase = 0;
        event = stepLoop(gas, topSpeed);
      } else {
        event = stepGround(slope);
      }
    } else {
      event = stepAir(gas, topSpeed);
    }

    if (event != Crash && !loopRadius && hitHazard()) {
      over = true;
      event = Crash;
    }
    const uint32_t current = score();
    if (current > best) best = current;
    return event;
  }

 private:
  Event stepGround(float slope) {
    x += vx;
    const Surface next = surface(x);
    const float tangent = y + vy + GRAVITY * 0.5f;
    if (tangent < next.y - 0.45f) {
      air = true;
      angle = slope;
      y = tangent;
      vy += GRAVITY * 0.5f;
      spin = rotation = 0;
      return Takeoff;
    }
    y = next.y;
    angle = atanf(next.grade);
    return Riding;
  }

  Event stepAir(bool gas, float topSpeed) {
    Event event = Riding;
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
    return event;
  }

  Event stepLoop(bool gas, float topSpeed) {
    // A lower speed at the top loses contact with the inside of the loop.
    const float before = loopPhase;
    v = clamp(v + (gas ? 0.20f : -0.06f) - GRAVITY * 0.73f * sinf(before),
              0.6f, topSpeed + 0.85f);
    loopPhase = clamp(loopPhase + v / loopRadius, 0, 2 * PI_F);
    loopDistance += loopRadius * (loopPhase - before);
    y = ROAD_Y - loopRadius * (1 - cosf(loopPhase));
    angle = wrapAngle(-loopPhase);
    if (loopPhase > PI_F * 0.45f && loopPhase < PI_F * 1.30f && v < 2.65f) {
      over = true;
      crashCause = CrashCause::Loop;
      return Crash;
    }
    if (loopPhase < 2 * PI_F) return Riding;
    x += 0.1;  // pass the loop entrance before resuming the straight
    y = ROAD_Y;
    angle = 0;
    loopRadius = 0;
    lastBonus = 40;
    trickPoints += lastBonus;
    return LoopClear;
  }

  bool hitHazard() {
    const Section section(x);
    if (section.type != Terrain::Spikes && section.type != Terrain::Saw) return false;
    const float centerDx = static_cast<float>(x - section.featureX());
    if (fabsf(centerDx) > 34) return false;
    const float c = cosf(angle), s = sinf(angle);
    // Check both wheels and the rider's head rather than a single large hitbox.
    const float offsets[] = {5 * s - 9 * c, 5 * s + 9 * c, 14 * s};
    const float heights[] = {y - 5 * c - 9 * s, y - 5 * c + 9 * s, y - 14 * c};
    for (int i = 0; i < 3; ++i) {
      const float dx = centerDx + offsets[i];
      const float r = i < 2 ? 4 : 3;
      if (section.type == Terrain::Saw) {
        if (outsideCircle(dx, heights[i] - section.sawY(), SAW_RADIUS + r)) continue;
        crashCause = CrashCause::Saw;
        return true;
      }
      if (fabsf(dx) >= SPIKE_HALF + r) continue;
      if (heights[i] + r < ground(section.featureX()) - SPIKE_HEIGHT + 3) continue;
      crashCause = CrashCause::Spikes;
      return true;
    }
    return false;
  }

  float loopRadius = 0, loopPhase = 0;
  double loopDistance = 0;
  float spin = 0, rotation = 0;
  uint64_t trickPoints = 0;
  unsigned boostFrames = 0;
};

}  // namespace rider
