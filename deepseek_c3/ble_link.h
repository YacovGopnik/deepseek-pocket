// Bluetooth LE: связь с телефоном, пульт для телефона и игры гаджет-гаджет.
// Библиотека NimBLE-Arduino 1.4.x (Скетч -> Подключить библиотеку -> Управлять библиотеками).
//
// Гаджет - BLE-устройство «DS-<метка>» с тремя сервисами:
//  * UART (Nordic UART Service): телефон пишет строки - текст в поле ввода или уведомление;
//  * HID (пульт): медиа-клавиши - пуск/пауза, следующий/предыдущий трек, громкость;
//  * игра: строки между двумя гаджетами (у кого метка меньше - тот подключается к другому).
#pragma once
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <mutex>
#include <string>
#include <vector>

namespace ble {

static const char* NUS_SVC = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char* NUS_RX = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";   // телефон -> гаджет
static const char* NUS_TX = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";   // гаджет -> телефон
static const char* GAME_SVC = "8D2A0001-5F3B-4C8E-9B6E-2C1D0E7F4A10";
static const char* GAME_CHR = "8D2A0002-5F3B-4C8E-9B6E-2C1D0E7F4A10";

// Медиа-клавиши (HID Consumer Page)
const uint16_t KEY_PLAY = 0xCD, KEY_NEXT = 0xB5, KEY_PREV = 0xB6, KEY_VOL_UP = 0xE9, KEY_VOL_DOWN = 0xEA, KEY_MUTE = 0xE2;

static const uint8_t REPORT_MAP[] = {
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x03,  //   Logical Maximum (1023)
    0x19, 0x00,        //   Usage Minimum (0)
    0x2A, 0xFF, 0x03,  //   Usage Maximum (1023)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x00,        //   Input (Data, Array, Absolute)
    0xC0               // End Collection
};

struct State {
  bool on = false;
  std::string id;                       // своя метка
  NimBLEServer* server = nullptr;
  NimBLECharacteristic* nusTx = nullptr;
  NimBLECharacteristic* gameChr = nullptr;
  NimBLECharacteristic* hidIn = nullptr;
  NimBLEHIDDevice* hid = nullptr;
  int phones = 0;                       // подключённые не-игровые устройства
  bool hidSubscribed = false;
  // игра
  int gameConn = -1;                    // мы - сервер, к нам подключился гаджет
  NimBLEClient* client = nullptr;       // мы подключились к гаджету
  NimBLERemoteCharacteristic* peerChr = nullptr;
  std::mutex mu;
  std::vector<std::string> phoneLines, gameMsgs;
  std::string rxPart;                   // недописанная строка от телефона
};
static State S;

inline void pushPhoneBytes(const std::string& v) {
  std::lock_guard<std::mutex> lk(S.mu);
  S.rxPart += v;
  for (;;) {
    size_t nl = S.rxPart.find('\n');
    if (nl == std::string::npos) break;
    std::string line = S.rxPart.substr(0, nl);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    S.rxPart.erase(0, nl + 1);
    if (!line.empty() && S.phoneLines.size() < 16) S.phoneLines.push_back(line);
  }
  if (S.rxPart.size() > 400) {          // без перевода строки (некоторые приложения так шлют) - отдаём как есть
    if (S.phoneLines.size() < 16) S.phoneLines.push_back(S.rxPart);
    S.rxPart.clear();
  }
}

inline void pushGame(const std::string& v) {
  std::lock_guard<std::mutex> lk(S.mu);
  if (S.gameMsgs.size() < 16) S.gameMsgs.push_back(v);
}

class ServerCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, ble_gap_conn_desc* d) override {
    S.phones++;
    NimBLEDevice::startAdvertising();               // остаёмся видимыми для второго устройства
  }
  void onDisconnect(NimBLEServer* s, ble_gap_conn_desc* d) override {
    if (d && (int)d->conn_handle == S.gameConn) S.gameConn = -1;
    else if (S.phones > 0) S.phones--;
    if (S.phones == 0) S.hidSubscribed = false;
  }
};

class NusCB : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c) override { pushPhoneBytes(c->getValue()); }
};

class GameCB : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c) override { pushGame(c->getValue()); }
  void onSubscribe(NimBLECharacteristic* c, ble_gap_conn_desc* d, uint16_t sub) override {
    if (!d) return;
    if (sub) {
      if (S.gameConn != (int)d->conn_handle && S.phones > 0) S.phones--;   // это гаджет, а не телефон
      S.gameConn = d->conn_handle;
    } else if (S.gameConn == (int)d->conn_handle) {
      S.gameConn = -1;
      S.phones++;
    }
  }
};

class HidCB : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic* c, ble_gap_conn_desc* d, uint16_t sub) override { S.hidSubscribed = sub != 0; }
};

static ServerCB serverCB;
static NusCB nusCB;
static GameCB gameCB;
static HidCB hidCB;

inline void onPeerNotify(NimBLERemoteCharacteristic* c, uint8_t* data, size_t len, bool isNotify) {
  pushGame(std::string((const char*)data, len));
}

// Включить Bluetooth. id - метка гаджета (для имени «DS-<метка>»).
inline bool begin(const std::string& id) {
  if (S.on) return true;
  S.id = id;
  NimBLEDevice::init("DS-" + id);
  NimBLEDevice::setMTU(185);
  NimBLEDevice::setSecurityAuth(true, false, true);   // сопряжение без кода - нужно телефону для пульта
  S.server = NimBLEDevice::createServer();
  S.server->setCallbacks(&serverCB, false);

  NimBLEService* nus = S.server->createService(NUS_SVC);
  NimBLECharacteristic* rx = nus->createCharacteristic(NUS_RX, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rx->setCallbacks(&nusCB);
  S.nusTx = nus->createCharacteristic(NUS_TX, NIMBLE_PROPERTY::NOTIFY);
  nus->start();

  NimBLEService* game = S.server->createService(GAME_SVC);
  S.gameChr = game->createCharacteristic(GAME_CHR, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY);
  S.gameChr->setCallbacks(&gameCB);
  game->start();

  S.hid = new NimBLEHIDDevice(S.server);
  S.hidIn = S.hid->inputReport(1);
  S.hidIn->setCallbacks(&hidCB);
  S.hid->manufacturer()->setValue("DeepSeek pocket");
  S.hid->pnp(0x02, 0xe502, 0xa111, 0x0210);
  S.hid->hidInfo(0x00, 0x01);
  S.hid->reportMap((uint8_t*)REPORT_MAP, sizeof(REPORT_MAP));
  S.hid->setBatteryLevel(100);
  S.hid->startServices();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setAppearance(0x0180);                        // пульт
  adv->addServiceUUID(S.hid->hidService()->getUUID());
  adv->addServiceUUID(NimBLEUUID(GAME_SVC));
  adv->setScanResponse(true);
  adv->start();
  S.on = true;
  return true;
}

// Выключить и освободить память (Wi-Fi и защищённым соединениям нужна память).
inline void end() {
  if (!S.on) return;
  NimBLEDevice::deinit(true);
  delete S.hid;
  S.hid = nullptr;
  S.on = false;
  S.server = nullptr;
  S.nusTx = S.gameChr = S.hidIn = nullptr;
  S.phones = 0;
  S.hidSubscribed = false;
  S.gameConn = -1;
  S.client = nullptr;
  S.peerChr = nullptr;
  std::lock_guard<std::mutex> lk(S.mu);
  S.phoneLines.clear();
  S.gameMsgs.clear();
  S.rxPart.clear();
}

inline bool active() { return S.on; }
inline bool phoneConnected() { return S.on && S.phones > 0; }
inline bool remoteReady() { return S.on && S.hidSubscribed; }

inline bool takePhoneLine(std::string& line) {
  std::lock_guard<std::mutex> lk(S.mu);
  if (S.phoneLines.empty()) return false;
  line = S.phoneLines.front();
  S.phoneLines.erase(S.phoneLines.begin());
  return true;
}

inline void sendPhone(const std::string& s) {
  if (!S.on || !S.nusTx) return;
  for (size_t i = 0; i < s.size(); i += 180) {
    S.nusTx->setValue(s.substr(i, 180));
    S.nusTx->notify();
  }
}

// Нажать и отпустить медиа-клавишу на телефоне.
inline void media(uint16_t usage) {
  if (!remoteReady()) return;
  uint8_t on[2] = {(uint8_t)(usage & 0xFF), (uint8_t)(usage >> 8)}, off[2] = {0, 0};
  S.hidIn->setValue(on, 2);
  S.hidIn->notify();
  delay(20);
  S.hidIn->setValue(off, 2);
  S.hidIn->notify();
}

// ---------- игра ----------
inline bool gameLinked() {
  if (!S.on) return false;
  if (S.client && S.client->isConnected() && S.peerChr) return true;
  return S.gameConn >= 0;
}

inline void gameSend(const std::string& m) {
  if (!S.on) return;
  if (S.client && S.client->isConnected() && S.peerChr) {
    S.peerChr->writeValue((const uint8_t*)m.data(), m.size(), true);
  } else if (S.gameConn >= 0) {
    S.gameChr->setValue(m);
    S.gameChr->notify();
  }
}

inline bool gameTake(std::string& m) {
  std::lock_guard<std::mutex> lk(S.mu);
  if (S.gameMsgs.empty()) return false;
  m = S.gameMsgs.front();
  S.gameMsgs.erase(S.gameMsgs.begin());
  return true;
}

inline void gameDrop() {
  if (S.client) {
    if (S.client->isConnected()) S.client->disconnect();
    NimBLEDevice::deleteClient(S.client);
    S.client = nullptr;
    S.peerChr = nullptr;
  }
  if (S.gameConn >= 0 && S.server) S.server->disconnect((uint16_t)S.gameConn);
  S.gameConn = -1;
  std::lock_guard<std::mutex> lk(S.mu);
  S.gameMsgs.clear();
}

// Один шаг поиска соперника (около секунды). Подключаемся сами, только если наша метка меньше;
// иначе ждём, пока подключатся к нам. Возвращает имя найденного гаджета («DS-...») или пустую строку.
inline std::string gameSearchStep() {
  if (!S.on || gameLinked()) return "";
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  NimBLEScanResults res = scan->start(1, false);
  std::string found;
  for (int i = 0; i < res.getCount(); i++) {
    NimBLEAdvertisedDevice d = res.getDevice(i);
    if (!d.isAdvertisingService(NimBLEUUID(GAME_SVC)) || !d.haveName()) continue;
    std::string name = d.getName();
    if (name.compare(0, 3, "DS-") != 0) continue;
    std::string peer = name.substr(3);
    if (peer == S.id) continue;
    found = name;
    if (S.id < peer) {                                // подключаемся мы
      NimBLEClient* c = NimBLEDevice::createClient();
      c->setConnectTimeout(5);
      if (c->connect(d.getAddress())) {
        NimBLERemoteService* rs = c->getService(GAME_SVC);
        NimBLERemoteCharacteristic* rc = rs ? rs->getCharacteristic(GAME_CHR) : nullptr;
        if (rc && rc->canNotify() && rc->subscribe(true, onPeerNotify)) {
          S.client = c;
          S.peerChr = rc;
          break;
        }
        c->disconnect();
      }
      NimBLEDevice::deleteClient(c);
    }
    break;
  }
  scan->clearResults();
  return found;
}

}  // namespace ble
