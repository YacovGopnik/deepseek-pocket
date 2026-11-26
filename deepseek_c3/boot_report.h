#pragma once
#include <Arduino.h>
#include <Preferences.h>

namespace boottrace {

// Записи находятся в NVS, общей для обоих OTA-слотов. Порядок этапов — часть формата.
enum Stage : uint8_t {
  Start, Settings, Display, Sound, Accel, Touch, Input, Mirror, Files,
  Sd, Wifi, Bluetooth, Ready, Rollback, StageCount
};

constexpr unsigned Capacity = 8;
constexpr char Space[] = "boottrace";

struct Entry {
  uint32_t sequence;
  uint32_t elapsed;
  uint16_t version;
  uint8_t stage;
  uint8_t reset;
};

static_assert(sizeof(Entry) == 12, "Unexpected boot report layout");
static Entry current{};

inline void keyFor(unsigned slot, char (&key)[6]) {
  snprintf(key, sizeof(key), "boot%u", slot);
}

inline bool newer(uint32_t a, uint32_t b) {
  return a != b && (a - b) < 0x80000000u;
}

inline unsigned read(Preferences& storage, Entry (&entries)[Capacity]) {
  unsigned count = 0;
  for (unsigned slot = 0; slot < Capacity; ++slot) {
    char key[6];
    keyFor(slot, key);
    Entry entry{};
    if (storage.getBytesLength(key) != sizeof(entry) ||
        storage.getBytes(key, &entry, sizeof(entry)) != sizeof(entry) ||
        !entry.sequence || entry.sequence % Capacity != slot ||
        entry.stage >= StageCount) continue;

    unsigned pos = count++;
    while (pos && newer(entry.sequence, entries[pos - 1].sequence)) {
      entries[pos] = entries[pos - 1];
      --pos;
    }
    entries[pos] = entry;
  }
  return count;
}

inline unsigned history(Entry (&entries)[Capacity]) {
  Preferences storage;
  if (!storage.begin(Space, true)) return 0;
  unsigned count = read(storage, entries);
  storage.end();
  return count;
}

inline void store(Preferences& storage) {
  char key[6];
  keyFor(current.sequence % Capacity, key);
  storage.putBytes(key, &current, sizeof(current));
}

inline void begin(uint16_t version, uint8_t reset) {
  Preferences storage;
  if (!storage.begin(Space, false)) return;

  Entry entries[Capacity];
  const unsigned count = read(storage, entries);
  const uint32_t next = count ? entries[0].sequence + 1 : 1;
  current = {next ? next : 1, millis(), version, Start, reset};
  store(storage);
  storage.end();
}

inline void mark(Stage stage) {
  if (!current.sequence) return;
  Preferences storage;
  if (!storage.begin(Space, false)) return;
  current.stage = stage;
  current.elapsed = millis();
  store(storage);
  storage.end();
}

}  // namespace boottrace
