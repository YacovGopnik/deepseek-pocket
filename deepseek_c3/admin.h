// Администратор: подписанные команды для всех гаджетов.
//
// Ключ админа получается из логина и пароля (PBKDF2-HMAC-SHA256 -> закрытый ключ ECDSA P-256).
// В прошивку зашит только ОТКРЫТЫЙ ключ (ADMIN_PUBKEY): по нему любой гаджет проверяет подпись,
// но подделать команду без пароля нельзя, даже зная исходный код. Пароль нигде не хранится.
//
// Команда: «ADM1 \t время \t случайное число \t кому \t что \t параметр», в конце «\n» и подпись (128 hex).
// Старые (больше 10 минут) и повторённые команды не принимаются.
// Чистая логика на mbedtls (есть в пакете ESP32) - тестируется на компьютере.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "mbedtls/bignum.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/md.h"

namespace adm {

typedef int (*Rng)(void* ctx, unsigned char* buf, size_t len);

const int KDF_ITER = 10000;          // сколько раз хешировать пароль (подбор пароля - медленный)
const uint32_t MAX_AGE = 600;        // команда действует 10 минут
const char* const ALL = "*";         // «кому»: всем гаджетам

// ---------- hex ----------
inline std::string toHex(const uint8_t* b, size_t n) {
  static const char* H = "0123456789abcdef";
  std::string s;
  for (size_t i = 0; i < n; i++) {
    s += H[b[i] >> 4];
    s += H[b[i] & 15];
  }
  return s;
}

inline int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

inline bool fromHex(const std::string& h, std::vector<uint8_t>& out) {
  out.clear();
  if (h.size() % 2) return false;
  for (size_t i = 0; i < h.size(); i += 2) {
    int a = hexVal(h[i]), b = hexVal(h[i + 1]);
    if (a < 0 || b < 0) return false;
    out.push_back((uint8_t)(a * 16 + b));
  }
  return true;
}

// ---------- ключи ----------
// PBKDF2-HMAC-SHA256, один блок (32 байта).
inline bool pbkdf2(const std::string& pass, const std::string& salt, int iter, uint8_t out[32]) {
  const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!md) return false;
  mbedtls_md_context_t c;
  mbedtls_md_init(&c);
  bool ok = mbedtls_md_setup(&c, md, 1) == 0;
  uint8_t u[32];
  static const uint8_t blk[4] = {0, 0, 0, 1};
  ok = ok && mbedtls_md_hmac_starts(&c, (const uint8_t*)pass.data(), pass.size()) == 0;
  ok = ok && mbedtls_md_hmac_update(&c, (const uint8_t*)salt.data(), salt.size()) == 0;
  ok = ok && mbedtls_md_hmac_update(&c, blk, 4) == 0;
  ok = ok && mbedtls_md_hmac_finish(&c, u) == 0;
  if (ok) memcpy(out, u, 32);
  for (int i = 1; ok && i < iter; i++) {
    ok = mbedtls_md_hmac_reset(&c) == 0 && mbedtls_md_hmac_update(&c, u, 32) == 0 && mbedtls_md_hmac_finish(&c, u) == 0;
    for (int k = 0; k < 32; k++) out[k] ^= u[k];
  }
  mbedtls_md_free(&c);
  return ok;
}

inline std::string lower(const std::string& s) {
  std::string o = s;
  for (char& c : o)
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  return o;
}

// Закрытый ключ (32 байта) из логина и пароля. Логин без учёта регистра латиницы.
inline bool deriveKey(const std::string& login, const std::string& pass, uint8_t d[32]) {
  uint8_t seed[32];
  if (!pbkdf2(pass, "dspocket-admin:" + lower(login), KDF_ITER, seed)) return false;
  mbedtls_ecp_group g;
  mbedtls_mpi x, n1;
  mbedtls_ecp_group_init(&g);
  mbedtls_mpi_init(&x);
  mbedtls_mpi_init(&n1);
  bool ok = mbedtls_ecp_group_load(&g, MBEDTLS_ECP_DP_SECP256R1) == 0 && mbedtls_mpi_read_binary(&x, seed, 32) == 0 &&
            mbedtls_mpi_copy(&n1, &g.N) == 0 && mbedtls_mpi_sub_int(&n1, &n1, 1) == 0 &&
            mbedtls_mpi_mod_mpi(&x, &x, &n1) == 0 && mbedtls_mpi_add_int(&x, &x, 1) == 0 &&   // 1 .. N-1
            mbedtls_mpi_write_binary(&x, d, 32) == 0;
  mbedtls_mpi_free(&x);
  mbedtls_mpi_free(&n1);
  mbedtls_ecp_group_free(&g);
  return ok;
}

// Открытый ключ (65 байт, 130 hex) из закрытого.
inline bool publicKey(const uint8_t d[32], std::string& pubHex, Rng rng, void* rctx) {
  mbedtls_ecp_group g;
  mbedtls_ecp_point q;
  mbedtls_mpi m;
  mbedtls_ecp_group_init(&g);
  mbedtls_ecp_point_init(&q);
  mbedtls_mpi_init(&m);
  uint8_t buf[65];
  size_t olen = 0;
  bool ok = mbedtls_ecp_group_load(&g, MBEDTLS_ECP_DP_SECP256R1) == 0 && mbedtls_mpi_read_binary(&m, d, 32) == 0 &&
            mbedtls_ecp_mul(&g, &q, &m, &g.G, rng, rctx) == 0 &&
            mbedtls_ecp_point_write_binary(&g, &q, MBEDTLS_ECP_PF_UNCOMPRESSED, &olen, buf, sizeof(buf)) == 0 && olen == 65;
  if (ok) pubHex = toHex(buf, 65);
  mbedtls_mpi_free(&m);
  mbedtls_ecp_point_free(&q);
  mbedtls_ecp_group_free(&g);
  return ok;
}

inline bool sha256(const std::string& s, uint8_t h[32]) {
  const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  return md && mbedtls_md(md, (const uint8_t*)s.data(), s.size(), h) == 0;
}

// Подпись: r и s по 32 байта, 128 hex.
inline bool sign(const uint8_t d[32], const std::string& msg, std::string& sigHex, Rng rng, void* rctx) {
  uint8_t h[32];
  if (!sha256(msg, h)) return false;
  mbedtls_ecp_group g;
  mbedtls_mpi m, r, s;
  mbedtls_ecp_group_init(&g);
  mbedtls_mpi_init(&m);
  mbedtls_mpi_init(&r);
  mbedtls_mpi_init(&s);
  uint8_t out[64];
  bool ok = mbedtls_ecp_group_load(&g, MBEDTLS_ECP_DP_SECP256R1) == 0 && mbedtls_mpi_read_binary(&m, d, 32) == 0 &&
            mbedtls_ecdsa_sign(&g, &r, &s, &m, h, 32, rng, rctx) == 0 && mbedtls_mpi_write_binary(&r, out, 32) == 0 &&
            mbedtls_mpi_write_binary(&s, out + 32, 32) == 0;
  if (ok) sigHex = toHex(out, 64);
  mbedtls_mpi_free(&s);
  mbedtls_mpi_free(&r);
  mbedtls_mpi_free(&m);
  mbedtls_ecp_group_free(&g);
  return ok;
}

inline bool verify(const std::string& pubHex, const std::string& msg, const std::string& sigHex) {
  std::vector<uint8_t> pub, sig;
  if (!fromHex(pubHex, pub) || pub.size() != 65 || !fromHex(sigHex, sig) || sig.size() != 64) return false;
  uint8_t h[32];
  if (!sha256(msg, h)) return false;
  mbedtls_ecp_group g;
  mbedtls_ecp_point q;
  mbedtls_mpi r, s;
  mbedtls_ecp_group_init(&g);
  mbedtls_ecp_point_init(&q);
  mbedtls_mpi_init(&r);
  mbedtls_mpi_init(&s);
  bool ok = mbedtls_ecp_group_load(&g, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
            mbedtls_ecp_point_read_binary(&g, &q, pub.data(), pub.size()) == 0 && mbedtls_ecp_check_pubkey(&g, &q) == 0 &&
            mbedtls_mpi_read_binary(&r, sig.data(), 32) == 0 && mbedtls_mpi_read_binary(&s, sig.data() + 32, 32) == 0 &&
            mbedtls_ecdsa_verify(&g, h, 32, &q, &r, &s) == 0;
  mbedtls_mpi_free(&s);
  mbedtls_mpi_free(&r);
  mbedtls_ecp_point_free(&q);
  mbedtls_ecp_group_free(&g);
  return ok;
}

// ---------- команды ----------
// Что умеет админ:
//   ann  - объявление всем (параметр - текст)
//   upd  - обновить прошивку с GitHub          rbt - перезапуститься
//   kick - выгнать из комнаты                    bans - список заблокированных меток через запятую
//   del  - удалить сообщение (параметр - метка автора и хеш текста: «id:hash»)
//   pin  - закрепить сообщение в комнате (кому - название комнаты, параметр - текст; пусто - снять)
struct Cmd {
  uint32_t ts = 0;         // время отправки, секунды Unix
  uint32_t nonce = 0;      // случайное число: одинаковых команд не бывает
  std::string target;      // метка гаджета, «*» или (для pin) комната
  std::string cmd;
  std::string arg;
};

inline std::string clean(const std::string& s) {
  std::string o = s;
  for (char& c : o)
    if (c == '\t' || c == '\r') c = ' ';
  return o;
}

inline std::string body(const Cmd& c) {
  char b[40];
  snprintf(b, sizeof(b), "ADM1\t%lu\t%08lx\t", (unsigned long)c.ts, (unsigned long)c.nonce);
  return std::string(b) + clean(c.target) + "\t" + clean(c.cmd) + "\t" + clean(c.arg);
}

// Полное сообщение: текст команды, перевод строки, подпись.
inline std::string wrap(const std::string& b, const std::string& sigHex) { return b + "\n" + sigHex; }

inline bool unwrap(const std::string& payload, std::string& b, std::string& sigHex) {
  size_t nl = payload.rfind('\n');
  if (nl == std::string::npos || payload.size() - nl - 1 != 128) return false;
  b = payload.substr(0, nl);
  sigHex = payload.substr(nl + 1);
  return true;
}

inline bool parse(const std::string& b, Cmd& c) {
  std::vector<std::string> f;
  size_t a = 0;
  for (int i = 0; i < 5; i++) {
    size_t t = b.find('\t', a);
    if (t == std::string::npos) return false;
    f.push_back(b.substr(a, t - a));
    a = t + 1;
  }
  f.push_back(b.substr(a));                          // параметр (может содержать переводы строки)
  if (f[0] != "ADM1" || f[1].empty() || f[2].size() != 8 || f[4].empty()) return false;
  char* end = nullptr;
  c.ts = (uint32_t)strtoul(f[1].c_str(), &end, 10);
  if (*end) return false;
  c.nonce = (uint32_t)strtoul(f[2].c_str(), &end, 16);
  if (*end) return false;
  c.target = f[3];
  c.cmd = f[4];
  c.arg = f[5];
  return true;
}

// Защита от повтора: команда не старше MAX_AGE и не приходила раньше. now - секунды Unix (0 - время неизвестно).
struct Guard {
  uint32_t seen[16] = {0};
  int next = 0;
  bool fresh(const Cmd& c, uint32_t now) const {
    if (now < 1600000000UL) return false;            // часы ещё не настроены - проверить нельзя
    if (c.ts + MAX_AGE < now || c.ts > now + MAX_AGE) return false;
    for (uint32_t s : seen)
      if (s && s == (c.nonce ^ c.ts)) return false;
    return true;
  }
  void remember(const Cmd& c) {
    seen[next] = c.nonce ^ c.ts;
    next = (next + 1) % 16;
  }
};

// Хеш сообщения комнаты (для удаления): FNV-1a от «метка \t текст», 8 hex.
inline std::string msgHash(const std::string& id, const std::string& text) {
  uint32_t h = 2166136261u;
  std::string s = id + "\t" + text;
  for (char ch : s) h = (h ^ (uint8_t)ch) * 16777619u;
  char b[12];
  snprintf(b, sizeof(b), "%08lx", (unsigned long)h);
  return b;
}

inline void splitList(const std::string& s, std::vector<std::string>& out) {
  out.clear();
  size_t a = 0;
  while (a <= s.size()) {
    size_t c = s.find(',', a);
    if (c == std::string::npos) c = s.size();
    if (c > a) out.push_back(s.substr(a, c - a));
    a = c + 1;
  }
}

inline std::string joinList(const std::vector<std::string>& v) {
  std::string o;
  for (size_t i = 0; i < v.size(); i++) o += (i ? "," : "") + v[i];
  return o;
}

}  // namespace adm
