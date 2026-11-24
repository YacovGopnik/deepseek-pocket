#pragma once
#include <stdint.h>

namespace cube_run {
constexpr int SCREEN_W = 240;
constexpr int GROUND_Y = 188;
constexpr int CUBE_X = 44;
constexpr int CUBE_SIZE = 18;
constexpr int SPIKE_W = 18;
constexpr int SPIKE_H = 24;
constexpr int SPEED = 3;
constexpr int BEAT = 60;
constexpr int FINISH = 84 * BEAT;

struct Block { int x, width, height; };
struct Spike { int x, height; };

constexpr Block blocks[] = {
    {10 * BEAT, 120, 20}, {14 * BEAT, 60, 30},
    {20 * BEAT, 180, 24}, {24 * BEAT, 90, 36},
    {31 * BEAT, 72, 24}, {33 * BEAT, 96, 38}, {36 * BEAT, 108, 22},
    {43 * BEAT, 120, 28}, {47 * BEAT, 120, 36},
    {53 * BEAT, 180, 24}, {58 * BEAT, 72, 40},
    {63 * BEAT, 96, 20}, {65 * BEAT, 144, 36}, {69 * BEAT, 120, 24},
    {75 * BEAT, 120, 30}, {79 * BEAT, 90, 40},
};

constexpr Spike spikes[] = {
    {4 * BEAT, 0}, {7 * BEAT, 0}, {7 * BEAT + SPIKE_W, 0},
    {13 * BEAT, 0}, {17 * BEAT, 0}, {18 * BEAT, 0},
    {22 * BEAT, 24}, {27 * BEAT, 0}, {27 * BEAT + SPIKE_W, 0},
    {35 * BEAT, 0}, {39 * BEAT, 0}, {40 * BEAT, 0},
    {44 * BEAT, 28}, {46 * BEAT, 0}, {50 * BEAT, 0},
    {55 * BEAT, 24}, {57 * BEAT, 0}, {61 * BEAT, 0},
    {66 * BEAT, 36}, {71 * BEAT, 0}, {73 * BEAT, 0},
    {76 * BEAT, 30}, {82 * BEAT, 0}, {82 * BEAT + SPIKE_W, 0},
};

inline int spikeHalfWidth(int height) {
  return (SPIKE_H - 1 - height) * (SPIKE_W / 2 - 1) / (SPIKE_H - 1);
}

class Game {
 public:
  int distance = 0;
  int rotation = 0;
  bool over = false;
  bool finished = false;

  int cubeY() const { return foot / SUBPIXELS - CUBE_SIZE; }
  int progress() const { return distance * 100 / FINISH; }

  void step(bool jump) {
    if (over || finished) return;
    if (jump) jumpBuffer = 5;
    if (jumpBuffer && grounded) {
      velocity = -134;
      grounded = false;
      jumpBuffer = 0;
    }
    if (jumpBuffer) --jumpBuffer;

    distance += SPEED;
    const int left = distance + CUBE_X + 2;
    const int right = distance + CUBE_X + CUBE_SIZE - 2;
    const int previousFoot = foot;
    foot += velocity;
    int landing = GROUND_Y * SUBPIXELS;
    for (const Block& block : blocks) {
      if (right <= block.x) break;
      if (left >= block.x + block.width) continue;
      const int top = (GROUND_Y - block.height) * SUBPIXELS;
      if (previousFoot > top) {
        over = true;
        return;
      }
      if (top < landing) landing = top;
    }

    grounded = foot >= landing;
    if (grounded) {
      foot = landing;
      velocity = 0;
      rotation = ((rotation + 45) / 90 * 90) % 360;
    } else {
      velocity += 8;
      rotation = (rotation + 6) % 360;
    }

    for (const Spike& spike : spikes) {
      if (right <= spike.x) break;
      if (left >= spike.x + SPIKE_W) continue;
      int height = GROUND_Y - spike.height - foot / SUBPIXELS + 1;
      if (height >= SPIKE_H) continue;
      if (height < 0) height = 0;
      const int center = spike.x + SPIKE_W / 2;
      const int half = spikeHalfWidth(height);
      if (left <= center + half && right > center - half) {
        over = true;
        return;
      }
    }
    finished = distance >= FINISH;
  }

 private:
  static constexpr int SUBPIXELS = 16;
  int foot = GROUND_Y * SUBPIXELS;
  int velocity = 0;
  int jumpBuffer = 0;
  bool grounded = true;
};
}  // namespace cube_run
