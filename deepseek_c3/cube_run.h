#pragma once
#include <stdint.h>

namespace cube_run {
constexpr int SCREEN_W = 240;
constexpr int GROUND_Y = 188;
constexpr int CUBE_X = 44;
constexpr int CUBE_SIZE = 18;
constexpr int SPIKE_W = 18;
constexpr int SPIKE_H = 24;
constexpr int MAX_GROUPS = 3;

struct Spikes {
  int x = 0;
  uint8_t count = 0;
  bool passed = false;

  int right() const { return x + count * SPIKE_W; }
};

// Высота считается от основания шипа. Одна формула для рисунка и столкновений.
inline int spikeHalfWidth(int height) {
  return (SPIKE_H - 1 - height) * (SPIKE_W / 2 - 1) / (SPIKE_H - 1);
}

class Game {
 public:
  Spikes spikes[MAX_GROUPS];
  uint32_t score = 0;
  int speed = 3;
  bool over = false;

  int cubeY() const { return GROUND_Y - CUBE_SIZE - lift / SUBPIXELS; }

  void step(bool jump) {
    if (over) return;
    if (jump) jumpBuffer = 5;
    if (jumpBuffer && lift == 0) {
      velocity = 134;
      jumpBuffer = 0;
    }
    if (jumpBuffer) --jumpBuffer;
    lift += velocity;
    if (lift > 0) {
      velocity -= 8;
    } else {
      lift = velocity = 0;
    }

    nextX -= speed;
    for (Spikes& group : spikes) {
      if (group.count) {
        group.x -= speed;
        if (group.right() <= 0) group.count = 0;
      }
      if (!group.count && nextX < SCREEN_W) spawn(group);
    }

    for (Spikes& group : spikes) {
      if (!group.count || group.passed) continue;
      if (hits(group)) {
        over = true;
        return;
      }
      if (group.right() <= CUBE_X + 2) {
        group.passed = true;
        ++score;
        speed = score >= 20 ? 5 : score >= 8 ? 4 : 3;
      }
    }
  }

 private:
  static constexpr int SUBPIXELS = 16;
  int lift = 0;
  int velocity = 0;
  int jumpBuffer = 0;
  int nextX = SCREEN_W;
  unsigned pattern = 0;

  void spawn(Spikes& group) {
    struct Segment { uint8_t gap, count; };
    static constexpr Segment track[] = {
      {196, 1}, {182, 1}, {208, 2}, {180, 1}, {192, 2}, {204, 2}, {176, 1}, {188, 2}
    };
    group.x = nextX;
    group.count = track[pattern].count;
    group.passed = false;
    nextX += track[pattern].gap;
    pattern = (pattern + 1) % (sizeof(track) / sizeof(track[0]));
  }

  bool hits(const Spikes& group) const {
    // У куба небольшой допуск по краям. Основание треугольника шире вершины.
    const int left = CUBE_X + 2, right = CUBE_X + CUBE_SIZE - 3;
    const int height = GROUND_Y - 1 - (cubeY() + CUBE_SIZE - 2);
    if (height >= SPIKE_H) return false;
    const int half = spikeHalfWidth(height);
    for (uint8_t i = 0; i < group.count; ++i) {
      int center = group.x + i * SPIKE_W + SPIKE_W / 2;
      if (left <= center + half && right >= center - half) return true;
    }
    return false;
  }
};
}  // namespace cube_run
