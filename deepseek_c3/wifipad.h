// Пульт и клавиатура с телефона по Wi-Fi.
// Гаджет сам отдаёт страницу http://<адрес гаджета>/ ; страница держит WebSocket /ws и шлёт:
//   "d U" / "d D" / "d L" / "d R" - ручку наклонили (держат), "u" - отпустили;
//   "b1" / "b0" - кнопку нажали / отпустили;  "t:<текст>" - вставить текст, "s:<текст>" - вставить и OK;
//   "p" - «я на связи» (пока что-то нажато, страница шлёт его каждые 0,4 с).
// Пропала связь - всё нажатое отпускается само.
#pragma once
#include <WiFi.h>
#include <string>
#include <vector>
#include "mbedtls/sha1.h"
#include "mbedtls/base64.h"

namespace wpad {

extern const char PAGE[] PROGMEM;

struct State {
  WiFiServer srv{80};
  bool listening = false, paused = false;
  WiFiClient ws;
  bool wsOn = false;
  char dir = 0;                         // 'U' 'D' 'L' 'R' или 0
  bool btn = false;
  uint32_t lastRx = 0, lastPoll = 0;
  std::string rx;                       // принятые, ещё не разобранные байты
  std::vector<std::string> lines;       // текст для поля ввода ("T:..." / "S:...")
};
static State S;

inline void releaseAll() {
  S.dir = 0;
  S.btn = false;
}

inline void dropWs() {
  if (S.wsOn) S.ws.stop();
  S.wsOn = false;
  S.rx.clear();
  releaseAll();
  WiFi.setSleep(true);
}

inline void sendText(const char* t) {
  if (!S.wsOn) return;
  size_t n = strlen(t);
  if (n > 125) n = 125;
  uint8_t h[2] = {0x81, (uint8_t)n};
  S.ws.write(h, 2);
  S.ws.write((const uint8_t*)t, n);
}

inline void onMessage(const std::string& m) {
  S.lastRx = millis();
  if (m.size() >= 3 && m[0] == 'd') {
    S.dir = m[2];
  } else if (m == "u") {
    S.dir = 0;
  } else if (m == "b1") {
    S.btn = true;
  } else if (m == "b0") {
    S.btn = false;
  } else if (m.size() > 2 && (m[0] == 't' || m[0] == 's') && m[1] == ':') {
    if (S.lines.size() < 8) S.lines.push_back(std::string(m[0] == 't' ? "T:" : "S:") + m.substr(2));
  }
}

// Разобрать готовые кадры WebSocket из S.rx.
inline void parseFrames() {
  for (;;) {
    if (S.rx.size() < 2) return;
    const uint8_t* b = (const uint8_t*)S.rx.data();
    uint8_t op = b[0] & 0x0F;
    bool masked = b[1] & 0x80;
    size_t len = b[1] & 0x7F, pos = 2;
    if (len == 126) {
      if (S.rx.size() < 4) return;
      len = (b[2] << 8) | b[3];
      pos = 4;
    } else if (len == 127) {                          // такие длинные нам не шлют
      dropWs();
      return;
    }
    size_t need = pos + (masked ? 4 : 0) + len;
    if (S.rx.size() < need) return;
    std::string payload(len, '\0');
    const uint8_t* mask = b + pos;
    const uint8_t* data = b + pos + (masked ? 4 : 0);
    for (size_t i = 0; i < len; i++) payload[i] = (char)(masked ? data[i] ^ mask[i & 3] : data[i]);
    S.rx.erase(0, need);
    if (op == 0x8) {                                  // закрыть
      dropWs();
      return;
    }
    if (op == 0x9) {                                  // ping -> pong
      uint8_t h[2] = {0x8A, (uint8_t)(len > 125 ? 125 : len)};
      S.ws.write(h, 2);
      S.ws.write((const uint8_t*)payload.data(), h[1]);
      continue;
    }
    if (op == 0x1) onMessage(payload);
  }
}

// Новое подключение: страница или WebSocket.
inline void handleHttp(WiFiClient& c) {
  std::string req;
  uint32_t t = millis();
  while (millis() - t < 400 && req.size() < 1500 && req.find("\r\n\r\n") == std::string::npos) {
    int a = c.available();
    if (a > 0) {
      char buf[256];
      int n = c.read((uint8_t*)buf, a > 256 ? 256 : a);
      if (n > 0) req.append(buf, n);
    } else {
      delay(1);
    }
  }
  size_t k = req.find("Sec-WebSocket-Key:");
  if (req.compare(0, 7, "GET /ws") == 0 && k != std::string::npos) {
    size_t s = k + 18;
    while (s < req.size() && req[s] == ' ') s++;
    size_t e = req.find("\r\n", s);
    std::string key = req.substr(s, e - s) + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    uint8_t sha[20];
    mbedtls_sha1((const uint8_t*)key.data(), key.size(), sha);
    uint8_t acc[32];
    size_t olen = 0;
    mbedtls_base64_encode(acc, sizeof(acc), &olen, sha, 20);
    if (S.wsOn) dropWs();                             // новый телефон заменяет прежний
    c.print("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ");
    c.write(acc, olen);
    c.print("\r\n\r\n");
    c.setNoDelay(true);
    S.ws = c;
    S.wsOn = true;
    S.lastRx = millis();
    releaseAll();
    WiFi.setSleep(false);                             // без энергосбережения Wi-Fi нажатия доходят без задержек
    sendText("hi");
    return;
  }
  if (req.compare(0, 6, "GET / ") == 0 || req.compare(0, 6, "GET /?") == 0) {
    size_t n = strlen_P(PAGE);
    c.printf("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %u\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n",
             (unsigned)n);
    for (size_t i = 0; i < n; i += 1024) c.write((const uint8_t*)PAGE + i, n - i < 1024 ? n - i : 1024);
  } else {
    c.print("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
  }
  c.flush();
  c.stop();
}

// Вызывается из опроса ручки и кнопки - то есть постоянно, где бы ни был гаджет.
inline void poll() {
  uint32_t now = millis();
  if (now - S.lastPoll < 4) return;
  S.lastPoll = now;
  if (S.paused) return;
  if (!S.listening) {
    if (WiFi.status() != WL_CONNECTED) return;
    S.srv.begin();
    S.srv.setNoDelay(true);
    S.listening = true;
  }
  WiFiClient c = S.srv.accept();
  if (c) handleHttp(c);
  if (S.wsOn) {
    if (!S.ws.connected()) {
      dropWs();
      return;
    }
    int a = S.ws.available();
    while (a > 0 && S.rx.size() < 2048) {
      char buf[128];
      int n = S.ws.read((uint8_t*)buf, a > 128 ? 128 : a);
      if (n <= 0) break;
      S.rx.append(buf, n);
      a -= n;
    }
    parseFrames();
    if ((S.dir || S.btn) && millis() - S.lastRx > 1500) releaseAll();   // страница замолчала - отпустить
  }
}

// На время настройки Wi-Fi (там свой сервер на порту 80) пульт выключается.
inline void pause(bool on) {
  S.paused = on;
  if (on) {
    dropWs();
    if (S.listening) S.srv.end();
    S.listening = false;
  }
}

inline char heldDir() { return S.dir; }
inline bool heldBtn() { return S.btn; }
inline bool connected() { return S.wsOn; }
inline bool takeLine(std::string& l) {
  if (S.lines.empty()) return false;
  l = S.lines.front();
  S.lines.erase(S.lines.begin());
  return true;
}

const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="ru"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>DeepSeek pocket - пульт</title>
<style>
:root{--bg:#14171b;--card:#1d2127;--t:#e9edf1;--m:#98a1ab;--a:#4f7dff;--ok:#4cc785;--err:#ff7b7b;--l:#2d333b}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{margin:0;background:var(--bg);color:var(--t);font:16px/1.4 system-ui,sans-serif;-webkit-user-select:none;user-select:none}
main{max-width:460px;margin:0 auto;padding:12px}
#st{color:var(--m);font-size:14px;margin-bottom:10px}#st.ok{color:var(--ok)}#st.err{color:var(--err)}
.pad{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin:8px 0 16px;touch-action:none}
.k{background:var(--card);border:1px solid var(--l);border-radius:18px;aspect-ratio:1;display:flex;align-items:center;justify-content:center;font-size:34px;color:var(--t)}
.k.on{background:var(--a);border-color:var(--a)}
.ok{border-radius:50%;font-size:22px;font-weight:600}
.row{display:flex;gap:8px;margin-top:8px}
button{flex:1;font:inherit;border:0;border-radius:12px;padding:12px;background:var(--a);color:#fff}
button.s{background:transparent;color:var(--a);border:1px solid var(--a)}
textarea{width:100%;min-height:70px;font:inherit;color:var(--t);background:var(--card);border:1px solid var(--l);border-radius:12px;padding:10px;-webkit-user-select:text;user-select:text}
small{color:var(--m)}
</style></head><body><main>
<div id="st">подключаюсь…</div>
<div class="pad">
<div></div><div class="k" data-d="U">▲</div><div></div>
<div class="k" data-d="L">◀</div><div class="k ok" id="okb">OK</div><div class="k" data-d="R">▶</div>
<div></div><div class="k" data-d="D">▼</div><div></div>
</div>
<div class="row"><button class="s" id="long">Долгое нажатие</button></div>
<p><small>Держите стрелку - как наклон ручки. OK - как нажатие ручки; если держать дольше полсекунды, получится долгое нажатие.</small></p>
<textarea id="tx" placeholder="Текст для гаджета"></textarea>
<div class="row"><button id="send">Отправить</button><button class="s" id="ins">Только вставить</button></div>
<p><small>«Отправить» - как набрать на гаджете и нажать OK. «Только вставить» - текст появится в поле ввода.</small></p>
</main><script>
let ws=null,held=0;
const st=document.getElementById("st");
function S(t,c){st.textContent=t;st.className=c||""}
function tx(m){if(ws&&ws.readyState==1)ws.send(m)}
function conn(){
 ws=new WebSocket("ws://"+location.host+"/ws");
 ws.onopen=()=>S("подключено к гаджету","ok");
 ws.onclose=()=>{S("нет связи с гаджетом - переподключаюсь…","err");setTimeout(conn,1000)};
 ws.onerror=()=>ws.close();
}
setInterval(()=>{if(held)tx("p")},400);
function bind(el,down,up){
 el.addEventListener("pointerdown",e=>{e.preventDefault();el.setPointerCapture(e.pointerId);el.classList.add("on");held++;down();if(navigator.vibrate)navigator.vibrate(10)});
 const end=e=>{if(!el.classList.contains("on"))return;el.classList.remove("on");held=Math.max(0,held-1);up()};
 el.addEventListener("pointerup",end);el.addEventListener("pointercancel",end);el.addEventListener("lostpointercapture",end);
}
document.querySelectorAll("[data-d]").forEach(el=>bind(el,()=>tx("d "+el.dataset.d),()=>tx("u")));
bind(document.getElementById("okb"),()=>tx("b1"),()=>tx("b0"));
document.getElementById("long").onclick=()=>{tx("b1");held++;setTimeout(()=>{tx("b0");held--},900)};
function sendText(p){const t=document.getElementById("tx");const v=t.value.replace(/\r?\n/g," ").trim();if(!v)return;tx(p+v.slice(0,400));t.value=""}
document.getElementById("send").onclick=()=>sendText("s:");
document.getElementById("ins").onclick=()=>sendText("t:");
document.addEventListener("contextmenu",e=>e.preventDefault());
conn();
</script></body></html>)HTML";

}  // namespace wpad
