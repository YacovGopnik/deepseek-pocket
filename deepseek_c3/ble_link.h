// Bluetooth LE: связь с телефоном и пульт для телефона.
// Библиотека NimBLE-Arduino 1.4.x (Скетч -> Подключить библиотеку -> Управлять библиотеками).
// build_opt.h в папке скетча отключает ненужные роли Bluetooth, иначе прошивка не помещается.
//
// Гаджет - BLE-устройство «DS-<метка>» с двумя сервисами:
//  * UART (Nordic UART Service): телефон пишет строки - текст в поле ввода или уведомление;
//  * HID (пульт): медиа-клавиши - пуск/пауза, следующий/предыдущий трек, громкость.
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
  NimBLECharacteristic* hidIn = nullptr;
  NimBLEHIDDevice* hid = nullptr;
  int phones = 0;                       // подключённые устройства
  bool hidSubscribed = false;
  std::mutex mu;
  std::vector<std::string> phoneLines;
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

class ServerCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, ble_gap_conn_desc* d) override {
    S.phones++;
    NimBLEDevice::startAdvertising();               // остаёмся видимыми для второго устройства
  }
  void onDisconnect(NimBLEServer* s, ble_gap_conn_desc* d) override {
    if (S.phones > 0) S.phones--;
    if (S.phones == 0) S.hidSubscribed = false;
  }
};

class NusCB : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c) override { pushPhoneBytes(c->getValue()); }
};

class HidCB : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic* c, ble_gap_conn_desc* d, uint16_t sub) override { S.hidSubscribed = sub != 0; }
};

static ServerCB serverCB;
static NusCB nusCB;
static HidCB hidCB;

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
  S.nusTx = S.hidIn = nullptr;
  S.phones = 0;
  S.hidSubscribed = false;
  std::lock_guard<std::mutex> lk(S.mu);
  S.phoneLines.clear();
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

}  // namespace ble
