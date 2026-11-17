// Крошечный клиент MQTT 3.1.1 для «Комнаты»: только то, что нужно для обмена сообщениями
// (подключиться, подписаться, отправить, принять, «пинг»). Без сторонних библиотек.
// Здесь только сборка и разбор пакетов - чистые функции, тестируются на компьютере.
#pragma once
#include <stdint.h>
#include <string>

namespace mqtt {

enum { T_CONNECT = 1, T_CONNACK = 2, T_PUBLISH = 3, T_SUBSCRIBE = 8, T_SUBACK = 9, T_PINGREQ = 12, T_PINGRESP = 13 };

inline void putLen(std::string& o, size_t n) {       // «оставшаяся длина»: по 7 бит, старший бит - продолжение
  do {
    uint8_t b = n & 0x7F;
    n >>= 7;
    if (n) b |= 0x80;
    o += (char)b;
  } while (n);
}

inline void putStr(std::string& o, const std::string& s) {
  o += (char)(s.size() >> 8);
  o += (char)(s.size() & 0xFF);
  o += s;
}

inline std::string packet(uint8_t header, const std::string& body) {
  std::string o;
  o += (char)header;
  putLen(o, body.size());
  return o + body;
}

inline std::string connect(const std::string& clientId, uint16_t keepAliveSec) {
  std::string b;
  putStr(b, "MQTT");
  b += (char)4;                                      // версия 3.1.1
  b += (char)2;                                      // чистая сессия
  b += (char)(keepAliveSec >> 8);
  b += (char)(keepAliveSec & 0xFF);
  putStr(b, clientId);
  return packet(T_CONNECT << 4, b);
}

inline std::string subscribe(uint16_t id, const std::string& topic) {
  std::string b;
  b += (char)(id >> 8);
  b += (char)(id & 0xFF);
  putStr(b, topic);
  b += (char)0;                                      // QoS 0
  return packet((T_SUBSCRIBE << 4) | 2, b);
}

inline std::string publish(const std::string& topic, const std::string& payload) {
  std::string b;
  putStr(b, topic);
  return packet(T_PUBLISH << 4, b + payload);
}

// «Сохраняемое» сообщение: сервер отдаёт его каждому, кто подпишется позже (так живут аватарки).
inline std::string publishRetained(const std::string& topic, const std::string& payload) {
  std::string b;
  putStr(b, topic);
  return packet((T_PUBLISH << 4) | 1, b + payload);
}

inline std::string ping() { return packet(T_PINGREQ << 4, ""); }

struct Packet {
  int type = 0;
  uint8_t flags = 0;
  std::string body;      // всё после заголовка
  std::string topic;     // для PUBLISH
  std::string payload;   // для PUBLISH
};

// Достаёт из начала буфера один пакет. 1 - пакет готов (и убран из буфера), 0 - данных пока мало,
// -1 - мусор (буфер надо сбросить). maxLen - защита от слишком больших пакетов.
inline int take(std::string& buf, Packet& p, size_t maxLen) {
  if (buf.size() < 2) return 0;
  size_t len = 0, i = 1;
  int shift = 0;
  for (;;) {
    if (i >= buf.size()) return 0;
    uint8_t b = (uint8_t)buf[i++];
    len |= (size_t)(b & 0x7F) << shift;
    if (!(b & 0x80)) break;
    shift += 7;
    if (shift > 21) return -1;
  }
  if (len > maxLen) return -1;
  if (buf.size() < i + len) return 0;
  p.type = ((uint8_t)buf[0]) >> 4;
  p.flags = ((uint8_t)buf[0]) & 15;
  p.body = buf.substr(i, len);
  p.topic.clear();
  p.payload.clear();
  buf.erase(0, i + len);
  if (p.type == T_PUBLISH) {
    if (p.body.size() < 2) return -1;
    size_t tl = ((size_t)(uint8_t)p.body[0] << 8) | (uint8_t)p.body[1];
    size_t pos = 2 + tl;
    if (pos > p.body.size()) return -1;
    p.topic = p.body.substr(2, tl);
    if ((p.flags >> 1) & 3) pos += 2;                // у QoS 1/2 есть номер пакета
    if (pos > p.body.size()) return -1;
    p.payload = p.body.substr(pos);
  }
  return 1;
}

// ---------- сообщения комнаты: «id \t имя \t текст» ----------
inline std::string cleanRoom(const std::string& s) {  // название комнаты -> часть адреса
  std::string o;
  for (char c : s)
    if (c != '#' && c != '+' && c != '/' && (unsigned char)c >= 32) o += c;
  while (!o.empty() && o.back() == ' ') o.pop_back();
  size_t a = 0;
  while (a < o.size() && o[a] == ' ') a++;
  return o.substr(a);
}

inline std::string roomTopic(const std::string& room) { return "dspocket/v1/" + cleanRoom(room); }

inline std::string roomMessage(const std::string& id, const std::string& nick, const std::string& text) {
  return id + "\t" + nick + "\t" + text;
}

// Обрезать строку до n байт, не разрывая символ UTF-8.
inline std::string clip(const std::string& s, size_t n) {
  if (s.size() <= n) return s;
  size_t e = n;
  while (e > 0 && ((unsigned char)s[e] & 0xC0) == 0x80) e--;
  return s.substr(0, e);
}

inline bool parseRoomMessage(const std::string& payload, std::string& id, std::string& nick, std::string& text) {
  size_t a = payload.find('\t');
  if (a == std::string::npos || a == 0 || a > 16) return false;
  size_t b = payload.find('\t', a + 1);
  if (b == std::string::npos || b == a + 1) return false;
  id = payload.substr(0, a);
  nick = clip(payload.substr(a + 1, b - a - 1), 40);
  text = clip(payload.substr(b + 1), 400);
  for (char& c : nick)
    if ((unsigned char)c < 32) c = ' ';
  for (char& c : text)
    if ((unsigned char)c < 32 && c != '\n') c = ' ';
  return !text.empty();
}

// ---------- присутствие и аватарки ----------
// <комната>/p        - «я здесь»: «id \t имя \t 1» (или 0 - ушёл), раз в минуту
// <комната>/a/<id>   - аватарка: 576 байт, 24x24 точки, по байту на точку (3 бита красный, 3 зелёный, 2 синий)
const int AVA = 24;
const size_t AVA_BYTES = AVA * AVA;

inline std::string presence(const std::string& id, const std::string& nick, bool here) {
  return id + "\t" + nick + "\t" + (here ? "1" : "0");
}

inline bool parsePresence(const std::string& payload, std::string& id, std::string& nick, bool& here) {
  size_t a = payload.find('\t');
  if (a == std::string::npos || a == 0 || a > 16) return false;
  size_t b = payload.find('\t', a + 1);
  if (b == std::string::npos || b == a + 1) return false;
  id = payload.substr(0, a);
  nick = clip(payload.substr(a + 1, b - a - 1), 40);
  for (char& c : nick)
    if ((unsigned char)c < 32) c = ' ';
  here = payload.substr(b + 1) != "0";
  return true;
}

// Адрес сообщения относительно комнаты: 0 - чат, 1 - присутствие, 2 - аватарка (id в out), -1 - чужое.
inline int classify(const std::string& base, const std::string& topic, std::string& out) {
  if (topic == base) return 0;
  if (topic == base + "/p") return 1;
  std::string pre = base + "/a/";
  if (topic.compare(0, pre.size(), pre) == 0 && topic.size() > pre.size() && topic.size() <= pre.size() + 16) {
    out = topic.substr(pre.size());
    return 2;
  }
  return -1;
}

inline uint8_t to332(int r, int g, int b) { return (uint8_t)(((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6)); }
inline uint16_t from332(uint8_t v) {
  int r = (v >> 5) & 7, g = (v >> 2) & 7, b = v & 3;
  return (uint16_t)(((r * 31 / 7) << 11) | ((g * 63 / 7) << 5) | (b * 31 / 3));
}

// Цвет кружка для тех, у кого нет своей аватарки: зависит от метки гаджета.
inline uint16_t idColor(const std::string& id) {
  static const uint16_t PAL[8] = {0xE2A8, 0x2C9F, 0x1DC9, 0xFC60, 0x9A7F, 0x0679, 0xE8E4, 0x8C71};
  uint32_t h = 2166136261u;
  for (char c : id) h = (h ^ (uint8_t)c) * 16777619u;
  return PAL[h & 7];
}

}  // namespace mqtt
