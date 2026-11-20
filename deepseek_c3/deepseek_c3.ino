// DeepSeek на ESP32-C3 mini + дисплей ST7789 240x240 (без CS) + энкодер.
// Экранная клавиатура, ответ с прокруткой, языки: русский, English, 中文.
// Настройка Wi-Fi и API-ключа с телефона (точка доступа DeepSeek-Setup).
//
// Библиотеки (Менеджер библиотек): "Adafruit ST7735 and ST7789 Library"
// (вместе с Adafruit GFX и BusIO) и "ArduinoJson" версии 7.x.
// Плата: ESP32C3 Dev Module, USB CDC On Boot: Enabled,
// Partition Scheme: Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS), Erase All Flash: Disabled.
//
// Управление аналоговым джойстиком (модуль KY-023): наклон - перемещение,
// нажатие - ввод, долгое нажатие (больше 0,7 с) на клавиатуре - меню.
// Над клавиатурой строка подсказок: джойстик вверх, нажатие - вставить слово.
// Подсказки учатся на ваших вопросах и ответах DeepSeek (сохраняются во флеш-память).
//
// Подключение джойстика: GND - GND, +5V - 3V3 (обязательно 3,3 В, не 5 В!),
// VRx - GPIO0, VRy - GPIO1, SW - GPIO7.
// Экран: RES подключить к 3V3 (сброс программный), GPIO3 занят измерением батареи.
// Батарея: плюс (после выключателя) - 100 кОм - GPIO3 - 100 кОм - GND.
//
// Калькулятор: 2nd - вторая страница клавиш; переменная x, график y(x), решение уравнений x=? (см. README).
// Чат помнит последние сообщения (DeepSeek видит переписку). Меню - «Новый чат».
// Меню «Игры»: Блоки, 2048, Змейка, Падающие блоки, Лабиринт, Память, Пинг-понг, Летящая птичка
// псевдо-3D Гонка и игры с DeepSeek: Викторина, Приключение, «Угадай, кто я». Рекорды сохраняются,
// партии Блоков и 2048 продолжаются даже после выключения.

#include <Arduino.h>
#include <SPI.h>

// Плата: ESP32-C3 Super Mini (как раньше) или ESP32-S3 (S3-Zero / S3 Super Mini / DevKitC-1) - выбирается
// при сборке (плата в Arduino IDE), распиновка подставляется сама.
#if CONFIG_IDF_TARGET_ESP32S3
#define BOARD_S3 1
#else
#define BOARD_S3 0
#endif
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_ota_ops.h>
#include <esp_image_format.h>
#include <Update.h>
#include "driver/gpio.h"
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <vector>
#include <functional>
#include <LittleFS.h>
#include <SD.h>

#include "font8x16.h"
// Китайский язык и иероглифы: 1 - есть, 0 - нет (экономит около 730 КБ; без этого прошивка
// не помещается в половину памяти, а это нужно для обновления по воздуху).
#ifndef ENABLE_CJK
#define ENABLE_CJK 0
#endif
#if ENABLE_CJK
#include "font_cjk.h"
#endif
#include "font12x24.h"
#if ENABLE_CJK
#include "font_cjk24.h"
#endif
#include "textutil.h"
#include "suggest.h"
#include "dict_words.h"
#include "blocks_game.h"
#include "game2048.h"
#include "snake_game.h"
#include "falling_game.h"
#include "arcade_games.h"
#include "ai_games.h"
#include "race_game.h"
#include "pixabay.h"
#include "calc.h"
#include "calc_solve.h"
#include "admin.h"
// Bluetooth (телефон и пульт) занимает ~250 КБ и в разметку с обновлением по воздуху на 4 МБ не помещается.
// 1 - включить (нужна библиотека NimBLE-Arduino 1.4.x и плата с флешем побольше или без части приложений).
#ifndef ENABLE_BLE
#define ENABLE_BLE 0
#endif
// Сборка «с Bluetooth» (ENABLE_BLE 1) не помещается целиком: в ней нет части приложений (ниже).
// Полная сборка - всё, кроме Bluetooth. Обе собирает GitHub; на гаджете: Настройки -> Обновление -> Сменить сборку.
// Игровая сборка (GAMES_EDITION 1): только игры, комната, телефон-пульт и настройки - место под большие игры.
#ifndef GAMES_EDITION
#define GAMES_EDITION 0
#endif
#if GAMES_EDITION && ENABLE_BLE
#error "GAMES_EDITION and ENABLE_BLE cannot be on together"
#endif
#define FULL_EDITION (!ENABLE_BLE)
#define EDITION_ID (GAMES_EDITION ? 2 : ENABLE_BLE ? 1 : 0)   // 0 полная, 1 с Bluetooth, 2 игровая
#ifndef ENABLE_WEBIMG
#define ENABLE_WEBIMG (FULL_EDITION && !GAMES_EDITION)                   // картинки из интернета (галерея с телефона есть всегда)
#endif
#ifndef ENABLE_CHESS
#define ENABLE_CHESS FULL_EDITION
#endif
#ifndef ENABLE_BIGGAMES
#define ENABLE_BIGGAMES FULL_EDITION                 // Подземелье 3D, Гонки, Райдер
#endif
#ifndef ENABLE_AIGAMES
#define ENABLE_AIGAMES FULL_EDITION                  // Викторина и История с DeepSeek
#endif
#ifndef ENABLE_IR
#define ENABLE_IR (FULL_EDITION && !GAMES_EDITION)                       // ИК-пульт (GPIO8)
#endif
#ifndef ENABLE_TORCH
#define ENABLE_TORCH (FULL_EDITION && !GAMES_EDITION)                    // фонарик (GPIO2)
#endif
#if ENABLE_BLE
#include "ble_link.h"
#else
namespace ble {                                      // заглушка: Bluetooth выключен при сборке
const uint16_t KEY_PLAY = 0, KEY_NEXT = 0, KEY_PREV = 0, KEY_VOL_UP = 0, KEY_VOL_DOWN = 0;
inline bool begin(const std::string&) { return false; }
inline void end() {}
inline bool active() { return false; }
inline bool phoneConnected() { return false; }
inline bool remoteReady() { return false; }
inline bool takePhoneLine(std::string&) { return false; }
inline void sendPhone(const std::string&) {}
inline void media(uint16_t) {}
}  // namespace ble
#endif
#ifndef ENABLE_FWBACKUP
#define ENABLE_FWBACKUP (!(ENABLE_BLE && !BOARD_S3))
#define ENABLE_JOYCAL (!(ENABLE_BLE && !BOARD_S3))    // калибровка джойстика (в C3 с Bluetooth нет места)  // копии прошивки на SD-карту (в C3 с Bluetooth нет места)
#endif
#ifndef ENABLE_PCSCREEN
#define ENABLE_PCSCREEN FULL_EDITION                 // «Экран ПК» (картинки с компьютера по USB)
#endif
#ifndef ENABLE_RASP
#define ENABLE_RASP (FULL_EDITION && !GAMES_EDITION)  // расписание занятий с сайта вуза
#endif
#if ENABLE_RASP
#include "rasp.h"
#endif
#ifndef ENABLE_SANDBOX
#define ENABLE_SANDBOX FULL_EDITION                  // «Песочница» (мир из блоков)
#endif
#if ENABLE_SANDBOX
#include "sandbox_game.h"
#include <new>
#endif
#ifndef ENABLE_WTTR
#define ENABLE_WTTR (FULL_EDITION && !GAMES_EDITION)                     // запасной сервер погоды wttr.in (~20 КБ)
#endif
#ifndef ENABLE_WIFIPAD
#define ENABLE_WIFIPAD FULL_EDITION                  // пульт и клавиатура с телефона по Wi-Fi
#endif
#if ENABLE_WIFIPAD
#include "wifipad.h"
#else
namespace wpad {                                     // заглушка
inline void poll() {}
inline void pause(bool) {}
inline char heldDir() { return 0; }
inline bool heldBtn() { return false; }
inline bool connected() { return false; }
inline bool takeLine(std::string&) { return false; }
}  // namespace wpad
#endif
// Текст с телефона: по Bluetooth или по Wi-Fi.
// (макрос, а не функция: функция здесь сбила бы Arduino IDE, которая вставляет объявления перед первой функцией)
#define phoneTakeLine(l) (ble::takePhoneLine(l) || wpad::takeLine(l))
#include <esp_now.h>
#include <esp_wifi.h>
#include "esp_random.h"
#include "tools.h"
#include "games2.h"
#include "chess.h"
#include "chess_pieces.h"
#include "reference.h"
#include "mqtt_lite.h"
#include "rider_game.h"
#include "shooter3d.h"
#include "ir_remote.h"
#include <time.h>

// Поиск картинок (Wikimedia Commons). Нужна библиотека TJpg_Decoder (Менеджер библиотек, автор Bodmer).
// Если не хотите её ставить - поставьте 0: всё остальное соберётся и будет работать.
#define ENABLE_IMAGES 1
#if ENABLE_IMAGES
#include <TJpg_Decoder.h>
#endif

// ========================= НАСТРОЙКИ =========================
// Можно оставить пустыми и настроить всё с телефона.
// То, что сохранено через телефон, важнее того, что вписано здесь.
const char* DEFAULT_SSID    = "";   // имя сети Wi-Fi (только 2,4 ГГц)
const char* DEFAULT_PASS    = "";   // пароль Wi-Fi
const char* DEFAULT_API_KEY = "";   // ключ с platform.deepseek.com (sk-...)

const char* AP_NAME = "DeepSeek-Setup";              // сеть для настройки с телефона
const char* API_URL = "https://api.deepseek.com/chat/completions";
const int MAX_TOKENS = 400;                          // длина ответа
const int HTTP_TIMEOUT_MS = 30000;                   // сколько ждать ответ

// Джойстик. Если направления перепутаны - поменяйте эти три настройки.
const bool JOY_SWAP_XY = false;   // true, если вверх/вниз и влево/вправо поменялись местами
const bool JOY_INVERT_X = true;   // true, если влево и вправо перепутаны
const bool JOY_INVERT_Y = false;   // true, если вверх и вниз перепутаны (ещё есть переключатель: Настройки -> Ось Y джойстика)
const int JOY_THRESHOLD = 1100;   // насколько отклонить ручку (из 2048), чтобы сработало
const uint32_t REPEAT_DELAY_MS = 400;  // удержание: через сколько начинается автоповтор
const uint32_t REPEAT_RATE_MS = 110;   // удержание: шаг автоповтора
const int TFT_ROTATION = 2;     // 0..3; для 240x240 обычно 2
const uint32_t TFT_SPI_HZ = 40000000;   // скорость шины экрана; если появились полосы/мусор - поставьте 20000000
const uint32_t LONG_PRESS_MS = 700;
// «Комната» - общий чат таких гаджетов. Все, у кого здесь одинаковые сервер и комната, видят друг друга.
#define MQTT_HOST "broker.emqx.io"   // открытый сервер сообщений (MQTT, порт 1883, без шифрования)
#define MQTT_PORT 1883
#define ROOM_DEFAULT "lobby"         // общая комната для всех гаджетов с этой прошивкой

#define LOW_TX_POWER 1          // 1 = занизить мощность Wi-Fi (помогает платам Super Mini)

// Пины
// Сброс экрана (RES) и индикатор батареи. Прошивка одна для обеих схем и разбирается сама:
//  - старая схема: RES экрана на GPIO3, процентов батареи нет;
//  - схема с батареей: RES экрана на GPIO21 (на плате подписан 21 или TX), а на GPIO3 - делитель 100к/100к от вывода 5V.
// При включении сброс подаётся на обе ножки, потом по напряжению на GPIO3 видно, есть ли там делитель.
// Оставлять RES на 3V3 нельзя: экрану без вывода CS нужен настоящий сброс, иначе он не запускается.
// BATTERY_MODE: -1 - определять самой (обычно так), 0 - батареи нет, 1 - батарея есть (если определяется неверно).
#ifndef BATTERY_MODE
#define BATTERY_MODE -1
#endif
bool BATTERY_ENABLED = false;      // выясняется при включении

// Пищалка (необязательно): «+» на GPIO20 (на плате подписан 20 или RX), «-» на GND.
// Пассивная (пьезо, умеет разные ноты) - false; активная (со своим генератором, только «бип») - true.
// Если пищалки нет - ничего менять не нужно: звук просто не будет слышен.
#if BOARD_S3
const int PIN_BUZ = 44;                              // S3: RX
#else
const int PIN_BUZ = 20;
#endif
const bool BUZZER_ACTIVE = false;

// ИК-светодиод для «Пульта» (необязательно): GPIO8 -> база транзистора (S9014 и подобные, NPN),
// коллектор -> 5V, эмиттер -> «+» светодиода (длинная ножка), «-» светодиода -> резистор 33-68 Ом -> GND.
// Если светодиода нет - ничего менять не нужно. На GPIO8 сидит и синий огонёк платы: при отправке он мигает.
#if BOARD_S3
const int PIN_IR = 43;                               // S3: TX
#else
const int PIN_IR = 8;
#endif
const int IR_TRIM_US = 3;          // поправка на время работы самих команд, мкс (подстройка несущей частоты)
#define IR_DB_REPO "logickworkshop/Flipper-IRDB"   // база пультов на GitHub
#define IR_DB_BRANCH "main"

// Обновление по воздуху. FW_VERSION - номер этой прошивки: увеличивайте на 1 в каждой новой версии.
// Гаджет сравнивает его с числом в файле firmware/version.txt вашего репозитория и, если там больше,
// скачивает firmware/firmware.bin. Нужна разметка памяти «Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)».
#ifndef FW_VERSION
#define FW_VERSION 1                // при сборке на GitHub номер подставляется сам
#endif
#define OTA_REPO "YacovGopnik/deepseek-pocket"
// Администратор (Настройки -> Администратор). Здесь - ОТКРЫТЫЙ ключ админа (130 знаков hex), его можно публиковать:
// по нему гаджеты проверяют подпись команд. Получить: Настройки -> Администратор -> Создать ключ (ключ появится
// на экране и в мониторе порта). Пусто - вход админа отключён, команды не принимаются.
#ifndef ADMIN_PUBKEY
#define ADMIN_PUBKEY "04c6080c912c43330b4110a82623d310da3f0946166dfa617f464eeb68097650a6c7a0800dfdd286a89cb05e93409f38a99baad0fc16f7e45b135540ff9625ff00"
#endif
#define OTA_BRANCH "main"

// Фонарик (необязательно): белый светодиод, включается в Настройках.
// GPIO2 -> резистор 22 кОм -> база NPN-транзистора (S9014); ещё один резистор 4,7 кОм с GPIO2 на 3V3
// (он нужен, чтобы плата нормально запускалась: при включении на GPIO2 должно быть «высокое» напряжение).
// Эмиттер -> GND, коллектор -> «-» светодиода, «+» светодиода -> резистор 47 Ом -> 5V.
// При включении платы светодиод на мгновение вспыхивает - это нормально.
#if BOARD_S3
const int PIN_LED = 18;                              // S3: площадка 18 снизу
#else
const int PIN_LED = 2;
#endif
bool torchOn = false;

#if BOARD_S3
// ESP32-S3 (S3-Zero): экран SCL 12, SDA 11, DC 10, RES 9, BLK 8; батарея - делитель на GPIO3
const int PIN_SCK = 12, PIN_MOSI = 11, PIN_DC = 10, PIN_BLK = 8;
const int PIN_RST_A = 9, PIN_RST_B = 3;
const int PIN_BAT = 3;
#else
const int PIN_SCK = 4, PIN_MOSI = 6, PIN_DC = 5, PIN_BLK = 10;
const int PIN_RST_A = 21, PIN_RST_B = 3;        // сброс экрана: GPIO21 (схема с батареей) или GPIO3 (старая схема)
const int PIN_BAT = 3;                          // середина делителя напряжения батареи
#endif

// Батарея
const float BAT_DIVIDER = 2.0;       // 100 кОм + 100 кОм = делит пополам
const float BAT_CALIBRATION = 1.0;   // поправка: (напряжение по тестеру) / (показания платы)

// Память чата: сколько последних сообщений (ваших и ответов) отправлять DeepSeek
const int MAX_HISTORY_MSGS = 10;          // 5 вопросов и 5 ответов
const unsigned MAX_HISTORY_CHARS = 5000;  // и не больше этого объёма текста
#if BOARD_S3
const int PIN_JOY_X = 1, PIN_JOY_Y = 2, PIN_SW = 7;
#else
const int PIN_JOY_X = 0, PIN_JOY_Y = 1, PIN_SW = 7;
#endif

// Имена моделей пробуются по очереди, рабочее запоминается.
struct Attempt { const char* model; bool noThink; };
const Attempt ATTEMPTS[] = {
  {"deepseek-flash", true},
  {"deepseek-flash", false},
  {"deepseek-v4-flash", false},
  {"deepseek-chat", false},
};
const int N_ATTEMPTS = sizeof(ATTEMPTS) / sizeof(ATTEMPTS[0]);
int bestAttempt = 0;

// Клавиша экранной клавиатуры.
enum KeyKind : uint8_t { K_CHAR, K_DEL, K_LAYER, K_SEND };
struct Key {
  uint16_t cp;
  KeyKind kind;
  const char* label;   // nullptr = рисуем сам символ
};

// Событие кнопки.
enum BtnEvent : uint8_t { EV_NONE, EV_CLICK, EV_LONG };

// Сообщение переписки с DeepSeek (вы или ответ).
struct ChatMsg {
  bool user;
  String text;
};

// Направление джойстика. Функции принимают и возвращают uint8_t, а не свой тип:
// так Arduino IDE 1.8 не спотыкается при автоматическом объявлении функций.
const uint8_t J_NONE = 0, J_UP = 1, J_DOWN = 2, J_LEFT = 3, J_RIGHT = 4;

// ========================= ТЕКСТЫ НА ТРЁХ ЯЗЫКАХ =========================
enum Lang : uint8_t { LANG_RU = 0, LANG_EN = 1, LANG_ZH = 2 };
const int N_LANGS = 3;

struct Texts {
  const char* langName;
  const char* starting;
  const char* wifiTry;        // %d
  const char* wifiFail;
  const char* noSettings;
  const char* thinking;
  const char* errPrefix;
  const char* err401;
  const char* err402;
  const char* err429;
  const char* errConn;
  const char* errMem;
  const char* errTimeout;     // %d
  const char* errNet;
  const char* errBadReply;
  const char* errEmpty;
  const char* errNoKey;
  const char* menuTitle;
  const char* menuLang;
  const char* menuWifi;
  const char* menuBack;
  const char* wifiNone;
  const char* portal;         // %s = имя сети
  const char* portalSaved;
  const char* webTitle;
  const char* webNetwork;
  const char* webOther;
  const char* webManual;
  const char* webPass;
  const char* webKey;
  const char* webKeyHint;
  const char* webSave;
  const char* webDone;
  const char* webNoSsid;
  const char* systemPrompt;
  const char* menuForget;
  const char* forgetDone;
  const char* menuNewChat;
  const char* chatCleared;
  const char* youLabel;
  const char* botLabel;
  const char* chatCount;      // %d
  const char* menuGame;
  const char* gameScore;      // %u
  const char* gameBest;       // %u
  const char* gameHintPick;
  const char* gameHintPlace;
  const char* gameOver;
  const char* gameAgain;
  const char* menuGames;
  const char* gBlocks;
  const char* gSnake;
  const char* gFall;
  const char* hint2048;
  const char* hintSnake;
  const char* paused;
  const char* nextLabel;
  const char* linesFmt;       // %u
  const char* levelFmt;       // %u
  const char* fallRotate;
  const char* fallDrop;
  const char* fallExit;
  const char* won2048;
  const char* promptLang;     // язык для DeepSeek (по-английски)
  const char* gMaze;
  const char* gMemory;
  const char* gPong;
  const char* gFlappy;
  const char* gQuiz;
  const char* gStory;
  const char* gGuess;
  const char* mazeDone;       // %u %u
  const char* timeFmt;        // %u
  const char* movesFmt;       // %u
  const char* memDone;        // %u
  const char* pongYou;
  const char* pongCpu;
  const char* pongWin;
  const char* pongLose;
  const char* flapHint;
  const char* topicTitle;
  const char* quizTopics;     // через |, последний - «своя тема»
  const char* genreTitle;
  const char* storyGenres;
  const char* guessTitle;
  const char* guessCats;
  const char* typeTopic;
  const char* quizNum;        // %d
  const char* right;
  const char* wrongIs;
  const char* quizResult;     // %d
  const char* storyWhat;
  const char* storyEnd;
  const char* guessRound;     // %d
  const char* guessMore;
  const char* guessRight;     // %d
  const char* guessTotal;     // %d
  const char* aiBadFormat;
  const char* clickNext;
  const char* wifiWait;
  const char* wifiNoConn;
  const char* wifiRetry;
  const char* gRace;
  const char* raceKmh;        // %u
  const char* raceMeters;     // %u
  const char* raceCheck;      // %u
  const char* raceOver;
  const char* raceDist;       // %u
  const char* raceStart;
  const char* menuImages;
  const char* webPxKey;
  const char* imgNoKey;
  const char* imgPrompt;
  const char* imgNone;
  const char* imgLoading;
  const char* imgError;
  const char* imgHelp;
  const char* imgBy;          // %s
  const char* imgSearching;
  const char* imgByWiki;      // %s
};

const Texts TEXTS[N_LANGS] = {
  {  // ---------- Русский ----------
    "Русский",
    "Запуск...",
    "Wi-Fi: попытка %d из 3...",
    "Не удалось подключиться к Wi-Fi.\n\nНажмите кнопку, чтобы попробовать ещё раз.\nДолгое нажатие - настроить Wi-Fi.",
    "Wi-Fi ещё не настроен.",
    "Думаю...",
    "Ошибка: ",
    "Неверный API-ключ (401). Укажите ключ в настройке Wi-Fi.",
    "Нет денег на балансе DeepSeek (402). Пополните счёт.",
    "Слишком много запросов (429). Подождите минуту.",
    "Нет соединения с сервером.",
    "Не хватает памяти.",
    "Таймаут: сервер не ответил за %d с.",
    "Сетевая ошибка ",
    "Неожиданный ответ сервера.",
    "Пустой ответ. Увеличьте MAX_TOKENS.",
    "Не задан API-ключ. Долгое нажатие - меню - Настроить Wi-Fi.",
    "Меню",
    "Язык: ",
    "Wi-Fi и ключ",
    "Назад",
    "Wi-Fi: нет связи",
    "Настройка Wi-Fi\n\n1. На телефоне подключитесь к сети:\n%s\n\n2. Откройте страницу 192.168.4.1 (обычно она откроется сама).\n\n3. Выберите сеть и введите пароль.\n\nКнопка - отмена.",
    "Сохранено. Перезапуск...",
    "Настройка DeepSeek",
    "Сеть Wi-Fi",
    "Другая сеть (ввести вручную)",
    "Имя сети, если её нет в списке",
    "Пароль Wi-Fi",
    "API-ключ DeepSeek",
    "оставьте пустым, чтобы не менять",
    "Сохранить",
    "Сохранено! Устройство перезапускается и подключается к сети.",
    "Не выбрана сеть. Вернитесь назад.",
    "Отвечай по-русски, кратко (до 400 символов), простым текстом: "
    "без markdown и эмодзи. Формулы пиши обычными символами (x², √, π, ·, ≤), без LaTeX.",
    "Забыть слова",
    "Выученные слова удалены.",
    "Новый чат",
    "Начат новый чат. DeepSeek забыл прошлую переписку.",
    "Вы:",
    "DeepSeek:",
    "Чат: %d",
    "Игра «Блоки»",
    "Счёт: %u",
    "Рекорд: %u",
    "Нажать-взять, держать-выход",
    "Нажать-ставить, держать-отмена",
    "Игра окончена",
    "Нажать-заново, держать-выход",
    "Игры",
    "Блоки",
    "Змейка",
    "Падающие блоки",
    "Наклон-сдвиг, держать-выход",
    "Нажать-старт, держать-выход",
    "Пауза",
    "Далее",
    "Линии: %u",
    "Уровень: %u",
    "Вверх-поворот",
    "Нажать-сброс",
    "Держать-выход",
    "2048! Играем дальше",
    "Russian",
    "Лабиринт",
    "Память",
    "Пинг-понг",
    "Летящая птичка",
    "Викторина",
    "Приключение",
    "Угадай, кто я",
    "Уровень %u пройден за %u с!",
    "%u с",
    "Ходы: %u",
    "Все пары найдены! Ходов: %u",
    "Вы",
    "Плата",
    "Победа!",
    "Поражение",
    "Нажать-взмах, держать-выход",
    "Тема викторины",
    "Любая тема|Наука|История|География|Кино и книги|Спорт|Животные|Своя тема...",
    "Жанр истории",
    "Фэнтези|Космос|Детектив|Ужасы|Пираты|Своя тема...",
    "Что загадать?",
    "Животное|Персонаж кино и книг|Страна|Историческая личность|Предмет|Своя тема...",
    "Наберите тему и нажмите OK",
    "Вопрос %d из 10",
    "Верно!",
    "Неверно. Правильно: ",
    "Результат: %d из 10",
    "Что делать?",
    "Конец истории",
    "Раунд %d из 5",
    "Ещё подсказка",
    "Верно! +%d",
    "Итого очков: %d",
    "DeepSeek ответил не по формату. Попробуйте ещё раз.",
    "Нажать - дальше",
    "Подключаюсь к Wi-Fi...\n\nНажать - отмена\nДержать - поиграть, пока ждём",
    "Нет связи",
    "Повторить",
    "Гонка",
    "%u км/ч",
    "%u м",
    "Чекпойнт! +%u с",
    "Время вышло",
    "Дистанция: %u м",
    "Нажать-старт, вниз-тормоз",
    "Картинки",
    "Ключ Pixabay (необязательно, из России не работает без VPN)",
    "Для картинок нужен ключ Pixabay. Получите его бесплатно на pixabay.com/api/docs и введите в Меню - Wi-Fi и ключ.",
    "Что найти? Наберите запрос и нажмите OK.",
    "Ничего не нашлось. Попробуйте другой запрос.",
    "Загружаю картинку...",
    "Не удалось загрузить: ",
    "Влево/вправо - листать, нажать - новый поиск, держать - выход",
    "Pixabay, автор: %s",
    "Ищу картинки...",
    "Wikimedia, автор: %s",
  },
  {  // ---------- English ----------
    "English",
    "Starting...",
    "Wi-Fi: attempt %d of 3...",
    "Could not connect to Wi-Fi.\n\nPress the button to try again.\nLong press - set up Wi-Fi.",
    "Wi-Fi is not set up yet.",
    "Thinking...",
    "Error: ",
    "Wrong API key (401). Enter the key in Wi-Fi setup.",
    "No money on the DeepSeek balance (402). Top it up.",
    "Too many requests (429). Wait a minute.",
    "No connection to the server.",
    "Out of memory.",
    "Timeout: no reply in %d s.",
    "Network error ",
    "Unexpected server reply.",
    "Empty reply. Increase MAX_TOKENS.",
    "No API key. Long press - menu - Set up Wi-Fi.",
    "Menu",
    "Language: ",
    "Wi-Fi and key",
    "Back",
    "Wi-Fi: not connected",
    "Wi-Fi setup\n\n1. On your phone, connect to the network:\n%s\n\n2. Open 192.168.4.1 (it usually opens by itself).\n\n3. Choose your network and enter the password.\n\nButton - cancel.",
    "Saved. Restarting...",
    "DeepSeek setup",
    "Wi-Fi network",
    "Other network (type it)",
    "Network name, if it is not listed",
    "Wi-Fi password",
    "DeepSeek API key",
    "leave empty to keep the current one",
    "Save",
    "Saved! The device restarts and connects to the network.",
    "No network chosen. Go back.",
    "Answer in English, briefly (up to 400 characters), in plain text: "
    "no markdown or emoji. Write formulas with plain symbols (x², √, π, ·, ≤), no LaTeX.",
    "Forget words",
    "Learned words deleted.",
    "New chat",
    "New chat started. DeepSeek forgot the previous conversation.",
    "You:",
    "DeepSeek:",
    "Chat: %d",
    "Blocks game",
    "Score: %u",
    "Best: %u",
    "Click-take, hold-exit",
    "Click-place, hold-cancel",
    "Game over",
    "Click-again, hold-exit",
    "Games",
    "Blocks",
    "Snake",
    "Falling blocks",
    "Tilt-move, hold-exit",
    "Click-start, hold-exit",
    "Pause",
    "Next",
    "Lines: %u",
    "Level: %u",
    "Up-rotate",
    "Click-drop",
    "Hold-exit",
    "2048! Keep going",
    "English",
    "Maze",
    "Memory",
    "Ping-pong",
    "Flying bird",
    "Quiz",
    "Adventure",
    "Guess who",
    "Level %u done in %u s!",
    "%u s",
    "Moves: %u",
    "All pairs found! Moves: %u",
    "You",
    "Board",
    "You win!",
    "You lose",
    "Click-flap, hold-exit",
    "Quiz topic",
    "Any topic|Science|History|Geography|Movies and books|Sports|Animals|Own topic...",
    "Story genre",
    "Fantasy|Space|Detective|Horror|Pirates|Own topic...",
    "What to guess?",
    "Animal|Movie or book character|Country|Historical figure|Object|Own topic...",
    "Type a topic and press OK",
    "Question %d of 10",
    "Correct!",
    "Wrong. Answer: ",
    "Result: %d of 10",
    "What will you do?",
    "The end",
    "Round %d of 5",
    "Another hint",
    "Correct! +%d",
    "Total score: %d",
    "DeepSeek replied in a wrong format. Try again.",
    "Click - next",
    "Connecting to Wi-Fi...\n\nClick - cancel\nHold - play while waiting",
    "No connection",
    "Retry",
    "Race",
    "%u km/h",
    "%u m",
    "Checkpoint! +%u s",
    "Time is up",
    "Distance: %u m",
    "Click-start, down-brake",
    "Images",
    "Pixabay key (optional)",
    "Image search needs a Pixabay key. Get one for free at pixabay.com/api/docs and enter it in Menu - Wi-Fi and key.",
    "What to find? Type a query and press OK.",
    "Nothing found. Try another query.",
    "Loading image...",
    "Could not load: ",
    "Left/right - browse, click - new search, hold - exit",
    "Pixabay, by %s",
    "Searching images...",
    "Wikimedia, by %s",
  },
  {  // ---------- 中文 ----------
    "中文",
    "启动中……",
    "Wi-Fi：第%d次尝试（共3次）……",
    "无法连接Wi-Fi。\n\n按下按钮重试。\n长按：设置Wi-Fi。",
    "尚未设置Wi-Fi。",
    "思考中……",
    "错误：",
    "API密钥错误（401）。请在Wi-Fi设置中填写密钥。",
    "DeepSeek余额不足（402）。请充值。",
    "请求过多（429）。请稍等一分钟。",
    "无法连接服务器。",
    "内存不足。",
    "超时：服务器%d秒内未响应。",
    "网络错误 ",
    "服务器响应异常。",
    "回复为空。请增大MAX_TOKENS。",
    "未设置API密钥。长按进入菜单，选择设置Wi-Fi。",
    "菜单",
    "语言：",
    "设置Wi-Fi和密钥",
    "返回",
    "Wi-Fi：未连接",
    "Wi-Fi设置\n\n1. 用手机连接网络：\n%s\n\n2. 打开192.168.4.1（通常会自动打开）。\n\n3. 选择网络并输入密码。\n\n按钮：取消。",
    "已保存。正在重启……",
    "DeepSeek设置",
    "Wi-Fi网络",
    "其他网络（手动输入）",
    "网络名称（如果不在列表中）",
    "Wi-Fi密码",
    "DeepSeek API密钥",
    "留空则保持不变",
    "保存",
    "已保存！设备正在重启并连接网络。",
    "未选择网络。请返回。",
    "请用简体中文简短回答（不超过150字），使用纯文本，"
    "不要使用markdown和表情符号。公式用普通符号（x²、√、π、·、≤），不要用LaTeX。",
    "清除学习的词语",
    "已删除学习的词语。",
    "新对话",
    "已开始新对话。DeepSeek已忘记之前的聊天内容。",
    "你：",
    "DeepSeek：",
    "对话：%d",
    "方块游戏",
    "得分：%u",
    "最高：%u",
    "按下：选取，长按：退出",
    "按下：放置，长按：取消",
    "游戏结束",
    "按下：重来，长按：退出",
    "游戏",
    "方块",
    "贪吃蛇",
    "落块",
    "倾斜：移动，长按：退出",
    "按下：开始，长按：退出",
    "暂停",
    "下一个",
    "行数：%u",
    "等级：%u",
    "上：旋转",
    "按下：落下",
    "长按：退出",
    "2048！继续游戏",
    "Simplified Chinese",
    "迷宫",
    "记忆",
    "乒乓球",
    "飞鸟",
    "问答",
    "冒险",
    "猜猜我是谁",
    "第%u关完成，用时%u秒！",
    "%u秒",
    "步数：%u",
    "全部配对！步数：%u",
    "你",
    "电脑",
    "你赢了！",
    "你输了",
    "按下：拍翅，长按：退出",
    "问答主题",
    "任意主题|科学|历史|地理|电影和书|体育|动物|自定义...",
    "故事类型",
    "奇幻|太空|侦探|恐怖|海盗|自定义...",
    "猜什么？",
    "动物|影视或书中角色|国家|历史人物|物品|自定义...",
    "输入主题后按OK",
    "第%d题，共10题",
    "正确！",
    "错误。正确答案：",
    "结果：%d/10",
    "你要怎么做？",
    "故事结束",
    "第%d轮，共5轮",
    "再给提示",
    "正确！+%d",
    "总分：%d",
    "DeepSeek的回答格式不对，请重试。",
    "按下：继续",
    "正在连接Wi-Fi……\n\n按下：取消\n长按：等待时玩游戏",
    "无网络",
    "重试",
    "赛车",
    "%u公里/时",
    "%u米",
    "检查点！+%u秒",
    "时间到",
    "距离：%u米",
    "按下：开始，下：刹车",
    "图片",
    "Pixabay密钥（可选）",
    "搜索图片需要Pixabay密钥。请在pixabay.com/api/docs免费获取，并在菜单-Wi-Fi和密钥中填写。",
    "要找什么？输入关键词后按OK。",
    "没有找到。请换个关键词。",
    "正在加载图片……",
    "加载失败：",
    "左右：翻看，按下：重新搜索，长按：退出",
    "Pixabay，作者：%s",
    "正在搜索图片……",
    "Wikimedia，作者：%s",
  },
};

// ========================= СОХРАНЁННЫЕ НАСТРОЙКИ =========================
Preferences prefs;
String cfgSsid, cfgPass, cfgKey;                    // cfgSsid/cfgPass - сеть, к которой подключаемся сейчас
// Запомненные сети (до 5): дом, телефон, школа... Плата по очереди пробует их, пока не подключится.
const size_t MAX_NETS = 5;
std::vector<String> netSsid, netPass;
int netIdx = 0;                                      // какую сеть пробуем сейчас
int cfgSaver = 1;                                    // заставка с часами на экране ввода: 1 - да, 0 - нет
extern String cfgNick, cfgRoom, roomId;              // «Комната»: имя, название комнаты, метка гаджета
extern int cfgRoomBg, roomUnread;
extern bool hsDirty;                                 // рекорд обновился - отправить в общую таблицу
bool timeNow(struct tm& t);
extern bool roomUnreadDirty;
void roomTick();
void loadAvatar();
void (*g_bgTick)() = nullptr;                        // фоновая работа, пока человек набирает текст (приём сообщений комнаты)
uint8_t cfgLang = LANG_RU;

const Texts& T() { return TEXTS[cfgLang]; }

// Короткая надпись на текущем языке (для новых экранов).
#if ENABLE_CJK
const char* L3(const char* ru, const char* en, const char* zh) {
  return cfgLang == LANG_RU ? ru : cfgLang == LANG_ZH ? zh : en;
}
#else
const char* L2(const char* ru, const char* en) { return cfgLang == LANG_RU ? ru : en; }
#define L3(ru, en, zh) L2(ru, en)                    // китайские надписи в прошивку не попадают
#endif

const char* APP_UA = "DeepSeekPocket/1.0 (ESP32-C3 hobby device)";   // так устройство представляется сайтам
int cfgBright = 4;        // яркость: 0..4 (8, 25, 50, 75, 100 %)
int cfgSound = 1;         // звук: 0 - нет, 1 - только сигналы, 2 - сигналы и щелчки клавиш
int cfgIdle = 2;          // гасить экран: 0 - нет, 1 - 30 с, 2 - 1 мин, 3 - 2 мин, 4 - 5 мин
int cfgJoy = 2;           // чувствительность ручки: 0 (надо отклонять до упора) .. 4 (хватает лёгкого наклона)
int cfgSd = 0;            // подключена SD-карта (вместо ИК-пульта, фонарика и пищалки)
int cfgBle = 0;           // Bluetooth включён (Настройки -> Bluetooth)
int cfgRot = 0;           // поворот экрана: 0..3 шага по 90° (Настройки -> Поворот экрана); ручка поворачивается вместе с ним
int cfgTouch = 0;         // 1 - сенсорные кнопки (площадки из меди) работают как ручка (только S3)
int cfgTilt = 0;          // 1 - наклон гаджета управляет в играх (датчик ADXL345, только S3)
int cfgJoyInvY = 1;       // 1 - ось Y ручки перевёрнута относительно JOY_INVERT_Y (Настройки -> Ось Y джойстика)
// Насколько отклонить ручку (из 2048), чтобы сработало: середина шкалы - JOY_THRESHOLD.
int joyThr() {
  static const int PCT[5] = {150, 125, 100, 70, 45};
  int t = JOY_THRESHOLD * PCT[cfgJoy] / 100;
  return t > 1850 ? 1850 : t < 250 ? 250 : t;
}
int cfgTz = 3;            // часовой пояс, часов от UTC (Москва +3)
String cfgCity;           // город для погоды
float cfgLat = 0, cfgLon = 0;

void loadSettings() {
  prefs.begin("dsk", true);
  cfgSsid = prefs.getString("ssid", DEFAULT_SSID);
  cfgPass = prefs.getString("pass", DEFAULT_PASS);
  cfgSaver = (int)prefs.getUChar("saver", 1);
  cfgNick = prefs.getString("nick", "");
  cfgRoom = prefs.getString("room", ROOM_DEFAULT);
  cfgRoomBg = (int)prefs.getUChar("roombg", 1);
  if (!cfgRoom.length()) cfgRoom = ROOM_DEFAULT;
  {
    char b[12];
    snprintf(b, sizeof(b), "%06x", (unsigned)((ESP.getEfuseMac() >> 24) & 0xFFFFFF));   // метка этого гаджета
    roomId = b;
  }
  {                                                  // список сетей: имя \x1F пароль \x1E ...
    std::string d = prefs.getString("nets", "").c_str();
    netSsid.clear();
    netPass.clear();
    size_t p = 0;
    while (p < d.size() && netSsid.size() < MAX_NETS) {
      size_t e = d.find('\x1E', p);
      if (e == std::string::npos) e = d.size();
      std::string rec = d.substr(p, e - p);
      size_t m = rec.find('\x1F');
      if (m != std::string::npos && m > 0) {
        netSsid.push_back(String(rec.substr(0, m).c_str()));
        netPass.push_back(String(rec.substr(m + 1).c_str()));
      }
      p = e + 1;
    }
    if (netSsid.empty() && cfgSsid.length()) {       // сеть из прошлой версии прошивки (или из настроек скетча)
      netSsid.push_back(cfgSsid);
      netPass.push_back(cfgPass);
    }
    netIdx = 0;
    cfgSsid = netSsid.empty() ? String("") : netSsid[0];
    cfgPass = netSsid.empty() ? String("") : netPass[0];
  }
  cfgKey = prefs.getString("key", DEFAULT_API_KEY);
  cfgLang = prefs.getUChar("lang", LANG_RU);
  cfgBright = (int)prefs.getUChar("bri", 4);
  cfgSound = (int)prefs.getUChar("snd", 1);
  if (cfgSound < 0 || cfgSound > 2) cfgSound = 1;
  cfgIdle = (int)prefs.getUChar("idle", 2);
  cfgJoy = (int)prefs.getUChar("joy", 2);
  if (cfgJoy < 0 || cfgJoy > 4) cfgJoy = 2;
  cfgJoyInvY = prefs.getUChar("joyiy", 1) ? 1 : 0;
  cfgTilt = prefs.getUChar("tilt", 0) ? 1 : 0;
  cfgTouch = prefs.getUChar("touch", 0) ? 1 : 0;
  cfgRot = prefs.getUChar("rot", 0) & 3;
  cfgBle = prefs.getUChar("ble", 0) ? 1 : 0;
  cfgSd = prefs.getUChar("sd", 0) ? 1 : 0;
  cfgTz = (int)prefs.getUChar("tz", 3 + 12) - 12;
  cfgCity = prefs.getString("city", "");
  cfgLat = prefs.getString("lat", "0").toFloat();
  cfgLon = prefs.getString("lon", "0").toFloat();
  prefs.end();
  if (cfgLang >= N_LANGS || (!ENABLE_CJK && cfgLang == LANG_ZH)) cfgLang = LANG_RU;
  if (cfgBright < 0 || cfgBright > 4) cfgBright = 4;
  if (cfgIdle < 0 || cfgIdle > 4) cfgIdle = 2;
  if (cfgTz < -12 || cfgTz > 14) cfgTz = 3;
}

void saveScreenSettings() {
  prefs.begin("dsk", false);
  prefs.putUChar("saver", (uint8_t)cfgSaver);
  prefs.putUChar("snd", (uint8_t)cfgSound);
  prefs.putUChar("bri", (uint8_t)cfgBright);
  prefs.putUChar("idle", (uint8_t)cfgIdle);
  prefs.putUChar("joy", (uint8_t)cfgJoy);
  prefs.putUChar("joyiy", (uint8_t)cfgJoyInvY);
  prefs.putUChar("tilt", (uint8_t)cfgTilt);
  prefs.putUChar("touch", (uint8_t)cfgTouch);
  prefs.putUChar("rot", (uint8_t)cfgRot);
  prefs.putUChar("ble", (uint8_t)cfgBle);
  prefs.putUChar("sd", (uint8_t)cfgSd);
  prefs.putUChar("tz", (uint8_t)(cfgTz + 12));
  prefs.end();
}

void saveLang() {
  prefs.begin("dsk", false);
  prefs.putUChar("lang", cfgLang);
  prefs.end();
}

void storeNets() {
  std::string d;
  for (size_t i = 0; i < netSsid.size(); i++) d += std::string(netSsid[i].c_str()) + '\x1F' + netPass[i].c_str() + '\x1E';
  prefs.begin("dsk", false);
  prefs.putString("nets", String(d.c_str()));
  prefs.putString("ssid", netSsid.empty() ? String("") : netSsid[0]);
  prefs.putString("pass", netSsid.empty() ? String("") : netPass[0]);
  prefs.end();
}

// Запомнить сеть (или обновить пароль). Новая становится первой; лишняя (самая давняя) забывается.
void addNet(const String& ssid, const String& pass) {
  for (size_t i = 0; i < netSsid.size(); i++)
    if (netSsid[i] == ssid) {
      netSsid.erase(netSsid.begin() + i);
      netPass.erase(netPass.begin() + i);
      break;
    }
  netSsid.insert(netSsid.begin(), ssid);
  netPass.insert(netPass.begin(), pass);
  while (netSsid.size() > MAX_NETS) {
    netSsid.pop_back();
    netPass.pop_back();
  }
  storeNets();
}

void removeNet(size_t i) {
  if (i >= netSsid.size()) return;
  netSsid.erase(netSsid.begin() + i);
  netPass.erase(netPass.begin() + i);
  storeNets();
}

void saveKey(const String& key) {
  prefs.begin("dsk", false);
  prefs.putString("key", key);
  prefs.end();
}

String fmt(const char* f, int v) {
  char b[160];
  snprintf(b, sizeof(b), f, v);
  return String(b);
}

String fmt(const char* f, const char* v) {
  char b[800];
  snprintf(b, sizeof(b), f, v);
  return String(b);
}

std::string toStd(const String& s) { return std::string(s.c_str()); }
String fromStd(const std::string& s) { return String(s.c_str()); }

// ========================= ДИСПЛЕЙ =========================
const uint16_t C_BLACK = 0x0000, C_WHITE = 0xFFFF, C_BLUE = 0x001F;
const uint16_t C_RED = 0xF800, C_YELLOW = 0xFFE0, C_GRAY = 0x4228, C_GREEN = 0x07E0;
const uint16_t C_LIGHT = 0xAD55;
const uint16_t C_BAR = 0x18E4;      // строка состояния и заголовки окон
const uint16_t C_SEL = 0x2B3E;      // выбранный пункт
const uint16_t C_ACCENT = 0x04B2;   // тонкая линия под заголовком

// Два шрифта: крупный 12x24 (основной) и мелкий 8x16 (служебные строки).
// GW/GH/COLS/ROWS - размеры текущего шрифта; переключать через SmallFont.
bool g_big = true;
#define GW (g_big ? BIG_W : GLYPH_W)
#define GH (g_big ? BIG_H : GLYPH_H)
#define COLS (240 / GW)      // крупный: 20 позиций (10 иероглифов), мелкий: 30
#define ROWS (240 / GH)      // крупный: 10 строк, мелкий: 15

// Пока объект жив - рисуем мелким шрифтом.
struct SmallFont {
  bool prev;
  SmallFont() : prev(g_big) { g_big = false; }
  ~SmallFont() { g_big = prev; }
};

Adafruit_ST7789 tft = Adafruit_ST7789(&SPI, -1, PIN_DC, -1);  // без CS; сброс делаем сами (displayReset)
static uint16_t cbuf[24 * 24];

// Номер иероглифа в font_cjk.h (двоичный поиск) или -1.
int cjkIndex(uint16_t cp) {
#if !ENABLE_CJK
  (void)cp;
  return -1;
#else
  int lo = 0, hi = CJK_COUNT - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    uint16_t v = pgm_read_word(&CJK_CODES[mid]);
    if (v == cp) return mid;
    if (v < cp) lo = mid + 1;
    else hi = mid - 1;
  }
  return -1;
#endif
}

void blit(int x, int y, int w, int h) {
  tft.startWrite();
  tft.setAddrWindow(x, y, w, h);
  tft.writePixels(cbuf, (uint32_t)(w * h));
  tft.endWrite();
}

// Рисует символ текущим шрифтом, возвращает его ширину в пикселях.
int drawCp(uint16_t cp, int x, int y, uint16_t fg, uint16_t bg) {
  if (isWide(cp)) {
    int idx = cjkIndex(cp);
#if ENABLE_CJK
    if (idx >= 0) {
      if (g_big) {                            // иероглиф 24x24: 3 байта на строку
        const uint8_t* g = &CJK24_BITMAPS[idx * 72];
        for (int r = 0; r < 24; r++) {
          uint32_t bits = ((uint32_t)pgm_read_byte(g + r * 3) << 16) |
                          ((uint32_t)pgm_read_byte(g + r * 3 + 1) << 8) | pgm_read_byte(g + r * 3 + 2);
          for (int c = 0; c < 24; c++) cbuf[r * 24 + c] = (bits & (0x800000UL >> c)) ? fg : bg;
        }
        blit(x, y, 24, 24);
        return 24;
      }
      const uint8_t* g = &CJK_BITMAPS[idx * 32];
      for (int r = 0; r < 16; r++) {
        uint16_t bits = ((uint16_t)pgm_read_byte(g + r * 2) << 8) | pgm_read_byte(g + r * 2 + 1);
        for (int c = 0; c < 16; c++) cbuf[r * 16 + c] = (bits & (0x8000 >> c)) ? fg : bg;
      }
      blit(x, y, 16, 16);
      return 16;
    }
#endif
    (void)idx;
    drawCp('?', x, y, fg, bg);            // нет в шрифте: знак вопроса на 2 позиции
    tft.fillRect(x + GW, y, GW, GH, bg);
    return 2 * GW;
  }
  int gi = glyphIndex(cp);
  if (gi < 0) gi = '?' - 32;
  if (g_big) {                              // 12x24: 2 байта на строку, 12 бит слева
    const uint8_t* g = &FONT12x24[gi * 48];
    for (int r = 0; r < 24; r++) {
      uint16_t bits = ((uint16_t)pgm_read_byte(g + r * 2) << 8) | pgm_read_byte(g + r * 2 + 1);
      for (int c = 0; c < 12; c++) cbuf[r * 12 + c] = (bits & (0x8000 >> c)) ? fg : bg;
    }
    blit(x, y, 12, 24);
    return 12;
  }
  const uint8_t* g = &FONT8x16[gi * 16];
  for (int r = 0; r < 16; r++) {
    uint8_t b = pgm_read_byte(g + r);
    for (int c = 0; c < 8; c++) cbuf[r * 8 + c] = (b & (0x80 >> c)) ? fg : bg;
  }
  blit(x, y, 8, 16);
  return 8;
}

// Рисует строку из n символов и стирает остаток строки экрана.
void drawLine(const uint16_t* cps, int n, int y, uint16_t fg, uint16_t bg) {
  int x = 0;
  for (int i = 0; i < n && x + cellWidth(cps[i]) * GW <= 240; i++) x += drawCp(cps[i], x, y, fg, bg);
  if (x < 240) tft.fillRect(x, y, 240 - x, GH, bg);
}

// Рисует короткую строку UTF-8 с позиции x (без переноса), возвращает конечный x.
int drawUtf8(const char* s, int x, int y, uint16_t fg, uint16_t bg) {
  std::vector<uint16_t> cps;
  utf8ToCps(s, cps);
  for (uint16_t c : cps) {
    if (x + cellWidth(c) * GW > 240) break;
    x += drawCp(c, x, y, fg, bg);
  }
  return x;
}

int textWidth(const char* s) {
  std::vector<uint16_t> cps;
  utf8ToCps(s, cps);
  int w = 0;
  for (uint16_t c : cps) w += cellWidth(c) * GW;
  return w;
}

void renderRows(const std::vector<uint16_t>& cps, const std::vector<Line>& lines,
                int top, uint16_t fg) {
  for (int r = 0; r < ROWS; r++) {
    size_t idx = (size_t)(top + r);
    if (idx < lines.size()) drawLine(cps.data() + lines[idx].start, lines[idx].len, r * GH, fg, C_BLACK);
    else tft.fillRect(0, r * GH, 240, GH, C_BLACK);
  }
}

void showMsg(const String& s, uint16_t fg) {
  std::vector<uint16_t> cps;
  utf8ToCps(s.c_str(), cps);
  std::vector<Line> lines;
  bool prevBig = g_big;
  g_big = true;
  wrapCps(cps, COLS, lines);
  if ((int)lines.size() > ROWS) {          // длинный текст (например, инструкция Wi-Fi) - мелким шрифтом
    g_big = false;
    lines.clear();
    wrapCps(cps, COLS, lines);
  }
  tft.fillScreen(C_BLACK);
  for (size_t i = 0; i < lines.size() && (int)i < ROWS; i++)
    drawLine(cps.data() + lines[i].start, lines[i].len, (int)i * GH, fg, C_BLACK);
  g_big = prevBig;
}

// ========================= ЗВУК =========================
// Короткие сигналы пищалкой. Все звуки короткие и проигрываются «на месте» - зависнуть со включённым тоном нельзя.
// Когда экран спрятан или погас, гаджет молчит.
extern bool screenAsleep;

#if BOARD_S3
// S3: кроме пищалки, звук идёт на I2S-усилитель MAX98357A (BCLK 14, LRC 15, DIN 16; микрофон INMP441 - SD 17).
// Отдельная задача всё время отдаёт усилителю звук: синус нужной частоты с плавным нарастанием (без щелчков)
// или тишину.
#include <ESP_I2S.h>
const int I2S_BCLK = 14, I2S_WS = 15, I2S_DOUT = 16, I2S_DIN = 17;
const int AUD_RATE = 22050;
I2SClass audioI2s;
volatile int audFreq = 0;
volatile int audVol = 6000;                          // громкость (амплитуда, до 32767)
bool audOk = false;

void audioTask(void*) {
  static int16_t SINE[64];
  for (int i = 0; i < 64; i++) SINE[i] = (int16_t)(sinf(i * 6.2831853f / 64) * 32767);
  int16_t buf[256];
  uint32_t phase = 0;
  int amp = 0;                                       // текущая амплитуда - плавно идёт к нужной
  for (;;) {
    int f = audFreq;
    int target = f > 0 ? audVol : 0;
    uint32_t step = f > 0 ? (uint32_t)((uint64_t)f * 4294967296ULL / AUD_RATE) : 0;
    for (int i = 0; i < 256; i++) {
      if (amp < target) amp = amp + 40 > target ? target : amp + 40;
      else if (amp > target) amp = amp - 40 < target ? target : amp - 40;
      phase += step;
      buf[i] = amp ? (int16_t)((int32_t)SINE[phase >> 26] * amp / 32767) : 0;
    }
    audioI2s.write((uint8_t*)buf, sizeof(buf));
  }
}

void audioInit() {
  audioI2s.setPins(I2S_BCLK, I2S_WS, I2S_DOUT);
  audOk = audioI2s.begin(I2S_MODE_STD, AUD_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
  Serial.printf("I2S audio: %s\n", audOk ? "ok" : "failed");
  if (audOk) xTaskCreate(audioTask, "audio", 4096, nullptr, 3, nullptr);
}
#endif

void sndTone(int freq) {
#if BOARD_S3
  audFreq = freq > 0 ? freq : 0;
#endif
  if (cfgSd && !BOARD_S3) return;                                 // вывод пищалки занят SD-картой
  if (BUZZER_ACTIVE) digitalWrite(PIN_BUZ, freq > 0 ? HIGH : LOW);
  else ledcWriteTone(PIN_BUZ, freq > 0 ? (uint32_t)freq : 0);
}

void soundInit() {
#if BOARD_S3
  audioInit();
#endif
  if (cfgSd && !BOARD_S3) return;
  if (BUZZER_ACTIVE) {
    pinMode(PIN_BUZ, OUTPUT);
    digitalWrite(PIN_BUZ, LOW);
  } else {
    ledcAttachChannel(PIN_BUZ, 2000, 10, 2);         // свой канал и таймер, чтобы не мешать подсветке экрана
    ledcWriteTone(PIN_BUZ, 0);
  }
}

// Один «бип». level: 1 - сигнал, 2 - щелчок клавиши (играет, только если в настройках «всё»).
void beep(int level, int freq, int ms) {
  if (cfgSound < level || screenAsleep) return;
  sndTone(freq);
  delay((uint32_t)ms);
  sndTone(0);
}

// Мелодия: пары «частота, длительность»; частота 0 - пауза.
void playNotes(int level, const uint16_t* notes, int count) {
  if (cfgSound < level || screenAsleep) return;
  for (int i = 0; i < count; i++) {
    sndTone(notes[i * 2]);
    delay(notes[i * 2 + 1]);
  }
  sndTone(0);
}

void sndOk() { static const uint16_t n[] = {880, 60, 1320, 90}; playNotes(1, n, 2); }
void sndError() { static const uint16_t n[] = {300, 180}; playNotes(1, n, 1); }
void sndMessage() { static const uint16_t n[] = {1200, 70, 0, 40, 1600, 90}; playNotes(1, n, 3); }
void sndWin() { static const uint16_t n[] = {660, 90, 880, 90, 1320, 170}; playNotes(1, n, 3); }
void sndLose() { static const uint16_t n[] = {440, 110, 330, 110, 220, 220}; playNotes(1, n, 3); }
void sndBlip() { beep(1, 1500, 12); }

// ========================= ЭКРАН: ЯРКОСТЬ И СОН =========================
// Подсветка управляется ШИМ. Экран гаснет сам после простоя и сразу - если держать кнопку 2 секунды.
// Любое движение джойстика или нажатие будит экран (само это действие не срабатывает).
const int BRIGHT_PCT[5] = {8, 25, 50, 75, 100};
const int IDLE_SECS[5] = {0, 30, 60, 120, 300};      // 0 - не гасить
const uint32_t HIDE_HOLD_MS = 2000;
bool screenAsleep = false;
uint32_t lastActivity = 0;
int keepAwakeCount = 0;

void applyBrightness() { analogWrite(PIN_BLK, BRIGHT_PCT[cfgBright] * 255 / 100); }
void noteActivity() { lastActivity = millis(); }
void sleepScreen() {
  if (screenAsleep) return;
  screenAsleep = true;
  analogWrite(PIN_BLK, 0);
}
void wakeScreen() {
  screenAsleep = false;
  lastActivity = millis();
  applyBrightness();
}
bool saverArmed = false;      // мы на экране ввода: вместо гашения можно показать заставку
bool saverDue = false;        // пора показать заставку
void idleCheck() {
  if (screenAsleep || keepAwakeCount > 0 || IDLE_SECS[cfgIdle] == 0) return;
  if (millis() - lastActivity > (uint32_t)IDLE_SECS[cfgIdle] * 1000UL) {
    if (saverArmed && cfgSaver) saverDue = true;
    else sleepScreen();
  }
}
struct SaverArm {             // пока объект жив - экран ввода «на дежурстве»
  SaverArm() { saverArmed = true; saverDue = false; }
  ~SaverArm() { saverArmed = false; saverDue = false; }
};
// Пока объект жив, экран сам не гаснет (часы, таймер, показ последовательности в игре).
struct KeepAwake {
  KeepAwake() { keepAwakeCount++; noteActivity(); }
  ~KeepAwake() { keepAwakeCount--; noteActivity(); }
};

// Крупные цифры и буквы: символ шрифта 12x24, увеличенный в scale раз.
void drawBigCp(uint16_t cp, int x, int y, int scale, uint16_t fg, uint16_t bg) {
  int gi = glyphIndex(cp);
  if (gi < 0) gi = '?' - 32;
  const uint8_t* g = &FONT12x24[gi * 48];
  tft.fillRect(x, y, 12 * scale, 24 * scale, bg);
  for (int r = 0; r < 24; r++) {
    uint16_t bits = ((uint16_t)pgm_read_byte(g + r * 2) << 8) | pgm_read_byte(g + r * 2 + 1);
    int c = 0;
    while (c < 12) {                                 // закрашиваем горизонтальными отрезками
      if (!(bits & (0x8000 >> c))) {
        c++;
        continue;
      }
      int c0 = c;
      while (c < 12 && (bits & (0x8000 >> c))) c++;
      tft.fillRect(x + c0 * scale, y + r * scale, (c - c0) * scale, scale, fg);
    }
  }
}

// Строка крупными символами; prev - что уже на экране (перерисовываются только изменившиеся знаки).
void drawBigStr(const char* s, const char* prev, int x, int y, int scale, uint16_t fg, uint16_t bg) {
  std::vector<uint16_t> a, b;
  utf8ToCps(s, a);
  if (prev) utf8ToCps(prev, b);
  for (size_t i = 0; i < a.size(); i++)
    if (!prev || i >= b.size() || a[i] != b[i]) drawBigCp(a[i], x + (int)i * 12 * scale, y, scale, fg, bg);
}

// ========================= ДЖОЙСТИК И КНОПКА =========================
// Аварийный выход: кнопку держат 6 секунд - гаджет перезапускается и открывает главный экран.
// Работает отдельной задачей, поэтому срабатывает, даже если основная программа зависла
// (например, на SD-карте или в сети).
const uint32_t HOLD_RESET_MS = 6000;
const uint32_t HOLD_RESET_MAGIC = 0xB0071E5E;
RTC_NOINIT_ATTR uint32_t holdReset;                  // перезапуск был по удержанию (переживает перезапуск)
void holdWatchTask(void*) {
  uint32_t down = 0;
  for (;;) {
    if (digitalRead(PIN_SW) == LOW) {
      if (!down) down = millis();
      else if (millis() - down > HOLD_RESET_MS) {
        holdReset = HOLD_RESET_MAGIC;
        analogWrite(PIN_BLK, 0);                     // экран гаснет - видно, что сработало
        delay(200);
        esp_restart();
      }
    } else {
      down = 0;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

int joyCx = 2048, joyCy = 2048;
// Края хода ручки (Настройки -> Джойстик -> Калибровка). -1 - не калибровали: считаем от центра как есть.
int joyX0 = -1, joyX1 = -1, joyY0 = -1, joyY1 = -1;

// Значение оси: медиана трёх замеров - одиночные выбросы АЦП (дрожащий контакт) не дёргают курсор.
int joyRead(int pin) {
  int a = analogRead(pin), b = analogRead(pin), c = analogRead(pin);
  if (a > b) { int t = a; a = b; b = t; }
  if (b > c) { int t = b; b = c; c = t; }
  if (a > b) { int t = a; a = b; b = t; }
  return b;
}

// Отклонение от центра в шкале -2048..2048 с учётом откалиброванных краёв.
int joyNorm(int raw, int c, int lo, int hi) {
  int d = raw - c;
  if (lo < 0 || hi < 0) return d;
  int span = d >= 0 ? hi - c : c - lo;
  if (span < 300) return d;                          // калибровка странная - без масштаба
  long v = (long)d * 2048 / span;
  return v > 2048 ? 2048 : v < -2048 ? -2048 : (int)v;
}
#define JOY_DX() joyNorm(joyRead(PIN_JOY_X), joyCx, joyX0, joyX1)
#define JOY_DY() joyNorm(joyRead(PIN_JOY_Y), joyCy, joyY0, joyY1)

// Запоминаем положение ручки в покое (при включении джойстик не трогать). Если сохранённая калибровка есть,
// а замер при включении от неё далеко (ручку держали) - берём сохранённый центр.
void joyCalibrate() {
  long sx = 0, sy = 0;
  for (int i = 0; i < 16; i++) {
    sx += joyRead(PIN_JOY_X);
    sy += joyRead(PIN_JOY_Y);
    delay(2);
  }
  joyCx = (int)(sx / 16);
  joyCy = (int)(sy / 16);
  prefs.begin("dsk", true);
  int scx = prefs.getInt("jcx", -1), scy = prefs.getInt("jcy", -1);
  joyX0 = prefs.getInt("jx0", -1);
  joyX1 = prefs.getInt("jx1", -1);
  joyY0 = prefs.getInt("jy0", -1);
  joyY1 = prefs.getInt("jy1", -1);
  prefs.end();
  if (scx >= 0 && scy >= 0 && (abs(joyCx - scx) > 300 || abs(joyCy - scy) > 300)) {
    joyCx = scx;
    joyCy = scy;
  }
}

// Экран повёрнут на cfgRot шагов по 90°, значит, и гаджет держат повёрнутым: «вверх» для человека -
// это другая сторона ручки. Поворачиваем направление так же, как картинку.
void joyRotate(int& dx, int& dy) {
  for (int k = 0; k < cfgRot; k++) {
    int t = dx;
    dx = dy;
    dy = -t;
  }
}

// ========================= ДАТЧИК НАКЛОНА ADXL345 (только S3) =========================
// I2C: SDA - 38, SCL - 39, питание 3V3. Адрес 0x53 (SDO на GND) или 0x1D (SDO на 3V3) - находится сам.
// Как датчик повёрнут относительно экрана, неизвестно, поэтому оси задаются калибровкой (Настройки -> Наклон).
#if BOARD_S3
#include <Wire.h>
const int ACC_SDA = 38, ACC_SCL = 39;
#endif
bool accOk = false;
uint8_t accAddr = 0;
int accZero[3] = {0, 0, 0};                          // показания, когда гаджет лежит ровно
int8_t accAxX = 0, accSgX = 1, accAxY = 1, accSgY = 1;   // какая ось датчика - «вправо» и «к себе»

uint8_t accReg(uint8_t r) {
#if BOARD_S3
  Wire.beginTransmission(accAddr);
  Wire.write(r);
  if (Wire.endTransmission(false) != 0) return 0;
  if (Wire.requestFrom((int)accAddr, 1) != 1) return 0;
  return Wire.read();
#else
  (void)r;
  return 0;
#endif
}
void accSet(uint8_t r, uint8_t v) {
#if BOARD_S3
  Wire.beginTransmission(accAddr);
  Wire.write(r);
  Wire.write(v);
  Wire.endTransmission();
#else
  (void)r;
  (void)v;
#endif
}

// ---------- Гироскоп на той же шине I2C (MPU6050/6500/9250 или L3G4200D/L3GD20) ----------
// У MPU есть и свой акселерометр: если ADXL345 нет, наклон берётся с него.
uint8_t gyroAddr = 0, gyroType = 0;                  // 1 - MPU60x0/65x0/92x0, 2 - L3G
const char* gyroName = "";
float gyroBias[3] = {0, 0, 0};                       // показания в покое
int8_t gAxX = -1, gSgX = 1, gAxY = -1, gSgY = 1;     // какая ось гироскопа - «наклон вправо» и «к себе»
uint8_t accSrc = 0;                                  // откуда наклон: 1 - ADXL345, 2 - MPU

uint8_t i2cRead8(uint8_t addr, uint8_t r) {
#if BOARD_S3
  Wire.beginTransmission(addr);
  Wire.write(r);
  if (Wire.endTransmission(false) != 0) return 0xFF;
  if (Wire.requestFrom((int)addr, 1) != 1) return 0xFF;
  return Wire.read();
#else
  (void)addr;
  (void)r;
  return 0xFF;
#endif
}
void i2cWrite8(uint8_t addr, uint8_t r, uint8_t v) {
#if BOARD_S3
  Wire.beginTransmission(addr);
  Wire.write(r);
  Wire.write(v);
  Wire.endTransmission();
#else
  (void)addr;
  (void)r;
  (void)v;
#endif
}
bool i2cReadN(uint8_t addr, uint8_t r, uint8_t* b, int n) {
#if BOARD_S3
  Wire.beginTransmission(addr);
  Wire.write(r);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)addr, n) != n) return false;
  for (int i = 0; i < n; i++) b[i] = Wire.read();
  return true;
#else
  (void)addr;
  (void)r;
  (void)b;
  (void)n;
  return false;
#endif
}

// Скорость поворота по трём осям, градусы в секунду (без смещения покоя).
bool gyroRead(float g[3]) {
  uint8_t b[6];
  if (gyroType == 1) {
    if (!i2cReadN(gyroAddr, 0x43, b, 6)) return false;
    for (int i = 0; i < 3; i++) g[i] = (int16_t)((b[i * 2] << 8) | b[i * 2 + 1]) / 65.5f - gyroBias[i];
    return true;
  }
  if (gyroType == 2) {
    if (!i2cReadN(gyroAddr, 0x28 | 0x80, b, 6)) return false;
    for (int i = 0; i < 3; i++) g[i] = (int16_t)(b[i * 2] | (b[i * 2 + 1] << 8)) * 0.0175f - gyroBias[i];
    return true;
  }
  return false;
}

void gyroInit() {
  for (uint8_t a : {(uint8_t)0x68, (uint8_t)0x69}) {
    uint8_t who = i2cRead8(a, 0x75);
    if (who == 0x68 || who == 0x70 || who == 0x71 || who == 0x73 || who == 0x72 || who == 0x98) {
      gyroAddr = a;
      gyroType = 1;
      gyroName = who == 0x68 ? "MPU6050" : who == 0x70 ? "MPU6500" : "MPU9250";
      i2cWrite8(a, 0x6B, 0x00);                      // проснуться
      delay(50);
      i2cWrite8(a, 0x1A, 0x03);                      // фильтр ~44 Гц
      i2cWrite8(a, 0x1B, 0x08);                      // гироскоп +-500 град/с
      i2cWrite8(a, 0x1C, 0x00);                      // акселерометр +-2g
      break;
    }
    uint8_t l3 = i2cRead8(a, 0x0F);
    if (l3 == 0xD3 || l3 == 0xD4 || l3 == 0xD7) {
      gyroAddr = a;
      gyroType = 2;
      gyroName = l3 == 0xD3 ? "L3G4200D" : "L3GD20";
      i2cWrite8(a, 0x20, 0x0F);                      // включить, 100 Гц
      i2cWrite8(a, 0x23, 0x10);                      // +-500 град/с
      break;
    }
  }
  if (!gyroType) return;
  delay(100);
  float sum[3] = {0, 0, 0}, g[3];
  int n = 0;
  for (int i = 0; i < 50; i++) {                     // смещение покоя (при включении гаджет обычно лежит)
    if (gyroRead(g)) {
      for (int k = 0; k < 3; k++) sum[k] += g[k];
      n++;
    }
    delay(4);
  }
  if (n)
    for (int k = 0; k < 3; k++) gyroBias[k] = sum[k] / n;
  Serial.printf("gyro: %s at 0x%02X\n", gyroName, gyroAddr);
}

void accInit() {
#if BOARD_S3
  Wire.begin(ACC_SDA, ACC_SCL, 400000);
  for (uint8_t a : {(uint8_t)0x53, (uint8_t)0x1D}) {
    accAddr = a;
    if (accReg(0x00) == 0xE5) {                      // DEVID
      accSet(0x31, 0x08);                            // полное разрешение, +-2g: 256 на 1g
      accSet(0x2C, 0x0A);                            // 100 измерений в секунду
      accSet(0x2D, 0x08);                            // измерять
      accOk = true;
      accSrc = 1;
      break;
    }
  }
  Serial.printf("ADXL345: %s\n", accOk ? "ok" : "not found");
  gyroInit();
  if (!accOk && gyroType == 1) {                     // нет ADXL345 - наклон с акселерометра MPU
    accOk = true;
    accSrc = 2;
  }
  prefs.begin("dsk", true);
  accZero[0] = prefs.getShort("az0", 0);
  accZero[1] = prefs.getShort("az1", 0);
  accZero[2] = prefs.getShort("az2", 0);
  accAxX = prefs.getChar("aax", 0);
  accSgX = prefs.getChar("asx", 1);
  accAxY = prefs.getChar("aay", 1);
  accSgY = prefs.getChar("asy", 1);
  gAxX = prefs.getChar("gax", -1);
  gSgX = prefs.getChar("gsx", 1);
  gAxY = prefs.getChar("gay", -1);
  gSgY = prefs.getChar("gsy", 1);
  prefs.end();
#endif
}

// Сырые показания трёх осей (256 = 1g).
bool accRaw(int v[3]) {
#if BOARD_S3
  if (!accOk) return false;
  if (accSrc == 2) {                                 // MPU: +-2g = 16384 на 1g -> 256 на 1g
    uint8_t b[6];
    if (!i2cReadN(gyroAddr, 0x3B, b, 6)) return false;
    for (int i = 0; i < 3; i++) v[i] = (int16_t)((b[i * 2] << 8) | b[i * 2 + 1]) / 64;
    return true;
  }
  Wire.beginTransmission(accAddr);
  Wire.write(0x32);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)accAddr, 6) != 6) return false;
  uint8_t b[6];
  for (int i = 0; i < 6; i++) b[i] = Wire.read();
  for (int i = 0; i < 3; i++) v[i] = (int16_t)(b[i * 2] | (b[i * 2 + 1] << 8));
  return true;
#else
  (void)v;
  return false;
#endif
}

// Наклон в осях экрана: tx > 0 - вправо, ty > 0 - к себе (вниз по экрану). 256 = 90 градусов примерно.
// С гироскопом наклон плавный: быстрые повороты - от гироскопа, а медленно поправляет акселерометр
// (тряска акселерометра почти не видна, а гироскоп не «уплывает»).
bool accTilt(int& tx, int& ty) {
  static uint32_t last = 0;
  static float fx = 0, fy = 0;
  static bool ok = false, init = false;
  uint32_t now = millis();
  if (now - last >= 10) {                            // не чаще 100 раз в секунду
    float dt = init ? (now - last) / 1000.0f : 0;
    last = now;
    int v[3];
    ok = accRaw(v);
    if (ok) {
      float ax = (v[accAxX] - accZero[accAxX]) * accSgX, ay = (v[accAxY] - accZero[accAxY]) * accSgY;
      float g[3];
      if (init && gAxX >= 0 && gAxY >= 0 && dt < 0.2f && gyroRead(g)) {
        const float K = 256.0f * 3.14159f / 180.0f;  // град/с -> наши единицы (256 ~ 1g)
        fx = 0.97f * (fx + g[gAxX] * gSgX * dt * K) + 0.03f * ax;
        fy = 0.97f * (fy + g[gAxY] * gSgY * dt * K) + 0.03f * ay;
      } else {
        fx = ax;
        fy = ay;
      }
      init = true;
    }
  }
  tx = (int)fx;
  ty = (int)fy;
  return ok;
}

// Идёт игра (а не меню): только тогда наклон гаджета работает как ручка.
// Экраны выбора и сообщения внутри игр наклон не слушают - иначе меню «уезжает».
bool g_inGame = false;
struct InGame {
  bool prev;
  InGame() : prev(g_inGame) { g_inGame = true; }
  ~InGame() { g_inGame = prev; }
};
struct UiPause {
  bool prev;
  UiPause() : prev(g_inGame) { g_inGame = false; }
  ~UiPause() { g_inGame = prev; }
};

// ========================= СЕНСОРНЫЕ КНОПКИ И ВИБРОМОТОР (S3) =========================
// Площадки - просто медь (скотч, фольга, позже - медь на плате) на выводах, которые S3 умеет читать
// касанием (GPIO1-14). Касание = направление ручки; есть отклик вибромотором.
// Проверка на S3-Zero: вверх - 4, вниз - 5, влево - 6, вправо - 13 (эти выводы свободны без SD-карты).
#if BOARD_S3
const int TOUCH_PINS[4] = {4, 5, 6, 13};             // вверх, вниз, влево, вправо
const int PIN_VIBRO = 40;                            // вибромотор через транзистор
#endif
uint32_t touchBase[4];
bool touchReady = false;
uint32_t vibroOffAt = 0;

// Короткий толчок вибромотора (не ждёт: выключится сам при следующем опросе ручки).
void vibe(int ms) {
#if BOARD_S3
  digitalWrite(PIN_VIBRO, HIGH);
  vibroOffAt = millis() + (uint32_t)ms;
  if (!vibroOffAt) vibroOffAt = 1;
#else
  (void)ms;
#endif
}
void vibroService() {
#if BOARD_S3
  if (vibroOffAt && (int32_t)(millis() - vibroOffAt) >= 0) {
    digitalWrite(PIN_VIBRO, LOW);
    vibroOffAt = 0;
  }
#endif
}

void touchInit() {
#if BOARD_S3
  pinMode(PIN_VIBRO, OUTPUT);
  digitalWrite(PIN_VIBRO, LOW);
  if (!cfgTouch || cfgSd) return;                    // 4, 5, 6, 13 у SD-карты
  for (int i = 0; i < 4; i++) {                      // «покой» каждой площадки (в этот момент её не трогают)
    uint64_t sum = 0;
    for (int k = 0; k < 8; k++) sum += touchRead(TOUCH_PINS[i]);
    touchBase[i] = (uint32_t)(sum / 8);
  }
  touchReady = true;
  Serial.printf("touch base: %u %u %u %u\n", (unsigned)touchBase[0], (unsigned)touchBase[1], (unsigned)touchBase[2], (unsigned)touchBase[3]);
#endif
}

// Какая площадка нажата (J_NONE - никакая). На S3 значение при касании растёт.
uint8_t touchDir() {
#if BOARD_S3
  if (!touchReady || !cfgTouch) return J_NONE;
  static uint32_t last = 0;
  static uint8_t cached = J_NONE, prev = J_NONE;
  if (millis() - last < 15) return cached;
  last = millis();
  static const uint8_t DIRS[4] = {J_UP, J_DOWN, J_LEFT, J_RIGHT};
  uint8_t d = J_NONE;
  uint32_t best = 0;
  for (int i = 0; i < 4; i++) {
    uint32_t v = touchRead(TOUCH_PINS[i]), b = touchBase[i];
    uint32_t over = v > b ? v - b : 0;
    if (over > b / 12 && over > best) {             // касание: больше покоя на ~8%
      best = over;
      d = DIRS[i];
    } else if (over < b / 40) {
      touchBase[i] = (b * 63 + v) / 64;              // покой медленно подстраивается (влажность, температура)
    }
  }
  if (d != J_NONE && d != prev) vibe(18);            // отклик на новое касание
  prev = d;
  cached = d;
  return d;
#else
  return J_NONE;
#endif
}

// Наклон как направление ручки (для игр, которые слушают «вверх/вниз/влево/вправо»). ~15 градусов - срабатывает.
uint8_t tiltDir() {
#if BOARD_S3
  if (!g_inGame || !cfgTilt || !accOk) return J_NONE;
  int tx, ty;
  if (!accTilt(tx, ty)) return J_NONE;
  int ax = abs(tx), ay = abs(ty);
  if (ax < 66 && ay < 66) return J_NONE;
  if (ax > ay) return tx > 0 ? J_RIGHT : J_LEFT;
  return ty > 0 ? J_DOWN : J_UP;
#else
  return J_NONE;
#endif
}

// Ручка, которую держат на телефоне (пульт по Wi-Fi): J_NONE, если не держат.
uint8_t padDir() {
  wpad::poll();
  switch (wpad::heldDir()) {
    case 'U': return J_UP;
    case 'D': return J_DOWN;
    case 'L': return J_LEFT;
    case 'R': return J_RIGHT;
  }
  return J_NONE;
}

// Кнопка нажата: на гаджете или на телефоне.
bool swDown() {
  wpad::poll();
  return digitalRead(PIN_SW) == LOW || wpad::heldBtn();
}

// Отклонение ручки от центра с учётом настроек осей: примерно -2048..2048.
void joyAnalog(int& dx, int& dy) {
  dx = JOY_DX();
  dy = JOY_DY();
  if (JOY_SWAP_XY) { int t = dx; dx = dy; dy = t; }
  if (JOY_INVERT_X) dx = -dx;
  if (JOY_INVERT_Y != (cfgJoyInvY != 0)) dy = -dy;
  joyRotate(dx, dy);
  switch (padDir()) {                                // телефон: до упора в нужную сторону
    case J_UP: dx = 0; dy = -2000; break;
    case J_DOWN: dx = 0; dy = 2000; break;
    case J_LEFT: dx = -2000; dy = 0; break;
    case J_RIGHT: dx = 2000; dy = 0; break;
  }
  // наклон гаджета (если включён и ручку не трогают): ~30 градусов - до упора
  if (BOARD_S3 && g_inGame && cfgTilt && accOk && abs(dx) < joyThr() / 3 && abs(dy) < joyThr() / 3) {
    int tx, ty;
    if (accTilt(tx, ty)) {
      auto conv = [](int t) {
        int a = abs(t);
        if (a < 20) return 0;                        // мёртвая зона ~5 градусов
        int v = (a - 20) * 2048 / 108;
        if (v > 2047) v = 2047;
        return t < 0 ? -v : v;
      };
      dx = conv(tx);
      dy = conv(ty);
    }
  }
  int half = joyThr() / 2;
  if (dx > half || dx < -half || dy > half || dy < -half) {
    if (screenAsleep) wakeScreen();
    noteActivity();
  }
  idleCheck();
}

uint8_t joyRawDir() {
  int dx = JOY_DX();
  int dy = JOY_DY();
  if (JOY_SWAP_XY) { int t = dx; dx = dy; dy = t; }
  if (JOY_INVERT_X) dx = -dx;
  if (JOY_INVERT_Y != (cfgJoyInvY != 0)) dy = -dy;
  joyRotate(dx, dy);
  int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
  if (ax < joyThr() && ay < joyThr()) {
    vibroService();
    uint8_t p = padDir();
    if (p == J_NONE) p = touchDir();
    return p != J_NONE ? p : tiltDir();
  }
  if (ax > ay) return dx > 0 ? J_RIGHT : J_LEFT;
  return dy > 0 ? J_DOWN : J_UP;
}

// Направление ручки. Движение будит погасший экран (и само при этом не срабатывает).
uint8_t joyRaw() {
  static bool swallow = false;
  uint8_t d = joyRawDir();
  if (d == J_NONE) {
    swallow = false;
  } else {
    if (screenAsleep) {
      wakeScreen();
      swallow = true;
    }
    noteActivity();
  }
  idleCheck();
  return swallow ? (uint8_t)J_NONE : d;
}

// Одно событие на наклон; при удержании - автоповтор.
bool g_joyRepeated = false;   // true - последнее событие было автоповтором (ручку держат)

uint8_t joyEvent() {
  static uint8_t last = J_NONE;
  static uint32_t tNext = 0;
  uint8_t d = joyRaw();
  uint32_t now = millis();
  if (d == J_NONE) {
    last = J_NONE;
    return J_NONE;
  }
  if (d != last) {
    last = d;
    tNext = now + REPEAT_DELAY_MS;
    g_joyRepeated = false;
    return d;
  }
  if ((int32_t)(now - tNext) >= 0) {
    tNext = now + REPEAT_RATE_MS;
    g_joyRepeated = true;
    return d;
  }
  return J_NONE;
}

int joyVertical(uint8_t d) { return d == J_UP ? -1 : d == J_DOWN ? 1 : 0; }

// Короткое нажатие срабатывает при отпускании, долгое - сразу по истечении времени.
BtnEvent btnEvent() {
  static int last = HIGH;
  static uint32_t tChange = 0, tDown = 0;
  static bool longFired = false;
  static uint8_t swallow = 0;            // 1 - нажатие разбудило экран, 2 - экран спрятан удержанием: ждём отпускания
  int v = swDown() ? LOW : HIGH;
  uint32_t now = millis();
  if (swallow == 0 && v == LOW && screenAsleep) {
    wakeScreen();
    swallow = 1;
  }
  if (swallow) {
    if (v == HIGH) {
      swallow = 0;
      last = HIGH;
      tChange = now;
    }
    return EV_NONE;
  }
  if (v == LOW) noteActivity();
  idleCheck();
  if (last == LOW && longFired && now - tDown >= HIDE_HOLD_MS) {   // держат 2 секунды - спрятать экран
    sleepScreen();
    swallow = 2;
    return EV_NONE;
  }
  if (v != last && now - tChange > 30) {
    last = v;
    tChange = now;
    if (v == LOW) {
      tDown = now;
      longFired = false;
    } else if (!longFired) {
      beep(2, 2200, 5);
      return EV_CLICK;
    }
  }
  if (last == LOW && !longFired && now - tDown >= LONG_PRESS_MS) {
    longFired = true;
    beep(2, 1100, 12);
    return EV_LONG;
  }
  return EV_NONE;
}

BtnEvent waitButton() {
  for (;;) {
    BtnEvent e = btnEvent();
    if (e != EV_NONE) return e;
    delay(10);
  }
}

// ========================= НАСТРОЙКА WI-FI С ТЕЛЕФОНА =========================
WebServer server(80);
DNSServer dns;
String scanOptions;
bool portalSaved = false;
bool serverReady = false;

String htmlEscape(const String& s) {
  String o;
  for (unsigned i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

String pageHead() {
  return String("<!doctype html><html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>") + T().webTitle + "</title><style>"
                "body{font-family:sans-serif;max-width:420px;margin:24px auto;padding:0 16px;"
                "background:#111;color:#eee}h2{font-weight:600}label{display:block;margin-top:16px}"
                "input,select,button,textarea{width:100%;box-sizing:border-box;font-size:17px;padding:10px;"
                "margin-top:6px;border-radius:8px;border:1px solid #555;background:#222;color:#eee}"
                "button{background:#2563eb;border:0;margin-top:24px}small{color:#999}"
                "input[type=checkbox]{width:auto;margin-right:8px}hr{border:0;border-top:1px solid #444;margin:28px 0}"
                "</style></head><body>";
}

String maskedKey() {
  if (cfgKey.length() < 8) return "";
  return cfgKey.substring(0, 3) + "..." + cfgKey.substring(cfgKey.length() - 4);
}

int noteCount();
bool addNote(const String& title, const String& text);
String portalNote;                                   // что только что пришло с телефона (показать на экране)

// ---------- microSD (по желанию) ----------
// Карта подключается вместо ИК-пульта, фонарика и пищалки: SCK - GPIO8, MOSI - GPIO2, MISO - GPIO20, CS - GPIO9
// (плюс 10 кОм от GPIO2 и от GPIO9 к 3V3). Включается в Настройках. Шина SPI у платы одна, а у экрана нет CS,
// поэтому на время работы с картой выводы шины переключаются на карту, потом обратно на экран.
#if BOARD_S3
const int SD_SCK = 5, SD_MOSI = 6, SD_MISO = 13, SD_CS = 4;   // S3: у карты своя шина SPI
#else
const int SD_SCK = 8, SD_MOSI = 2, SD_MISO = 20, SD_CS = 9;
#endif
bool sdOk = false;                                   // карта смонтирована
int sdDepth = 0;
// Шаг работы с картой - переживает перезапуск кнопкой RST: если гаджет завис, после RST видно, где.
RTC_NOINIT_ATTR uint32_t sdDiagStage;
#define SD_STAGE(n) (sdDiagStage = 0x5D000000u | (uint32_t)(n))

#if BOARD_S3
// У S3 две свободные шины SPI: карта на своей, ничего переключать не нужно.
SPIClass sdSpi(HSPI);
#define SD_SPI sdSpi
void sdBusOn() {
  static bool started = false;
  sdDepth++;
  if (!started) {
    sdSpi.begin(SD_SCK, SD_MISO, SD_MOSI, -1);
    started = true;
  }
}
void sdBusOff() { sdDepth--; }
#else
#define SD_SPI SPI
// Шина не останавливается: на работающей шине через матрицу GPIO переключаются только выводы.
void sdBusOn() {
  if (sdDepth++ > 0) return;
  spi_t* b = SPI.bus();
  SD_STAGE(1);
  // У экрана нет CS: любой лишний фронт на SCK сдвигает ему байты, и он перестаёт понимать команды
  // до сброса. В режиме 3 такт в покое = 1, поэтому вывод заранее держим в 1 - фронта не будет.
  gpio_set_level((gpio_num_t)PIN_SCK, 1);
  gpio_set_level((gpio_num_t)PIN_MOSI, 1);
  spiDetachSCK(b);
  spiDetachMOSI(b);
  SD_STAGE(2);
  pinMode(PIN_SCK, OUTPUT);
  digitalWrite(PIN_SCK, HIGH);
  pinMode(PIN_MOSI, OUTPUT);
  digitalWrite(PIN_MOSI, HIGH);
  SD_STAGE(3);
  spiAttachSCK(b, SD_SCK);
  spiAttachMISO(b, SD_MISO);
  spiAttachMOSI(b, SD_MOSI);
  SD_STAGE(4);
}

void sdBusOff() {
  if (--sdDepth > 0) return;
  spi_t* b = SPI.bus();
  SD_STAGE(5);
  spiDetachSCK(b);
  spiDetachMISO(b);
  spiDetachMOSI(b);
  SD_STAGE(6);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  // spiSetDataMode на C3 не применяет режим сразу, поэтому пустая передача в режиме 3 без подключённых выводов:
  // после неё такт в покое = 1, и подключение экрана не даст лишнего фронта.
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE3));
  SPI.transfer(0xFF);
  SPI.endTransaction();
  spiAttachSCK(b, PIN_SCK);
  spiAttachMOSI(b, PIN_MOSI);
  SD_STAGE(7);
}

#endif

struct SdBus {                                       // на время жизни - шина у карты
  SdBus() { sdBusOn(); }
  ~SdBus() { sdBusOff(); }
};

struct PicBus {                                      // то же, но только если картинки на карте
  bool on;
  PicBus() : on(sdOk) {
    if (on) sdBusOn();
  }
  ~PicBus() {
    if (on) sdBusOff();
  }
};

bool sdMount() {
  if (!cfgSd) return false;
  uint32_t t0 = millis();
  SdBus b;
  // 8 МГц: на проводах и через преобразователь уровней модуля надёжнее, чем 20 МГц.
  // Первая попытка после включения часто не удаётся - карте нужно время, поэтому пробуем дважды.
  for (int k = 0; k < 3 && !sdOk; k++) {
    if (k) {
      SD.end();
      delay(200);
    }
    SD_STAGE(8);
    // 4 МГц: на проводах 8 МГц работало через раз; не вышло - пробуем медленнее (длинные провода, слабый модуль)
    sdOk = SD.begin(SD_CS, SD_SPI, k == 0 ? 4000000 : k == 1 ? 1000000 : 400000, "/sd", 4);
    SD_STAGE(9);
  }
  if (sdOk) {
    SD.mkdir("/pics");
    SD.mkdir("/fw");
  }
  Serial.printf("SD card: %s, type %d, %llu MB, %lu ms\n", sdOk ? "ok" : "not found", sdOk ? (int)SD.cardType() : -1,
                sdOk ? (unsigned long long)(SD.cardSize() / (1024 * 1024)) : 0ULL, (unsigned long)(millis() - t0));
  return sdOk;
}

// Карта иногда отвечает только через 10-20 с после включения. Если при запуске не нашлась -
// первые 2 минуты пробуем ещё раз каждые 15 с (одна попытка ~1 с).
void sdRetryService() {
  static uint32_t last = 0;
  if (!cfgSd || sdOk || millis() > 120000 || millis() - last < 15000) return;
  last = millis();
  SdBus bus;
  SD.end();
  sdOk = SD.begin(SD_CS, SD_SPI, 1000000, "/sd", 4);
  if (sdOk) {
    SD.mkdir("/pics");
    SD.mkdir("/fw");
  }
  Serial.printf("SD card retry: %s\n", sdOk ? "ok" : "not found");
}

#if BOARD_S3
// ---------- Проверка проводки карты (Настройки -> SD-карта, когда карта не найдена) ----------
// Без библиотеки SD: выводы дёргаются вручную, карте посылается команда сброса CMD0 - живая карта
// отвечает 0x01. Если не ответила - перебираем, не перепутаны ли провода местами.
uint8_t sdBbByte(int sck, int mosi, int miso, uint8_t out) {
  uint8_t in = 0;
  for (int i = 7; i >= 0; i--) {
    digitalWrite(mosi, (out >> i) & 1);
    delayMicroseconds(4);
    digitalWrite(sck, HIGH);
    delayMicroseconds(4);
    in = (uint8_t)((in << 1) | (digitalRead(miso) ? 1 : 0));
    digitalWrite(sck, LOW);
  }
  return in;
}

// Ответ карты на CMD0 (0xFF - молчит, 0x00 - линия MISO прижата к земле, 0x01 - карта жива).
uint8_t sdBbCmd0(int cs, int sck, int mosi, int miso) {
  pinMode(miso, INPUT_PULLUP);
  pinMode(cs, OUTPUT);
  pinMode(sck, OUTPUT);
  pinMode(mosi, OUTPUT);
  digitalWrite(cs, HIGH);
  digitalWrite(mosi, HIGH);
  digitalWrite(sck, LOW);
  delay(2);
  for (int i = 0; i < 12; i++) sdBbByte(sck, mosi, miso, 0xFF);   // >74 тактов при CS=1: карта переходит в SPI
  digitalWrite(cs, LOW);
  const uint8_t cmd[6] = {0x40, 0, 0, 0, 0, 0x95};
  for (uint8_t b : cmd) sdBbByte(sck, mosi, miso, b);
  uint8_t r = 0xFF;
  for (int i = 0; i < 12 && r == 0xFF; i++) r = sdBbByte(sck, mosi, miso, 0xFF);
  digitalWrite(cs, HIGH);
  sdBbByte(sck, mosi, miso, 0xFF);
  return r;
}

void viewAnswer(const String& utf8, uint16_t fg);    // определено ниже
void sdWiringTest() {
  showMsg(L3("Проверяю карту и провода... (~10 с)", "Checking the card and wires... (~10 s)", "正在检查SD卡和接线……"), C_YELLOW);
  SD.end();
  sdSpi.end();
  const int P[4] = {SD_CS, SD_SCK, SD_MOSI, SD_MISO};
  const char* NM[4] = {"CS", "SCK", "MOSI", "MISO"};
  String rep = L3("Проверка SD-карты\n\n", "SD card check\n\n", "SD卡检查\n\n");
  // 1. Состояние линий: с подтяжкой к 0 и к 1 свободный провод читается 0 и 1.
  rep += L3("Линии (подтяжка вниз / вверх):\n", "Lines (pull-down / pull-up):\n", "线路：\n");
  bool stuck0 = false;
  for (int i = 0; i < 4; i++) {
    pinMode(P[i], INPUT_PULLDOWN);
    delay(3);
    int lo = digitalRead(P[i]);
    pinMode(P[i], INPUT_PULLUP);
    delay(3);
    int hi = digitalRead(P[i]);
    pinMode(P[i], INPUT);
    rep += String(NM[i]) + " (GPIO" + String(P[i]) + "): " + String(lo) + " / " + String(hi);
    if (lo == 0 && hi == 0) {
      rep += L3("  прижат к GND!", "  stuck to GND!", "  接地!");
      stuck0 = true;
    }
    rep += "\n";
  }
  // 2. Команда сброса по проводам как в прошивке.
  uint8_t r = sdBbCmd0(SD_CS, SD_SCK, SD_MOSI, SD_MISO);
  char hx[8];
  snprintf(hx, sizeof(hx), "0x%02X", r);
  rep += String(L3("\nОтвет карты на сброс: ", "\nCard reply to reset: ", "\n复位应答: ")) + hx + "\n";
  bool good = (r & 0xFE) == 0;                       // 0x01 (или 0x00 у уже запущенной карты)
  String verdict;
  if (r == 0x01) {
    verdict = L3("Карта отвечает - провода в порядке.", "The card answers - the wires are fine.", "卡有应答，接线正常。");
  } else {
    // 3. Не ответила - может, провода перепутаны местами? Перебираем все 24 варианта.
    int found = -1;
    int perm[4] = {0, 1, 2, 3};
    std::sort(perm, perm + 4);
    int n = 0;
    do {
      if (n++ == 0) continue;                        // исходный порядок уже проверен
      if (sdBbCmd0(P[perm[0]], P[perm[1]], P[perm[2]], P[perm[3]]) == 0x01) {
        found = perm[0] * 1000 + perm[1] * 100 + perm[2] * 10 + perm[3];
        break;
      }
    } while (std::next_permutation(perm, perm + 4));
    if (found >= 0) {
      int q[4] = {found / 1000, found / 100 % 10, found / 10 % 10, found % 10};
      verdict = L3("Провода перепутаны! Карта ответила так:\n", "Wires are swapped! The card answered with:\n", "接线接反！\n");
      for (int i = 0; i < 4; i++)
        verdict += String("  ") + NM[i] + L3(" модуля сейчас на GPIO", " of the module is on GPIO", " 在 GPIO") + String(P[q[i]]) +
                   L3(", а должен быть на GPIO", ", must be on GPIO", "，应在 GPIO") + String(P[i]) + "\n";
    } else if (r == 0xFF) {
      verdict = L3("Карта молчит. Проверьте: карта вставлена до щелчка; питание модуля (со стабилизатором - 5V, без - 3V3) и GND; "
                   "что провод MISO (DO) действительно идёт на GPIO13. Попробуйте другую карту.",
                   "The card is silent. Check: card fully inserted; module power (5V with a regulator, 3V3 without) and GND; "
                   "that MISO (DO) really goes to GPIO13. Try another card.", "卡无应答。检查供电和接线。");
    } else if (r == 0x00 || stuck0) {
      verdict = L3("Линия MISO прижата к земле: модуль без питания, замыкание на GND или MISO не на том выводе.",
                   "MISO is held low: module unpowered, a short to GND or MISO on a wrong pin.", "MISO被拉低。");
    } else {
      verdict = L3("Карта отвечает мусором - плохой контакт или слишком длинные провода. Пропаяйте контакты, укоротите провода, "
                   "поставьте конденсатор 10-100 мкФ на питание модуля.",
                   "The card answers garbage - bad contact or wires too long. Resolder, shorten wires, add a 10-100 uF capacitor on the module power.",
                   "应答错误，检查焊点。");
    }
  }
  rep += "\n" + verdict + "\n";
  // 4. Если карта живая - пробуем смонтировать на разных скоростях.
  sdSpi.begin(SD_SCK, SD_MISO, SD_MOSI, -1);
  if (good) {
    const uint32_t F[3] = {4000000, 1000000, 400000};
    for (uint32_t f : F) {
      SD.end();
      bool ok = SD.begin(SD_CS, sdSpi, f, "/sd", 4);
      rep += String(f / 1000) + L3(" кГц: ", " kHz: ", " kHz: ") +
             (ok ? String(L3("открылась, ", "mounted, ", "成功，")) + String((unsigned long)(SD.cardSize() / (1024 * 1024))) + L3(" МБ", " MB", " MB")
                 : String(L3("не открылась", "failed", "失败")));
      rep += "\n";
      if (ok) {
        sdOk = true;
        SD.mkdir("/pics");
        SD.mkdir("/fw");
        break;
      }
    }
    if (!sdOk)
      rep += L3("Карта отвечает, но не открывается: отформатируйте её в FAT32 (до 32 ГБ) или возьмите другую.\n",
                "The card answers but does not mount: format it as FAT32 (up to 32 GB) or try another.\n", "请格式化为FAT32。\n");
  }
  Serial.println(rep);
  viewAnswer(rep, sdOk ? C_GREEN : C_WHITE);
}
#endif

fs::FS& picFs() {
  if (sdOk) return SD;
  return LittleFS;
}

// ---------- Галерея: картинки в памяти платы или на SD-карте ----------
// Телефон сам уменьшает фото под экран (до 240x200) и присылает маленький JPEG.
const size_t MAX_PIC_BYTES = 40000;
int picMax() { return sdOk ? 99 : 30; }

String picPath(int i) {
  char b[24];
  snprintf(b, sizeof(b), sdOk ? "/pics/pic%02d.jpg" : "/pic%02d.jpg", i);
  return String(b);
}

bool picExists(int i) {
  PicBus b;
  return picFs().exists(picPath(i).c_str());
}

void picRemove(int i) {
  PicBus b;
  picFs().remove(picPath(i).c_str());
}

// Прочитать картинку в память (освободить free()). nullptr - не вышло.
uint8_t* picLoad(int slot, size_t& n) {
  PicBus b;
  File f = picFs().open(picPath(slot).c_str(), "r");
  n = f ? (size_t)f.size() : 0;
  uint8_t* buf = (n > 0 && n <= MAX_PIC_BYTES) ? (uint8_t*)malloc(n) : nullptr;
  if (buf && f.read(buf, n) != n) {
    free(buf);
    buf = nullptr;
  }
  if (f) f.close();
  return buf;
}

// Номера всех картинок по возрастанию. Папка читается один раз: проверять 99 файлов по одному
// на SD-карте долго (каждая проверка - поиск по FAT), отсюда и были паузы при открытии галереи.
void picSlots(std::vector<int>& out) {
  out.clear();
  PicBus b;
  std::vector<bool> has(picMax(), false);
  File dir = picFs().open(sdOk ? "/pics" : "/");
  if (dir && dir.isDirectory()) {
    // только имена, без открытия каждого файла: на SD-карте открытие файла долгое
    for (int guard = 0; guard < 400; guard++) {
      bool isDir = false;
      String full = dir.getNextFileName(&isDir);
      if (!full.length()) break;
      int sl = full.lastIndexOf('/');
      String nm = sl >= 0 ? full.substring(sl + 1) : full;
      int i = -1;
      if (!isDir && sscanf(nm.c_str(), "pic%d.jpg", &i) == 1 && i >= 0 && i < picMax()) has[i] = true;
    }
  }
  if (dir) dir.close();
  for (int i = 0; i < picMax(); i++)
    if (has[i]) out.push_back(i);
}

int picCount() {
  std::vector<int> v;
  picSlots(v);
  return (int)v.size();
}

int picFreeSlot() {
  std::vector<int> v;
  picSlots(v);
  for (int i = 0; i < picMax(); i++)
    if (i >= (int)v.size() || v[i] != i) return i;  // первый пропуск в нумерации
  return -1;
}

// Хватит ли места на картинку размером n байт (с запасом на служебные данные файловой системы).
bool picRoomFor(size_t n) {
  if (sdOk) {
    PicBus b;
    return SD.totalBytes() - SD.usedBytes() > (uint64_t)n + 65536;
  }
  return LittleFS.totalBytes() - LittleFS.usedBytes() > n + 12000;
}

// Сохранить готовый JPEG из памяти (например, найденную в интернете картинку). Пустая строка - успех, иначе причина.
String savePic(const uint8_t* data, size_t n) {
  if (n < 100 || data[0] != 0xFF || data[1] != 0xD8) return L3("это не JPEG", "not a JPEG", "不是JPEG");
  if (n > MAX_PIC_BYTES) return L3("файл слишком большой", "the file is too big", "文件太大");
  int slot = picFreeSlot();
  if (slot < 0) return L3("галерея заполнена", "the gallery is full", "相册已满");
  if (!picRoomFor(n)) return L3("в памяти платы нет места", "no free memory on the device", "设备存储空间不足");
  PicBus b;
  File f = picFs().open(picPath(slot).c_str(), "w");
  if (!f) return L3("не удалось записать", "cannot write the file", "无法写入");
  size_t w = f.write(data, n);
  f.close();
  if (w != n) {
    picFs().remove(picPath(slot).c_str());
    return L3("не удалось записать", "cannot write the file", "无法写入");
  }
  return "";
}

// Приём файла с телефона по кусочкам.
File picFile;
String picFilePath, picErr;
size_t picSize = 0;
bool picOk = false;

void picAbort(const char* why) {
  PicBus b;
  if (picFile) picFile.close();
  if (picFilePath.length()) picFs().remove(picFilePath.c_str());
  picFilePath = "";
  picErr = why;
}

void handlePicUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    picOk = false;
    picSize = 0;
    picErr = "";
    picFilePath = "";
    int slot = picFreeSlot();
    if (slot < 0) {
      picErr = L3("галерея заполнена. Удалите лишнее на устройстве.", "the gallery is full", "相册已满");
    } else if (!picRoomFor(16000)) {                 // с телефона приходит до ~14 КБ
      picErr = L3("в памяти платы нет места", "no free memory on the device", "设备存储空间不足");
    } else {
      picFilePath = picPath(slot);
      PicBus b;
      picFile = picFs().open(picFilePath.c_str(), "w");
      if (!picFile) picAbort(L3("не удалось записать", "cannot write the file", "无法写入"));
    }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!picFilePath.length()) return;                // уже отказали
    if (picSize == 0 && (up.currentSize < 2 || up.buf[0] != 0xFF || up.buf[1] != 0xD8)) {
      picAbort(L3("это не JPEG", "not a JPEG", "不是JPEG"));
      return;
    }
    picSize += up.currentSize;
    if (picSize > MAX_PIC_BYTES) {
      picAbort(L3("файл слишком большой", "the file is too big", "文件太大"));
      return;
    }
    size_t w;
    {
      PicBus b;
      w = picFile.write(up.buf, up.currentSize);
    }
    if (w != up.currentSize) picAbort(L3("не удалось записать", "cannot write the file", "无法写入"));
  } else if (up.status == UPLOAD_FILE_END) {
    if (!picFilePath.length()) return;
    {
      PicBus b;
      picFile.close();
    }
    if (picSize < 100) picAbort(L3("файл пустой", "the file is empty", "文件为空"));
    else picOk = true;
  } else {                                           // связь оборвалась посреди файла
    if (picFilePath.length()) picAbort(L3("загрузка прервана", "the upload was interrupted", "上传中断"));
  }
}

void handlePicDone() {
  if (picOk) {
    server.send(200, "text/plain; charset=utf-8", "OK");
    portalNote = String(L3("Получена картинка. В галерее: ", "Picture received. In the gallery: ", "已收到图片。相册中：")) + String(picCount());
  } else {
    server.send(200, "text/plain; charset=utf-8", String(L3("Ошибка: ", "Error: ", "错误：")) + (picErr.length() ? picErr : String("?")));
  }
  picOk = false;
}

// ---------- ИК-пульты: файлы .ir в памяти платы ----------
// Формат файлов - как у Flipper Zero (база Flipper-IRDB). Первой строкой дописываем название: «# title: ...».
const int IR_SLOTS = 12;
const size_t IR_MAX_BYTES = 150000;

String irPath(int i) {
  char b[16];
  snprintf(b, sizeof(b), "/ir%02d.ir", i);
  return String(b);
}

int irCount() {
  int n = 0;
  for (int i = 0; i < IR_SLOTS; i++)
    if (LittleFS.exists(irPath(i).c_str())) n++;
  return n;
}

int irFreeSlot() {
  for (int i = 0; i < IR_SLOTS; i++)
    if (!LittleFS.exists(irPath(i).c_str())) return i;
  return -1;
}

bool irRoom() { return LittleFS.totalBytes() - LittleFS.usedBytes() > 40000; }

String irTitle(int slot) {
  File f = LittleFS.open(irPath(slot).c_str(), "r");
  if (!f) return "";
  uint8_t b[80];
  size_t n = f.read(b, sizeof(b) - 1);
  f.close();
  std::string s((const char*)b, n);
  size_t nl = s.find('\n');
  if (nl != std::string::npos) s.erase(nl);
  if (s.compare(0, 9, "# title: ") != 0) return String("?");
  return fromStd(ir::trim(s.substr(9)));
}

// Похоже ли начало файла на файл пульта.
bool irLooksValid(const uint8_t* b, size_t n) {
  if (n >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) {
    b += 3;
    n -= 3;
  }
  return n >= 12 && memcmp(b, "Filetype: IR", 12) == 0;
}

// Приёмник: пишет скачиваемый файл сразу в память платы.
class IrFileSink : public Stream {
 public:
  File f;
  size_t n = 0;
  bool tooBig = false, failed = false, badType = false;
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* b, size_t len) override {
    if (n == 0 && !irLooksValid(b, len)) badType = true;
    n += len;
    if (n > IR_MAX_BYTES) tooBig = true;
    if (!badType && !tooBig && !failed && f.write(b, len) != len) failed = true;
    return len;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
};

// Приём файла пульта с телефона по кусочкам.
File irUpFile;
String irUpPath, irUpErr;
size_t irUpSize = 0;
bool irUpOk = false;

void irUpAbort(const char* why) {
  if (irUpFile) irUpFile.close();
  if (irUpPath.length()) LittleFS.remove(irUpPath.c_str());
  irUpPath = "";
  irUpErr = why;
}

void handleIrUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    irUpOk = false;
    irUpSize = 0;
    irUpErr = "";
    irUpPath = "";
    int slot = irFreeSlot();
    if (slot < 0) {
      irUpErr = L3("уже 12 пультов. Удалите лишний на устройстве.", "12 remotes already. Delete one on the device.", "已有12个遥控器，请先删除。");
    } else if (!irRoom()) {
      irUpErr = L3("в памяти платы нет места", "no free memory on the device", "设备存储空间不足");
    } else {
      irUpPath = irPath(slot);
      irUpFile = LittleFS.open(irUpPath.c_str(), "w");
      if (!irUpFile) {
        irUpAbort(L3("не удалось записать", "cannot write the file", "无法写入"));
      } else {
        std::string head = "# title: " + ir::titleFromPath(toStd(up.filename)) + "\n";
        irUpFile.write((const uint8_t*)head.data(), head.size());
      }
    }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!irUpPath.length()) return;                   // уже отказали
    if (irUpSize == 0 && !irLooksValid(up.buf, up.currentSize)) {
      irUpAbort(L3("это не файл пульта (.ir)", "this is not a remote file (.ir)", "这不是遥控器文件（.ir）"));
      return;
    }
    irUpSize += up.currentSize;
    if (irUpSize > IR_MAX_BYTES) {
      irUpAbort(L3("файл слишком большой", "the file is too big", "文件太大"));
      return;
    }
    if (irUpFile.write(up.buf, up.currentSize) != up.currentSize) irUpAbort(L3("не удалось записать", "cannot write the file", "无法写入"));
  } else if (up.status == UPLOAD_FILE_END) {
    if (!irUpPath.length()) return;
    irUpFile.close();
    if (irUpSize < 30) irUpAbort(L3("файл пустой", "the file is empty", "文件为空"));
    else irUpOk = true;
  } else {
    if (irUpPath.length()) irUpAbort(L3("загрузка прервана", "the upload was interrupted", "上传中断"));
  }
}

void handleIrDone() {
  if (irUpOk) {
    String t = String(L3("Пульт сохранён. Всего пультов: ", "Remote saved. Remotes: ", "遥控器已保存。总数：")) + String(irCount());
    server.send(200, "text/plain; charset=utf-8", t);
    portalNote = t;
  } else {
    server.send(200, "text/plain; charset=utf-8", String(L3("Ошибка: ", "Error: ", "错误：")) + (irUpErr.length() ? irUpErr : String("?")));
  }
  irUpOk = false;
}

void handleFwDone();                                 // обновление прошивки с телефона - определено ниже
void handleFwUpload();

void handleRoot() {
  String h = pageHead();
  h += "<h2>"; h += T().webTitle; h += "</h2><form method='post' action='/save'>";
  if (netSsid.size()) {                              // запомненные сети: галочка - забыть
    h += "<label>";
    h += L3("Запомненные сети (отметьте, чтобы забыть):", "Saved networks (tick to forget):", "已保存的网络（勾选以删除）：");
    h += "</label>";
    for (size_t i = 0; i < netSsid.size(); i++) {
      h += "<label><input type='checkbox' name='d"; h += String((int)i); h += "' value='1'>";
      h += htmlEscape(netSsid[i]); h += "</label>";
    }
  }
  h += "<label>";
  h += L3("Добавить сеть (до 5: дом, телефон, школа...)", "Add a network (up to 5)", "添加网络（最多5个）");
  h += "<select name='s'><option value=''>";
  h += L3("- не добавлять -", "- do not add -", "- 不添加 -");
  h += "</option>";
  h += scanOptions;
  h += "</select></label>";
  h += "<label>"; h += T().webManual; h += "<input name='m' autocomplete='off'></label>";
  h += "<label>"; h += T().webPass; h += "<input name='p' type='password'></label>";
  h += "<label>"; h += T().webKey;
  h += "<input name='k' autocomplete='off' placeholder='sk-...'><small>";
  h += T().webKeyHint;
  String mk = maskedKey();
  if (mk.length()) { h += " ("; h += htmlEscape(mk); h += ")"; }
  h += "</small></label><button type='submit'>"; h += T().webSave; h += "</button></form>";
  // конспекты: текст с телефона сохраняется в память платы и читается без интернета
  h += "<hr><h2>"; h += L3("Конспект", "Note", "笔记"); h += "</h2><form method='post' action='/note'>";
  h += "<label>"; h += L3("Название", "Title", "标题"); h += "<input name='t' maxlength='60'></label>";
  h += "<label>"; h += L3("Текст (до 4000 знаков)", "Text (up to 4000 characters)", "正文（最多4000字）");
  h += "<textarea name='x' rows='10' maxlength='4000'></textarea></label><small>";
  h += L3("Сохранено конспектов: ", "Notes saved: ", "已保存笔记："); h += String(noteCount()); h += " / 20</small>";
  h += "<button type='submit'>"; h += L3("Сохранить конспект", "Save the note", "保存笔记"); h += "</button></form>";
  // галерея: телефон уменьшает фото до 240x200 и отправляет по одному
  h += "<hr><h2>"; h += L3("Картинки в галерею", "Pictures for the gallery", "相册图片"); h += "</h2>";
  h += "<p><small>";
  h += L3("Если кнопка выбора не нажимается: это окно входа в сеть, в нём выбор файлов не работает. Откройте в обычном браузере (Chrome, Safari) адрес ",
          "If the button does not respond, this is the network sign-in window, where file selection does not work. Open in a normal browser (Chrome, Safari): ",
          "如果按钮没有反应：这是网络登录窗口，无法选择文件。请在普通浏览器（Chrome、Safari）中打开：");
  h += "<b>192.168.4.1</b></small></p>";
  h += "<p id='oc' style='display:none'><a style='color:#8ab4ff' href='intent://192.168.4.1/#Intent;scheme=http;package=com.android.chrome;end'>";
  h += L3("Открыть эту страницу в Chrome", "Open this page in Chrome", "在Chrome中打开此页面");
  h += "</a></p>";
  h += "<input type='file' id='pf' accept='image/*' multiple><p><small id='ps'>";
  h += L3("В галерее: ", "In the gallery: ", "相册中："); h += String(picCount()); h += " / "; h += String(picMax()); h += "</small></p>";
  h += "<script>var T1='"; h += L3("Отправляю", "Sending", "正在发送");
  h += "',T2='"; h += L3("Загружено картинок: ", "Pictures uploaded: ", "已上传图片：");
  h += "',T3='"; h += L3("Не получилось: ", "Failed: ", "失败：");
  h += "';"
       "if(/Android/.test(navigator.userAgent))document.getElementById('oc').style.display='block';"
       "document.getElementById('pf').onchange=async function(){var s=document.getElementById('ps'),fs=this.files,ok=0;"
       "for(var i=0;i<fs.length;i++){s.textContent=T1+' '+(i+1)+' / '+fs.length+'...';try{"
       "var im=await new Promise(function(res,rej){var m=new Image();m.onload=function(){res(m)};m.onerror=function(){rej('image')};m.src=URL.createObjectURL(fs[i])});"
       "var k=Math.min(240/im.width,200/im.height,1),w=Math.max(8,Math.round(im.width*k)),hh=Math.max(8,Math.round(im.height*k));"
       "var c=document.createElement('canvas');c.width=w;c.height=hh;var g=c.getContext('2d');g.fillStyle='#000';g.fillRect(0,0,w,hh);g.drawImage(im,0,0,w,hh);"
       "var q=0.8,b;do{b=await new Promise(function(r){c.toBlob(r,'image/jpeg',q)});q-=0.1}while(b&&b.size>14000&&q>0.25);"
       "var fd=new FormData();fd.append('f',b,'p.jpg');var r=await fetch('/pic',{method:'POST',body:fd});var t=await r.text();"
       "if(t.indexOf('OK')==0)ok++;else{s.textContent=T3+t;this.value='';return}"
       "}catch(e){s.textContent=T3+e;this.value='';return}}"
       "s.textContent=T2+ok;this.value=''};</script>";
  if (ENABLE_IR) {                                    // в сборке без ИК-пульта раздела нет
  h += "<h3>"; h += L3("Пульты (.ir)", "Remotes (.ir)", "遥控器（.ir）"); h += "</h3><p><small>";
  h += L3("Файл пульта из базы Flipper-IRDB (github.com/" IR_DB_REPO "): скачайте нужный .ir на телефон и выберите его здесь.",
          "A remote file from Flipper-IRDB (github.com/" IR_DB_REPO "): download the .ir file to your phone and pick it here.",
          "来自Flipper-IRDB的遥控器文件（github.com/" IR_DB_REPO "）：先把.ir文件下载到手机，再在这里选择。");
  h += "</small></p><input type='file' id='rf'><p><small id='rs'>";
  h += L3("Пультов: ", "Remotes: ", "遥控器："); h += String(irCount()); h += " / 12</small></p>";
  h += "<script>document.getElementById('rf').onchange=async function(){var s=document.getElementById('rs'),f=this.files[0];if(!f)return;"
       "s.textContent='...';try{var fd=new FormData();fd.append('f',f,f.name);var r=await fetch('/ir',{method:'POST',body:fd});"
       "s.textContent=await r.text()}catch(e){s.textContent=''+e}this.value=''};</script>";
  }
  h += "<h3>"; h += L3("Обновление прошивки", "Firmware update", "固件更新"); h += "</h3><p><small>";
  h += L3("Файл прошивки .bin. Сейчас стоит версия ", "Firmware .bin file. Installed version: ", "固件.bin文件。当前版本："); h += String(FW_VERSION);
  h += "</small></p><input type='file' id='ff'><p><small id='fs'></small></p>";
  h += "<script>document.getElementById('ff').onchange=async function(){var s=document.getElementById('fs'),f=this.files[0];if(!f)return;"
       "s.textContent='...';try{var fd=new FormData();fd.append('f',f,f.name);var r=await fetch('/fw',{method:'POST',body:fd});"
       "s.textContent=await r.text()}catch(e){s.textContent=''+e}this.value=''};</script></body></html>";
  server.send(200, "text/html; charset=utf-8", h);
}

void handleSave() {
  bool changed = false;
  for (int i = (int)netSsid.size() - 1; i >= 0; i--)   // забыть отмеченные сети
    if (server.arg((String("d") + String(i)).c_str()) == "1") {
      removeNet((size_t)i);
      changed = true;
    }
  String ssid = server.arg("m");
  ssid.trim();
  if (ssid.length() == 0) ssid = server.arg("s");
  if (ssid.length()) {
    addNet(ssid, server.arg("p"));
    changed = true;
  }
  String key = server.arg("k");
  key.trim();
  if (key.length()) {
    saveKey(key);
    changed = true;
  }
  if (!changed) {
    server.send(200, "text/html; charset=utf-8", pageHead() + "<p>" + T().webNoSsid + "</p><p><a href='/'>&larr;</a></p></body></html>");
    return;
  }
  server.send(200, "text/html; charset=utf-8", pageHead() + "<h2>" + T().webDone + "</h2></body></html>");
  portalSaved = true;
}

void handleNote() {
  String title = server.arg("t"), text = server.arg("x");
  title.trim();
  text.trim();
  String msg;
  if (text.length() == 0) {
    msg = L3("Текст пустой - нечего сохранять.", "The text is empty.", "正文为空。");
  } else {
    if (title.length() == 0) title = L3("Без названия", "Untitled", "无标题");
    if (addNote(title, text)) {
      msg = String(L3("Конспект сохранён: ", "Note saved: ", "笔记已保存：")) + htmlEscape(title);
      portalNote = String(L3("Получен конспект: ", "Note received: ", "已收到笔记：")) + title;
    } else {
      msg = L3("Нет места: уже 20 конспектов. Удалите лишние на устройстве.", "No room: 20 notes already. Delete some on the device.",
               "没有空间：已有20条笔记。请在设备上删除一些。");
    }
  }
  server.send(200, "text/html; charset=utf-8",
              pageHead() + "<p>" + msg + "</p><p><a href='/' style='color:#8ab4ff'>" + L3("Добавить ещё", "Add another", "继续添加") + "</a></p></body></html>");
}

void handleRedirect() {
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}

void buildScanList() {
  scanOptions = "";
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (s.length() == 0) continue;
    bool dup = false;                       // одна сеть может встречаться несколько раз
    for (int j = 0; j < i; j++) if (WiFi.SSID(j) == s) dup = true;
    if (dup) continue;
    String e = htmlEscape(s);
    scanOptions += "<option value=\"" + e + "\"";
    scanOptions += ">" + e + " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
  }
  WiFi.scanDelete();
}

void viewAnswer(const String& utf8, uint16_t fg);
// Приложение есть только в полной сборке.
void notInEdition() {
  viewAnswer(GAMES_EDITION ? L3("Этого нет в игровой сборке. Полная сборка: Настройки -> Обновление -> Сменить сборку.",
                                "Not available in the games edition. Full edition: Settings -> Update -> Switch edition.", "游戏版不包含此功能。")
                           : L3("Этого нет в сборке «с Bluetooth»: она не вмещает всё. Полная сборка: Настройки -> Обновление -> Сменить сборку.",
                                "Not available in the Bluetooth edition: it cannot hold everything. Full edition: Settings -> Update -> Switch edition.",
                                "蓝牙版不包含此功能。完整版：设置 -> 更新 -> 切换版本。"),
             C_YELLOW);
}

// Сборки прошивки: 0 - полная, 1 - с Bluetooth, 2 - игровая.
#if BOARD_S3
const char* edFile(int e) { return e == 1 ? "firmware-s3-ble.bin" : e == 2 ? "firmware-s3-games.bin" : "firmware-s3.bin"; }
#else
const char* edFile(int e) { return e == 1 ? "firmware-ble.bin" : e == 2 ? "firmware-games.bin" : "firmware.bin"; }
#endif
const char* edName(int e) {
  return e == 1 ? L3("с Bluetooth", "Bluetooth", "蓝牙版") : e == 2 ? L3("игровая", "games", "游戏版") : L3("полная", "full", "完整版");
}
const char* edAbout(int e) {
  return e == 1 ? L3("Сборка с Bluetooth: телефон как клавиатура и пульт для музыки. Без части игр, картинок из интернета, ИК-пульта и фонарика.",
                     "Bluetooth edition: phone keyboard and music remote. Without some games, web images, IR remote and torch.", "蓝牙版。")
         : e == 2 ? L3("Игровая сборка: только игры (в том числе большие), комната, пульт с телефона и настройки. Без DeepSeek-чата, справочника, калькулятора и остальных приложений.",
                       "Games edition: games only (including the big ones), room, phone pad and settings. No DeepSeek chat, reference, calculator or other apps.", "游戏版。")
                  : L3("Полная сборка: все приложения и игры, без Bluetooth.", "Full edition: all apps and games, no Bluetooth.", "完整版。");
}
// Точка доступа + страница настройки. Возвращается при отмене кнопкой,
// после сохранения перезапускает плату.
struct PadPause {                                    // пульт с телефона занимает порт 80 - на время настройки выключаем
  PadPause() { wpad::pause(true); }
  ~PadPause() { wpad::pause(false); }
};

void runPortal() {
  PadPause padPause;
  Serial.println("Wi-Fi setup portal");
  showMsg("Wi-Fi...", C_YELLOW);
  WiFi.disconnect(true);
  delay(300);
  WiFi.mode(WIFI_AP_STA);
  delay(200);
#if LOW_TX_POWER
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
#endif
  buildScanList();
  // Точка доступа: открытая сеть на канале 1. Если не включилась - пробуем ещё раз.
  bool apOk = false;
  for (int attempt = 0; attempt < 3 && !apOk; attempt++) {
    apOk = WiFi.softAP(AP_NAME, nullptr, 1, 0, 4);
    if (!apOk) {
      WiFi.mode(WIFI_OFF);
      delay(300);
      WiFi.mode(WIFI_AP_STA);
      delay(300);
    }
  }
#if LOW_TX_POWER
  // У плат Super Mini на полной мощности точку доступа часто не видно: мощность ставим после запуска точки,
  // иначе запуск её снова поднимает.
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
#endif
  delay(300);
  Serial.printf("Wi-Fi setup: access point %s %s, IP %s\n", AP_NAME, apOk ? "started" : "FAILED", WiFi.softAPIP().toString().c_str());
  if (!apOk) {
    viewAnswer(L3("Не удалось включить сеть для настройки. Перезапустите гаджет и попробуйте ещё раз.",
                  "Could not start the setup network. Restart the device and try again.", "无法启动设置网络。请重启设备后重试。"),
               C_RED);
    WiFi.mode(WIFI_STA);
    return;
  }
  dns.start(53, "*", WiFi.softAPIP());
  if (!serverReady) {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/note", HTTP_POST, handleNote);
    server.on("/pic", HTTP_POST, handlePicDone, handlePicUpload);
    if (ENABLE_IR) server.on("/ir", HTTP_POST, handleIrDone, handleIrUpload);
    server.on("/fw", HTTP_POST, handleFwDone, handleFwUpload);
    server.onNotFound(handleRedirect);   // открывает страницу сама (captive portal)
    serverReady = true;
  }
  server.begin();
  portalSaved = false;
  showMsg(fmt(T().portal, AP_NAME), C_WHITE);

  for (;;) {
    dns.processNextRequest();
    server.handleClient();
    if (portalSaved) {
      showMsg(T().portalSaved, C_GREEN);
      uint32_t t = millis();
      while (millis() - t < 1500) {        // даём странице «Сохранено» дойти до телефона
        server.handleClient();
        delay(10);
      }
      ESP.restart();
    }
    if (portalNote.length()) {             // с телефона пришёл конспект - сообщить и ждать дальше
      showMsg(portalNote, C_GREEN);
      sndOk();
      portalNote = "";
      uint32_t t = millis();
      while (millis() - t < 2000) {
        server.handleClient();
        delay(10);
      }
      showMsg(fmt(T().portal, AP_NAME), C_WHITE);
    }
    if (btnEvent() == EV_CLICK) break;     // отмена
    delay(2);
  }
  server.stop();
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  delay(200);
}

// ========================= ПОДКЛЮЧЕНИЕ К WI-FI =========================
// Подключение идёт в фоне: плата сразу работает, а Wi-Fi подключается сам.
uint32_t wifiBeginMs = 0;
bool wifiStarted = false;

bool wifiWasOk = false;

void wifiStart() {
  if (netSsid.empty()) {
    cfgSsid = "";
    return;
  }
  if (netIdx < 0 || netIdx >= (int)netSsid.size()) netIdx = 0;
  cfgSsid = netSsid[(size_t)netIdx];
  cfgPass = netPass[(size_t)netIdx];
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);
#if LOW_TX_POWER
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
#endif
  WiFi.setAutoReconnect(true);
  WiFi.begin(cfgSsid.c_str(), cfgPass.c_str());
  wifiBeginMs = millis();
  wifiStarted = true;
  Serial.printf("Wi-Fi: trying network %d of %u\n", netIdx + 1, (unsigned)netSsid.size());
}

bool wifiOk() { return WiFi.status() == WL_CONNECTED; }

// Вызывать почаще: если связи долго нет - начинаем подключение заново (раз в 30 с).
void startNtp();
extern bool ntpStarted;

void wifiTick() {
  if (wifiOk()) {
    if (!ntpStarted) startNtp();                     // точное время - для часов
    if (!wifiWasOk) {
      wifiWasOk = true;
      if (netIdx > 0 && netIdx < (int)netSsid.size()) {   // подключились не к первой - в следующий раз начнём с неё
        String s1 = netSsid[(size_t)netIdx], p1 = netPass[(size_t)netIdx];
        netSsid.erase(netSsid.begin() + netIdx);
        netPass.erase(netPass.begin() + netIdx);
        netSsid.insert(netSsid.begin(), s1);
        netPass.insert(netPass.begin(), p1);
        netIdx = 0;
        storeNets();
      }
    }
    return;
  }
  wifiWasOk = false;
  if (netSsid.empty()) return;
  // одна сеть - повторяем раз в 30 с; несколько - пробуем следующую каждые 12 с
  uint32_t patience = netSsid.size() > 1 ? 12000 : 30000;
  if (!wifiStarted) wifiStart();
  else if (millis() - wifiBeginMs > patience) {
    netIdx = (netIdx + 1) % (int)netSsid.size();
    wifiStart();
  }
}

// 0 - подключено, 1 - подключается, 2 - сеть не настроена
int wifiState() {
  if (wifiOk()) return 0;
  return cfgSsid.length() ? 1 : 2;
}

void runGamesMenu();
int listScreen(const char* title, const std::vector<String>& items, const std::vector<String>& right, int sel);

// Ждём связь перед запросом к DeepSeek. true - подключились.
// allowGames: удержание открывает игры (поиграть, пока ждём).
bool waitWifi(bool allowGames) {
  for (;;) {
    if (wifiOk()) return true;
    if (cfgSsid.length()) {
      showMsg(T().wifiWait, C_YELLOW);
      uint32_t t0 = millis();
      while (millis() - t0 < 20000) {
        if (wifiOk()) return true;
        wifiTick();
        BtnEvent e = btnEvent();
        if (e == EV_CLICK) return false;
        if (e == EV_LONG && allowGames) {
          runGamesMenu();
          if (wifiOk()) return true;
          showMsg(T().wifiWait, C_YELLOW);
          t0 = millis();
        }
        delay(20);
      }
    }
    // не дождались (или сеть не настроена) - что дальше?
    std::vector<String> items = {T().wifiRetry, T().menuWifi, T().menuBack};
    if (allowGames) items.insert(items.begin() + 1, T().menuGames);
    int k = listScreen(T().wifiNoConn, items, std::vector<String>(), 0);
    if (k < 0 || k == (int)items.size() - 1) return false;
    if (k == 0) {
      netIdx = 0;
      wifiStart();
      continue;
    }
    if (allowGames && k == 1) {
      runGamesMenu();
      continue;
    }
    runPortal();                                     // после сохранения плата перезапустится
    wifiStart();
  }
}

// ========================= БАТАРЕЯ =========================
// Сброс экрана и определение схемы. Сброс подаём сразу на GPIO21 и GPIO3: к какой бы ножке ни был припаян RES,
// экран его получит. Потом смотрим на GPIO3 со слабой подтяжкой к земле: делитель батареи (100к/100к) даёт
// при этом 0,5-1,6 В; свободная ножка или RES экрана - около нуля либо около 3 В.
void displayReset() {
  pinMode(PIN_RST_A, OUTPUT);
  pinMode(PIN_RST_B, OUTPUT);
  digitalWrite(PIN_RST_A, HIGH);
  digitalWrite(PIN_RST_B, HIGH);
  delay(100);
  digitalWrite(PIN_RST_A, LOW);
  digitalWrite(PIN_RST_B, LOW);
  delay(100);
  digitalWrite(PIN_RST_A, HIGH);     // GPIO21 так и остаётся выходом с «1»
  digitalWrite(PIN_RST_B, HIGH);
  delay(20);
  bool bat = BATTERY_MODE == 1;
  uint32_t mv = 0;
  if (BATTERY_MODE < 0) {
    pinMode(PIN_RST_B, INPUT);
    analogReadResolution(12);
    analogSetPinAttenuation(PIN_BAT, ADC_11db);
    analogReadMilliVolts(PIN_BAT);                   // первое чтение переключает ножку в режим измерения
    gpio_pulldown_en((gpio_num_t)PIN_BAT);
    delay(30);
    for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(PIN_BAT);
    mv /= 8;
    gpio_pulldown_dis((gpio_num_t)PIN_BAT);
    bat = mv >= 450 && mv <= 1600;
  }
  BATTERY_ENABLED = bat;
  if (bat) {
    pinMode(PIN_RST_B, INPUT);                       // GPIO3 меряет батарею
  } else {
    pinMode(PIN_RST_B, OUTPUT);                      // GPIO3 держит RES экрана в «1»
    digitalWrite(PIN_RST_B, HIGH);
  }
  delay(200);                                        // экрану нужно время после сброса
  Serial.printf("reset done, GPIO3 with pull-down: %u mV, battery %d\n", (unsigned)mv, (int)bat);
}

float batVolts = 0;          // сглаженное напряжение
uint32_t batNextMs = 0;
int batShownPct = -2;        // что сейчас нарисовано (-1 = USB)

float readBatVolts() {
  uint32_t mv = 0;
  for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(PIN_BAT);
  return mv / 8.0f / 1000.0f * BAT_DIVIDER * BAT_CALIBRATION;
}

// Кривая разряда литиевого аккумулятора: напряжение -> процент.
int batPercent(float v) {
  static const float V[] = {3.27f, 3.61f, 3.69f, 3.71f, 3.73f, 3.75f, 3.77f, 3.79f, 3.80f, 3.82f, 3.84f,
                            3.85f, 3.87f, 3.91f, 3.95f, 3.98f, 4.02f, 4.08f, 4.11f, 4.15f, 4.20f};
  if (v <= V[0]) return 0;
  if (v >= V[20]) return 100;
  for (int i = 1; i <= 20; i++)
    if (v < V[i]) return (int)((i - 1) * 5 + (v - V[i - 1]) / (V[i] - V[i - 1]) * 5 + 0.5f);
  return 100;
}

// Обновляет напряжение раз в 5 секунд. true - если процент на экране надо перерисовать.
bool updateBattery() {
  if (!BATTERY_ENABLED) return false;
  uint32_t now = millis();
  if (batVolts > 0 && (int32_t)(now - batNextMs) < 0) return false;
  batNextMs = now + 5000;
  float v = readBatVolts();
  batVolts = batVolts <= 0 ? v : batVolts * 0.7f + v * 0.3f;
  int pct = batVolts > 4.35f ? -1 : batPercent(batVolts);   // больше 4,35 В - питание от USB
  if (pct == batShownPct) return false;
  batShownPct = pct;
  return true;
}

// Маленький виджет: время и заряд, прижат вправо к xRight. Мелким шрифтом.
void drawMiniStatus(int xRight, int y, uint16_t bg) {
  SmallFont small;
  char b[24] = "";
  struct tm t;
  if (timeNow(t)) snprintf(b, sizeof(b), "%02d:%02d", t.tm_hour, t.tm_min);
  if (BATTERY_ENABLED && batShownPct >= -1) {
    size_t n = strlen(b);
    if (batShownPct == -1) snprintf(b + n, sizeof(b) - n, "%sUSB", n ? " " : "");
    else snprintf(b + n, sizeof(b) - n, "%s%d%%", n ? " " : "", batShownPct);
  }
  if (!b[0]) return;
  int w = textWidth(b);
  tft.fillRect(xRight - w - 4, y, w + 4, GH, bg);
  uint16_t col = (BATTERY_ENABLED && batShownPct >= 0 && batShownPct <= 15) ? C_RED : C_LIGHT;
  drawUtf8(b, xRight - w, y, col, bg);
}

// ========================= ПАМЯТЬ ЧАТА =========================
std::vector<ChatMsg> chat;

// Убираем самые старые пары «вопрос-ответ», пока история не влезет в лимиты.
void trimHistory() {
  for (;;) {
    unsigned total = 0;
    for (const ChatMsg& m : chat) total += m.text.length();
    if ((int)chat.size() <= MAX_HISTORY_MSGS && total <= MAX_HISTORY_CHARS) return;
    if (chat.size() <= 2) return;           // последнюю пару оставляем всегда
    chat.erase(chat.begin(), chat.begin() + 2);
  }
}

int chatPairs() { return (int)chat.size() / 2; }

// Строка состояния внизу экрана: длина чата слева, батарея справа.
const int STATUS_Y = 216;

int statusMinute = -1;                               // какая минута сейчас показана в строке состояния

// Пора ли обновить часы в строке состояния (раз в минуту).
bool statusClockDue() {
  static uint32_t next = 0;
  if ((int32_t)(millis() - next) < 0) return false;
  next = millis() + 2000;
  struct tm t;
  return timeNow(t) && t.tm_min != statusMinute;
}

void drawStatusBar() {
  SmallFont small;
  tft.fillRect(0, STATUS_Y, 240, 240 - STATUS_Y, C_BLACK);
  tft.fillRect(0, STATUS_Y, 240, 1, C_GRAY);
  int sx = 2;
  {                                                  // время (когда оно известно), затем счётчик чата
    struct tm t;
    if (timeNow(t)) {
      char b[8];
      snprintf(b, sizeof(b), "%02d:%02d", t.tm_hour, t.tm_min);
      drawUtf8(b, sx, STATUS_Y + 5, C_WHITE, C_BLACK);
      sx += 46;
      statusMinute = t.tm_min;
    }
  }
  String cc = String(L3("Чат", "Chat", "聊")) + ":" + String(chatPairs());
  drawUtf8(cc.c_str(), sx, STATUS_Y + 5, C_LIGHT, C_BLACK);
  int ws = wifiState();                              // Wi-Fi: зелёный - есть, жёлтый - подключается, красный - не настроен
  drawUtf8("Wi-Fi", 100, STATUS_Y + 5, ws == 0 ? C_GREEN : ws == 1 ? C_YELLOW : C_RED, C_BLACK);
  if (roomUnread > 0) {                              // новые сообщения в комнате: конвертик и число
    int ex = 144, ey = STATUS_Y + 8;
    tft.drawRect(ex, ey, 14, 10, C_YELLOW);
    tft.drawLine(ex, ey, ex + 6, ey + 5, C_YELLOW);
    tft.drawLine(ex + 13, ey, ex + 7, ey + 5, C_YELLOW);
    String n = roomUnread > 9 ? String("9+") : String(roomUnread);
    drawUtf8(n.c_str(), ex + 17, STATUS_Y + 5, C_YELLOW, C_BLACK);
  }
  roomUnreadDirty = false;
  if (!BATTERY_ENABLED || batShownPct < -1) return;
  if (batShownPct == -1) {
    drawUtf8("USB", 240 - 3 * GW - 2, STATUS_Y + 5, C_LIGHT, C_BLACK);
    return;
  }
  uint16_t col = batShownPct > 30 ? C_GREEN : batShownPct > 15 ? C_YELLOW : C_RED;
  String t = String(batShownPct) + "%";
  int tw = (int)t.length() * GW;
  int tx = 240 - tw - 2;
  drawUtf8(t.c_str(), tx, STATUS_Y + 5, col, C_BLACK);
  // значок батареи 20x10 слева от процентов
  int bx = tx - 26, by = STATUS_Y + 8;
  tft.fillRect(bx, by, 20, 10, C_LIGHT);
  tft.fillRect(bx + 1, by + 1, 18, 8, C_BLACK);
  tft.fillRect(bx + 20, by + 3, 2, 4, C_LIGHT);
  int fw = (16 * batShownPct + 50) / 100;
  if (fw > 0) tft.fillRect(bx + 2, by + 2, fw, 6, col);
}

// ========================= ИГРА «БЛОКИ» =========================
// Поле 8x8 клеток по 24 пикселя слева, лоток с тремя фигурами справа.
const int GCELL = 24, GX0 = 0, GY0 = 22;          // поле: x 0..191, y 22..213
const int TRAY_X = 194, TRAY_W = 46, SLOT_H = 64;   // лоток справа
const int MINI = 8;                                 // клетка фигуры в лотке
const uint16_t C_EMPTY_CELL = 0x18E3;

blocks::Game game;
bool gameStarted = false;

uint32_t gameRandom() { return esp_random(); }

uint16_t darker(uint16_t c) { return (c >> 1) & 0x7BEF; }
uint16_t lighter(uint16_t c) { return ((c >> 1) & 0x7BEF) + 0x7BEF; }

void drawBoardCellColor(int r, int c, uint16_t col, bool shine) {
  int x = GX0 + c * GCELL, y = GY0 + r * GCELL;
  tft.fillRect(x + 1, y + 1, GCELL - 2, GCELL - 2, col);
  if (shine) tft.fillRect(x + 1, y + 1, GCELL - 2, 3, lighter(col));
}

void drawBoardCell(int r, int c) {
  uint16_t v = game.cell[r][c];
  drawBoardCellColor(r, c, v ? v : C_EMPTY_CELL, v != 0);
}

void drawBoard() {
  tft.fillRect(GX0, GY0, GCELL * blocks::N, GCELL * blocks::N, C_BLACK);
  for (int r = 0; r < blocks::N; r++)
    for (int c = 0; c < blocks::N; c++) drawBoardCell(r, c);
}

// Призрак фигуры на поле: тёмный цвет фигуры, если встаёт; красный, если нет.
void drawGhost(int idx, int gr, int gc) {
  blocks::Piece p = blocks::Game::piece(idx);
  bool ok = game.canPlace(idx, gr, gc);
  for (int r = 0; r < p.h; r++)
    for (int c = 0; c < p.w; c++) {
      if (!blocks::pieceCell(p, r, c)) continue;
      int br = gr + r, bc = gc + c;
      if (ok) {
        drawBoardCellColor(br, bc, darker(p.color), false);
      } else if (game.cell[br][bc]) {
        drawBoardCell(br, bc);
        int x = GX0 + bc * GCELL, y = GY0 + br * GCELL;
        tft.fillRect(x + 7, y + 7, GCELL - 14, GCELL - 14, C_RED);
      } else {
        drawBoardCellColor(br, bc, darker(C_RED), false);
      }
    }
}

void eraseGhost(int idx, int gr, int gc) {
  blocks::Piece p = blocks::Game::piece(idx);
  for (int r = 0; r < p.h; r++)
    for (int c = 0; c < p.w; c++)
      if (blocks::pieceCell(p, r, c)) drawBoardCell(gr + r, gc + c);
}

// mode: 0 - обычный, 1 - выбран (жёлтая рамка), 2 - взят и ставится (белая рамка)
void drawTraySlot(int i, int mode) {
  int y0 = GY0 + i * SLOT_H;
  tft.fillRect(TRAY_X, y0, TRAY_W, SLOT_H - 2, C_BLACK);
  if (mode) {
    uint16_t fc = mode == 1 ? C_YELLOW : C_WHITE;
    tft.fillRect(TRAY_X, y0, TRAY_W, 2, fc);
    tft.fillRect(TRAY_X, y0 + SLOT_H - 4, TRAY_W, 2, fc);
    tft.fillRect(TRAY_X, y0, 2, SLOT_H - 2, fc);
    tft.fillRect(TRAY_X + TRAY_W - 2, y0, 2, SLOT_H - 2, fc);
  }
  int idx = game.tray[i];
  if (idx < 0) return;
  blocks::Piece p = blocks::Game::piece(idx);
  int px = TRAY_X + (TRAY_W - p.w * MINI) / 2;
  int py = y0 + (SLOT_H - 2 - p.h * MINI) / 2;
  bool fits = game.fitsAnywhere(idx);
  uint16_t col = fits ? p.color : C_GRAY;           // серым - если ей уже некуда встать
  for (int r = 0; r < p.h; r++)
    for (int c = 0; c < p.w; c++)
      if (blocks::pieceCell(p, r, c)) tft.fillRect(px + c * MINI, py + r * MINI, MINI - 1, MINI - 1, col);
}

void drawGameTop() {
  SmallFont small;
  tft.fillRect(0, 0, 240, GY0 - 2, C_BLACK);
  char b[48];
  snprintf(b, sizeof(b), T().gameScore, (unsigned)game.score);
  drawUtf8(b, 2, 2, C_WHITE, C_BLACK);
  snprintf(b, sizeof(b), T().gameBest, (unsigned)game.best);
  drawUtf8(b, 240 - textWidth(b) - 2, 2, C_YELLOW, C_BLACK);
}

void drawGameHint(const char* h) {
  SmallFont small;
  tft.fillRect(0, 222, 240, 18, C_BLACK);
  drawUtf8(h, (240 - textWidth(h)) / 2, 223, C_LIGHT, C_BLACK);
}

// Партия пишется в память платы после каждого хода - переживает выключение.
void saveGameState() {
  uint8_t buf[blocks::Game::SAVE_SIZE];
  size_t n = game.saveTo(buf);
  prefs.begin("dsk", false);
  prefs.putBytes("game", buf, n);
  prefs.end();
}

void clearGameState() {
  prefs.begin("dsk", false);
  prefs.remove("game");
  prefs.end();
}

// true - если удалось продолжить сохранённую партию.
bool loadGameState() {
  uint8_t buf[blocks::Game::SAVE_SIZE];
  prefs.begin("dsk", true);
  size_t n = prefs.getBytesLength("game");
  bool ok = n == sizeof(buf) && prefs.getBytes("game", buf, n) == n;
  prefs.end();
  return ok && game.loadFrom(buf, n) && !game.over;
}

void saveBest() {
  prefs.begin("dsk", false);
  if (game.best > prefs.getUInt("best", 0)) prefs.putUInt("best", game.best);
  prefs.end();
}

int firstFreeSlot(int from, int dir) {
  for (int k = 0; k < blocks::SLOTS; k++) {
    int i = ((from + dir * k) % blocks::SLOTS + blocks::SLOTS) % blocks::SLOTS;
    if (game.tray[i] >= 0) return i;
  }
  return -1;
}

// Вспышка сгоревших рядов и столбцов.
void flashCleared() {
  for (int r = 0; r < blocks::N; r++)
    for (int c = 0; c < blocks::N; c++)
      if ((game.clearedRows >> r & 1) || (game.clearedCols >> c & 1)) drawBoardCellColor(r, c, C_WHITE, false);
  delay(160);
}

// Возвращает true, если играть заново; false - выйти в меню.
// Окошко итога: заголовок и строка; нажатие - true (ещё раз), удержание - false (выход).
bool resultBox(const char* title, const char* line, uint16_t titleCol) {
  UiPause uiPause;
  if (titleCol == C_GREEN) sndWin();
  else if (titleCol == C_RED) sndLose();
  else sndOk();
  tft.fillRect(6, 74, 228, 92, C_BLACK);
  tft.fillRect(6, 74, 228, 2, titleCol);
  tft.fillRect(6, 164, 228, 2, titleCol);
  drawUtf8(title, (240 - textWidth(title)) / 2 < 0 ? 0 : (240 - textWidth(title)) / 2, 82, titleCol, C_BLACK);
  drawUtf8(line, (240 - textWidth(line)) / 2 < 0 ? 0 : (240 - textWidth(line)) / 2, 110, C_WHITE, C_BLACK);
  {
    SmallFont small;
    drawUtf8(T().gameAgain, (240 - textWidth(T().gameAgain)) / 2, 142, C_LIGHT, C_BLACK);
  }
  delay(400);                                   // чтобы случайное нажатие не перезапустило сразу
  while (btnEvent() != EV_NONE) delay(5);
  return waitButton() == EV_CLICK;
}

bool gameOverBox(uint32_t score) {
  char b[48];
  snprintf(b, sizeof(b), T().gameScore, (unsigned)score);
  return resultBox(T().gameOver, b, C_RED);
}

bool gameOverScreen() {
  saveBest();
  return gameOverBox(game.score);
}

bool askWith(const char* system, const std::vector<ChatMsg>& history, const String& question, int maxTokens, String& result);

const char* blocksPickHint() { return L3("Нажать - взять, влево - совет", "Click - take, left - advice", "按下：拿起，左：建议"); }

// Подсказка хода: плата отбирает лучшие варианты, DeepSeek (если есть интернет) выбирает из них и объясняет.
// Без интернета советует сама плата. false - ходов нет.
bool blocksAdvice(int& slot, int& row, int& col, String& why) {
  blocks::Advice adv[6];
  int n = blocks::bestMoves(game, adv, 6);
  if (!n) return false;
  int pick = 0;
  bool ai = false;
  if (wifiOk() && cfgKey.length()) {
    drawGameHint(L3("DeepSeek думает...", "DeepSeek is thinking...", "DeepSeek思考中……"));
    std::vector<ChatMsg> none;
    String reply;
    if (askWith(blocks::adviceSystem(T().promptLang).c_str(), none, String(blocks::adviceQuestion(game, adv, n).c_str()), 60, reply)) {
      std::string w;
      int k = blocks::parseAdvice(std::string(reply.c_str()), n, w);
      if (k) {
        pick = k - 1;
        ai = true;
        why = String("DeepSeek: ") + w.c_str();
      }
    }
    noteActivity();
  }
  if (!ai) {
    char b[64];
    if (adv[0].lines) snprintf(b, sizeof(b), "%s %d", L3("Плата: сгорит линий:", "Board: lines cleared:", "设备：消除行数："), adv[0].lines);
    else snprintf(b, sizeof(b), "%s", L3("Плата: так больше места", "Board: keeps more room", "设备：这样空间更大"));
    why = b;
  }
  slot = adv[pick].slot;
  row = adv[pick].row;
  col = adv[pick].col;
  return true;
}

// Совет на поле: клетки фигуры в белой рамке.
void drawAdviceGhost(int idx, int gr, int gc) {
  blocks::Piece p = blocks::Game::piece(idx);
  for (int r = 0; r < p.h; r++)
    for (int c = 0; c < p.w; c++) {
      if (!blocks::pieceCell(p, r, c)) continue;
      int x = GX0 + (gc + c) * GCELL, y = GY0 + (gr + r) * GCELL;
      drawBoardCellColor(gr + r, gc + c, darker(p.color), false);
      tft.drawRect(x + 1, y + 1, GCELL - 2, GCELL - 2, C_WHITE);
      tft.drawRect(x + 2, y + 2, GCELL - 4, GCELL - 4, C_WHITE);
    }
}

void runGame() {
  if (!gameStarted || game.over) {
    prefs.begin("dsk", true);
    game.best = prefs.getUInt("best", 0);
    prefs.end();
    game.rnd = gameRandom;
    if (gameStarted || !loadGameState()) game.reset();   // после включения - продолжаем сохранённую
    gameStarted = true;
  }
  for (;;) {                                    // одна партия (или продолжение)
    tft.fillScreen(C_BLACK);
    drawGameTop();
    drawBoard();
    int slot = firstFreeSlot(0, 1);
    for (int i = 0; i < blocks::SLOTS; i++) drawTraySlot(i, i == slot ? 1 : 0);
    drawGameHint(blocksPickHint());
    bool placing = false;
    int gr = 0, gc = 0;
    int advSlot = -1, advRow = 0, advCol = 0;        // показанный совет (advSlot < 0 - нет)
    auto clearAdvice = [&]() {
      if (advSlot >= 0 && game.tray[advSlot] >= 0) eraseGhost(game.tray[advSlot], advRow, advCol);
      advSlot = -1;
    };

    for (;;) {
      uint8_t j = joyEvent();
      BtnEvent e = btnEvent();
      if (!placing) {
        if (j == J_LEFT && !g_joyRepeated) {   // совет: какую фигуру и куда поставить
          clearAdvice();
          String why;
          int s2, r2, c2;
          if (blocksAdvice(s2, r2, c2, why)) {
            advSlot = s2;
            advRow = r2;
            advCol = c2;
            if (slot != s2) {
              drawTraySlot(slot, 0);
              slot = s2;
              drawTraySlot(slot, 1);
            }
            drawAdviceGhost(game.tray[advSlot], advRow, advCol);
            drawGameHint(why.c_str());
          }
          while (btnEvent() != EV_NONE) delay(5);
        } else if (j == J_UP || j == J_DOWN || j == J_RIGHT) {
          int dir = j == J_UP ? -1 : 1;
          int ns = firstFreeSlot(slot + dir, dir);
          if (ns >= 0 && ns != slot) {
            drawTraySlot(slot, 0);
            slot = ns;
            drawTraySlot(slot, 1);
          }
        }
        if (e == EV_LONG) {                    // выход в меню, партия сохранена
          saveBest();
          saveGameState();
          return;
        }
        if (e == EV_CLICK && slot >= 0) {
          blocks::Piece p = blocks::Game::piece(game.tray[slot]);
          gr = (blocks::N - p.h) / 2;
          gc = (blocks::N - p.w) / 2;
          if (advSlot == slot) {                 // взяли фигуру из совета - она сразу встаёт на предложенное место
            gr = advRow;
            gc = advCol;
          }
          clearAdvice();
          placing = true;
          drawTraySlot(slot, 2);
          drawGhost(game.tray[slot], gr, gc);
          drawGameHint(T().gameHintPlace);
        }
      } else {
        int idx = game.tray[slot];
        blocks::Piece p = blocks::Game::piece(idx);
        if (j != J_NONE) {
          int nr = gr + (j == J_DOWN) - (j == J_UP);
          int nc = gc + (j == J_RIGHT) - (j == J_LEFT);
          if (nr < 0) nr = 0;
          if (nc < 0) nc = 0;
          if (nr > blocks::N - p.h) nr = blocks::N - p.h;
          if (nc > blocks::N - p.w) nc = blocks::N - p.w;
          if (nr != gr || nc != gc) {
            eraseGhost(idx, gr, gc);
            gr = nr;
            gc = nc;
            drawGhost(idx, gr, gc);
          }
        }
        if (e == EV_LONG) {                    // отмена - вернуть фигуру в лоток
          eraseGhost(idx, gr, gc);
          placing = false;
          drawTraySlot(slot, 1);
          drawGameHint(blocksPickHint());
        }
        if (e == EV_CLICK && game.canPlace(idx, gr, gc)) {
          int lines = game.place(slot, gr, gc);
          if (lines > 0) beep(1, 1100 + lines * 250, 50);
          else beep(1, 500, 8);
          if (lines > 0) flashCleared();
          drawBoard();
          drawGameTop();
          placing = false;
          slot = firstFreeSlot(slot, 1);
          for (int i = 0; i < blocks::SLOTS; i++) drawTraySlot(i, i == slot ? 1 : 0);
          drawGameHint(blocksPickHint());
          if (game.over) {
            clearGameState();
            if (!gameOverScreen()) return;
            game.reset();
            saveGameState();
            break;                              // новая партия - перерисовать всё
          }
          saveGameState();
        }
      }
      delay(2);
    }
  }
}

// ========================= ОБЩЕЕ ДЛЯ ИГР =========================
uint32_t loadBestKey(const char* key) {
  prefs.begin("dsk", true);
  uint32_t v = prefs.getUInt(key, 0);
  prefs.end();
  return v;
}

void saveBestKey(const char* key, uint32_t v) {
  prefs.begin("dsk", false);
  if (v > prefs.getUInt(key, 0)) {
    prefs.putUInt(key, v);
    hsDirty = true;
  }
  prefs.end();
}

// Верхняя строка игры: счёт слева, рекорд справа (мелким шрифтом).
void drawScoreBar(uint32_t score, uint32_t best) {
  SmallFont small;
  tft.fillRect(0, 0, 240, 18, C_BLACK);
  char b[48];
  snprintf(b, sizeof(b), T().gameScore, (unsigned)score);
  drawUtf8(b, 2, 1, C_WHITE, C_BLACK);
  snprintf(b, sizeof(b), T().gameBest, (unsigned)best);
  drawUtf8(b, 240 - textWidth(b) - 2, 1, C_YELLOW, C_BLACK);
}

void drawSmallCenteredBg(const char* s, int y, uint16_t col, uint16_t bg) {
  SmallFont small;
  tft.fillRect(0, y, 240, 16, bg);
  drawUtf8(s, (240 - textWidth(s)) / 2, y, col, bg);
}

void drawSmallCentered(const char* s, int y, uint16_t col) { drawSmallCenteredBg(s, y, col, C_BLACK); }

// ========================= 2048 =========================
g2048::Game g48;
bool g48Started = false;
const int T48 = 46, GAP48 = 4, X48 = (240 - (4 * T48 + 5 * GAP48)) / 2, Y48 = 20;

uint16_t tileColor(uint8_t e) {
  static const uint16_t C[] = {0x4208, 0xEF3B, 0xEEF9, 0xF58F, 0xF4AC, 0xF3EB, 0xF2E7,
                               0xEE6E, 0xEE6C, 0xEE4A, 0xEE27, 0xEE05};
  return e < 12 ? C[e] : 0x39C7;
}

void draw48Tile(int r, int c) {
  uint8_t e = g48.cell[r][c];
  int x = X48 + GAP48 + c * (T48 + GAP48), y = Y48 + GAP48 + r * (T48 + GAP48);
  uint16_t bg = tileColor(e);
  tft.fillRect(x, y, T48, T48, bg);
  if (!e) return;
  char b[12];
  snprintf(b, sizeof(b), "%lu", (unsigned long)g2048::Game::value(e));
  uint16_t fg = e <= 2 ? 0x7BAE : C_WHITE;
  int n = (int)strlen(b);
  if (n <= 3) {
    drawUtf8(b, x + (T48 - n * 12) / 2, y + (T48 - 24) / 2, fg, bg);
  } else {
    SmallFont small;
    drawUtf8(b, x + (T48 - n * 8) / 2, y + (T48 - 16) / 2, fg, bg);
  }
}

void draw48Board() {
  tft.fillRect(X48, Y48, 4 * T48 + 5 * GAP48, 4 * T48 + 5 * GAP48, 0x3186);
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++) draw48Tile(r, c);
}

void save48() {
  uint8_t buf[g2048::Game::SAVE_SIZE];
  size_t n = g48.saveTo(buf);
  prefs.begin("dsk", false);
  prefs.putBytes("g2048", buf, n);
  prefs.end();
}

void clear48() {
  prefs.begin("dsk", false);
  prefs.remove("g2048");
  prefs.end();
}

bool load48() {
  uint8_t buf[g2048::Game::SAVE_SIZE];
  prefs.begin("dsk", true);
  size_t n = prefs.getBytesLength("g2048");
  bool ok = n == sizeof(buf) && prefs.getBytes("g2048", buf, n) == n;
  prefs.end();
  return ok && g48.loadFrom(buf, n) && !g48.over;
}

void run2048() {
  if (!g48Started || g48.over) {
    g48.best = loadBestKey("b2048");
    g48.rnd = gameRandom;
    if (g48Started || !load48()) g48.reset();
    g48Started = true;
  }
  bool wonShown = g48.won;
  for (;;) {
    tft.fillScreen(C_BLACK);
    drawScoreBar(g48.score, g48.best);
    draw48Board();
    drawSmallCentered(T().hint2048, 224, C_LIGHT);
    for (;;) {
      uint8_t j = joyEvent();
      BtnEvent e = btnEvent();
      if (e == EV_LONG) {
        saveBestKey("b2048", g48.best);
        save48();
        return;
      }
      if (j != J_NONE && !g_joyRepeated) {        // один наклон - один ход
        int dir = j == J_UP ? g2048::UP : j == J_DOWN ? g2048::DOWN : j == J_LEFT ? g2048::LEFT : g2048::RIGHT;
        if (g48.move(dir)) {
          { static uint32_t ps = 0; beep(1, g48.score > ps ? 1300 : 600, g48.score > ps ? 18 : 5); ps = g48.score; }
          draw48Board();
          drawScoreBar(g48.score, g48.best);
          save48();
          if (g48.won && !wonShown) {
            wonShown = true;
            drawSmallCentered(T().won2048, 224, C_YELLOW);
            delay(1500);
            drawSmallCentered(T().hint2048, 224, C_LIGHT);
          }
        }
        if (g48.over) {
          clear48();
          saveBestKey("b2048", g48.best);
          if (!gameOverBox(g48.score)) return;
          g48.reset();
          wonShown = false;
          save48();
          break;
        }
      }
      delay(2);
    }
  }
}

// ========================= ЗМЕЙКА =========================
// Поле 20x18 клеток по 12 пикселей, под строкой счёта.
const int SC = 12, SY0 = 24;
const uint16_t C_SNAKE = 0x2DC5, C_SNAKE_HEAD = 0x5FEA, C_FOOD = 0xF8C3, C_FIELD = 0x0861;

void drawSnakeCell(int x, int y, uint8_t what) {
  int px = x * SC, py = SY0 + y * SC;
  if (what == 0) {
    tft.fillRect(px, py, SC, SC, C_FIELD);
    return;
  }
  tft.fillRect(px, py, SC, SC, C_FIELD);
  uint16_t col = what == 1 ? C_SNAKE : what == 2 ? C_SNAKE_HEAD : C_FOOD;
  if (what == 3) tft.fillRect(px + 2, py + 2, SC - 4, SC - 4, col);
  else tft.fillRect(px + 1, py + 1, SC - 2, SC - 2, col);
}

void runSnake() {
  snake::Game sg;
  sg.rnd = gameRandom;
  sg.best = loadBestKey("bsnake");
  sg.reset();
  static uint8_t shown[snake::H][snake::W];
  for (;;) {                                         // одна партия
    tft.fillScreen(C_BLACK);
    tft.fillRect(0, SY0, 240, 240 - SY0, C_FIELD);
    memset(shown, 0, sizeof(shown));
    drawScoreBar(sg.score, sg.best);
    bool paused = true;                              // ждём первого нажатия
    drawSmallCentered(T().hintSnake, 120, C_WHITE);
    uint32_t nextStep = 0;
    bool needDraw = true;
    for (;;) {
      uint8_t j = joyEvent();
      BtnEvent e = btnEvent();
      if (e == EV_LONG) {
        saveBestKey("bsnake", sg.best);
        return;
      }
      if (e == EV_CLICK) {
        paused = !paused;
        if (paused) drawSmallCentered(T().paused, 120, C_WHITE);
        else {
          tft.fillRect(0, 120, 240, 16, C_FIELD);    // стираем надпись
          memset(shown, 0, sizeof(shown));
          tft.fillRect(0, SY0, 240, 240 - SY0, C_FIELD);
          needDraw = true;
          nextStep = millis() + sg.stepMs();
        }
      }
      if (!paused && j != J_NONE)
        sg.turn(j == J_UP ? snake::UP : j == J_DOWN ? snake::DOWN : j == J_LEFT ? snake::LEFT : snake::RIGHT);
      if (!paused && (int32_t)(millis() - nextStep) >= 0) {
        nextStep = millis() + sg.stepMs();
        int res = sg.step();
        if (res == 1) beep(1, 1500, 12);
        if (res == 1) drawScoreBar(sg.score, sg.best);
        needDraw = true;
        if (res < 0) {
          saveBestKey("bsnake", sg.best);
          if (!gameOverBox(sg.score)) return;
          sg.reset();
          break;
        }
      }
      if (needDraw && !paused) {                     // перерисовываем только изменившиеся клетки
        static uint8_t want[snake::H][snake::W];
        memset(want, 0, sizeof(want));
        for (int i = sg.len - 1; i >= 0; i--) want[sg.body[i].y][sg.body[i].x] = i == 0 ? 2 : 1;
        want[sg.food.y][sg.food.x] = 3;
        for (int y = 0; y < snake::H; y++)
          for (int x = 0; x < snake::W; x++)
            if (want[y][x] != shown[y][x]) {
              drawSnakeCell(x, y, want[y][x]);
              shown[y][x] = want[y][x];
            }
        needDraw = false;
      }
      delay(2);
    }
  }
}

// ========================= ПАДАЮЩИЕ БЛОКИ =========================
// Стакан 10x20 клеток по 11 пикселей слева, справа - следующая фигура и счёт.
const int FC = 11, FX0 = 6, FY0 = 10, PANEL_X = 126;
fall::Game fallG;
bool fallStarted = false;

void drawFallCell(int x, int y, uint16_t v) {
  int px = FX0 + x * FC, py = FY0 + y * FC;
  if (!v) {
    tft.fillRect(px, py, FC, FC, C_FIELD);
    return;
  }
  bool ghost = v & 1;                                // нечётный цвет - метка «тени»
  uint16_t col = v & 0xFFFE;
  if (ghost) {
    tft.fillRect(px, py, FC, FC, C_FIELD);
    tft.fillRect(px + 1, py + 1, FC - 2, 1, col);
    tft.fillRect(px + 1, py + FC - 2, FC - 2, 1, col);
    tft.fillRect(px + 1, py + 1, 1, FC - 2, col);
    tft.fillRect(px + FC - 2, py + 1, 1, FC - 2, col);
  } else {
    tft.fillRect(px, py, FC, FC, C_FIELD);
    tft.fillRect(px + 1, py + 1, FC - 2, FC - 2, col);
  }
}

void drawFallPanel() {
  SmallFont small;
  tft.fillRect(PANEL_X, 0, 240 - PANEL_X, 240, C_BLACK);
  int y = 8;
  drawUtf8(T().nextLabel, PANEL_X, y, C_LIGHT, C_BLACK);
  y += 20;
  for (int i = 0; i < 4; i++) {
    int x, yy;
    fall::Game::cellOf(fallG.nextKind, 0, i, x, yy);
    tft.fillRect(PANEL_X + 8 + x * 10, y + yy * 10, 9, 9, fall::KIND[fallG.nextKind].color);
  }
  y += 46;
  char b[40];
  snprintf(b, sizeof(b), T().gameScore, (unsigned)fallG.score);
  drawUtf8(b, PANEL_X, y, C_WHITE, C_BLACK);
  y += 20;
  snprintf(b, sizeof(b), T().linesFmt, (unsigned)fallG.lines);
  drawUtf8(b, PANEL_X, y, C_WHITE, C_BLACK);
  y += 20;
  snprintf(b, sizeof(b), T().levelFmt, (unsigned)fallG.level);
  drawUtf8(b, PANEL_X, y, C_WHITE, C_BLACK);
  y += 20;
  snprintf(b, sizeof(b), T().gameBest, (unsigned)fallG.best);
  drawUtf8(b, PANEL_X, y, C_YELLOW, C_BLACK);
  drawUtf8(T().fallRotate, PANEL_X, 184, C_LIGHT, C_BLACK);
  drawUtf8(T().fallDrop, PANEL_X, 202, C_LIGHT, C_BLACK);
  drawUtf8(T().fallExit, PANEL_X, 220, C_LIGHT, C_BLACK);
}

void runFalling() {
  if (!fallStarted || fallG.over) {
    fallG.rnd = gameRandom;
    fallG.best = loadBestKey("bfall");
    fallG.reset();
    fallStarted = true;
  }
  static uint16_t shown[fall::H][fall::W], want[fall::H][fall::W];
  for (;;) {
    tft.fillScreen(C_BLACK);
    tft.fillRect(FX0 - 2, FY0 - 2, fall::W * FC + 4, fall::H * FC + 4, C_GRAY);
    tft.fillRect(FX0, FY0, fall::W * FC, fall::H * FC, C_FIELD);
    for (int y = 0; y < fall::H; y++)
      for (int x = 0; x < fall::W; x++) shown[y][x] = 0;
    drawFallPanel();
    uint32_t nextFall = millis() + fallG.gravityMs();
    uint32_t lastScore = fallG.score;
    int lastNext = fallG.nextKind;
    for (;;) {
      uint8_t j = joyEvent();
      BtnEvent e = btnEvent();
      if (e == EV_LONG) {                            // выход; партия ждёт, пока плата включена
        saveBestKey("bfall", fallG.best);
        return;
      }
      if (j == J_LEFT) fallG.moveH(-1);
      if (j == J_RIGHT) fallG.moveH(1);
      if (j == J_UP && !g_joyRepeated) fallG.rotate();
      if (j == J_DOWN) {
        if (!fallG.down(true)) nextFall = millis() + fallG.gravityMs();
      }
      uint32_t linesBefore = fallG.lines;
      if (e == EV_CLICK) {
        fallG.drop();
        nextFall = millis() + fallG.gravityMs();
        beep(1, 400, 8);
      }
      if ((int32_t)(millis() - nextFall) >= 0) {
        fallG.down(false);
        nextFall = millis() + fallG.gravityMs();
      }
      if (fallG.lines > linesBefore) beep(1, 1000 + (int)(fallG.lines - linesBefore) * 250, 60);
      // картинка: стакан + тень + фигура; рисуем только изменения
      for (int y = 0; y < fall::H; y++)
        for (int x = 0; x < fall::W; x++) want[y][x] = fallG.cell[y][x] & 0xFFFE;
      if (!fallG.over) {
        uint16_t col = fall::KIND[fallG.cur].color & 0xFFFE;
        int gy = fallG.ghostY();
        for (int i = 0; i < 4; i++) {
          int x, y;
          fall::Game::cellOf(fallG.cur, fallG.rot, i, x, y);
          if (gy + y >= 0 && !want[gy + y][fallG.px + x]) want[gy + y][fallG.px + x] = col | 1;
        }
        for (int i = 0; i < 4; i++) {
          int x, y;
          fall::Game::cellOf(fallG.cur, fallG.rot, i, x, y);
          if (fallG.py + y >= 0) want[fallG.py + y][fallG.px + x] = col;
        }
      }
      for (int y = 0; y < fall::H; y++)
        for (int x = 0; x < fall::W; x++)
          if (want[y][x] != shown[y][x]) {
            drawFallCell(x, y, want[y][x]);
            shown[y][x] = want[y][x];
          }
      if (fallG.score != lastScore || fallG.nextKind != lastNext) {
        drawFallPanel();
        lastScore = fallG.score;
        lastNext = fallG.nextKind;
      }
      if (fallG.over) {
        saveBestKey("bfall", fallG.best);
        if (!gameOverBox(fallG.score)) return;
        fallG.reset();
        break;
      }
      delay(2);
    }
  }
}

// ========================= СПИСКИ И ВЫБОР =========================
uint32_t loadMinKey(const char* key) { return loadBestKey(key); }
void saveMinKey(const char* key, uint32_t v) {           // для рекордов «чем меньше, тем лучше»
  prefs.begin("dsk", false);
  uint32_t old = prefs.getUInt(key, 0);
  if (old == 0 || v < old) {
    prefs.putUInt(key, v);
    hsDirty = true;
  }
  prefs.end();
}

// Прокручиваемый список: крупные пункты, справа мелкая подпись. Возвращает номер или -1 (удержание).
int listScreen(const char* title, const std::vector<String>& items, const std::vector<String>& right, int sel) {
  UiPause uiPause;
  const int ROW = 30, Y0 = 32, VIS = 6;
  int n = (int)items.size(), top = 0;
  if (sel < 0 || sel >= n) sel = 0;
  auto drawRow = [&](int i) {
    int y = Y0 + (i - top) * ROW;
    uint16_t bg = i == sel ? C_SEL : C_BLACK;
    tft.fillRect(0, y, 240, ROW - 2, C_BLACK);
    if (i == sel) tft.fillRoundRect(2, y, 236, ROW - 2, 7, C_SEL);
    drawUtf8(items[(size_t)i].c_str(), 8, y + 2, C_WHITE, bg);
    if ((size_t)i < right.size() && right[(size_t)i].length()) {
      SmallFont small;
      drawUtf8(right[(size_t)i].c_str(), 240 - textWidth(right[(size_t)i].c_str()) - 6, y + 6, C_YELLOW, bg);
    }
  };
  auto drawAll = [&]() {
    tft.fillScreen(C_BLACK);
    tft.fillRect(0, 0, 240, 28, C_BAR);            // заголовок окна, как в настольной системе
    tft.fillRect(0, 28, 240, 1, C_ACCENT);
    drawUtf8(title, 6, 2, C_WHITE, C_BAR);
    updateBattery();
    drawMiniStatus(222, 6, C_BAR);                 // время и заряд - в правом углу любого меню
    for (int i = top; i < n && i < top + VIS; i++) drawRow(i);
    SmallFont small;                               // стрелки, если список длиннее экрана
    if (top > 0) drawUtf8("^", 228, 6, C_LIGHT, C_BAR);
    if (top + VIS < n) drawUtf8("v", 228, 218, C_LIGHT, C_BLACK);
  };
  if (sel >= VIS) top = sel - VIS + 1;
  drawAll();
  for (;;) {
    int d = joyVertical(joyEvent());
    if (d) {
      int old = sel;
      sel = ((sel + d) % n + n) % n;
      if (sel < top || sel >= top + VIS) {
        top = sel < top ? sel : sel - VIS + 1;
        drawAll();
      } else {
        drawRow(old);
        drawRow(sel);
      }
    }
    BtnEvent e = btnEvent();
    if (e == EV_CLICK) return sel;
    if (e == EV_LONG) return -1;
    delay(2);
  }
}

std::vector<String> splitBar(const char* s) {
  std::vector<String> out;
  String cur;
  for (const char* p = s;; p++) {
    if (*p == '|' || *p == 0) {
      out.push_back(cur);
      cur = "";
      if (!*p) break;
    } else {
      cur += *p;
    }
  }
  return out;
}

// Экран с текстом сверху и вариантами ниже - крупным шрифтом. Всё в одной колонке:
// если не помещается, экран прокручивается так, чтобы выбранный вариант был виден целиком.
// Возвращает номер варианта или -1 (удержание).
int choiceScreen(const String& header, const std::vector<String>& opts) {
  UiPause uiPause;
  struct Row {
    int opt;                                         // -1 - текст вопроса, -2 - пустая строка, иначе номер варианта
    std::vector<uint16_t> cps;
  };
  std::vector<Row> rows;
  std::vector<uint16_t> hc;
  utf8ToCps(header.c_str(), hc);
  std::vector<Line> hl;
  wrapCps(hc, COLS, hl);
  for (const Line& l : hl) rows.push_back({-1, std::vector<uint16_t>(hc.begin() + l.start, hc.begin() + l.start + l.len)});
  if (!hl.empty()) rows.push_back({-2, std::vector<uint16_t>()});
  int n = (int)opts.size();
  std::vector<int> first(opts.size()), last(opts.size());
  for (int i = 0; i < n; i++) {
    std::vector<uint16_t> oc;
    utf8ToCps(opts[(size_t)i].c_str(), oc);
    std::vector<Line> ol;
    wrapCps(oc, COLS - 1, ol);
    if (ol.empty()) ol.push_back({0, 0});
    first[(size_t)i] = (int)rows.size();
    for (const Line& l : ol) rows.push_back({i, std::vector<uint16_t>(oc.begin() + l.start, oc.begin() + l.start + l.len)});
    last[(size_t)i] = (int)rows.size() - 1;
  }
  int sel = 0, top = 0;
  auto fit = [&]() {                                 // прокрутить так, чтобы выбранный вариант был виден
    int t = top;
    if (last[(size_t)sel] - t >= ROWS) t = last[(size_t)sel] - ROWS + 1;
    if (first[(size_t)sel] < t) t = first[(size_t)sel];
    bool changed = t != top;
    top = t;
    return changed;
  };
  auto drawRow = [&](int idx) {
    int r = idx - top;
    if (r < 0 || r >= ROWS) return;
    int y = r * GH;
    if (idx >= (int)rows.size()) {
      tft.fillRect(0, y, 240, GH, C_BLACK);
      return;
    }
    const Row& row = rows[(size_t)idx];
    uint16_t bg = row.opt < 0 ? C_BLACK : row.opt == sel ? C_BLUE : (row.opt % 2 ? 0x2104 : 0x18C3);
    tft.fillRect(0, y, 240, GH, bg);
    int x = row.opt >= 0 ? GW / 2 : 0;
    for (uint16_t c : row.cps) {
      if (x + cellWidth(c) * GW > 240) break;
      x += drawCp(c, x, y, row.opt == -1 ? C_WHITE : C_WHITE, bg);
    }
  };
  auto drawAll = [&]() {
    for (int r = 0; r < ROWS; r++) drawRow(top + r);
  };
  auto drawOpt = [&](int i) {
    for (int k = first[(size_t)i]; k <= last[(size_t)i]; k++) drawRow(k);
  };
  fit();
  tft.fillScreen(C_BLACK);
  drawAll();
  for (;;) {
    int d = joyVertical(joyEvent());
    if (d && n > 0) {
      int old = sel;
      sel = ((sel + d) % n + n) % n;
      if (fit()) drawAll();
      else {
        drawOpt(old);
        drawOpt(sel);
      }
    }
    BtnEvent e = btnEvent();
    if (e == EV_CLICK && n > 0) return sel;
    if (e == EV_LONG) return -1;
    delay(2);
  }
}

// ========================= ЛАБИРИНТ =========================
arcade::maze::Game mz;
bool mzStarted = false;
const uint16_t C_MAZE_BG = 0x0841, C_MAZE_WALL = 0xBDF7, C_TRAIL = 0x2A6B;

void mazeGeom(int& cell, int& ox, int& oy) {
  cell = 220 / mz.n;
  ox = (240 - cell * mz.n) / 2;
  oy = 20 + (220 - cell * mz.n) / 2;
}

void drawMazeCellInner(int x, int y, uint16_t col, int inset) {
  int cell, ox, oy;
  mazeGeom(cell, ox, oy);
  int t = cell >= 14 ? 2 : 1;
  tft.fillRect(ox + x * cell + t + inset, oy + y * cell + t + inset, cell - t - 2 * inset, cell - t - 2 * inset, col);
}

void drawMaze() {
  int cell, ox, oy;
  mazeGeom(cell, ox, oy);
  int t = cell >= 14 ? 2 : 1;
  tft.fillRect(0, 18, 240, 222, C_BLACK);
  tft.fillRect(ox, oy, cell * mz.n + t, cell * mz.n + t, C_MAZE_BG);
  for (int y = 0; y < mz.n; y++)
    for (int x = 0; x < mz.n; x++) {
      int px = ox + x * cell, py = oy + y * cell;
      if (mz.wall[y][x] & arcade::maze::WN) tft.fillRect(px, py, cell + t, t, C_MAZE_WALL);
      if (mz.wall[y][x] & arcade::maze::WW) tft.fillRect(px, py, t, cell + t, C_MAZE_WALL);
      if (y == mz.n - 1 && (mz.wall[y][x] & arcade::maze::WS)) tft.fillRect(px, py + cell, cell + t, t, C_MAZE_WALL);
      if (x == mz.n - 1 && (mz.wall[y][x] & arcade::maze::WE)) tft.fillRect(px + cell, py, t, cell + t, C_MAZE_WALL);
    }
  drawMazeCellInner(mz.n - 1, mz.n - 1, C_GREEN, 1);
  drawMazeCellInner(mz.px, mz.py, C_YELLOW, 1);
}

void drawMazeBar(uint32_t secs) {
  SmallFont small;
  tft.fillRect(0, 0, 240, 18, C_BLACK);
  char b[40];
  snprintf(b, sizeof(b), T().levelFmt, (unsigned)mz.level);
  drawUtf8(b, 2, 1, C_WHITE, C_BLACK);
  snprintf(b, sizeof(b), T().timeFmt, (unsigned)secs);
  drawUtf8(b, 240 - textWidth(b) - 2, 1, C_YELLOW, C_BLACK);
}

void runMaze() {
  if (!mzStarted) {
    mz.rnd = gameRandom;
    mz.best = (int)loadBestKey("bmaze");
    mz.level = 1;
    mz.generate();
    mzStarted = true;
  }
  for (;;) {                                         // один уровень
    tft.fillScreen(C_BLACK);
    drawMaze();
    uint32_t t0 = millis(), shownSec = 0xFFFFFFFF;
    for (;;) {
      uint32_t secs = (millis() - t0) / 1000;
      if (secs != shownSec) {
        drawMazeBar(secs);
        shownSec = secs;
      }
      uint8_t j = joyEvent();
      BtnEvent e = btnEvent();
      if (e == EV_LONG) return;
      if (j != J_NONE) {
        int dir = j == J_UP ? 0 : j == J_RIGHT ? 1 : j == J_DOWN ? 2 : 3;
        int ox = mz.px, oy = mz.py;
        if (mz.move(dir)) {
          beep(2, 900, 3);
          drawMazeCellInner(ox, oy, C_MAZE_BG, 0);
          drawMazeCellInner(ox, oy, C_TRAIL, 3);      // след
          drawMazeCellInner(mz.px, mz.py, C_YELLOW, 1);
        }
      }
      if (mz.atExit()) {
        char b[64];
        snprintf(b, sizeof(b), T().mazeDone, (unsigned)mz.level, (unsigned)secs);
        drawSmallCentered(b, 222, C_GREEN);
        delay(1500);
        mz.nextLevel();
        saveBestKey("bmaze", (uint32_t)mz.best);
        break;
      }
      delay(2);
    }
  }
}

// ========================= ПАМЯТЬ =========================
arcade::memory::Game mem;
bool memStarted = false;                             // незаконченная партия продолжается, пока плата включена
const int MC = 50, MG = 4, MX0 = 10, MY0 = 18;

void drawMemSymbol(int k, int x, int y) {
  int cx = x + MC / 2, cy = y + MC / 2;
  switch (k) {
    case 0: tft.fillRect(cx - 12, cy - 12, 24, 24, 0xF8C3); break;                         // красный квадрат
    case 1: tft.fillCircle(cx, cy, 13, 0x2DC5); break;                                      // зелёный круг
    case 2: tft.fillTriangle(cx, cy - 14, cx - 14, cy + 12, cx + 14, cy + 12, 0x3C1F); break;  // синий треугольник
    case 3: tft.fillRect(cx - 14, cy - 4, 28, 8, 0xFC40); tft.fillRect(cx - 4, cy - 14, 8, 28, 0xFC40); break;  // крест
    case 4: tft.fillCircle(cx, cy, 14, 0xA27F); tft.fillCircle(cx, cy, 7, 0xEF7D); break;   // фиолетовое кольцо
    case 5: tft.fillTriangle(cx, cy - 15, cx - 12, cy, cx + 12, cy, 0x2EDD);
            tft.fillTriangle(cx, cy + 15, cx - 12, cy, cx + 12, cy, 0x2EDD); break;          // голубой ромб
    case 6: tft.fillRect(cx - 13, cy - 13, 13, 13, 0xEE05); tft.fillRect(cx, cy, 13, 13, 0xEE05); break;  // шахматка
    default: for (int i = 0; i < 3; i++) tft.fillRect(cx - 14, cy - 12 + i * 10, 28, 5, 0xF97B); break;   // полоски
  }
}

void drawMemCard(int i, bool cursor) {
  int x = MX0 + MG + (i % 4) * (MC + MG), y = MY0 + MG + (i / 4) * (MC + MG);
  tft.fillRect(x - 2, y - 2, MC + 4, MC + 4, cursor ? C_YELLOW : C_BLACK);
  uint8_t st = mem.state[i];
  if (st == arcade::memory::HIDDEN) {
    tft.fillRect(x, y, MC, MC, 0x3A7F);
    tft.fillRect(x + 4, y + 4, MC - 8, MC - 8, 0x29B5);
  } else {
    tft.fillRect(x, y, MC, MC, st == arcade::memory::MATCHED ? 0x8C51 : 0xEF7D);
    drawMemSymbol(mem.card[i], x, y);
  }
}

void drawMemBar() {
  SmallFont small;
  tft.fillRect(0, 0, 240, 18, C_BLACK);
  char b[40];
  snprintf(b, sizeof(b), T().movesFmt, (unsigned)mem.moves);
  drawUtf8(b, 2, 1, C_WHITE, C_BLACK);
  uint32_t best = loadBestKey("bmem");
  if (best) {
    snprintf(b, sizeof(b), T().gameBest, (unsigned)best);
    drawUtf8(b, 240 - textWidth(b) - 2, 1, C_YELLOW, C_BLACK);
  }
}

void runMemory() {
  mem.rnd = gameRandom;
  if (!memStarted || mem.done()) mem.reset();
  memStarted = true;
  mem.hideMismatch();                                // если вышли, пока пара была открыта
  for (;;) {
    tft.fillScreen(C_BLACK);
    drawMemBar();
    int cur = 0;
    for (int i = 0; i < arcade::memory::CARDS; i++) drawMemCard(i, i == cur);
    for (;;) {
      uint8_t j = joyEvent();
      BtnEvent e = btnEvent();
      if (e == EV_LONG) return;
      if (j != J_NONE) {
        int old = cur, x = cur % 4, y = cur / 4;
        if (j == J_LEFT) x = (x + 3) % 4;
        if (j == J_RIGHT) x = (x + 1) % 4;
        if (j == J_UP) y = (y + 3) % 4;
        if (j == J_DOWN) y = (y + 1) % 4;
        cur = y * 4 + x;
        drawMemCard(old, false);
        drawMemCard(cur, true);
      }
      if (e == EV_CLICK) {
        int a = mem.first;
        int res = mem.pick(cur);
        if (res != arcade::memory::INVALID) {
          drawMemCard(cur, true);
          if (res == arcade::memory::MATCH) {
            beep(1, 1400, 40);
            drawMemCard(a, false);
            drawMemBar();
          }
          if (res == arcade::memory::MISMATCH) {
            beep(1, 300, 50);
            drawMemBar();
            delay(800);                              // запомнить, что было открыто
            int b = mem.second;
            mem.hideMismatch();
            drawMemCard(a, a == cur);
            drawMemCard(b, b == cur);
          }
        }
        if (mem.done()) {
          saveMinKey("bmem", (uint32_t)mem.moves);
          char b[64];
          snprintf(b, sizeof(b), T().movesFmt, (unsigned)mem.moves);
          if (!resultBox(T().gMemory, b, C_GREEN)) return;
          mem.reset();
          break;
        }
      }
      delay(2);
    }
  }
}

// ========================= ПИНГ-ПОНГ =========================
arcade::pong::Game png;

void drawPongBar() {
  SmallFont small;
  tft.fillRect(0, 0, 240, 18, C_BLACK);
  char b[64];
  snprintf(b, sizeof(b), "%s  %d : %d  %s", T().pongYou, png.playerScore, png.cpuScore, T().pongCpu);
  drawUtf8(b, (240 - textWidth(b)) / 2, 1, C_WHITE, C_BLACK);
}

void drawPongNet(int y0, int y1) {
  for (int y = 20; y < 240; y += 12)
    if (y + 6 >= y0 && y <= y1) tft.fillRect(119, y, 2, 6, C_GRAY);
}

// Сдвиг ракетки: стираем/дорисовываем только полоску, которая изменилась (без мерцания).
void movePaddle(int x, int oldY, int newY) {
  const int W = (int)arcade::pong::PW, H = (int)arcade::pong::PH;
  if (newY == oldY) return;
  if (newY > oldY) {
    int d = newY - oldY < H ? newY - oldY : H;
    tft.fillRect(x, oldY, W, d, C_BLACK);
    tft.fillRect(x, newY + H - d, W, d, C_WHITE);
  } else {
    int d = oldY - newY < H ? oldY - newY : H;
    tft.fillRect(x, oldY + H - d, W, d, C_BLACK);
    tft.fillRect(x, newY, W, d, C_WHITE);
  }
}

void runPong() {
  png.rnd = gameRandom;
  png.reset();
  for (;;) {
    tft.fillScreen(C_BLACK);
    drawPongBar();
    drawPongNet(0, 240);
    int opy = (int)png.py, ocy = (int)png.cy, obx = (int)png.bx, oby = (int)png.by;
    tft.fillRect((int)arcade::pong::PLAYER_X, opy, (int)arcade::pong::PW, (int)arcade::pong::PH, C_WHITE);
    tft.fillRect((int)arcade::pong::CPU_X, ocy, (int)arcade::pong::PW, (int)arcade::pong::PH, C_WHITE);
    uint32_t next = millis();
    for (;;) {
      BtnEvent e = btnEvent();
      if (e == EV_LONG) return;
      if ((int32_t)(millis() - next) < 0) {
        delay(1);
        continue;
      }
      next += 16;                                   // ~60 кадров в секунду
      uint8_t raw = joyRaw();
      int in = raw == J_UP ? -1 : raw == J_DOWN ? 1 : 0;
      int r = png.step(in);
      if (r != 0) beep(1, 900, 25);
      int npy = (int)png.py, ncy = (int)png.cy, nbx = (int)png.bx, nby = (int)png.by;
      tft.fillRect(obx, oby, (int)arcade::pong::BALL, (int)arcade::pong::BALL, C_BLACK);
      drawPongNet(oby, oby + (int)arcade::pong::BALL);
      movePaddle((int)arcade::pong::PLAYER_X, opy, npy);
      movePaddle((int)arcade::pong::CPU_X, ocy, ncy);
      tft.fillRect(nbx, nby, (int)arcade::pong::BALL, (int)arcade::pong::BALL, C_WHITE);
      opy = npy; ocy = ncy; obx = nbx; oby = nby;
      if (r) {
        drawPongBar();
        delay(500);                                 // пауза после очка
        next = millis();
      }
      if (png.over) {
        bool won = png.playerWon();
        if (won) saveBestKey("bpong", loadBestKey("bpong") + 1);   // счётчик побед
        char b[32];
        snprintf(b, sizeof(b), "%d : %d", png.playerScore, png.cpuScore);
        if (!resultBox(won ? T().pongWin : T().pongLose, b, won ? C_GREEN : C_RED)) return;
        png.reset();
        break;
      }
    }
  }
}

// ========================= ЛЕТЯЩАЯ ПТИЧКА =========================
arcade::flappy::Game fly;
const uint16_t C_SKY = 0x5D7F, C_PIPE = 0x2E64, C_BIRD = 0xFFE0;

void drawPipeStrip(const arcade::flappy::Pipe& p, int x, int w, uint16_t col) {
  if (x < 0) { w += x; x = 0; }
  if (x + w > 240) w = 240 - x;
  if (w <= 0) return;
  int top = (int)arcade::flappy::Y0, gy = p.gapY, gb = p.gapY + (int)arcade::flappy::GAP;
  tft.fillRect(x, top, w, gy - top, col);
  tft.fillRect(x, gb, w, 240 - gb, col);
}

void runFlappy() {
  fly.rnd = gameRandom;
  fly.best = loadBestKey("bflap");
  fly.reset();
  for (;;) {
    tft.fillScreen(C_BLACK);
    tft.fillRect(0, 20, 240, 220, C_SKY);
    drawScoreBar(fly.score, fly.best);
    drawSmallCenteredBg(T().flapHint, 200, C_BLACK, C_SKY);
    int prevX[arcade::flappy::MAXP];
    bool prevUsed[arcade::flappy::MAXP];
    for (int i = 0; i < arcade::flappy::MAXP; i++) prevUsed[i] = false;
    int oby = (int)fly.y;
    const int BX = (int)arcade::flappy::BIRD_X, BW = (int)arcade::flappy::BW, BH = (int)arcade::flappy::BH;
    tft.fillRect(BX, oby, BW, BH, C_BIRD);
    bool hintShown = true;
    uint32_t next = millis();
    for (;;) {
      BtnEvent e = btnEvent();
      uint8_t j = joyEvent();
      if (e == EV_LONG) {
        saveBestKey("bflap", fly.best);
        return;
      }
      if (e == EV_CLICK || (j == J_UP && !g_joyRepeated)) fly.flap();
      if ((int32_t)(millis() - next) < 0) {
        delay(1);
        continue;
      }
      next += 20;                                   // 50 кадров в секунду
      bool scored = fly.step();
      if (scored) beep(1, 1600, 10);
      if (hintShown && fly.started) {
        tft.fillRect(0, 200, 240, 16, C_SKY);
        hintShown = false;
      }
      // трубы: дорисовываем левый край, стираем правый
      for (int i = 0; i < arcade::flappy::MAXP; i++) {
        const arcade::flappy::Pipe& p = fly.pipe[i];
        int W = (int)arcade::flappy::PIPE_W;
        if (!p.used) {
          prevUsed[i] = false;
          continue;
        }
        int nx = (int)p.x;
        if (!prevUsed[i]) {
          drawPipeStrip(p, nx, W, C_PIPE);
        } else if (nx != prevX[i]) {
          drawPipeStrip(p, nx, prevX[i] - nx, C_PIPE);
          drawPipeStrip(p, nx + W, prevX[i] - nx, C_SKY);
        }
        prevX[i] = nx;
        prevUsed[i] = true;
      }
      int nby = (int)fly.y;
      if (nby != oby) {
        tft.fillRect(BX, oby, BW, BH, C_SKY);
        tft.fillRect(BX, nby, BW, BH, C_BIRD);
        tft.fillRect(BX + BW - 4, nby + 2, 2, 2, C_BLACK);   // глаз
        oby = nby;
      }
      if (scored) drawScoreBar(fly.score, fly.best);
      if (fly.over) {
        saveBestKey("bflap", fly.best);
        if (!gameOverBox(fly.score)) return;
        fly.reset();
        break;
      }
    }
  }
}

// ========================= ГОНКА =========================
void viewAnswer(const String& utf8, uint16_t fg);    // определено ниже
// Кадр 120x110 считается в памяти и выводится на экран с увеличением в 2 раза (240x220 под панелью).
void pushRaceFrame(const uint16_t* fb) {
  static uint16_t line[240];
  tft.startWrite();
  tft.setAddrWindow(0, 20, 240, 220);
  for (int y = 0; y < race::H; y++) {
    const uint16_t* src = fb + y * race::W;
    for (int r = 0; r < 2; r++) {                      // каждую строку - дважды
      for (int x = 0; x < race::W; x++) line[2 * x] = line[2 * x + 1] = src[x];
      tft.writePixels(line, 240, true);
    }
  }
  tft.endWrite();
}

void drawRaceBar(const race::Game& rg, const char* msg, uint16_t msgCol) {
  SmallFont small;
  tft.fillRect(0, 0, 240, 20, C_BLACK);
  char b[48];
  snprintf(b, sizeof(b), T().timeFmt, (unsigned)ceilf(rg.timeLeft));
  drawUtf8(b, 2, 2, rg.timeLeft < 10 ? C_RED : C_YELLOW, C_BLACK);
  if (msg) {
    drawUtf8(msg, (240 - textWidth(msg)) / 2, 2, msgCol, C_BLACK);
  } else {
    snprintf(b, sizeof(b), T().raceKmh, (unsigned)rg.kmh());
    drawUtf8(b, (240 - textWidth(b)) / 2, 2, C_WHITE, C_BLACK);
  }
  snprintf(b, sizeof(b), T().raceMeters, (unsigned)rg.meters());
  drawUtf8(b, 240 - textWidth(b) - 2, 2, C_WHITE, C_BLACK);
}

void runRace() {
  if (!ENABLE_BIGGAMES) {
    notInEdition();
    return;
  }
  // кадр и трасса берут ~45 КБ памяти - только на время игры
  uint16_t* fb = (uint16_t*)malloc(race::W * race::H * sizeof(uint16_t));
  race::Game* rg = new race::Game();
  if (!fb || !rg) {
    free(fb);
    delete rg;
    viewAnswer(T().errMem, C_RED);
    return;
  }
  rg->rnd = gameRandom;
  uint32_t best = loadBestKey("brace");
  for (;;) {                                         // один заезд
    rg->reset();
    tft.fillScreen(C_BLACK);
    rg->render(fb);
    pushRaceFrame(fb);
    drawRaceBar(*rg, nullptr, C_WHITE);
    drawSmallCenteredBg(T().raceStart, 112, C_WHITE, C_BLACK);
    bool paused = true, quit = false;
    uint32_t last = millis(), acc = 0, nextBar = 0;
    const uint32_t STEP_MS = 33;
    while (!quit) {
      BtnEvent e = btnEvent();
      if (e == EV_LONG) quit = true;
      if (e == EV_CLICK) {
        paused = !paused;
        last = millis();
        acc = 0;
        if (paused) drawSmallCenteredBg(T().paused, 112, C_WHITE, C_BLACK);
      }
      if (paused) {
        delay(5);
        continue;
      }
      uint32_t now = millis();
      acc += now - last;
      last = now;
      if (acc > STEP_MS * 4) acc = STEP_MS * 4;         // если кадр долго рисовался - не «прыгаем»
      int dx, dy;
      joyAnalog(dx, dy);
      float steer = 0;
      if (dx > 300 || dx < -300) steer = (float)dx / 1500.0f;   // плавный руль: чем сильнее наклон, тем круче
      if (steer > 1) steer = 1;
      if (steer < -1) steer = -1;
      bool brake = dy > joyThr();
      bool check = false;
      while (acc >= STEP_MS) {
        if (rg->step(steer, brake)) check = true;
        acc -= STEP_MS;
      }
      rg->render(fb);
      pushRaceFrame(fb);
      if (check || millis() >= nextBar) {               // панель - 4 раза в секунду
        nextBar = millis() + 250;
        if (rg->checkMsg > 0) {
          char m[40];
          snprintf(m, sizeof(m), T().raceCheck, (unsigned)(rg->lastBonus + 0.5f));
          drawRaceBar(*rg, m, C_GREEN);
        } else {
          drawRaceBar(*rg, nullptr, C_WHITE);
        }
      }
      if (rg->over) break;
      delay(1);                                          // отдать процессор системе (иначе сработает сторожевой таймер)
    }
    uint32_t m = rg->meters();
    if (m > best) best = m;
    saveBestKey("brace", m);
    if (quit) break;
    char b[48];
    snprintf(b, sizeof(b), T().raceDist, (unsigned)m);
    if (!resultBox(T().raceOver, b, C_YELLOW)) break;
  }
  free(fb);
  delete rg;
}

// ========================= МЕНЮ =========================
void runGamesMenu();                                 // определено ниже, после DeepSeek

Suggester sugg;

void saveLearned() {
  File f = LittleFS.open("/learn.txt", "w");
  if (!f) return;
  std::string data = sugg.serialize();
  f.write((const uint8_t*)data.data(), data.size());
  f.close();
}

void loadLearned() {
  File f = LittleFS.open("/learn.txt", "r");
  if (!f) return;
  String data = f.readString();
  f.close();
  sugg.deserialize(data.c_str());
}

void runImages();
void runSaver();
void runGallery();
void runRoom();
void runNotes();
void runReference();
void runTools();
void runSaved();
bool runTemplates();
bool runSettings();
String inputText();
bool admService();                                   // администратор: показать объявление / выполнить команду
void viewAnswer(const String& utf8, uint16_t fg);
BtnEvent viewText(const String& utf8, uint16_t fg);
int choiceScreen(const String& header, const std::vector<String>& opts);
extern String pendingInput;

std::vector<String> bleNotes;                        // уведомления с телефона, ждут показа
bool bleService();
void bleSetEnabled(bool on);
bool g_kbChat = false;                               // следующий inputText - поле вопроса в чате (удержание - меню чата)
bool g_goHome = false;                               // выбрано «Домой»: выйти из чата на главный экран

// Меню чата (удержание кнопки на клавиатуре чата). true - перерисовать клавиатуру с начала (выбран шаблон).
bool runMenu() {
  int sel = 0;
  for (;;) {
    std::vector<String> items = splitBar(L3("На главный экран|Новый чат|Шаблоны вопросов|Сохранённое|Назад",
                                            "Home screen|New chat|Question templates|Saved|Back",
                                            "主屏幕|新对话|问题模板|已保存|返回"));
    int k = listScreen(L3("Чат DeepSeek", "DeepSeek chat", "DeepSeek 对话"), items, std::vector<String>(), sel);
    if (k < 0) return false;
    sel = k;
    switch (k) {
      case 0:
        g_goHome = true;
        return false;
      case 1:
        chat.clear();
        showMsg(T().chatCleared, C_GREEN);
        delay(1200);
        return false;
      case 2:
        if (runTemplates()) return true;
        break;
      case 3:
        runSaved();
        break;
      default:
        return false;
    }
  }
}

// ========================= ЭКРАННАЯ КЛАВИАТУРА =========================
const char* LAYERS[] = {
  "абвгдеёжзийклмнопрстуфхцчшщъыьэюя",
  "АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯ",
  "abcdefghijklmnopqrstuvwxyz",
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
  "0123456789.,?!-:;()+=/%@*'\"",
};
const int N_LAYERS = sizeof(LAYERS) / sizeof(LAYERS[0]);
const int CW = 24, CH = 26, PER_ROW = 10, KY = 108, MAX_IN = 200;   // клавиши 24x26, ряды 108..211
const int INPUT_LINES = 3;          // строк под набранный текст (0..71)
const int SUG_Y = 80, SUG_N = 3;    // строка подсказок 80..103
const uint16_t C_SUG_BG = 0x2124;   // тёмно-серый фон подсказки

int startLayer() { return cfgLang == LANG_RU ? 0 : 2; }   // английский и китайский - с латиницы (пиньинь)

void buildLayer(int layer, std::vector<Key>& keys) {
  keys.clear();
  std::vector<uint16_t> cps;
  utf8ToCps(LAYERS[layer], cps);
  for (uint16_t c : cps) keys.push_back({c, K_CHAR, nullptr});
  keys.push_back({' ', K_CHAR, "_"});
  keys.push_back({0, K_DEL, "<-"});
  keys.push_back({0, K_LAYER, "Aa"});
  keys.push_back({0, K_SEND, "OK"});
}

int kbRows(const std::vector<Key>& ks) { return ((int)ks.size() + PER_ROW - 1) / PER_ROW; }
int kbRowLen(const std::vector<Key>& ks, int r) {
  int n = (int)ks.size() - r * PER_ROW;
  return n < PER_ROW ? n : PER_ROW;
}

void drawCell(const std::vector<Key>& ks, int i, bool sel) {
  int cx = (i % PER_ROW) * CW;
  int cy = KY + (i / PER_ROW) * CH;
  uint16_t bg = sel ? C_BLUE : C_BLACK;
  tft.fillRect(cx, cy, CW, CH, bg);
  const Key& k = ks[i];
  if (k.label) {                       // служебные клавиши подписаны мелким шрифтом - чтобы не слипались
    SmallFont small;
    int w = (int)strlen(k.label) * GW;
    drawUtf8(k.label, cx + (CW - w) / 2, cy + 5, C_YELLOW, bg);
  } else {
    drawCp(k.cp, cx + (CW - GW) / 2, cy + 1, C_WHITE, bg);
  }
}

void drawKeyboard(const std::vector<Key>& ks, int sel) {
  tft.fillRect(0, KY, 240, STATUS_Y - KY, C_BLACK);
  for (int i = 0; i < (int)ks.size(); i++) drawCell(ks, i, i == sel);
}

// Подсказки идут подряд, каждая шириной по своему слову; сколько влезло - столько и показываем.
const int SUG_MAX_CH = 17;            // длиннее - обрезаем с точкой
int sugWidth(const std::vector<uint16_t>& w) {
  int n = (int)w.size() > SUG_MAX_CH ? SUG_MAX_CH : (int)w.size();
  return n * GW + GW;                 // по полсимвола поля слева и справа
}

// Сколько подсказок помещается в строку (остальные отбрасываем).
size_t fitSuggestions(std::vector<std::vector<uint16_t>>& sg) {
  int x = 0;
  size_t n = 0;
  for (; n < sg.size(); n++) {
    int w = sugWidth(sg[n]);
    if (x + w > 240) break;
    x += w + 4;
  }
  sg.resize(n);
  return n;
}

// Строка подсказок; sel = номер выделенной или -1.
void drawSuggestions(const std::vector<std::vector<uint16_t>>& sg, int sel) {
  tft.fillRect(0, SUG_Y, 240, 24, C_BLACK);
  int x = 0;
  for (int i = 0; i < (int)sg.size(); i++) {
    int w = sugWidth(sg[i]);
    uint16_t bg = i == sel ? C_BLUE : C_SUG_BG;
    tft.fillRect(x, SUG_Y, w, 24, bg);
    int n = (int)sg[i].size();
    bool cut = n > SUG_MAX_CH;
    if (cut) n = SUG_MAX_CH - 1;
    int tx = x + GW / 2;
    for (int k = 0; k < n; k++) tx += drawCp(sg[i][k], tx, SUG_Y, C_WHITE, bg);
    if (cut) drawCp('.', tx, SUG_Y, C_WHITE, bg);
    x += w + 4;
  }
}

void drawInput(const std::vector<uint16_t>& text) {
  std::vector<uint16_t> s(text);
  s.push_back('_');
  int total = (int)s.size();
  int nLines = (total + COLS - 1) / COLS;
  int first = nLines > INPUT_LINES ? nLines - INPUT_LINES : 0;
  tft.fillRect(0, 0, 240, INPUT_LINES * GH, C_BLACK);
  for (int l = first; l < nLines; l++) {
    int st = l * COLS;
    int n = total - st < COLS ? total - st : COLS;
    drawLine(s.data() + st, n, (l - first) * GH, C_WHITE, C_BLACK);
  }
}

// Курсор: row = -1 - строка подсказок, 0.. - ряды клавиатуры.
String pendingInput;           // вопрос, который не удалось отправить (вернётся в поле ввода)

String inputText() {
  UiPause uiPause;
  bool chatMode = g_kbChat;                          // в чате удержание открывает меню чата, в остальных местах - «назад»
  g_kbChat = false;
  SaverArm arm;
  int layer = startLayer();
  int row = 0, col = 0;        // где курсор
  int kbRow = 0, kbCol = 0;    // куда вернуться из строки подсказок
  std::vector<Key> keys;
  std::vector<uint16_t> text;
  if (pendingInput.length()) {
    utf8ToCps(pendingInput.c_str(), text);
    pendingInput = "";
  }
  std::vector<std::vector<uint16_t>> sg;
  bool redraw = true;

  auto kbIndex = [&]() { return row >= 0 ? row * PER_ROW + col : -1; };
  auto refreshSuggestions = [&]() {
    size_t pl = partialWordLen(text);
    if (pl) sg = sugg.suggest(std::vector<uint16_t>(text.end() - (long)pl, text.end()), SUG_N);
    else sg.clear();
    fitSuggestions(sg);
    if (row < 0) {
      if (sg.empty()) {                        // подсказки пропали - назад на клавиатуру
        row = kbRow;
        col = kbCol;
        drawCell(keys, kbIndex(), true);
      } else if (col >= (int)sg.size()) {
        col = (int)sg.size() - 1;
      }
    }
    drawSuggestions(sg, row < 0 ? col : -1);
  };
  auto goSuggestions = [&]() {
    kbRow = row;
    kbCol = col;
    drawCell(keys, kbIndex(), false);
    row = -1;
    col = col * (int)sg.size() / PER_ROW;   // примерно над той же клавишей
    if (col >= (int)sg.size()) col = (int)sg.size() - 1;
    drawSuggestions(sg, col);
  };
  auto leaveSuggestions = [&](int newRow) {
    row = newRow;
    col = kbCol;
    if (col >= kbRowLen(keys, row)) col = kbRowLen(keys, row) - 1;
    drawSuggestions(sg, -1);
    drawCell(keys, kbIndex(), true);
  };

  for (;;) {
    if (saverDue) {                                  // долго не трогали - заставка с часами, потом клавиатура заново
      runSaver();
      saverDue = false;
      redraw = true;
    }
    if (redraw) {
      buildLayer(layer, keys);
      if (row >= kbRows(keys)) row = kbRows(keys) - 1;
      if (row >= 0 && col >= kbRowLen(keys, row)) col = kbRowLen(keys, row) - 1;
      tft.fillScreen(C_BLACK);
      tft.fillRect(0, INPUT_LINES * GH + 4, 240, 1, C_GRAY);   // линия под полем ввода (y=76)
      drawInput(text);
      drawKeyboard(keys, kbIndex());
      refreshSuggestions();
      updateBattery();
      drawStatusBar();
      redraw = false;
    }
    if (g_bgTick) g_bgTick();
    {                                                // текст с телефона по Bluetooth
      std::string line;
      bool submit = false;
      while (!submit && phoneTakeLine(line)) {
        if (line.compare(0, 2, "N:") == 0) {         // уведомление - покажем после
          bleNotes.push_back(fromStd(line.substr(2)));
          continue;
        }
        bool insertOnly = line.compare(0, 2, "T:") == 0;
        if (insertOnly || line.compare(0, 2, "S:") == 0) line = line.substr(2);
        std::vector<uint16_t> cps;
        utf8ToCps(line.c_str(), cps);
        text.insert(text.end(), cps.begin(), cps.end());
        submit = !insertOnly;
      }
      if (submit && text.size()) return String(cpsToUtf8(text.data(), text.size()).c_str());
      if (line.size()) {
        drawInput(text);
        refreshSuggestions();
      }
    }
    if (admService()) {                              // объявление или команда администратора заняли экран
      redraw = true;
      continue;
    }
    if (updateBattery() || roomUnreadDirty || statusClockDue()) drawStatusBar();
    {                                                // фоновый Wi-Fi: обновить значок, если состояние сменилось
      static uint32_t nextWifiCheck = 0;
      static int shownWifi = -1;
      if ((int32_t)(millis() - nextWifiCheck) >= 0) {
        nextWifiCheck = millis() + 500;
        wifiTick();
        int ws = wifiState();
        if (ws != shownWifi) {
          shownWifi = ws;
          drawStatusBar();
        }
      }
    }

    uint8_t j = joyEvent();
    if (j != J_NONE) {
      int nRows = kbRows(keys);
      if (row < 0) {                                   // в строке подсказок
        int n = (int)sg.size();
        if (j == J_LEFT || j == J_RIGHT) {
          col = (col + (j == J_RIGHT ? 1 : -1) + n) % n;
          drawSuggestions(sg, col);
        } else if (j == J_DOWN) {
          leaveSuggestions(0);
        } else {
          leaveSuggestions(nRows - 1);
        }
      } else if (j == J_LEFT || j == J_RIGHT) {
        int n = kbRowLen(keys, row);
        drawCell(keys, kbIndex(), false);
        col = (col + (j == J_RIGHT ? 1 : -1) + n) % n;
        drawCell(keys, kbIndex(), true);
      } else if (j == J_UP && row == 0) {
        if (!sg.empty()) goSuggestions();
        else {
          drawCell(keys, kbIndex(), false);
          row = nRows - 1;
          if (col >= kbRowLen(keys, row)) col = kbRowLen(keys, row) - 1;
          drawCell(keys, kbIndex(), true);
        }
      } else if (j == J_DOWN && row == nRows - 1) {
        if (!sg.empty()) goSuggestions();
        else {
          drawCell(keys, kbIndex(), false);
          row = 0;
          drawCell(keys, kbIndex(), true);
        }
      } else {
        drawCell(keys, kbIndex(), false);
        row += j == J_DOWN ? 1 : -1;
        if (col >= kbRowLen(keys, row)) col = kbRowLen(keys, row) - 1;
        drawCell(keys, kbIndex(), true);
      }
    }

    BtnEvent e = btnEvent();
    if (e == EV_LONG) {                      // долгое нажатие: в чате - меню, в остальных местах - назад
      if (!chatMode) return String("");
      saverArmed = false;
      bool menuChanged = runMenu();
      if (g_goHome) return String("");
      saverArmed = true;
      saverDue = false;
      noteActivity();
      if (menuChanged) { layer = startLayer(); row = 0; col = 0; }
      if (pendingInput.length()) {                   // выбран шаблон - он становится началом вопроса
        text.clear();
        utf8ToCps(pendingInput.c_str(), text);
        pendingInput = "";
      }
      redraw = true;
      continue;
    }
    if (e == EV_CLICK) {
      if (row < 0) {                         // вставить подсказку вместо недописанного слова
        size_t pl = partialWordLen(text);
        text.erase(text.end() - (long)pl, text.end());
        for (uint16_t c : sg[(size_t)col]) if ((int)text.size() < MAX_IN) text.push_back(c);
        if ((int)text.size() < MAX_IN) text.push_back(' ');
        if (layer == 1 || layer == 3) {      // заглавные - обратно на строчные
          layer--;
          buildLayer(layer, keys);
        }
        row = kbRow;
        col = kbCol;
        if (col >= kbRowLen(keys, row)) col = kbRowLen(keys, row) - 1;
        drawKeyboard(keys, kbIndex());
        drawInput(text);
        refreshSuggestions();
      } else {
        Key k = keys[(size_t)kbIndex()];
        switch (k.kind) {
          case K_CHAR:
            if ((int)text.size() < MAX_IN) {
              text.push_back(k.cp);
              drawInput(text);
            }
            if (layer == 1 || layer == 3) {    // после заглавной - снова строчные
              layer--;
              buildLayer(layer, keys);
              if (col >= kbRowLen(keys, row)) col = kbRowLen(keys, row) - 1;
              drawKeyboard(keys, kbIndex());
            }
            refreshSuggestions();
            break;
          case K_DEL:
            if (!text.empty()) {
              text.pop_back();
              drawInput(text);
              refreshSuggestions();
            }
            break;
          case K_LAYER:
            layer = (layer + 1) % N_LAYERS;
            buildLayer(layer, keys);
            if (row >= kbRows(keys)) row = kbRows(keys) - 1;
            if (col >= kbRowLen(keys, row)) col = kbRowLen(keys, row) - 1;
            drawKeyboard(keys, kbIndex());
            break;
          case K_SEND:
            return String(cpsToUtf8(text.data(), text.size()).c_str());
        }
      }
    }
    delay(2);
  }
}

// ========================= СОХРАНЁННЫЕ ОТВЕТЫ =========================
// Хранятся в памяти платы (файл /saved.txt) и читаются без интернета.
const size_t MAX_SAVED = 20;
std::vector<String> savedTitle, savedText;
bool savedLoaded = false;

void loadSaved() {
  if (savedLoaded) return;
  savedLoaded = true;
  File f = LittleFS.open("/saved.txt", "r");
  if (!f) return;
  std::string d = toStd(f.readString());
  f.close();
  size_t p = 0;
  while (p < d.size()) {                             // запись: заголовок \x1F текст \x1E
    size_t e = d.find('\x1E', p);
    if (e == std::string::npos) e = d.size();
    std::string rec = d.substr(p, e - p);
    size_t m = rec.find('\x1F');
    if (m != std::string::npos && savedTitle.size() < MAX_SAVED) {
      savedTitle.push_back(fromStd(rec.substr(0, m)));
      savedText.push_back(fromStd(rec.substr(m + 1)));
    }
    p = e + 1;
  }
}

void storeSaved() {
  File f = LittleFS.open("/saved.txt", "w");
  if (!f) return;
  for (size_t i = 0; i < savedTitle.size(); i++) {
    std::string rec = toStd(savedTitle[i]) + '\x1F' + toStd(savedText[i]) + '\x1E';
    f.write((const uint8_t*)rec.data(), rec.size());
  }
  f.close();
}

// Сохранить (новые - в начало списка). Если места нет - самая старая запись удаляется.
void saveItem(const String& title, const String& text) {
  loadSaved();
  for (size_t i = 0; i < savedTitle.size(); i++)
    if (savedTitle[i] == title && savedText[i] == text) return;   // уже есть
  if (savedTitle.size() >= MAX_SAVED) {
    savedTitle.pop_back();
    savedText.pop_back();
  }
  savedTitle.insert(savedTitle.begin(), title);
  savedText.insert(savedText.begin(), text);
  storeSaved();
}

// ========================= ПРОСМОТР ОТВЕТА =========================
// Вверх/вниз - на строку, влево/вправо - на страницу, нажатие - выход.
BtnEvent viewText(const String& utf8, uint16_t fg) {
  noteActivity();
  std::vector<uint16_t> cps;
  utf8ToCps(utf8.c_str(), cps);
  std::vector<Line> lines;
  wrapCps(cps, COLS, lines);
  int top = 0;
  int maxTop = (int)lines.size() > ROWS ? (int)lines.size() - ROWS : 0;
  renderRows(cps, lines, top, fg);
  for (;;) {
    uint8_t j = joyEvent();
    int d = j == J_UP ? -1 : j == J_DOWN ? 1 : j == J_LEFT ? -(ROWS - 1) : j == J_RIGHT ? ROWS - 1 : 0;
    if (d) {
      int nt = top + d;
      if (nt < 0) nt = 0;
      if (nt > maxTop) nt = maxTop;
      if (nt != top) {
        top = nt;
        renderRows(cps, lines, top, fg);
      }
    }
    BtnEvent e = btnEvent();
    if (e != EV_NONE) return e;
    delay(2);
  }
}

void viewAnswer(const String& utf8, uint16_t fg) {
  UiPause uiPause;
  viewText(utf8, fg);
}

// ========================= ЛЕНТА ЧАТА =========================
struct ViewLine {
  std::vector<uint16_t> cps;
  uint16_t color;
};

// Вся переписка: «Вы:» и вопрос жёлтым, «DeepSeek:» и ответ белым.
// Открывается на последнем вопросе; вверх - листать к старым сообщениям.
// Возвращает: 0 - назад к клавиатуре, 1 - «решить по шагам», 2 - «объяснить проще».
int viewChat() {
  noteActivity();
  std::vector<ViewLine> vl;
  int lastQuestionLine = 0;
  for (size_t m = 0; m < chat.size(); m++) {
    const ChatMsg& msg = chat[m];
    if (msg.user) lastQuestionLine = (int)vl.size();
    uint16_t labelCol = msg.user ? C_YELLOW : 0x07FF;   // голубой для DeepSeek
    uint16_t textCol = msg.user ? 0xFFF0 : C_WHITE;
    std::vector<uint16_t> lab;
    utf8ToCps(msg.user ? T().youLabel : T().botLabel, lab);
    vl.push_back({lab, labelCol});
    std::vector<uint16_t> cps;
    utf8ToCps(msg.text.c_str(), cps);
    std::vector<Line> lines;
    wrapCps(cps, COLS, lines);
    for (const Line& l : lines)
      vl.push_back({std::vector<uint16_t>(cps.begin() + l.start, cps.begin() + l.start + l.len), textCol});
    if (m + 1 < chat.size()) vl.push_back({std::vector<uint16_t>(), C_WHITE});   // пустая строка между сообщениями
  }
  int maxTop = (int)vl.size() > ROWS ? (int)vl.size() - ROWS : 0;
  int top = lastQuestionLine < maxTop ? lastQuestionLine : maxTop;
  auto render = [&]() {
    for (int r = 0; r < ROWS; r++) {
      size_t i = (size_t)(top + r);
      if (i < vl.size()) drawLine(vl[i].cps.data(), (int)vl[i].cps.size(), r * GH, vl[i].color, C_BLACK);
      else tft.fillRect(0, r * GH, 240, GH, C_BLACK);
    }
  };
  render();
  for (;;) {
    uint8_t j = joyEvent();
    int d = j == J_UP ? -1 : j == J_DOWN ? 1 : j == J_LEFT ? -(ROWS - 1) : j == J_RIGHT ? ROWS - 1 : 0;
    if (d) {
      int nt = top + d;
      if (nt < 0) nt = 0;
      if (nt > maxTop) nt = maxTop;
      if (nt != top) {
        top = nt;
        render();
      }
    }
    BtnEvent e = btnEvent();
    if (e == EV_CLICK) return 0;
    if (e == EV_LONG && chat.size() >= 2) {          // удержание - что сделать с последним ответом
      std::vector<String> o = splitBar(L3("Решить по шагам|Объяснить проще|Сохранить|Назад", "Step by step|Explain simpler|Save|Back",
                                          "分步解答|讲得更简单|保存|返回"));
      int k = choiceScreen(L3("Что сделать с ответом?", "What to do with the answer?", "如何处理这个回答？"), o);
      if (k == 0) return 1;
      if (k == 1) return 2;
      if (k == 2) {
        saveItem(chat[chat.size() - 2].text, chat[chat.size() - 1].text);
        showMsg(L3("Сохранено. Смотрите в Меню - Сохранённое.", "Saved. See Menu - Saved.", "已保存。见菜单-已保存。"), C_GREEN);
        delay(1200);
      }
      render();
    }
    delay(2);
  }
}

// ========================= DEEPSEEK =========================
// Потоковый ответ: сервер присылает текст кусочками, мы сразу показываем его на экране.
// HTTPClient сам разбирает «chunked» и передаёт данные сюда через write().
class SseSink : public Stream {
 public:
  std::string line, text, raw;
  bool sawData = false;
  std::function<void(const std::string&)> onText;
  size_t write(uint8_t c) override {
    feed((char)c);
    return 1;
  }
  size_t write(const uint8_t* b, size_t n) override {
    for (size_t i = 0; i < n; i++) feed((char)b[i]);
    return n;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void feed(char c) {
    if (raw.size() < 3000) raw += c;
    if (c == '\n') {
      std::string piece;
      bool done = false;
      if (tools::sseLine(line, piece, done)) {
        sawData = true;
        if (!piece.empty()) {
          text += piece;
          if (onText) onText(text);
        }
      }
      line.clear();
    } else if (c != '\r' && line.size() < 8000) {
      line += c;
    }
  }
};
std::function<void(const std::string&)> g_streamCb;   // задан - просим ответ потоком и вызываем по мере прихода текста
bool g_streamed = false;                              // последний ответ пришёл потоком (в payload - готовый текст)

// Возвращает HTTP-код (200 = успех) или отрицательный код ошибки сети.
int apiCall(const Attempt& a, const char* system, const std::vector<ChatMsg>& history, const String& question,
            int maxTokens, String& payload) {
  JsonDocument doc;
  doc["model"] = a.model;
  JsonArray msgs = doc["messages"].to<JsonArray>();
  JsonObject sys = msgs.add<JsonObject>();
  sys["role"] = "system";
  sys["content"] = system;
  for (const ChatMsg& m : history) {       // прошлые сообщения - чтобы DeepSeek помнил разговор
    JsonObject h = msgs.add<JsonObject>();
    h["role"] = m.user ? "user" : "assistant";
    h["content"] = m.text;
  }
  JsonObject usr = msgs.add<JsonObject>();
  usr["role"] = "user";
  usr["content"] = question;
  doc["max_tokens"] = maxTokens;
  if (a.noThink) doc["thinking"]["type"] = "disabled";
  bool stream = (bool)g_streamCb;
  if (stream) doc["stream"] = true;
  g_streamed = false;
  String body;
  serializeJson(doc, body);

  WiFiClientSecure client;
  client.setInsecure();   // без проверки сертификата
  client.setHandshakeTimeout(20);   // секунды: если сервер недоступен - ошибка, а не вечное «Думаю»
  HTTPClient http;
  http.setConnectTimeout(15000);
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(client, API_URL)) return -100;
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + cfgKey);
  int code = http.POST(body);
  if (code == 200 && stream) {
    SseSink sink;
    sink.onText = g_streamCb;
    http.writeToStream(&sink);
    if (sink.line.size()) sink.feed('\n');           // последняя строка без перевода строки
    if (sink.sawData) {
      payload = sink.text.c_str();
      g_streamed = true;
    } else {
      payload = sink.raw.c_str();                    // сервер ответил обычным JSON
    }
  } else if (code > 0) payload = http.getString();
  else payload = HTTPClient::errorToString(code);
  http.end();
  Serial.printf("HTTP %d, free heap: %u\n", code, (unsigned)ESP.getFreeHeap());
  return code;
}

String errorText(int code, const String& payload) {
  if (code == 401) return T().err401;
  if (code == 402) return T().err402;
  if (code == 429) return T().err429;
  if (code == -1) return T().errConn;
  if (code == -8) return T().errMem;
  if (code == -11) return fmt(T().errTimeout, HTTP_TIMEOUT_MS / 1000);
  if (code < 0) return String(T().errNet) + "(" + String(code) + "): " + payload;
  String msg;
  JsonDocument r;
  if (!deserializeJson(r, payload)) msg = r["error"]["message"] | "";
  return "HTTP " + String(code) + ": " + msg;
}

// true = в result текст ответа, false = в result текст ошибки.
bool askWith(const char* system, const std::vector<ChatMsg>& history, const String& question, int maxTokens,
             String& result) {
  if (cfgKey.length() == 0) {
    result = T().errNoKey;
    return false;
  }
  int code = 0;
  String payload;
  for (int i = bestAttempt; i < N_ATTEMPTS; i++) {
    payload = "";
    code = apiCall(ATTEMPTS[i], system, history, question, maxTokens, payload);
    if (code == 200) {
      String text;
      if (g_streamed) {
        text = payload;
      } else {
        JsonDocument r;
        if (deserializeJson(r, payload)) {
          result = T().errBadReply;
          return false;
        }
        text = r["choices"][0]["message"]["content"] | "";
      }
      text.trim();
      if (text.length() == 0) {
        result = T().errEmpty;
        return false;
      }
      bestAttempt = i;
      result = text;
      return true;
    }
    Serial.println(payload.substring(0, 300));
    if (code != 400 && code != 404 && code != 422) break;  // ошибка модели - пробуем другое имя
  }
  result = errorText(code, payload);
  return false;
}

// Показ ответа по мере прихода: последние строки текста, не чаще 4 раз в секунду.
uint32_t streamLastDraw = 0;
void streamDraw(const std::string& t) {
  if (millis() - streamLastDraw < 250) return;
  streamLastDraw = millis();
  noteActivity();
  std::string p = tools::prettify(t);
  std::vector<uint16_t> cps;
  utf8ToCps(p.c_str(), cps);
  std::vector<Line> lines;
  wrapCps(cps, COLS, lines);
  int top = (int)lines.size() > ROWS ? (int)lines.size() - ROWS : 0;
  renderRows(cps, lines, top, C_WHITE);
}

// Обычный чат: инструкция и память переписки; ответ печатается на экране по мере генерации.
bool askLen(const String& question, int maxTokens, String& result) {
  streamLastDraw = 0;
  g_streamCb = streamDraw;
  bool ok = askWith(T().systemPrompt, chat, question, maxTokens, result);
  g_streamCb = nullptr;
  if (ok) result = fromStd(tools::prettify(toStd(result)));   // формулы - читаемыми символами
  return ok;
}

bool ask(const String& question, String& result) { return askLen(question, MAX_TOKENS, result); }

// ========================= КАРТИНКИ (WIKIMEDIA) =========================
#if ENABLE_IMAGES
const int IMG_Y0 = 20, IMG_Y1 = 220;                 // область картинки на экране
#define IMG_UA APP_UA
bool imgFromWiki = true;                             // откуда текущие картинки (для подписи)

// Кусочки картинки от декодера JPEG: выводим только то, что попадает в область.
bool jpgOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bmp) {
  if (y >= IMG_Y1 || y + h <= IMG_Y0 || x >= 240 || x + w <= 0) return true;
  int x0 = x < 0 ? 0 : x, x1 = x + w > 240 ? 240 : x + w;
  int y0 = y < IMG_Y0 ? IMG_Y0 : y, y1 = y + h > IMG_Y1 ? IMG_Y1 : y + h;
  tft.startWrite();
  tft.setAddrWindow(x0, y0, x1 - x0, y1 - y0);
  for (int yy = y0; yy < y1; yy++) tft.writePixels(bmp + (yy - y) * w + (x0 - x), (uint32_t)(x1 - x0), true);
  tft.endWrite();
  return true;
}

// Приёмник для скачивания файла в память. HTTPClient сам следит за длиной, паузами и «chunked».
class BufSink : public Stream {
 public:
  uint8_t* buf = nullptr;
  size_t cap = 0, n = 0;
  bool overflow = false;
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* b, size_t len) override {
    size_t room = cap - n;
    if (len > room) overflow = true;
    size_t k = len < room ? len : room;
    memcpy(buf + n, b, k);
    n += k;
    return len;                                      // лишнее молча отбрасываем, чтобы загрузка не зависла
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
};

// Одна попытка скачать файл в память (до 120 КБ). nullptr - ошибка (текст в err).
uint8_t* httpDownloadOnce(const String& url, size_t& n, String& err) {
  n = 0;
  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(15);
  HTTPClient http;
  http.setUserAgent(IMG_UA);
  http.setConnectTimeout(15000);
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) {
    err = "begin";
    return nullptr;
  }
  int code = http.GET();
  if (code != 200) {
    err = "HTTP " + String(code);
    http.end();
    return nullptr;
  }
  int len = http.getSize();                          // -1 - длина заранее неизвестна
  BufSink sink;
  sink.cap = (len > 0 && len < 120000) ? (size_t)len : 120000;
  sink.buf = (uint8_t*)malloc(sink.cap);
  if (!sink.buf) {
    err = T().errMem;
    http.end();
    return nullptr;
  }
  int got = http.writeToStream(&sink);
  http.end();
  Serial.printf("[img] %d of %d bytes, result %d\n", (int)sink.n, len, got);
  // файл должен прийти целиком: обрезанную картинку декодер покажет наполовину и выдаст ошибку
  bool whole = got >= 0 && sink.n > 0 && !sink.overflow && (len < 0 || sink.n == (size_t)len);
  if (!whole) {
    free(sink.buf);
    err = sink.overflow ? String(L3("файл слишком большой", "the file is too big", "文件太大"))
                        : String(L3("связь оборвалась", "the connection dropped", "连接中断"));
    return nullptr;
  }
  n = sink.n;
  return sink.buf;
}

// Скачать файл; при обрыве связи - ещё одна попытка.
uint8_t* httpDownload(const String& url, size_t& n, String& err) {
  uint8_t* p = httpDownloadOnce(url, n, err);
  if (!p && wifiOk()) {
    delay(300);
    p = httpDownloadOnce(url, n, err);
  }
  return p;
}

bool wikiSearch(const String& query, std::vector<pixabay::Hit>& hits, String& err) {
  std::string url = wiki::searchUrl(toStd(query), 240, IMG_Y1 - IMG_Y0);
  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(15);
  HTTPClient http;
  http.setUserAgent(IMG_UA);
  http.setConnectTimeout(15000);
  http.setTimeout(15000);
  if (!http.begin(client, String(url.c_str()))) {
    err = "begin";
    return false;
  }
  int code = http.GET();
  String body = code > 0 ? http.getString() : HTTPClient::errorToString(code);
  http.end();
  Serial.printf("[img] wikimedia HTTP %d, %u bytes\n", code, (unsigned)body.length());
  if (code != 200) {
    err = "HTTP " + String(code) + (code < 0 ? String(" ") + body : String(""));
    return false;
  }
  hits = wiki::parseHits(toStd(body));
  return true;
}

// Поиск картинок в Wikimedia Commons: без ключа, без VPN.
bool imageSearch(const String& query, std::vector<pixabay::Hit>& hits, String& err) {
  imgFromWiki = true;
  return wikiSearch(query, hits, err);
}

void drawImgBars(int i, int n, const pixabay::Hit& h) {
  SmallFont small;
  tft.fillRect(0, 0, 240, IMG_Y0, C_BLACK);
  tft.fillRect(0, IMG_Y1, 240, 240 - IMG_Y1, C_BLACK);
  String top = String(i + 1) + "/" + String(n) + "  " + fromStd(h.tags);
  drawUtf8(top.c_str(), 2, 2, C_WHITE, C_BLACK);
  char b[96];
  snprintf(b, sizeof(b), imgFromWiki ? T().imgByWiki : T().imgBy, h.user.c_str());
  drawUtf8(b, 2, IMG_Y1 + 2, C_LIGHT, C_BLACK);  // автор - так просит лицензия Wikimedia
}

uint8_t* lastJpg = nullptr;                          // последняя показанная картинка из поиска
size_t lastJpgN = 0;

// Нарисовать JPEG из памяти по центру области картинки. false - показать не удалось (на экране сообщение).
bool drawJpegFit(const uint8_t* jpg, size_t n) {
  TJpgDec.setSwapBytes(false);
  TJpgDec.setCallback(jpgOutput);
  tft.fillRect(0, IMG_Y0, 240, IMG_Y1 - IMG_Y0, C_BLACK);
  uint16_t w = 0, h = 0;
  TJpgDec.getJpgSize(&w, &h, jpg, (uint32_t)n);
  int sc = 1;                                        // слишком большая - уменьшаем в 2/4/8 раз при декодировании
  while (sc < 8 && ((int)w / sc > 340 || (int)h / sc > IMG_Y1 - IMG_Y0 + 10)) sc *= 2;
  TJpgDec.setJpgScale((uint8_t)sc);
  int x = (240 - (int)w / sc) / 2;                   // чуть шире экрана - обрезаем края поровну
  int y = IMG_Y0 + ((IMG_Y1 - IMG_Y0) - (int)h / sc) / 2;
  // JPEG начинается с метки FF D8; если её нет - пришло что-то другое, рисовать нечего
  bool complete = n > 4 && jpg[0] == 0xFF && jpg[1] == 0xD8;
  int rc = (w && h && complete) ? TJpgDec.drawJpg(x, y, jpg, (uint32_t)n) : -1;
  if (rc == 0) return true;
  // повреждённый файл или «прогрессивный» JPEG - декодер такие не умеет
  tft.fillRect(0, IMG_Y0, 240, IMG_Y1 - IMG_Y0, C_BLACK);
  SmallFont small;
  const char* m1 = L3("Эту картинку показать не удалось", "This picture cannot be shown", "无法显示这张图片");
  const char* m2 = L3("листайте дальше", "go to the next one", "请看下一张");
  drawUtf8(m1, (240 - textWidth(m1)) / 2 < 0 ? 0 : (240 - textWidth(m1)) / 2, 104, C_YELLOW, C_BLACK);
  drawUtf8(m2, (240 - textWidth(m2)) / 2, 124, C_LIGHT, C_BLACK);
  Serial.printf("[img] decode error %d (%ux%u)\n", rc, (unsigned)w, (unsigned)h);
  return false;
}

// Показать i-ю картинку: скачать и нарисовать по центру.
void showImage(const std::vector<pixabay::Hit>& hits, int i) {
  tft.fillRect(0, IMG_Y0, 240, IMG_Y1 - IMG_Y0, C_BLACK);
  drawImgBars(i, (int)hits.size(), hits[(size_t)i]);
  {
    SmallFont small;
    drawUtf8(T().imgLoading, (240 - textWidth(T().imgLoading)) / 2, 112, C_YELLOW, C_BLACK);
  }
  size_t n;
  String err;
  uint8_t* jpg = httpDownload(fromStd(hits[(size_t)i].url), n, err);
  tft.fillRect(0, IMG_Y0, 240, IMG_Y1 - IMG_Y0, C_BLACK);
  if (!jpg) {
    if (lastJpg) free(lastJpg);
    lastJpg = nullptr;
    lastJpgN = 0;
    SmallFont small;
    drawUtf8(T().imgError, (240 - textWidth(T().imgError)) / 2 < 0 ? 0 : (240 - textWidth(T().imgError)) / 2, 104, C_RED, C_BLACK);
    drawUtf8(err.c_str(), (240 - textWidth(err.c_str())) / 2 < 0 ? 0 : (240 - textWidth(err.c_str())) / 2, 124, C_RED, C_BLACK);
    return;
  }
  if (lastJpg) free(lastJpg);                        // держим последнюю картинку: её можно сохранить в галерею
  lastJpg = jpg;
  lastJpgN = n;
  drawJpegFit(jpg, n);
}

void runImages() {
  if (!ENABLE_WEBIMG) {
    notInEdition();
    return;
  }
  struct FreeLast {                                  // при выходе освобождаем память последней картинки
    ~FreeLast() {
      if (lastJpg) free(lastJpg);
      lastJpg = nullptr;
      lastJpgN = 0;
    }
  } freeLast;
  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(false);
  TJpgDec.setCallback(jpgOutput);
  for (;;) {                                         // один поиск
    showMsg(T().imgPrompt, C_YELLOW);
    delay(1200);
    String q = inputText();
    q.trim();
    if (!q.length()) return;
    if (!waitWifi(false)) return;
    showMsg(T().imgSearching, C_YELLOW);
    std::vector<pixabay::Hit> hits;
    String err;
    if (!imageSearch(q, hits, err)) {
      String m = String(T().errPrefix) + err;
      viewAnswer(m, C_RED);
      continue;
    }
    if (hits.empty()) {
      viewAnswer(T().imgNone, C_YELLOW);
      continue;
    }
    showMsg(T().imgHelp, C_WHITE);
    delay(1500);
    int i = 0;
    tft.fillScreen(C_BLACK);
    showImage(hits, i);
    bool again = false;
    for (;;) {
      uint8_t j = joyEvent();
      BtnEvent e = btnEvent();
      if (e == EV_LONG) {
        std::vector<String> o = splitBar(L3("Сохранить в галерею|Вернуться|Выйти из картинок", "Save to the gallery|Return|Leave images",
                                            "保存到相册|返回|退出图片"));
        int k = choiceScreen(L3("Картинка", "Picture", "图片"), o);
        if (k == 2) return;
        if (k == 0) {
          String why = lastJpg ? savePic(lastJpg, lastJpgN) : String(L3("картинка не загружена", "the picture is not loaded", "图片未加载"));
          showMsg(why.length() ? String(L3("Не сохранено: ", "Not saved: ", "未保存：")) + why
                               : String(L3("Сохранено в галерею. Картинок: ", "Saved to the gallery. Pictures: ", "已保存到相册。图片数：")) + String(picCount()),
                  why.length() ? C_RED : C_GREEN);
          delay(1500);
        }
        tft.fillScreen(C_BLACK);                      // вернуть картинку на экран
        drawImgBars(i, (int)hits.size(), hits[(size_t)i]);
        if (lastJpg) drawJpegFit(lastJpg, lastJpgN);
        continue;
      }
      if (e == EV_CLICK) {
        again = true;
        break;
      }
      if ((j == J_RIGHT || j == J_DOWN) && !g_joyRepeated) {
        i = (i + 1) % (int)hits.size();
        showImage(hits, i);
      }
      if ((j == J_LEFT || j == J_UP) && !g_joyRepeated) {
        i = (i + (int)hits.size() - 1) % (int)hits.size();
        showImage(hits, i);
      }
      delay(2);
    }
    if (!again) return;
  }
}

// ---------- Галерея ----------
void drawGalleryBars(int pos, int total) {
  SmallFont small;
  tft.fillRect(0, 0, 240, IMG_Y0, C_BLACK);
  tft.fillRect(0, IMG_Y1, 240, 240 - IMG_Y1, C_BLACK);
  char b[40];
  snprintf(b, sizeof(b), "%s %d/%d", L3("Галерея", "Gallery", "相册"), pos + 1, total);
  drawUtf8(b, 2, 2, C_WHITE, C_BLACK);
  drawUtf8(L3("Держать - меню", "Hold - menu", "长按：菜单"), 2, IMG_Y1 + 2, C_LIGHT, C_BLACK);
}

void showGalleryPic(int slot, int pos, int total) {
  drawGalleryBars(pos, total);
  size_t n = 0;
  uint8_t* buf = picLoad(slot, n);
  if (buf) {
    drawJpegFit(buf, n);
  } else {
    tft.fillRect(0, IMG_Y0, 240, IMG_Y1 - IMG_Y0, C_BLACK);
    SmallFont small;
    const char* m = L3("Файл не читается", "Cannot read the file", "无法读取文件");
    drawUtf8(m, (240 - textWidth(m)) / 2, 112, C_RED, C_BLACK);
  }
  if (buf) free(buf);
}

void runGallery() {
  int pos = 0;
  for (;;) {
    std::vector<int> slots;
    showMsg(L3("Открываю галерею...\n\nЗависло - держите кнопку 6 секунд", "Opening the gallery...\n\nStuck? Hold the button for 6 seconds",
               "正在打开相册……\n\n卡住了？按住按键6秒"), C_YELLOW);
    picSlots(slots);
    if (slots.empty()) {
      std::vector<String> o = splitBar(L3("Загрузить с телефона|Назад", "Upload from phone|Back", "从手机上传|返回"));
      int k = choiceScreen(L3("В галерее пока пусто. Картинки загружаются с телефона или сохраняются из поиска картинок.",
                              "The gallery is empty. Upload pictures from your phone or save them from image search.",
                              "相册是空的。可以从手机上传图片，或从图片搜索中保存。"), o);
      if (k != 0) return;
      runPortal();
      netIdx = 0;
      wifiStart();
      continue;
    }
    if (pos >= (int)slots.size()) pos = (int)slots.size() - 1;
    tft.fillScreen(C_BLACK);
    showGalleryPic(slots[(size_t)pos], pos, (int)slots.size());
    for (;;) {
      uint8_t j = joyEvent();
      BtnEvent e = btnEvent();
      if (e == EV_CLICK) return;
      if (e == EV_LONG) {
        std::vector<String> o = splitBar(L3("Вернуться|Удалить эту картинку|Загрузить с телефона|Выйти", "Return|Delete this picture|Upload from phone|Leave",
                                            "返回|删除这张图片|从手机上传|退出"));
        int k = choiceScreen(L3("Галерея", "Gallery", "相册"), o);
        if (k == 3) return;
        if (k == 1) picRemove(slots[(size_t)pos]);
        if (k == 2) {
          runPortal();
          netIdx = 0;
          wifiStart();
        }
        break;                                        // список мог измениться - перечитать и перерисовать
      }
      int n = (int)slots.size();
      if ((j == J_RIGHT || j == J_DOWN) && !g_joyRepeated && n > 1) {
        pos = (pos + 1) % n;
        showGalleryPic(slots[(size_t)pos], pos, n);
      }
      if ((j == J_LEFT || j == J_UP) && !g_joyRepeated && n > 1) {
        pos = (pos + n - 1) % n;
        showGalleryPic(slots[(size_t)pos], pos, n);
      }
      delay(2);
    }
  }
}

// ---------- Аватарка из галереи ----------
extern uint8_t myAva[];
extern bool myAvaSet;
void drawAvatar(const uint8_t* ava, const String& id, const String& nick, int x, int y);
uint32_t* avaSum = nullptr;                          // суммы цветов по клеткам 24x24 (r, g, b, сколько точек)
int avaOx = 0, avaOy = 0, avaSide = 1;

// Декодер отдаёт кусочки картинки; складываем точки центрального квадрата в клетки 24x24.
bool avaCollect(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bmp) {
  for (int j = 0; j < h; j++) {
    int cy = (y + j - avaOy) * mqtt::AVA / avaSide;
    if (y + j < avaOy || cy < 0 || cy >= mqtt::AVA) continue;
    for (int i = 0; i < w; i++) {
      int cx = (x + i - avaOx) * mqtt::AVA / avaSide;
      if (x + i < avaOx || cx < 0 || cx >= mqtt::AVA) continue;
      uint16_t c = bmp[j * w + i];
      uint32_t* cell = avaSum + (cy * mqtt::AVA + cx) * 4;
      cell[0] += (c >> 11) << 3;
      cell[1] += ((c >> 5) & 63) << 2;
      cell[2] += (c & 31) << 3;
      cell[3]++;
    }
  }
  return true;
}

// Сделать аватарку 24x24 из картинки галереи. false - не получилось.
bool makeAvatar(int slot, uint8_t* out) {
  size_t n = 0;
  uint8_t* buf = picLoad(slot, n);
  bool ok = buf != nullptr;
  avaSum = ok ? (uint32_t*)calloc(mqtt::AVA_BYTES * 4, sizeof(uint32_t)) : nullptr;
  if (!avaSum) ok = false;
  if (ok) {
    uint16_t w = 0, h = 0;
    TJpgDec.getJpgSize(&w, &h, buf, (uint32_t)n);
    int m = w < h ? w : h, sc = 1;
    while (sc < 8 && m / (sc * 2) >= mqtt::AVA) sc *= 2;   // уменьшаем при декодировании, но не мельче 24 точек
    int W = w / sc, H = h / sc;
    avaSide = W < H ? W : H;
    if (avaSide < 1) avaSide = 1;
    avaOx = (W - avaSide) / 2;
    avaOy = (H - avaSide) / 2;
    TJpgDec.setSwapBytes(false);
    TJpgDec.setJpgScale((uint8_t)sc);
    TJpgDec.setCallback(avaCollect);
    ok = w && h && TJpgDec.drawJpg(0, 0, buf, (uint32_t)n) == 0;
    TJpgDec.setCallback(jpgOutput);
    for (size_t i = 0; ok && i < mqtt::AVA_BYTES; i++) {
      uint32_t* cell = avaSum + i * 4;
      uint32_t k = cell[3] ? cell[3] : 1;
      out[i] = mqtt::to332((int)(cell[0] / k), (int)(cell[1] / k), (int)(cell[2] / k));
    }
  }
  if (avaSum) free(avaSum);
  avaSum = nullptr;
  if (buf) free(buf);
  return ok;
}

bool pickAvatar() {
  std::vector<int> slots;
  picSlots(slots);
  std::vector<String> o = splitBar(L3("Выбрать из галереи|Убрать аватарку|Назад", "Choose from the gallery|Remove the avatar|Back", "从相册选择|移除头像|返回"));
  int k = choiceScreen(L3("Аватарка", "Avatar", "头像"), o);
  if (k == 1) {
    myAvaSet = false;
    LittleFS.remove("/avatar.bin");
    return true;
  }
  if (k != 0) return false;
  if (slots.empty()) {
    viewAnswer(L3("В галерее пусто. Загрузите картинку: Инструменты - Галерея.", "The gallery is empty. Upload a picture: Tools - Gallery.",
                  "相册是空的。请上传图片：工具-相册。"), C_YELLOW);
    return false;
  }
  int pos = 0, n = (int)slots.size();
  auto show = [&]() {
    tft.fillScreen(C_BLACK);
    SmallFont small;
    char b[48];
    snprintf(b, sizeof(b), "%s %d/%d", L3("Аватарка", "Avatar", "头像"), pos + 1, n);
    drawUtf8(b, 2, 2, C_WHITE, C_BLACK);
    drawUtf8(L3("Нажать - выбрать, держать - отмена", "Click - choose, hold - cancel", "按下：选择，长按：取消"), 2, IMG_Y1 + 2, C_LIGHT, C_BLACK);
  };
  auto pic = [&]() {
    size_t sz = 0;
    uint8_t* buf = picLoad(slots[(size_t)pos], sz);
    if (buf) drawJpegFit(buf, sz);
    if (buf) free(buf);
  };
  show();
  pic();
  for (;;) {
    uint8_t j = joyEvent();
    BtnEvent e = btnEvent();
    if (e == EV_LONG) return false;
    if (e == EV_CLICK) break;
    if (j != J_NONE && !g_joyRepeated && n > 1) {
      pos = (j == J_RIGHT || j == J_DOWN) ? (pos + 1) % n : (pos + n - 1) % n;
      show();
      pic();
    }
    delay(2);
  }
  uint8_t a[mqtt::AVA_BYTES];
  if (!makeAvatar(slots[(size_t)pos], a)) {
    showMsg(L3("Из этой картинки аватарку сделать не удалось.", "Could not make an avatar from this picture.", "无法用这张图片制作头像。"), C_RED);
    delay(1800);
    return false;
  }
  memcpy(myAva, a, mqtt::AVA_BYTES);
  myAvaSet = true;
  File f = LittleFS.open("/avatar.bin", "w");
  if (f) {
    f.write(myAva, mqtt::AVA_BYTES);
    f.close();
  }
  tft.fillScreen(C_BLACK);                            // показать результат крупно (x4) и в настоящем размере
  for (int y = 0; y < mqtt::AVA; y++)
    for (int x = 0; x < mqtt::AVA; x++) tft.fillRect(72 + x * 4, 40 + y * 4, 4, 4, mqtt::from332(myAva[y * mqtt::AVA + x]));
  drawAvatar(myAva, roomId, cfgNick, 108, 150);
  drawSmallCentered(L3("Аватарка готова", "The avatar is ready", "头像已设置"), 190, C_GREEN);
  delay(1800);
  return true;
}
#else
bool pickAvatar() {
  viewAnswer(L3("Аватарки выключены: в скетче ENABLE_IMAGES 0.", "Avatars are off: ENABLE_IMAGES is 0 in the sketch.", "头像已关闭。"), C_YELLOW);
  return false;
}
void runGallery() {
  viewAnswer(L3("Галерея выключена: в скетче ENABLE_IMAGES 0.", "The gallery is off: ENABLE_IMAGES is 0 in the sketch.", "相册已关闭。"), C_YELLOW);
}
void runImages() {}
#endif

// ========================= ИНСТРУМЕНТЫ =========================
// Запрос GET: возвращает HTTP-код (или отрицательный код ошибки сети), в body - ответ.
int httpGetText(const String& url, String& body) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(15);
  HTTPClient http;
  http.setUserAgent(APP_UA);
  http.setConnectTimeout(15000);
  http.setTimeout(15000);
  if (!http.begin(client, url)) return -100;
  int code = http.GET();
  body = code > 0 ? http.getString() : HTTPClient::errorToString(code);
  http.end();
  Serial.printf("[get] %d, %u bytes\n", code, (unsigned)body.length());
  if (code < 0) {
    Serial.printf("[get] %s failed, heap %u, largest block %u\n", url.c_str(), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    // Погода (Open-Meteo) отвечает и без шифрования: если защищённое соединение не установилось
    // (сеть не пускает или не хватило памяти) - пробуем обычное. Ключей и личных данных в этих адресах нет.
    if (url.startsWith("https://") && url.indexOf("open-meteo.com/") > 0) {
      WiFiClient plain;
      HTTPClient h2;
      h2.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
      h2.setUserAgent(APP_UA);
      h2.setConnectTimeout(15000);
      h2.setTimeout(15000);
      if (h2.begin(plain, String("http://") + url.substring(8))) {
        code = h2.GET();
        body = code > 0 ? h2.getString() : HTTPClient::errorToString(code);
        h2.end();
        Serial.printf("[get] plain http: %d, %u bytes\n", code, (unsigned)body.length());
      }
    }
  }
  return code;
}

String httpErr(int code, const String& body) {
  String why;
  if (code < 0) {
    why = body;
  } else {
    int r = body.indexOf("\"reason\"");                // Open-Meteo и многие API пишут причину в поле reason
    if (r >= 0) {
      int a = body.indexOf('"', body.indexOf(':', r) + 1), b = a >= 0 ? body.indexOf('"', a + 1) : -1;
      if (a >= 0 && b > a) why = body.substring(a + 1, b);
    }
    if (!why.length()) {
      why = code == 429 ? L3("слишком много запросов, попробуйте через минуту", "too many requests, try again in a minute", "请求过多，请稍后再试")
            : code >= 500 ? L3("сервер временно не работает", "the server is temporarily down", "服务器暂时不可用")
            : code == 403 ? L3("сервер не пускает (доступ закрыт)", "the server refuses access", "服务器拒绝访问")
                          : "";
    }
  }
  if (why.length() > 120) why = why.substring(0, 120);
  return String(T().errPrefix) + "HTTP " + String(code) + (why.length() ? String("\n") + why : String(""));
}

// Запрос с повтором: сеть или сервер иногда сбоят на секунду.
int httpGetRetry(const String& url, String& body) {
  int code = httpGetText(url, body);
  for (int k = 0; k < 2 && code != 200 && code != 400 && code != 404; k++) {
    delay(800 + 1200 * k);
    code = httpGetText(url, body);
  }
  return code;
}

// Текст с прокруткой; удержание кнопки сохраняет его в «Сохранённое».
void viewSavable(const String& title, const String& text) {
  for (;;) {
    if (viewText(title + "\n\n" + text, C_WHITE) != EV_LONG) return;
    saveItem(title, text);
    showMsg(L3("Сохранено. Смотрите в Меню - Сохранённое.", "Saved. See Menu - Saved.", "已保存。见菜单-已保存。"), C_GREEN);
    delay(1200);
  }
}

// ---------- Шаблоны вопросов ----------
// true - шаблон выбран и положен в поле ввода.
bool runTemplates() {
  std::vector<String> items = splitBar(L3(
    "Объясни просто: |Реши задачу: |Формула: |Что такое |Кратко о |Сравни: |Исправь ошибки: |Приведи пример: |План ответа на тему: |Назад",
    "Explain simply: |Solve: |Formula for |What is |Briefly about |Compare: |Fix the mistakes: |Give an example of |Outline an answer on: |Back",
    "简单解释：|解题：|公式：|什么是|简述|比较：|改正错误：|举例说明|回答提纲：|返回"));
  int k = listScreen(L3("Шаблоны", "Templates", "模板"), items, std::vector<String>(), 0);
  if (k < 0 || k == (int)items.size() - 1) return false;
  pendingInput = items[(size_t)k];
  return true;
}

// ---------- Сохранённое ----------
void runSaved() {
  int sel = 0;
  for (;;) {
    loadSaved();
    if (savedTitle.empty()) {
      viewAnswer(L3("Пока пусто. Чтобы сохранить ответ, удерживайте кнопку, когда он на экране.",
                    "Nothing yet. To save an answer, hold the button while it is on the screen.",
                    "还没有内容。要保存回答，请在显示回答时长按按钮。"), C_YELLOW);
      return;
    }
    char t[48];
    snprintf(t, sizeof(t), "%s %u/%u", L3("Сохранённое", "Saved", "已保存"), (unsigned)savedTitle.size(), (unsigned)MAX_SAVED);
    sel = listScreen(t, savedTitle, std::vector<String>(), sel);
    if (sel < 0) return;
    if (viewText(savedTitle[(size_t)sel] + "\n\n" + savedText[(size_t)sel], C_WHITE) == EV_LONG) {
      std::vector<String> yn = splitBar(L3("Удалить|Оставить", "Delete|Keep", "删除|保留"));
      if (choiceScreen(L3("Удалить эту запись?", "Delete this entry?", "删除这条记录？"), yn) == 0) {
        savedTitle.erase(savedTitle.begin() + sel);
        savedText.erase(savedText.begin() + sel);
        storeSaved();
        if (sel > 0) sel--;
      }
    }
  }
}

// ---------- Калькулятор ----------
struct CalcKey {
  const char* label;     // что написано на клавише
  const char* ins;       // что вставить (nullptr - особое действие по label)
};
// Страница 1 - обычная, страница 2 («2nd») - гиперболические функции, график y(x), решение x=? и др.
const CalcKey CALC_KEYS[2][6][6] = {
  {
    {{"sin", "sin("}, {"cos", "cos("}, {"tan", "tan("}, {"(", "("}, {")", ")"}, {"<-", nullptr}},
    {{"asin", "asin("}, {"acos", "acos("}, {"atan", "atan("}, {"x\xC2\xB2", "\xC2\xB2"}, {"^", "^"}, {"C", nullptr}},
    {{"ln", "ln("}, {"log", "log("}, {"7", "7"}, {"8", "8"}, {"9", "9"}, {"\xC3\xB7", "\xC3\xB7"}},
    {{"\xE2\x88\x9A", "\xE2\x88\x9A("}, {"2nd", nullptr}, {"4", "4"}, {"5", "5"}, {"6", "6"}, {"\xC3\x97", "\xC3\x97"}},
    {{"\xCF\x80", "\xCF\x80"}, {"e", "e"}, {"1", "1"}, {"2", "2"}, {"3", "3"}, {"-", "-"}},
    {{"DEG", nullptr}, {"x", "x"}, {"0", "0"}, {".", "."}, {"=", nullptr}, {"+", "+"}},
  },
  {
    {{"y(x)", nullptr}, {"x=?", nullptr}, {"sinh", "sinh("}, {"(", "("}, {")", ")"}, {"<-", nullptr}},
    {{"cosh", "cosh("}, {"tanh", "tanh("}, {"abs", "abs("}, {"exp", "exp("}, {"^", "^"}, {"C", nullptr}},
    {{"\xE2\x88\x9B", "\xE2\x88\x9B("}, {"L=R", "="}, {"7", "7"}, {"8", "8"}, {"9", "9"}, {"\xC3\xB7", "\xC3\xB7"}},
    {{"x!", "!"}, {"1st", nullptr}, {"4", "4"}, {"5", "5"}, {"6", "6"}, {"\xC3\x97", "\xC3\x97"}},
    {{"Ans", "Ans"}, {"1/x", "\xE2\x81\xBB\xC2\xB9"}, {"1", "1"}, {"2", "2"}, {"3", "3"}, {"-", "-"}},
    {{"DEG", nullptr}, {"x", "x"}, {"0", "0"}, {".", "."}, {"=", nullptr}, {"+", "+"}},
  },
};
int calcPage = 0;                                    // 0 - обычные клавиши, 1 - вторая страница
std::string calcLastFn;                              // последняя функция/уравнение с x (для графика и решения)
#define calcKeyAt(r, c) CALC_KEYS[calcPage][(r)][(c)]   // макрос, а не функция: Arduino IDE 1.8 ставит прототипы выше struct CalcKey
calc::Parser calcP;
const int CK_W = 40, CK_H = 25, CK_Y = 90;

void drawCalcKey(int r, int c, bool sel) {
  const CalcKey& k = calcKeyAt(r, c);
  const char* label = k.label;
  if (r == 5 && c == 0) label = calcP.degrees ? "DEG" : "RAD";
  bool fnKey = !strcmp(label, "2nd") || !strcmp(label, "1st") || !strcmp(label, "y(x)") || !strcmp(label, "x=?");
  bool digit = c >= 2 && r >= 2 && c <= 4;
  uint16_t bg = sel ? C_BLUE : digit ? 0x2104 : (c == 5 || (r < 2 && c >= 3)) ? 0x39C7 : 0x10A2;
  uint16_t fg = (r == 5 && c == 4) ? C_GREEN : fnKey ? C_YELLOW : C_WHITE;
  int x = c * CK_W, y = CK_Y + r * CK_H;
  tft.fillRect(x, y, CK_W - 1, CK_H - 1, bg);
  std::vector<uint16_t> cps;
  utf8ToCps(label, cps);
  if ((int)cps.size() * BIG_W <= CK_W - 2) {         // короткая подпись - крупно
    int lx = x + (CK_W - 1 - (int)cps.size() * BIG_W) / 2;
    for (uint16_t ch : cps) lx += drawCp(ch, lx, y, fg, bg);
  } else {
    SmallFont small;
    int lx = x + (CK_W - 1 - (int)cps.size() * GLYPH_W) / 2;
    for (uint16_t ch : cps) lx += drawCp(ch, lx, y + 4, fg, bg);
  }
}

// Удалить последний знак; имя функции со скобкой («sin(») удаляется целиком.
void calcBackspace(std::string& e) {
  if (e.empty()) return;
  static const char* TOK[] = {"asin(", "acos(", "atan(", "sinh(", "cosh(", "tanh(", "sin(", "cos(", "tan(", "ln(", "log(",
                              "abs(", "exp(", "\xE2\x88\x9A(", "\xE2\x88\x9B(", "Ans", "\xE2\x81\xBB\xC2\xB9"};
  for (const char* t : TOK) {
    size_t n = strlen(t);
    if (e.size() >= n && e.compare(e.size() - n, n, t) == 0) {
      e.erase(e.size() - n);
      return;
    }
  }
  size_t i = e.size() - 1;
  while (i > 0 && ((unsigned char)e[i] & 0xC0) == 0x80) i--;   // целый символ UTF-8
  e.erase(i);
}

void drawCalcDisplay(const std::string& expr, const std::string& res, uint16_t resCol) {
  std::vector<uint16_t> cps;
  utf8ToCps(expr.c_str(), cps);
  size_t from = cps.size() > 40 ? cps.size() - 40 : 0;       // две строки по 20 знаков, виден конец
  for (int line = 0; line < 2; line++) {
    size_t a = from + (size_t)line * 20;
    int n = a < cps.size() ? (int)(cps.size() - a < 20 ? cps.size() - a : 20) : 0;
    drawLine(n ? cps.data() + a : nullptr, n, line * 24, C_WHITE, C_BLACK);
  }
  tft.fillRect(0, 52, 240, 24, C_BLACK);
  std::vector<uint16_t> rc;
  utf8ToCps(res.c_str(), rc);
  int w = (int)rc.size() * BIG_W;
  if (w <= 236) {
    int x = 238 - w;
    for (uint16_t ch : rc) x += drawCp(ch, x, 52, resCol, C_BLACK);
  } else {
    SmallFont small;
    drawUtf8(res.c_str(), 2, 56, resCol, C_BLACK);
  }
}

// ---------- График функции и решение уравнений ----------
const int PLOT_W = 240, PLOT_H = 216;                // область графика; снизу строка со значениями

// Вверх/вниз - масштаб, влево/вправо - сдвиг, нажатие - курсор (показывает x и y), удержание - выход.
void runGraph(const std::string& fn) {
  double xc = 0;
  double w = (calcP.degrees && calc::hasTrig(fn)) ? 720.0 : 20.0;   // ширина окна по x
  bool trace = false;
  int tx = PLOT_W / 2;
  std::vector<float> ys((size_t)PLOT_W);
  double ylo = -1, yhi = 1;
  auto sample = [&]() {
    double x0 = xc - w / 2, dx = w / (PLOT_W - 1);
    std::vector<float> fin;
    fin.reserve((size_t)PLOT_W);
    for (int i = 0; i < PLOT_W; i++) {
      calc::Err e;
      double v = calcP.evalAt(fn, x0 + i * dx, e);
      bool good = e == calc::E_NONE && isfinite(v) && fabs(v) < 1e30;
      ys[(size_t)i] = good ? (float)v : NAN;
      if (good) fin.push_back((float)v);
    }
    if (fin.empty()) {
      ylo = -1;
      yhi = 1;
      return;
    }
    std::sort(fin.begin(), fin.end());
    double mn = fin.front(), mx = fin.back();
    double p5 = fin[fin.size() * 5 / 100], p95 = fin[(fin.size() - 1) * 95 / 100];
    if (p95 > p5 && mx - mn > 20 * (p95 - p5)) {     // уходит в бесконечность (1/x, tan x): берём основную часть
      mn = p5 - 0.5 * (p95 - p5);
      mx = p95 + 0.5 * (p95 - p5);
    }
    if (mx - mn < 1e-12 * std::max(1.0, fabs(mx))) {
      mn -= 1;
      mx += 1;
    } else {
      double pad = (mx - mn) * 0.06;
      mn -= pad;
      mx += pad;
    }
    ylo = mn;
    yhi = mx;
  };
  auto py = [&](double v) {
    double t = (yhi - v) / (yhi - ylo) * (PLOT_H - 1);
    if (t < -2000) t = -2000;
    if (t > 2000) t = 2000;
    return (int)lround(t);
  };
  auto draw = [&]() {
    double x0 = xc - w / 2, dx = w / (PLOT_W - 1);
    tft.fillRect(0, 0, PLOT_W, PLOT_H, C_BLACK);
    if (ylo < 0 && yhi > 0) tft.drawFastHLine(0, py(0), PLOT_W, C_GRAY);
    if (x0 < 0 && x0 + w > 0) tft.drawFastVLine((int)lround(-x0 / dx), 0, PLOT_H, C_GRAY);
    char b[48];
    {
      SmallFont small;
      snprintf(b, sizeof(b), "%.4g", yhi);
      drawUtf8(b, 2, 0, C_LIGHT, C_BLACK);
      snprintf(b, sizeof(b), "%.4g", ylo);
      drawUtf8(b, 2, PLOT_H - 16, C_LIGHT, C_BLACK);
    }
    int prevY = 0;
    bool prevOk = false;
    for (int i = 0; i < PLOT_W; i++) {
      if (isnan(ys[(size_t)i])) {
        prevOk = false;
        continue;
      }
      int y = py(ys[(size_t)i]);
      if (prevOk && abs(y - prevY) < PLOT_H * 8 / 10) tft.drawLine(i - 1, prevY, i, y, C_YELLOW);   // разрыв (tan x) не соединяем
      else tft.drawPixel(i, y, C_YELLOW);
      prevY = y;
      prevOk = true;
    }
    if (trace) {
      tft.drawFastVLine(tx, 0, PLOT_H, 0x03FF);
      if (!isnan(ys[(size_t)tx])) tft.fillCircle(tx, py(ys[(size_t)tx]), 3, C_RED);
    }
    tft.fillRect(0, PLOT_H, 240, 240 - PLOT_H, C_BLACK);
    SmallFont small;
    if (trace) {
      if (isnan(ys[(size_t)tx])) snprintf(b, sizeof(b), "x=%.5g  y: --", x0 + tx * dx);
      else snprintf(b, sizeof(b), "x=%.5g  y=%.5g", x0 + tx * dx, (double)ys[(size_t)tx]);
    } else {
      snprintf(b, sizeof(b), "x: %.4g ... %.4g", x0, x0 + w);
    }
    drawUtf8(b, 2, PLOT_H + 4, C_WHITE, C_BLACK);
  };
  showMsg(L3("Строю график...", "Plotting...", "正在绘图……"), C_YELLOW);
  sample();
  draw();
  for (;;) {
    uint8_t j = joyEvent();
    bool resample = false, redraw = false;
    if (j == J_UP) {
      w = std::max(w / 1.5, 1e-4);
      resample = redraw = true;
    } else if (j == J_DOWN) {
      w = std::min(w * 1.5, 1e6);
      resample = redraw = true;
    } else if (j == J_LEFT || j == J_RIGHT) {
      int dir = j == J_LEFT ? -1 : 1;
      if (trace) {
        tx += dir * 3;
        if (tx < 0 || tx > PLOT_W - 1) {             // курсор дошёл до края - сдвигаем окно
          tx = tx < 0 ? 0 : PLOT_W - 1;
          xc += dir * w / 8;
          resample = true;
        }
      } else {
        xc += dir * w / 8;
        resample = true;
      }
      redraw = true;
    }
    if (redraw) {
      if (resample) sample();
      draw();
    }
    BtnEvent e = btnEvent();
    if (e == EV_LONG) return;
    if (e == EV_CLICK) {
      trace = !trace;
      tx = PLOT_W / 2;
      draw();
    }
    delay(2);
  }
}

// Корни уравнения: многочлены и одночлены - точно (с комплексными), остальное - приближённо.
void runSolve(const std::string& fn) {
  showMsg(L3("Решаю...", "Solving...", "正在求解……"), C_YELLOW);
  calc::Solution sol;
  calc::solve(calcP, fn, sol);
  std::string head = fn;
  if (!calc::hasEq(fn)) head += " = 0";
  viewAnswer(fromStd(head + "\n\n" + calc::describe(sol, cfgLang == LANG_RU)), C_WHITE);
}

void runCalc() {
  std::string expr, res;
  uint16_t resCol = C_LIGHT;
  bool fresh = false;                                // только что нажато «=»
  int row = 2, col = 2;
  bool ru = cfgLang == LANG_RU;
  calcPage = 0;
  auto drawKeys = [&]() {
    for (int r = 0; r < 6; r++)
      for (int c = 0; c < 6; c++) drawCalcKey(r, c, r == row && c == col);
  };
  tft.fillScreen(C_BLACK);
  tft.fillRect(0, 80, 240, 1, C_GRAY);
  drawKeys();
  auto preview = [&]() {                             // считаем на лету, пока идёт набор
    calc::Err err;
    if (expr.empty()) {
      res = "";
    } else if (calc::hasVar(expr) || calc::hasEq(expr)) {   // функция или уравнение - значения нет
      res = calc::hasEq(expr) ? L3("уравнение", "equation", "方程") : L3("функция f(x)", "function f(x)", "函数 f(x)");
    } else {
      double v = calcP.eval(expr, err);
      res = err == calc::E_NONE ? "= " + calc::format(v) : "";
    }
    resCol = C_LIGHT;
    drawCalcDisplay(expr, res, resCol);
  };
  drawCalcDisplay(expr, res, resCol);
  for (;;) {
    uint8_t j = joyEvent();
    if (j != J_NONE) {
      drawCalcKey(row, col, false);
      if (j == J_LEFT) col = (col + 5) % 6;
      if (j == J_RIGHT) col = (col + 1) % 6;
      if (j == J_UP) row = (row + 5) % 6;
      if (j == J_DOWN) row = (row + 1) % 6;
      drawCalcKey(row, col, true);
    }
    BtnEvent e = btnEvent();
    if (e == EV_LONG) return;
    if (e == EV_CLICK) {
      const CalcKey& k = calcKeyAt(row, col);
      if (k.ins) {
        char c0 = k.ins[0];
        bool op = c0 == '+' || c0 == '^' || c0 == '!' || !strcmp(k.ins, "\xC3\x97") || !strcmp(k.ins, "\xC3\xB7") ||
                  !strcmp(k.ins, "\xC2\xB2") || !strcmp(k.ins, "\xE2\x81\xBB\xC2\xB9");
        if (fresh) {                                 // после «=»: знак действия продолжает с Ans, остальное - новый пример
          expr = (op || c0 == '-') ? "Ans" : "";
          fresh = false;
        } else if (expr.empty() && op) {             // пример начат со знака действия - берём прошлый ответ
          expr = "Ans";
        }
        if (expr.size() < 120) expr += k.ins;
        preview();
      } else if (!strcmp(k.label, "<-")) {
        fresh = false;
        calcBackspace(expr);
        preview();
      } else if (!strcmp(k.label, "C")) {
        fresh = false;
        expr.clear();
        preview();
      } else if (!strcmp(k.label, "2nd") || !strcmp(k.label, "1st")) {   // вторая страница клавиш
        calcPage = 1 - calcPage;
        drawKeys();
      } else if (!strcmp(k.label, "y(x)") || !strcmp(k.label, "x=?")) {  // график / корни
        std::string fn = expr.empty() ? calcLastFn : expr;
        if (!fn.empty()) {
          calcLastFn = fn;
          if (k.label[0] == 'y') runGraph(fn);
          else runSolve(fn);
          tft.fillScreen(C_BLACK);                   // вернулись: перерисовать калькулятор
          tft.fillRect(0, 80, 240, 1, C_GRAY);
          drawKeys();
          drawCalcDisplay(expr, res, resCol);
        }
      } else if (!strcmp(k.label, "DEG")) {
        calcP.degrees = !calcP.degrees;
        drawCalcKey(row, col, true);
        if (!fresh) preview();
      } else if (!expr.empty() && (calc::hasVar(expr) || calc::hasEq(expr))) {   // «=» для функции: подсказка
        res = L3("2nd: y(x) график, x=? корни", "2nd: y(x) graph, x=? roots", "2nd: y(x)图像, x=?求根");
        resCol = C_YELLOW;
        drawCalcDisplay(expr, res, resCol);
      } else if (!expr.empty()) {                    // «=»
        calc::Err err;
        double v = calcP.eval(expr, err);
        if (err == calc::E_NONE) {
          calcP.ans = v;
          res = "= " + calc::format(v);
          resCol = C_GREEN;
          fresh = true;
        } else {
          res = calc::errName(err, ru);
          resCol = C_RED;
        }
        drawCalcDisplay(expr, res, resCol);
      }
    }
    delay(2);
  }
}

// ---------- Википедия ----------
void runWiki() {
  for (;;) {
    showMsg(L3("Википедия: о чём рассказать? Наберите и нажмите OK.", "Wikipedia: what to look up? Type it and press OK.",
               "维基百科：想查什么？输入后按OK。"), C_YELLOW);
    delay(1200);
    String q = inputText();
    q.trim();
    if (!q.length()) return;
    if (!waitWifi(false)) return;
    showMsg(L3("Ищу в Википедии...", "Searching Wikipedia...", "正在搜索维基百科……"), C_YELLOW);
    const char* lang = cfgLang == LANG_RU ? "ru" : cfgLang == LANG_ZH ? "zh" : "en";
    String body;
    int code = httpGetText(fromStd(tools::wikiUrl(lang, toStd(q))), body);
    if (code != 200) {
      viewAnswer(httpErr(code, body), C_RED);
      continue;
    }
    std::string title, text;
    if (!tools::parseWiki(toStd(body), title, text)) {
      viewAnswer(L3("В Википедии ничего не нашлось. Попробуйте другой запрос.", "Nothing found on Wikipedia. Try another query.",
                    "维基百科没有找到。请换个关键词。"), C_YELLOW);
      continue;
    }
    viewSavable(fromStd(title), fromStd(text));
  }
}

// ---------- Погода (Open-Meteo, без ключа) ----------
const char* weatherName(int code) {
  static const char* RU[] = {"Ясно", "Облачно", "Туман", "Морось", "Дождь", "Снег", "Ливень", "Гроза"};
  static const char* EN[] = {"Clear", "Cloudy", "Fog", "Drizzle", "Rain", "Snow", "Showers", "Thunder"};
  static const char* ZH[] = {"晴", "多云", "雾", "毛毛雨", "雨", "雪", "阵雨", "雷暴"};
  int k = tools::weatherKind(code);
  return cfgLang == LANG_RU ? RU[k] : cfgLang == LANG_ZH ? ZH[k] : EN[k];
}

// Координаты города через wttr.in - если геокодер Open-Meteo недоступен.
bool wttrGeo(const String& city, tools::Place& pl) {
#if !ENABLE_WTTR
  (void)city;
  (void)pl;
  return false;
#else
  String path = String("wttr.in/") + fromStd(tools::urlEncode(toStd(city))) + "?format=j1";
  JsonDocument filter;
  JsonObject na = filter["nearest_area"].add<JsonObject>();
  na["latitude"] = true;
  na["longitude"] = true;
  na["areaName"][0]["value"] = true;
  for (int pass = 0; pass < 2; pass++) {
    WiFiClientSecure sc;
    sc.setInsecure();
    sc.setHandshakeTimeout(15);
    WiFiClient pc;
    HTTPClient http;
    http.useHTTP10(true);
    http.setUserAgent("curl/8.5");
    http.setConnectTimeout(15000);
    http.setTimeout(15000);
    bool ok = pass == 0 ? http.begin(sc, String("https://") + path) : http.begin(pc, String("http://") + path);
    if (!ok) continue;
    int code = http.GET();
    Serial.printf("[wttr geo] pass %d: %d\n", pass, code);
    if (code != 200) {
      http.end();
      continue;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();
    JsonObject a = doc["nearest_area"][0];
    if (err || a.isNull()) continue;
    pl.lat = atof(a["latitude"] | "0");
    pl.lon = atof(a["longitude"] | "0");
    pl.name = toStd(city);                          // название - как набрали (wttr.in пишет его по-английски)
    return pl.lat != 0 || pl.lon != 0;
  }
  return false;
#endif
}

// Спросить город и найти его координаты. true - найден и сохранён.
bool chooseCity() {
  showMsg(L3("Погода: наберите город и нажмите OK.", "Weather: type a city and press OK.", "天气：输入城市后按OK。"), C_YELLOW);
  delay(1200);
  String q = inputText();
  q.trim();
  if (!q.length()) return false;
  if (!waitWifi(false)) return false;
  showMsg(L3("Ищу город...", "Looking up the city...", "正在查找城市……"), C_YELLOW);
  const char* lang = cfgLang == LANG_RU ? "ru" : cfgLang == LANG_ZH ? "zh" : "en";
  String body;
  int code = httpGetRetry(fromStd(tools::geoUrl(toStd(q), lang)), body);
  tools::Place pl;
  if (code != 200) {
    String first = httpErr(code, body);
    body = "";
    if (wttrGeo(q, pl)) goto found;
    viewAnswer(first, C_RED);
    return false;
  }
  if (!tools::parseGeo(toStd(body), pl)) {
    viewAnswer(L3("Город не найден. Проверьте название.", "City not found. Check the name.", "未找到城市。请检查名称。"), C_YELLOW);
    return false;
  }
found:
  cfgCity = fromStd(pl.name);
  cfgLat = (float)pl.lat;
  cfgLon = (float)pl.lon;
  prefs.begin("dsk", false);
  prefs.putString("city", cfgCity);
  prefs.putString("lat", String(cfgLat, 4));
  prefs.putString("lon", String(cfgLon, 4));
  prefs.end();
  return true;
}

String degStr(double t) {
  if (isnan(t)) return String("?");
  int v = (int)lround(t);
  return String(v > 0 ? "+" : "") + String(v) + "\xC2\xB0";
}

// Запасной источник погоды - wttr.in (тоже без ключа), если Open-Meteo недоступен (у некоторых
// провайдеров его адреса закрыты: «connection refused»). Ответ большой (~50 КБ), поэтому разбираем
// его прямо из потока и берём только нужные поля.
int wwoToWmo(int c) {                                // коды погоды wttr.in (WWO) -> коды Open-Meteo (WMO)
  if (c == 113) return 0;
  if (c == 116) return 2;
  if (c == 119 || c == 122) return 3;
  if (c == 143 || c == 248 || c == 260) return 45;
  if (c == 263 || c == 266 || c == 281 || c == 284) return 51;
  if (c == 200 || (c >= 386 && c <= 395)) return 95;
  if (c == 179 || c == 227 || c == 230 || (c >= 323 && c <= 338) || c == 368 || c == 371 || c == 374 || c == 377) return 71;
  if ((c >= 353 && c <= 359) || c == 299 || c == 305) return 80;
  if (c == 176 || c == 182 || c == 185 || (c >= 293 && c <= 320) || c == 350 || c == 362 || c == 365) return 61;
  return 3;
}

bool wttrForecast(tools::Forecast& f) {
#if !ENABLE_WTTR
  (void)f;
  return false;
#else
  char path[64];
  snprintf(path, sizeof(path), "wttr.in/%.3f,%.3f?format=j1", cfgLat, cfgLon);
  JsonDocument filter;
  JsonObject cc = filter["current_condition"].add<JsonObject>();
  cc["temp_C"] = true;
  cc["FeelsLikeC"] = true;
  cc["humidity"] = true;
  cc["windspeedKmph"] = true;
  cc["weatherCode"] = true;
  JsonObject wd = filter["weather"].add<JsonObject>();
  wd["date"] = true;
  wd["maxtempC"] = true;
  wd["mintempC"] = true;
  JsonObject hr = wd["hourly"].add<JsonObject>();
  hr["chanceofrain"] = true;
  hr["chanceofsnow"] = true;
  hr["weatherCode"] = true;
  for (int pass = 0; pass < 2; pass++) {             // сначала https, потом обычный http
    WiFiClientSecure sc;
    sc.setInsecure();
    sc.setHandshakeTimeout(15);
    WiFiClient pc;
    HTTPClient http;
    http.useHTTP10(true);                            // без «chunked» - поток можно разбирать сразу
    http.setUserAgent("curl/8.5");
    http.setConnectTimeout(15000);
    http.setTimeout(15000);
    bool ok = pass == 0 ? http.begin(sc, String("https://") + path) : http.begin(pc, String("http://") + path);
    if (!ok) continue;
    int code = http.GET();
    Serial.printf("[wttr] pass %d: %d\n", pass, code);
    if (code != 200) {
      http.end();
      continue;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();
    if (err) {
      Serial.printf("[wttr] json: %s\n", err.c_str());
      continue;
    }
    JsonObject c = doc["current_condition"][0];
    if (c.isNull()) continue;
    f.temp = atof(c["temp_C"] | "nan");
    f.feels = atof(c["FeelsLikeC"] | "nan");
    f.hum = atof(c["humidity"] | "nan");
    f.wind = atof(c["windspeedKmph"] | "nan") / 3.6;
    f.code = wwoToWmo(atoi(c["weatherCode"] | "122"));
    for (JsonObject d : doc["weather"].as<JsonArray>()) {
      f.days.push_back(std::string(d["date"] | ""));
      f.tmax.push_back(atof(d["maxtempC"] | "nan"));
      f.tmin.push_back(atof(d["mintempC"] | "nan"));
      double rain = NAN;
      int dc = -1;
      JsonArray h = d["hourly"].as<JsonArray>();
      for (size_t i = 0; i < h.size(); i++) {
        double r = atof(h[i]["chanceofrain"] | "0"), sn = atof(h[i]["chanceofsnow"] | "0");
        double m = r > sn ? r : sn;
        if (isnan(rain) || m > rain) rain = m;
      }
      if (h.size()) dc = wwoToWmo(atoi(h[h.size() / 2]["weatherCode"] | "122"));   // погода в середине дня
      f.rain.push_back(rain);
      f.dcode.push_back(dc < 0 ? NAN : (double)dc);
    }
    return !isnan(f.temp);
  }
  return false;
#endif
}

void runWeather() {
  for (;;) {
    if (cfgCity.length() == 0 && !chooseCity()) return;
    if (!waitWifi(false)) return;
    showMsg(L3("Узнаю погоду...", "Getting the weather...", "正在获取天气……"), C_YELLOW);
    String body;
    int code = httpGetRetry(fromStd(tools::forecastUrl(cfgLat, cfgLon)), body);
    tools::Forecast f;
    if (code != 200 || !tools::parseForecast(toStd(body), f)) {
      String first = code != 200 ? httpErr(code, body) : String(T().errBadReply);
      body = "";
      f = tools::Forecast();
      showMsg(L3("Open-Meteo не отвечает, пробую wttr.in...", "Open-Meteo is not answering, trying wttr.in...", "Open-Meteo无响应，尝试wttr.in……"), C_YELLOW);
      if (!wttrForecast(f)) {
        viewAnswer(first + L3("\n\nЗапасной сервер wttr.in тоже недоступен.", "\n\nThe backup server wttr.in is unreachable too.",
                              "\n\n备用服务器wttr.in也无法访问。"), C_RED);
        return;
      }
    }
    noteActivity();
    tft.fillScreen(C_BLACK);
    drawUtf8(cfgCity.c_str(), 2, 0, C_YELLOW, C_BLACK);
    String t = degStr(f.temp);
    drawBigStr(t.c_str(), nullptr, 4, 28, 2, C_WHITE, C_BLACK);
    drawUtf8(weatherName(f.code), 4 + textWidth(t.c_str()) * 2 + 10, 40, 0x07FF, C_BLACK);
    {
      SmallFont small;
      char b[80];
      snprintf(b, sizeof(b), "%s %s", L3("Ощущается как", "Feels like", "体感"), degStr(f.feels).c_str());
      drawUtf8(b, 4, 82, C_LIGHT, C_BLACK);
      snprintf(b, sizeof(b), "%s %.0f %s   %s %.0f%%", L3("Ветер", "Wind", "风"), f.wind, L3("м/с", "m/s", "米/秒"),
               L3("Влажность", "Humidity", "湿度"), f.hum);
      drawUtf8(b, 4, 100, C_LIGHT, C_BLACK);
      tft.fillRect(0, 122, 240, 1, C_GRAY);
      for (size_t i = 1; i < f.days.size() && i < 4; i++) {     // следующие три дня
        int y = 128 + (int)(i - 1) * 26;
        const std::string& d = f.days[i];
        std::string dd = d.size() >= 10 ? d.substr(8, 2) + "." + d.substr(5, 2) : d;
        drawUtf8(dd.c_str(), 4, y, C_WHITE, C_BLACK);
        String range = (i < f.tmin.size() ? degStr(f.tmin[i]) : String("?")) + ".." + (i < f.tmax.size() ? degStr(f.tmax[i]) : String("?"));
        drawUtf8(range.c_str(), 52, y, C_WHITE, C_BLACK);
        if (i < f.dcode.size() && !isnan(f.dcode[i])) drawUtf8(weatherName((int)f.dcode[i]), 132, y, 0x07FF, C_BLACK);
        if (i < f.rain.size() && !isnan(f.rain[i])) {
          snprintf(b, sizeof(b), "%.0f%%", f.rain[i]);
          drawUtf8(b, 240 - textWidth(b) - 2, y, C_LIGHT, C_BLACK);
        }
      }
      drawUtf8(L3("Нажать-назад, держать-город", "Click-back, hold-city", "按下：返回，长按：换城市"), 2, 222, C_LIGHT, C_BLACK);
    }
    while (btnEvent() != EV_NONE) delay(5);
    BtnEvent e = waitButton();
    if (e != EV_LONG) return;
    if (!chooseCity()) return;
  }
}

// ---------- Курсы валют ЦБ РФ ----------
void runRates() {
  if (!waitWifi(false)) return;
  showMsg(L3("Загружаю курсы ЦБ...", "Loading exchange rates...", "正在加载汇率……"), C_YELLOW);
  String body;
  int code = httpGetText(tools::cbrUrl(), body);
  std::vector<tools::Rate> rates;
  std::string date;
  if (code != 200) {
    viewAnswer(httpErr(code, body), C_RED);
    return;
  }
  bool ok = tools::parseCbr(toStd(body), rates, date);
  body = "";
  if (!ok) {
    viewAnswer(T().errBadReply, C_RED);
    return;
  }
  std::vector<String> items, right;
  for (const tools::Rate& r : rates) {
    char b[40];
    if (r.nominal != 1) snprintf(b, sizeof(b), "%.0f %s", r.nominal, r.code.c_str());
    else snprintf(b, sizeof(b), "%s", r.code.c_str());
    items.push_back(b);
    snprintf(b, sizeof(b), "%.2f %s", r.value, r.value > r.prev ? "\xE2\x86\x91" : r.value < r.prev ? "\xE2\x86\x93" : " ");
    right.push_back(b);
  }
  String title = String(L3("Курсы ЦБ ", "CBR rates ", "汇率 ")) + (date.size() >= 5 ? date.substr(0, 5).c_str() : "");
  int sel = 0;
  for (;;) {
    sel = listScreen(title.c_str(), items, right, sel);
    if (sel < 0) return;
    const tools::Rate& r = rates[(size_t)sel];
    char b[300];
    snprintf(b, sizeof(b), "%s\n\n%.0f %s = %.4f \xE2\x82\xBD\n%s: %.4f\n%s: %+.4f\n\n%s %s", r.name.c_str(), r.nominal, r.code.c_str(),
             r.value, L3("Вчера", "Before", "之前"), r.prev, L3("Изменение", "Change", "变化"), r.value - r.prev,
             L3("Курс ЦБ РФ на", "Bank of Russia rate for", "俄罗斯央行汇率"), date.c_str());
    viewAnswer(b, C_WHITE);
  }
}

// ---------- Часы, таймер, секундомер ----------
bool ntpStarted = false;
void startNtp() {                                    // время из интернета; дальше часы идут сами
  configTime((long)cfgTz * 3600, 0, "pool.ntp.org", "time.google.com", "ru.pool.ntp.org");
  ntpStarted = true;
}
bool timeNow(struct tm& t);
bool timeNow(struct tm& t) {
  if (!ntpStarted) {
    if (!wifiOk()) return false;
    startNtp();
  }
  return getLocalTime(&t, 5) && t.tm_year > 120;
}

void runClock() {
  KeepAwake awake;
  tft.fillScreen(C_BLACK);
  char prevT[8] = "", prevS[4] = "", prevD[24] = "";
  bool hadTime = false, first = true;
  for (;;) {
    if (btnEvent() != EV_NONE) return;
    static uint32_t next = 0;
    if (first || (int32_t)(millis() - next) >= 0) {
      next = millis() + 200;
      wifiTick();
      struct tm t;
      if (timeNow(t)) {
        if (!hadTime) tft.fillScreen(C_BLACK);
        hadTime = true;
        char b[24];
        snprintf(b, sizeof(b), "%02d:%02d", t.tm_hour, t.tm_min);
        drawBigStr(b, prevT[0] ? prevT : nullptr, 30, 44, 3, C_WHITE, C_BLACK);
        strcpy(prevT, b);
        snprintf(b, sizeof(b), "%02d", t.tm_sec);
        if (strcmp(b, prevS)) drawUtf8(b, 108, 120, C_YELLOW, C_BLACK);
        strcpy(prevS, b);
        static const char* WD_RU[] = {"воскресенье", "понедельник", "вторник", "среда", "четверг", "пятница", "суббота"};
        static const char* WD_EN[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
        static const char* WD_ZH[] = {"星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"};
        snprintf(b, sizeof(b), "%02d.%02d.%04d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
        if (strcmp(b, prevD)) {
          strcpy(prevD, b);
          tft.fillRect(0, 160, 240, 56, C_BLACK);
          drawUtf8(b, (240 - textWidth(b)) / 2, 160, C_WHITE, C_BLACK);
          const char* wd = cfgLang == LANG_RU ? WD_RU[t.tm_wday] : cfgLang == LANG_ZH ? WD_ZH[t.tm_wday] : WD_EN[t.tm_wday];
          drawUtf8(wd, (240 - textWidth(wd)) / 2, 188, 0x07FF, C_BLACK);
        }
      } else if (first) {
        showMsg(L3("Узнаю точное время... Нужен Wi-Fi.", "Getting the time... Wi-Fi is needed.", "正在获取时间……需要Wi-Fi。"), C_YELLOW);
      }
      first = false;
    }
    delay(5);
  }
}

void drawClockHint(const char* s) {
  SmallFont small;
  tft.fillRect(0, 204, 240, 36, C_BLACK);
  std::vector<uint16_t> cps;
  utf8ToCps(s, cps);
  std::vector<Line> lines;
  wrapCps(cps, COLS, lines);
  for (size_t i = 0; i < lines.size() && i < 2; i++) drawLine(cps.data() + lines[i].start, lines[i].len, 206 + (int)i * 16, C_LIGHT, C_BLACK);
}

void runTimer() {
  KeepAwake awake;
  static int setSecs = 300;                          // запоминается до выключения
  int left = setSecs;
  bool running = false, ringing = false;
  int ringShown = -1;                                // что нарисовано при мигании (-1 - ещё ничего)
  uint32_t endMs = 0;
  char prev[8] = "";
  tft.fillScreen(C_BLACK);
  drawUtf8(L3("Таймер", "Timer", "计时器"), 2, 0, C_YELLOW, C_BLACK);
  auto show = [&](uint16_t fg, uint16_t bg, bool all) {
    char b[8];
    snprintf(b, sizeof(b), "%02d:%02d", left / 60, left % 60);
    drawBigStr(b, all || !prev[0] ? nullptr : prev, 30, 70, 3, fg, bg);
    strcpy(prev, b);
  };
  auto hint = [&]() {
    drawClockHint(running ? L3("Нажать - пауза, держать - выход", "Click - pause, hold - exit", "按下：暂停，长按：退出")
                          : L3("Вверх/вниз: 1 мин, вбок: 10 с. Нажать - старт", "Up/down: 1 min, sideways: 10 s. Click - start",
                               "上下：分钟，左右：10秒。按下：开始"));
  };
  show(C_WHITE, C_BLACK, true);
  hint();
  for (;;) {
    BtnEvent e = btnEvent();
    if (e == EV_LONG) return;
    if (ringing) {                                   // время вышло: экран мигает, пока не нажмут
      int on = (int)((millis() / 350) % 2);
      if (on != ringShown) {
        ringShown = on;
        if (on) beep(1, 2000, 140);
        tft.fillRect(0, 60, 240, 92, on ? C_RED : C_BLACK);
        show(C_WHITE, on ? C_RED : C_BLACK, true);
      }
      if (e == EV_CLICK) {
        ringing = false;
        ringShown = -1;
        left = setSecs;
        tft.fillRect(0, 60, 240, 92, C_BLACK);
        show(C_WHITE, C_BLACK, true);
        hint();
      }
      delay(5);
      continue;
    }
    if (running) {
      int l = (int)((int32_t)(endMs - millis()) + 999) / 1000;
      if (l <= 0) {
        left = 0;
        running = false;
        ringing = true;
        drawClockHint(L3("Время вышло! Нажмите кнопку.", "Time is up! Press the button.", "时间到！请按按钮。"));
        continue;
      }
      if (l != left) {
        left = l;
        show(C_GREEN, C_BLACK, false);
      }
      if (e == EV_CLICK) {
        running = false;
        show(C_WHITE, C_BLACK, true);
        hint();
      }
    } else {
      uint8_t j = joyEvent();
      int d = j == J_UP ? 60 : j == J_DOWN ? -60 : j == J_RIGHT ? 10 : j == J_LEFT ? -10 : 0;
      if (d) {
        left += d;
        if (left < 10) left = 10;
        if (left > 99 * 60) left = 99 * 60;
        setSecs = left;
        show(C_WHITE, C_BLACK, false);
      }
      if (e == EV_CLICK) {
        running = true;
        endMs = millis() + (uint32_t)left * 1000UL;
        show(C_GREEN, C_BLACK, true);
        hint();
      }
    }
    delay(5);
  }
}

void runStopwatch() {
  KeepAwake awake;
  uint32_t acc = 0, startMs = 0;
  bool running = false;
  char prev[8] = "", prevT[4] = "";
  tft.fillScreen(C_BLACK);
  drawUtf8(L3("Секундомер", "Stopwatch", "秒表"), 2, 0, C_YELLOW, C_BLACK);
  auto show = [&](bool all) {
    uint32_t ms = acc + (running ? millis() - startMs : 0);
    uint32_t s = ms / 1000;
    char b[8], t[4];
    snprintf(b, sizeof(b), "%02u:%02u", (unsigned)((s / 60) % 100), (unsigned)(s % 60));
    snprintf(t, sizeof(t), ".%u", (unsigned)((ms / 100) % 10));
    uint16_t col = running ? C_GREEN : C_WHITE;
    drawBigStr(b, all || !prev[0] ? nullptr : prev, 18, 70, 3, col, C_BLACK);
    if (all || strcmp(t, prevT)) drawBigStr(t, nullptr, 200, 118, 1, col, C_BLACK);
    strcpy(prev, b);
    strcpy(prevT, t);
  };
  auto hint = [&]() {
    drawClockHint(running ? L3("Нажать - стоп, держать - выход", "Click - stop, hold - exit", "按下：停止，长按：退出")
                          : L3("Нажать - старт, влево - сброс, держать - выход", "Click - start, left - reset, hold - exit",
                               "按下：开始，左：清零，长按：退出"));
  };
  show(true);
  hint();
  uint32_t next = 0;
  for (;;) {
    BtnEvent e = btnEvent();
    if (e == EV_LONG) return;
    if (e == EV_CLICK) {
      if (running) acc += millis() - startMs;
      else startMs = millis();
      running = !running;
      show(true);
      hint();
    }
    if (!running && joyEvent() == J_LEFT && acc) {
      acc = 0;
      show(true);
    }
    if (running && (int32_t)(millis() - next) >= 0) {
      next = millis() + 100;
      show(false);
    }
    delay(5);
  }
}

// ---------- Конспекты: свои тексты, загруженные с телефона ----------
const int MAX_NOTES = 20;

String notePath(int i) {
  char b[20];
  snprintf(b, sizeof(b), "/note%02d.txt", i);
  return String(b);
}

int noteCount() {
  int n = 0;
  for (int i = 0; i < MAX_NOTES; i++)
    if (LittleFS.exists(notePath(i).c_str())) n++;
  return n;
}

// Файл конспекта: первая строка - название, дальше текст.
bool addNote(const String& title, const String& text) {
  for (int i = 0; i < MAX_NOTES; i++) {
    if (LittleFS.exists(notePath(i).c_str())) continue;
    File f = LittleFS.open(notePath(i).c_str(), "w");
    if (!f) return false;
    std::string t = toStd(title);
    for (char& c : t)
      if (c == '\n' || c == '\r') c = ' ';
    std::string d = t + "\n" + toStd(text);
    f.write((const uint8_t*)d.data(), d.size());
    f.close();
    return true;
  }
  return false;
}

bool readNote(int i, String& title, String& text) {
  File f = LittleFS.open(notePath(i).c_str(), "r");
  if (!f) return false;
  std::string d = toStd(f.readString());
  f.close();
  size_t e = d.find('\n');
  title = fromStd(e == std::string::npos ? d : d.substr(0, e));
  text = fromStd(e == std::string::npos ? std::string() : d.substr(e + 1));
  return true;
}

void runNotes() {
  int sel = 0;
  for (;;) {
    std::vector<String> items;
    std::vector<int> ids;
    for (int i = 0; i < MAX_NOTES; i++) {
      String t, x;
      if (!readNote(i, t, x)) continue;
      items.push_back(t);
      ids.push_back(i);
    }
    items.push_back(L3("+ С телефона", "+ From phone", "+ 从手机上传"));
    items.push_back(T().menuBack);
    char title[48];
    snprintf(title, sizeof(title), "%s %u/%d", L3("Конспекты", "Notes", "笔记"), (unsigned)ids.size(), MAX_NOTES);
    sel = listScreen(title, items, std::vector<String>(), sel);
    if (sel < 0 || sel == (int)items.size() - 1) return;
    if (sel == (int)ids.size()) {                    // страница на телефоне: там же, где настройка Wi-Fi
      viewAnswer(L3("Сейчас плата создаст сеть Wi-Fi. Подключитесь к ней с телефона, внизу страницы будет форма «Конспект». "
                    "Когда закончите - нажмите кнопку на устройстве.",
                    "The device will now create a Wi-Fi network. Join it from your phone; the Note form is at the bottom of the page. "
                    "Press the button on the device when done.",
                    "设备将创建Wi-Fi网络。用手机连接，页面底部有“笔记”表单。完成后按设备上的按钮。"), C_WHITE);
      runPortal();
      netIdx = 0;
      wifiStart();
      continue;
    }
    String t, x;
    if (!readNote(ids[(size_t)sel], t, x)) continue;
    if (viewText(t + "\n\n" + x, C_WHITE) == EV_LONG) {
      std::vector<String> yn = splitBar(L3("Удалить|Оставить", "Delete|Keep", "删除|保留"));
      if (choiceScreen(L3("Удалить этот конспект?", "Delete this note?", "删除这条笔记？"), yn) == 0) {
        LittleFS.remove(notePath(ids[(size_t)sel]).c_str());
        if (sel > 0) sel--;
      }
    }
  }
}

// ---------- Справочник (работает без интернета) ----------
void runReference() {
  const reference::Topic* topics = cfgLang == LANG_RU ? reference::TOPICS_RU : reference::TOPICS_EN;
  int sel = 0;
  for (;;) {
    std::vector<String> items;
    for (int i = 0; i < reference::TOPIC_COUNT; i++) items.push_back(topics[i].title);
    items.push_back(T().menuBack);
    sel = listScreen(L3("Справочник", "Reference", "手册"), items, std::vector<String>(), sel);
    if (sel < 0 || sel >= reference::TOPIC_COUNT) return;
    viewAnswer(topics[sel].text, C_WHITE);
  }
}

// ---------- Заставка: часы на тёмном экране ----------
// Показывается на экране ввода после простоя. Любое движение или нажатие возвращает клавиатуру.
// Через 10 минут подсветка гаснет совсем. Если точного времени нет - экран просто гаснет.
void runSaver() {
  struct tm t;
  if (!timeNow(t)) {
    sleepScreen();
    return;
  }
  KeepAwake awake;
  analogWrite(PIN_BLK, BRIGHT_PCT[0] * 255 / 100);   // самая слабая подсветка
  tft.fillScreen(C_BLACK);
  int shownMin = -1, x = 30, y = 80;
  uint32_t start = millis(), next = 0;
  bool dark = false;
  for (;;) {
    if (joyRawDir() != J_NONE || swDown()) break;
    if (!dark && millis() - start > 600000UL) {      // 10 минут - гасим подсветку совсем
      analogWrite(PIN_BLK, 0);
      dark = true;
    }
    if (!dark && (int32_t)(millis() - next) >= 0) {
      next = millis() + 500;
      if (timeNow(t) && t.tm_min != shownMin) {      // раз в минуту часы переезжают на новое место
        tft.fillRect(x, y, 180, 100, C_BLACK);
        shownMin = t.tm_min;
        x = (int)(esp_random() % 61);
        y = 10 + (int)(esp_random() % 121);
        char b[24];
        snprintf(b, sizeof(b), "%02d:%02d", t.tm_hour, t.tm_min);
        drawBigStr(b, nullptr, x, y, 3, 0x05FF, C_BLACK);
        snprintf(b, sizeof(b), "%02d.%02d.%04d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
        drawUtf8(b, x + (180 - textWidth(b)) / 2, y + 74, C_GRAY, C_BLACK);
      }
      wifiTick();
    }
    delay(10);
  }
  while (joyRawDir() != J_NONE || swDown()) delay(5);   // действие, которое разбудило, не срабатывает
  delay(40);
  applyBrightness();
  noteActivity();
}

// ========================= КОМНАТА =========================
// Общий чат всех таких гаджетов через открытый сервер сообщений (MQTT).
// Сообщения не шифруются и нигде не хранятся: их видят те, кто в этот момент в той же комнате.
WiFiClient mqClient;
std::string mqRx;                                    // принятые, ещё не разобранные байты
bool mqUp = false;
uint32_t mqLastPing = 0, mqLastTry = 0, mqLastHere = 0, avaResendAt = 0;
uint32_t mqLastRx = 0;                               // когда сервер последний раз что-то прислал
uint32_t ownEchoes = 0;                              // сколько своих сообщений вернулось от сервера (значит, доставлены)
int hsNext = 0;                                      // очередь отправки рекордов (по одному, чтобы не засыпать сервер)
uint32_t hsLastSend = 0;
String cfgNick, cfgRoom, roomId;
int cfgRoomBg = 1;                                   // принимать сообщения, когда комната закрыта (счётчик на главном экране)
struct RoomMsg {
  String id, nick, text, time;
  bool mine;
};
struct RoomUser {
  String id, nick;
  uint32_t seen;
  bool hasAva;
  uint8_t ava[mqtt::AVA_BYTES];
};
std::vector<RoomMsg> roomMsgs;
std::vector<RoomUser> roomUsers;                     // кого видели в комнате (до 10)
std::vector<std::string> netInbox;                   // сообщения сетевых игр (<комната>/g), ждут разбора
bool hsDirty = false;
extern const int HS_COUNT_FWD;
void hsSendNext();
void hsPublishAll();
void hsReceive(const std::string& rest, const std::string& payload);
uint8_t myAva[mqtt::AVA_BYTES];
bool myAvaSet = false;
bool roomDirty = true, inRoom = false, roomOpen = false;
int roomUnread = 0;                                  // новых сообщений, пока комната закрыта
bool roomUnreadDirty = false;
const size_t ROOM_KEEP = 40;                         // сколько последних сообщений помним
const size_t ROOM_USERS = 10;

// Администратор: состояние (код - в разделе «АДМИНИСТРАТОР» ниже).
bool admOn = false;                                  // этот гаджет вошёл как админ (до перезапуска)
uint8_t admKey[32];                                  // закрытый ключ админа - только в памяти
std::vector<std::string> roomBans;                   // заблокированные метки (от админа)
String roomPin;                                      // закреплённое сообщение комнаты
uint32_t roomPinTs = 0;
bool roomKicked = false;                             // админ выгнал из комнаты
void admReceive(const std::string& sub, const std::string& payload);
void admPresence(const std::string& room, const std::string& payload);
bool admBanned(const String& id);
bool admService();
void runAdmin();

bool mqWrite(const std::string& pkt) { return mqClient.write((const uint8_t*)pkt.data(), pkt.size()) == pkt.size(); }
std::string roomBase() { return mqtt::roomTopic(toStd(cfgRoom)); }

void mqDrop() {
  mqClient.stop();
  mqUp = false;
  mqRx.clear();
  roomDirty = true;
}

// Найти участника по метке (или завести; самый давний вытесняется). Индекс в roomUsers.
int roomUserIndex(const String& id, bool create) {
  for (size_t i = 0; i < roomUsers.size(); i++)
    if (roomUsers[i].id == id) return (int)i;
  if (!create) return -1;
  if (roomUsers.size() >= ROOM_USERS) {
    size_t old = 0;
    for (size_t i = 1; i < roomUsers.size(); i++)
      if ((int32_t)(roomUsers[i].seen - roomUsers[old].seen) < 0) old = i;
    roomUsers.erase(roomUsers.begin() + old);
  }
  RoomUser u;
  u.id = id;
  u.seen = millis();
  u.hasAva = false;
  roomUsers.push_back(u);
  return (int)roomUsers.size() - 1;
}

void sendHere(bool here) {
  std::string pl = mqtt::presence(toStd(roomId), toStd(cfgNick), here);
  if (here) pl += "\t" + std::to_string(FW_VERSION) + "\t" + std::to_string(BATTERY_ENABLED ? batShownPct : -2);   // для списка устройств у админа
  mqWrite(mqtt::publish(roomBase() + "/p", pl));
  mqLastHere = millis();
}

// Аватарка уходит «сохраняемым» сообщением: сервер отдаст её и тем, кто зайдёт позже.
void sendAvatar() {
  std::string data = myAvaSet ? std::string((const char*)myAva, mqtt::AVA_BYTES) : std::string();
  mqWrite(mqtt::publishRetained(roomBase() + "/a/" + toStd(roomId), data));
}

// Подключиться к серверу и подписаться на комнату. waitMs - сколько ждать сервер на каждом шаге.
bool mqConnect(int waitMs) {
  mqDrop();
  mqLastTry = millis();
  if (!mqClient.connect(MQTT_HOST, MQTT_PORT, waitMs)) return false;
  char cid[32];
  snprintf(cid, sizeof(cid), "dsp-%s-%04x", roomId.c_str(), (unsigned)(esp_random() & 0xFFFF));
  if (!mqWrite(mqtt::connect(cid, 120))) return false;
  uint32_t t0 = millis();
  mqtt::Packet p;
  bool ack = false;
  while (millis() - t0 < (uint32_t)waitMs && !ack) {  // ждём подтверждение
    while (mqClient.available() && mqRx.size() < 512) {
      uint8_t b[64];
      int r = mqClient.read(b, sizeof(b));
      if (r <= 0) break;
      mqRx.append((const char*)b, (size_t)r);
    }
    int k = mqtt::take(mqRx, p, 1500);
    if (k < 0) break;
    if (k == 1 && p.type == mqtt::T_CONNACK) ack = p.body.size() >= 2 && p.body[1] == 0;
    else if (k == 0) delay(20);
  }
  if (!ack) {
    mqClient.stop();
    return false;
  }
  if (!mqWrite(mqtt::subscribe(1, roomBase() + "/#"))) return false;   // чат, присутствие и аватарки комнаты
  mqWrite(mqtt::subscribe(2, "dspocket/v1/hs/#"));                     // общая таблица рекордов
  if (strlen(ADMIN_PUBKEY)) mqWrite(mqtt::subscribe(3, "dspocket/v1/adm/#"));   // команды администратора
  if (admOn) mqWrite(mqtt::subscribe(4, "dspocket/v1/+/p"));          // админ видит гаджеты во всех комнатах
  roomPinTs = 0;
  roomPin = "";
  mqUp = true;
  mqLastPing = millis();
  mqLastRx = millis();
  roomDirty = true;
  sendHere(true);
  sendAvatar();
  hsDirty = true;                                    // свои рекорды уйдут по одному, без спешки
  return true;
}

// Принять всё, что пришло; поддерживать связь. Вызывается часто (в том числе с экрана ввода).
void mqPoll() {
  if (!mqUp) return;
  int guard = 0;
  while (mqClient.available() && guard++ < 40) {
    uint8_t b[128];
    int r = mqClient.read(b, sizeof(b));
    if (r <= 0) break;
    mqLastRx = millis();
    mqRx.append((const char*)b, (size_t)r);
    if (mqRx.size() > 6000) {                        // мусор или сбой разбора - проще переподключиться, чем гадать
      mqDrop();
      mqLastTry = millis() - 15000;
      return;
    }
  }
  mqtt::Packet p;
  std::string base = roomBase();
  for (;;) {
    int k = mqtt::take(mqRx, p, 1500);
    if (k == 0) break;
    if (k < 0) {
      mqRx.clear();
      break;
    }
    if (p.type != mqtt::T_PUBLISH) continue;
    if (p.topic.compare(0, 16, "dspocket/v1/adm/") == 0) {   // команда администратора (проверяется подпись)
      admReceive(p.topic.substr(16), p.payload);
      continue;
    }
    if (admOn && p.topic.size() > 14 && p.topic.compare(0, 12, "dspocket/v1/") == 0 &&
        p.topic.compare(p.topic.size() - 2, 2, "/p") == 0) {    // админ: гаджет в любой комнате
      std::string room = p.topic.substr(12, p.topic.size() - 14);
      admPresence(room, p.payload);
      if (p.topic != base + "/p") continue;
    }
    if (p.topic.compare(0, 15, "dspocket/v1/hs/") == 0) {   // чей-то рекорд
      hsReceive(p.topic.substr(15), p.payload);
      continue;
    }
    if (p.topic == base + "/g") {                    // ход или приглашение в сетевой игре
      if (netInbox.size() < 30 && p.payload.size() < 120) netInbox.push_back(p.payload);
      continue;
    }
    std::string who;
    int kind = mqtt::classify(base, p.topic, who);
    if (kind == 0) {                                 // сообщение чата
      std::string id, nick, text;
      if (!mqtt::parseRoomMessage(p.payload, id, nick, text)) continue;   // чужой формат - пропускаем
      if (admBanned(fromStd(id))) continue;                                // заблокирован админом
      RoomMsg m;
      m.id = fromStd(id);
      m.nick = fromStd(nick);
      m.text = fromStd(text);
      m.mine = id == toStd(roomId);
      if (m.mine) ownEchoes++;
      struct tm t;
      if (timeNow(t)) {
        char b[8];
        snprintf(b, sizeof(b), "%02d:%02d", t.tm_hour, t.tm_min);
        m.time = b;
      }
      roomMsgs.push_back(m);
      if (roomMsgs.size() > ROOM_KEEP) roomMsgs.erase(roomMsgs.begin());
      if (!m.mine) {
        bool bot = id.size() > 3 && id.compare(id.size() - 3, 3, "-ai") == 0;   // ответ DeepSeek - не участник
        if (!bot) {
          int ui = roomUserIndex(m.id, true);
          roomUsers[(size_t)ui].nick = m.nick;
          roomUsers[(size_t)ui].seen = millis();
        }
        if (!roomOpen) {                             // комната закрыта - считаем непрочитанные
          roomUnread++;
          roomUnreadDirty = true;
        }
        sndMessage();
      }
      roomDirty = true;
      if (roomOpen) noteActivity();
    } else if (kind == 1) {                          // «я здесь» / «я ушёл»
      std::string id, nick;
      bool here;
      if (!mqtt::parsePresence(p.payload, id, nick, here) || id == toStd(roomId)) continue;
      int ui = roomUserIndex(fromStd(id), here);
      if (ui < 0) continue;
      if (!here) {
        roomUsers.erase(roomUsers.begin() + ui);
        continue;
      }
      bool isNew = roomUsers[(size_t)ui].nick.length() == 0;
      roomUsers[(size_t)ui].nick = fromStd(nick);
      roomUsers[(size_t)ui].seen = millis();
      if (isNew && myAvaSet) avaResendAt = millis() + 1500 + (esp_random() % 2000);   // новичку - свою аватарку (если сервер её не сохранил)
    } else if (kind == 2) {                          // аватарка участника
      if (who == toStd(roomId)) continue;
      int ui = roomUserIndex(fromStd(who), p.payload.size() == mqtt::AVA_BYTES);
      if (ui < 0) continue;
      if (p.payload.size() == mqtt::AVA_BYTES) {
        memcpy(roomUsers[(size_t)ui].ava, p.payload.data(), mqtt::AVA_BYTES);
        roomUsers[(size_t)ui].hasAva = true;
      } else {
        roomUsers[(size_t)ui].hasAva = false;
      }
      roomDirty = true;
    }
  }
  if (!mqClient.connected()) {
    mqDrop();
    return;
  }
  // Связь может «зависнуть» молча (сменилась сеть, роутер забыл соединение): плата думает, что подключена,
  // а сообщения никуда не доходят. Раз в 40 секунд мы спрашиваем сервер; если он молчит 100 секунд - переподключаемся.
  if (millis() - mqLastRx > 100000UL) {
    Serial.println("[room] server is silent - reconnecting");
    mqDrop();
    mqLastTry = millis() - 15000;                    // новая попытка уже через 5 секунд
    return;
  }
  if (millis() - mqLastPing > 40000) {
    mqLastPing = millis();
    if (!mqWrite(mqtt::ping())) {
      mqDrop();
      return;
    }
  }
  if (millis() - mqLastHere > 60000) sendHere(true);
  if (avaResendAt && (int32_t)(millis() - avaResendAt) >= 0) {
    avaResendAt = 0;
    sendAvatar();
  }
  if (hsDirty) {                                     // начать отправку рекордов с начала списка
    hsDirty = false;
    hsNext = 0;
  }
  if (hsNext < HS_COUNT_FWD && millis() - hsLastSend > 500) hsSendNext();
}

// Фоновая работа комнаты: приём сообщений и переподключение.
// В комнате - раз в 20 секунд; в фоне - раз в минуту и с коротким ожиданием, чтобы не мешать набору текста.
void roomTick() {
  bool bg = !inRoom;
  if (bg && !(cfgRoomBg && cfgNick.length())) return;
  if (!mqUp) {
    if (wifiOk() && millis() - mqLastTry > (bg ? 60000UL : 20000UL)) mqConnect(bg ? 3000 : 6000);
    return;
  }
  mqPoll();
}

void saveRoomSettings() {
  prefs.begin("dsk", false);
  prefs.putString("nick", cfgNick);
  prefs.putString("room", cfgRoom);
  prefs.putUChar("roombg", (uint8_t)cfgRoomBg);
  prefs.end();
}

void loadAvatar() {
  myAvaSet = false;
  File f = LittleFS.open("/avatar.bin", "r");
  if (!f) return;
  if ((size_t)f.size() == mqtt::AVA_BYTES && f.read(myAva, mqtt::AVA_BYTES) == mqtt::AVA_BYTES) myAvaSet = true;
  f.close();
}

// Спросить имя. false - отменено.
bool askNick() {
  showMsg(L3("Как вас называть в комнате? Наберите имя и нажмите OK.", "What is your name in the room? Type it and press OK.",
             "在房间里怎么称呼你？输入名字后按OK。"), C_YELLOW);
  delay(1500);
  if (cfgNick.length()) pendingInput = cfgNick;
  String n = inputText();
  n.trim();
  if (!n.length()) return false;
  cfgNick = fromStd(mqtt::clip(toStd(n), 24));
  saveRoomSettings();
  return true;
}

// Аватарка 24x24: своя картинка или цветной кружок с первой буквой имени.
void drawAvatar(const uint8_t* ava, const String& id, const String& nick, int x, int y) {
  if (ava) {
    for (size_t i = 0; i < mqtt::AVA_BYTES; i++) cbuf[i] = mqtt::from332(ava[i]);
    blit(x, y, 24, 24);
    return;
  }
  uint16_t col = mqtt::idColor(toStd(id));
  tft.fillRect(x, y, 24, 24, C_BLACK);
  tft.fillCircle(x + 12, y + 12, 11, col);
  std::vector<uint16_t> cps;
  utf8ToCps(nick.c_str(), cps);
  if (!cps.empty()) {
    SmallFont small;
    drawCp(cps[0], x + 8, y + 4, C_WHITE, col);
  }
}

struct RoomLine {
  std::vector<uint16_t> cps;
  uint16_t color;
  int msg;                                           // для строки с именем - номер сообщения, иначе -1
};

void drawRoom(int back) {
  std::vector<RoomLine> vl;
  for (size_t mi = 0; mi < roomMsgs.size(); mi++) {
    const RoomMsg& m = roomMsgs[mi];
    std::vector<uint16_t> lab;
    utf8ToCps(m.nick.c_str(), lab);
    if (lab.size() > 12) lab.resize(12);
    vl.push_back({lab, m.mine ? C_YELLOW : (uint16_t)0x07FF, (int)mi});
    std::vector<uint16_t> cps;
    utf8ToCps(m.text.c_str(), cps);
    std::vector<Line> lines;
    wrapCps(cps, COLS, lines);
    for (const Line& l : lines)
      vl.push_back({std::vector<uint16_t>(cps.begin() + l.start, cps.begin() + l.start + l.len), m.mine ? (uint16_t)0xFFF0 : C_WHITE, -1});
  }
  const int baseRows = ROWS - 1;                     // нижняя строка - состояние
  int pinRows = roomPin.length() ? 1 : 0;            // верхняя строка - закреплённое админом сообщение
  const int rows = baseRows - pinRows;
  if (pinRows) {
    std::vector<uint16_t> pc;
    utf8ToCps((String("\xE2\x98\x85 ") + roomPin).c_str(), pc);
    drawLine(pc.data(), (int)pc.size(), 0, C_BLACK, C_YELLOW);
  }
  if (vl.empty()) {
    std::vector<uint16_t> cps;
    utf8ToCps(L3("Пока тихо. Нажмите кнопку, чтобы написать. Сообщение увидят все, кто сейчас в этой комнате.",
                 "Quiet so far. Press the button to write. Everyone in this room right now will see it.",
                 "现在很安静。按按钮写消息，此刻在这个房间里的人都能看到。"), cps);
    std::vector<Line> lines;
    wrapCps(cps, COLS, lines);
    for (const Line& l : lines) vl.push_back({std::vector<uint16_t>(cps.begin() + l.start, cps.begin() + l.start + l.len), C_LIGHT, -1});
  }
  int maxTop = (int)vl.size() > rows ? (int)vl.size() - rows : 0;
  int top = maxTop - back;
  if (top < 0) top = 0;
  for (int r = 0; r < rows; r++) {
    size_t i = (size_t)(top + r);
    int y = (r + pinRows) * GH;
    if (i >= vl.size()) {
      tft.fillRect(0, y, 240, GH, C_BLACK);
    } else if (vl[i].msg < 0) {
      drawLine(vl[i].cps.data(), (int)vl[i].cps.size(), y, vl[i].color, C_BLACK);
    } else {                                         // строка с аватаркой, именем и временем
      const RoomMsg& m = roomMsgs[(size_t)vl[i].msg];
      tft.fillRect(24, y, 216, GH, C_BLACK);
      const uint8_t* ava = nullptr;
      if (m.mine) {
        if (myAvaSet) ava = myAva;
      } else {
        int ui = roomUserIndex(m.id, false);
        if (ui >= 0 && roomUsers[(size_t)ui].hasAva) ava = roomUsers[(size_t)ui].ava;
      }
      drawAvatar(ava, m.id, m.nick, 0, y);
      int x = 28;
      for (uint16_t ch : vl[i].cps) x += drawCp(ch, x, y, vl[i].color, C_BLACK);
      if (m.time.length()) {
        SmallFont small;
        drawUtf8(m.time.c_str(), 240 - 5 * 8 - 2, y + 5, C_GRAY, C_BLACK);
      }
    }
  }
  SmallFont small;
  tft.fillRect(0, baseRows * 24, 240, 240 - baseRows * 24, C_BLACK);
  int online = 1;
  for (const RoomUser& u : roomUsers)
    if (millis() - u.seen < 150000UL) online++;
  char st[64];
  snprintf(st, sizeof(st), "#%s", cfgRoom.c_str());
  drawUtf8(st, 2, baseRows * 24 + 5, C_YELLOW, C_BLACK);
  if (mqUp) snprintf(st, sizeof(st), "%s: %d", L3("в сети", "online", "在线"), online);
  else snprintf(st, sizeof(st), "%s", L3("нет связи", "offline", "离线"));
  drawUtf8(st, 240 - textWidth(st) - 2, baseRows * 24 + 5, mqUp ? C_GREEN : C_RED, C_BLACK);
}

// Отправить текст в комнату. false - нет связи.
bool roomSend(const String& text) {
  return mqUp && mqWrite(mqtt::publish(roomBase(), mqtt::roomMessage(toStd(roomId), toStd(cfgNick), mqtt::clip(toStd(text), 300))));
}

// «@ai вопрос» - DeepSeek отвечает в комнату (запрос идёт с этого гаджета и его ключа).
void roomAskAi(const String& text) {
  std::string q = toStd(text);
  size_t sp = q.find(' ');
  if (sp == std::string::npos || sp + 1 >= q.size()) return;
  showMsg(L3("DeepSeek отвечает в комнату...", "DeepSeek is answering in the room...", "DeepSeek正在房间里回答……"), C_YELLOW);
  std::vector<ChatMsg> none;
  String reply;
  std::string sys = std::string("You are a helpful participant of a small group chat. Answer in ") + T().promptLang +
                    ", briefly: at most 200 characters, plain text, no markdown.";
  if (!askWith(sys.c_str(), none, fromStd(q.substr(sp + 1)), 150, reply)) {
    showMsg(String(T().errPrefix) + reply, C_RED);
    delay(1800);
    return;
  }
  std::string a = tools::prettify(toStd(reply));
  for (char& c : a)
    if (c == '\n' || c == '\t') c = ' ';
  mqWrite(mqtt::publish(roomBase(), mqtt::roomMessage(toStd(roomId) + "-ai", "DeepSeek", mqtt::clip(a, 300))));
}

bool pickAvatar();                                   // выбрать картинку из галереи и сделать из неё аватарку

void runRoom() {
  if (inRoom) {                                      // меню открыли с экрана ввода сообщения
    showMsg(L3("Вы уже в комнате. Допишите сообщение или сотрите его и нажмите OK.", "You are already in the room.", "你已经在房间里了。"), C_YELLOW);
    delay(1800);
    return;
  }
  if (!waitWifi(false)) return;
  if (!cfgNick.length() && !askNick()) return;
  inRoom = true;
  roomOpen = true;
  roomKicked = false;
  roomUnread = 0;
  roomUnreadDirty = true;
  if (!mqUp) {
    showMsg(L3("Подключаюсь к комнате...", "Joining the room...", "正在进入房间……"), C_YELLOW);
    mqConnect(6000);
  }
  int back = 0;                                      // на сколько строк прокручено вверх
  roomDirty = true;
  String sentText;                                   // последнее отправленное: ждём, что сервер вернёт его нам же
  uint32_t sentWant = 0, sentDeadline = 0;
  for (;;) {
    wifiTick();
    roomTick();
    if (admService()) roomDirty = true;              // объявление или команда администратора
    if (roomKicked) {
      roomKicked = false;
      viewAnswer(L3("Администратор удалил вас из комнаты.", "The administrator removed you from the room.", "管理员已将你移出房间。"), C_YELLOW);
      break;
    }
    if (sentDeadline) {
      if (ownEchoes >= sentWant) {
        sentDeadline = 0;                            // доставлено
      } else if ((int32_t)(millis() - sentDeadline) >= 0) {   // сервер не вернул сообщение - связь зависла
        sentDeadline = 0;
        showMsg(L3("Сообщение не доставлено: связь с комнатой зависла. Переподключаюсь, текст вернётся в поле ввода.",
                   "The message was not delivered: the connection hung. Reconnecting; the text returns to the input field.",
                   "消息未送达：连接已卡住。正在重新连接，文字会回到输入框。"), C_RED);
        pendingInput = sentText;
        mqConnect(6000);
        roomDirty = true;
      }
    }
    if (roomDirty) {
      roomDirty = false;
      drawRoom(back);
    }
    uint8_t j = joyEvent();
    int d = j == J_UP ? 1 : j == J_DOWN ? -1 : j == J_LEFT ? ROWS - 2 : j == J_RIGHT ? -(ROWS - 2) : 0;
    if (d) {
      back += d;
      if (back < 0) back = 0;
      if (back > 200) back = 200;
      roomDirty = true;
    }
    BtnEvent e = btnEvent();
    String out;                                      // что отправить
    if (e == EV_CLICK) {                             // написать сообщение (приём продолжается и во время набора)
      out = inputText();
      out.trim();
      back = 0;
      roomDirty = true;
    }
    if (e == EV_LONG) {
      std::vector<String> o = splitBar(L3("Вернуться|Быстрый ответ|Кто в комнате|Аватарка|Другая комната|Общая комната|Сменить имя|Сообщения в фоне|Выйти из комнаты",
                                          "Return|Quick reply|Who is here|Avatar|Another room|Common room|Change name|Background messages|Leave the room",
                                          "返回|快捷回复|谁在房间|头像|换房间|公共房间|改名字|后台接收消息|离开房间"));
      std::vector<String> right(o.size());
      right[7] = cfgRoomBg ? L3("да", "on", "开") : L3("нет", "off", "关");
      int k = listScreen((String("#") + cfgRoom + " - " + cfgNick).c_str(), o, right, 0);
      if (k == 8) break;
      if (k == 1) {
        std::vector<String> q = splitBar(L3("ок|да|нет|привет|спасибо|сейчас|+1|не знаю|Назад", "ok|yes|no|hi|thanks|one moment|+1|no idea|Back",
                                            "好|是|不|你好|谢谢|马上|+1|不知道|返回"));
        int qi = listScreen(L3("Быстрый ответ", "Quick reply", "快捷回复"), q, std::vector<String>(), 0);
        if (qi >= 0 && qi < (int)q.size() - 1) out = q[(size_t)qi];
        back = 0;
      }
      if (k == 2) {
        String list = String(cfgNick) + L3(" (вы)", " (you)", "（你）");
        for (const RoomUser& u : roomUsers)
          if (millis() - u.seen < 150000UL && u.nick.length()) list += String("\n") + u.nick;
        viewAnswer(String(L3("Сейчас в комнате:\n\n", "In the room now:\n\n", "现在在房间里：\n\n")) + list, C_WHITE);
      }
      if (k == 3) {
        if (pickAvatar() && mqUp) sendAvatar();
      }
      if (k == 4 || k == 5) {
        String r = ROOM_DEFAULT;
        if (k == 4) {
          showMsg(L3("Название комнаты. У всех участников оно должно быть одинаковым.", "Room name. It must be the same for everyone.",
                     "房间名称。所有人必须输入相同的名称。"), C_YELLOW);
          delay(1500);
          r = inputText();
          r = fromStd(mqtt::clip(mqtt::cleanRoom(toStd(r)), 40));
        }
        if (r.length() && r != cfgRoom) {
          if (mqUp) sendHere(false);
          cfgRoom = r;
          saveRoomSettings();
          roomMsgs.clear();
          roomUsers.clear();
          showMsg(L3("Подключаюсь к комнате...", "Joining the room...", "正在进入房间……"), C_YELLOW);
          mqConnect(6000);
        }
      }
      if (k == 6) {
        if (askNick() && mqUp) sendHere(true);
      }
      if (k == 7) {
        cfgRoomBg = !cfgRoomBg;
        saveRoomSettings();
      }
      roomDirty = true;
    }
    if (out.length() && admBanned(roomId)) {
      showMsg(L3("Администратор запретил вам писать в комнату.", "The administrator has blocked you from writing in the room.", "管理员已禁止你在房间发言。"), C_RED);
      delay(2000);
      out = "";
      roomDirty = true;
    }
    if (out.length()) {
      if (!roomSend(out)) {
        pendingInput = out;                          // не отправилось - текст не пропадёт
        showMsg(L3("Нет связи с комнатой. Сообщение не отправлено, оно останется в поле ввода.",
                   "No connection to the room. The message was not sent; it stays in the input field.",
                   "与房间的连接已断开。消息未发送，仍保留在输入框中。"), C_RED);
        delay(2000);
        mqDrop();
        mqLastTry = 0;
      } else {
        sentText = out;
        sentWant = ownEchoes + 1;
        sentDeadline = millis() + 8000;
        std::string lo = toStd(out);
        if (lo.compare(0, 4, "@ai ") == 0 || lo.compare(0, 4, "@AI ") == 0 || lo.compare(0, 6, "@\xD0\xB8\xD0\xB8 ") == 0) roomAskAi(out);
      }
      roomDirty = true;
    }
    delay(5);
  }
  inRoom = false;
  roomOpen = false;
  roomUnread = 0;
  roomUnreadDirty = true;
  if (!(cfgRoomBg && cfgNick.length())) {            // фон выключен - уходим из комнаты совсем
    if (mqUp) sendHere(false);
    mqDrop();
  }
}

// ---------- Меню «Инструменты» ----------
// ---------- Пульт: ИК-светодиод на PIN_IR ----------
static std::vector<uint32_t> irLastSent;                 // что отправили в последний раз (для проверки)
static uint32_t irLastFreq = 0;

// Один кадр: чётные отрезки - вспышка (мигаем с несущей частотой), нечётные - пауза.
void irSendOnce(const std::vector<uint32_t>& t, uint32_t freq) {
  uint32_t period = 1000000 / freq, on = period / 3;
  int off = (int)period - (int)on - IR_TRIM_US;
  if (off < 1) off = 1;
  for (size_t i = 0; i < t.size(); i++) {
    if (i & 1) {
      digitalWrite(PIN_IR, LOW);
      if (t[i] > 12000) {
        delay(t[i] / 1000);
        delayMicroseconds(t[i] % 1000);
      } else {
        delayMicroseconds(t[i]);
      }
    } else {
      uint32_t t0 = micros();
      while (micros() - t0 < t[i]) {
        digitalWrite(PIN_IR, HIGH);
        delayMicroseconds(on);
        digitalWrite(PIN_IR, LOW);
        delayMicroseconds((uint32_t)off);
      }
    }
  }
  digitalWrite(PIN_IR, LOW);
}

// Отправить команду. false - такой вид сигнала не поддерживается.
bool irSend(const ir::Signal& s) {
  ir::Frame f;
  if (!ir::encode(s, f)) return false;
  irLastSent = f.t;
  irLastFreq = f.freq;
  pinMode(PIN_IR, OUTPUT);
  for (int r = 0; r < f.repeats; r++) {
    uint32_t t0 = millis();
    irSendOnce(f.t, f.freq);
    if (r + 1 < f.repeats) {
      uint32_t need = f.period / 1000, spent = millis() - t0;
      delay(spent < need ? need - spent : 5);
    }
  }
  pinMode(PIN_IR, INPUT);                               // в покое ножка свободна: не мешает запуску платы и не светит диодом на плате
  return true;
}

// Читает файл по строкам. Строка может быть длинной (сырые данные кнопки).
// Возвращает false в конце файла. start - место начала строки в файле.
bool irReadLine(File& f, std::string& line, uint32_t& start) {
  line.clear();
  start = (uint32_t)f.position();
  bool any = false;
  uint8_t c;
  while (f.read(&c, 1) == 1) {
    any = true;
    if (c == '\n') return true;
    if (c != '\r' && line.size() < 20000) line += (char)c;
  }
  return any;
}

// Кнопки пульта: названия и места в файле.
void irLoadButtons(int slot, std::vector<String>& names, std::vector<uint32_t>& offs) {
  names.clear();
  offs.clear();
  File f = LittleFS.open(irPath(slot).c_str(), "r");
  if (!f) return;
  std::string line, k, v;
  uint32_t start = 0;
  while (names.size() < 150 && irReadLine(f, line, start)) {
    if (line.size() > 200 || line.compare(0, 5, "name:") != 0) continue;
    if (!ir::splitLine(line, k, v)) continue;
    for (char& ch : v)
      if (ch == '_') ch = ' ';
    names.push_back(fromStd(v));
    offs.push_back(start);
  }
  f.close();
}

bool irLoadSignal(int slot, uint32_t off, ir::Signal& s) {
  File f = LittleFS.open(irPath(slot).c_str(), "r");
  if (!f) return false;
  f.seek(off);
  std::string line;
  uint32_t start = 0;
  bool first = true;
  while (irReadLine(f, line, start)) {
    if (!first && (line.compare(0, 1, "#") == 0 || line.compare(0, 5, "name:") == 0)) break;
    ir::feedLine(s, line);
    first = false;
  }
  f.close();
  return s.hasType;
}

void runRemoteButtons(int slot) {
  String title = irTitle(slot);
  std::vector<String> names;
  std::vector<uint32_t> offs;
  irLoadButtons(slot, names, offs);
  int n = (int)names.size(), sel = 0;
  for (;;) {
    std::vector<String> items = names;
    items.push_back(L3("Удалить пульт", "Delete the remote", "删除遥控器"));
    items.push_back(T().menuBack);
    sel = listScreen(title.c_str(), items, std::vector<String>(), sel);
    if (sel >= 0 && sel < n) {
      ir::Signal s;
      bool ok = irLoadSignal(slot, offs[(size_t)sel], s) && irSend(s);
      if (ok) {
        beep(1, 2400, 15);
      } else {
        showMsg(String(L3("Этот вид сигнала пока не поддерживается: ", "This kind of signal is not supported yet: ", "暂不支持这种信号：")) + fromStd(s.protocol), C_RED);
        delay(1800);
      }
      continue;
    }
    if (sel == n) {
      std::vector<String> yn = splitBar(L3("Оставить|Удалить", "Keep|Delete", "保留|删除"));
      if (choiceScreen(String(L3("Удалить пульт «", "Delete the remote \"", "删除遥控器“")) + title + L3("»?", "\"?", "”？"), yn) == 1) {
        LittleFS.remove(irPath(slot).c_str());
        return;
      }
      continue;
    }
    return;
  }
}

// Приёмник списка папки с GitHub.
class IrDirSink : public Stream {
 public:
  ir::DirScan scan;
  size_t write(uint8_t c) override {
    scan.feed((char)c);
    return 1;
  }
  size_t write(const uint8_t* b, size_t len) override {
    for (size_t i = 0; i < len; i++) scan.feed((char)b[i]);
    return len;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
};

// GET с выдачей ответа в приёмник. Возвращает код HTTP; при обрыве посреди ответа - -1000.
int httpGetSink(const String& url, Stream* sink) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(15);
  HTTPClient http;
  http.setUserAgent(APP_UA);
  http.setConnectTimeout(15000);
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) return -100;
  int code = http.GET();
  if (code == 200 && http.writeToStream(sink) < 0) code = -1000;
  http.end();
  Serial.printf("[ir] GET %d\n", code);
  return code;
}

String irNetErr(int code) {
  if (code == 403 || code == 429)
    return L3("GitHub временно ограничил запросы с вашего адреса (не больше 60 в час). Попробуйте позже или загрузите файл с телефона.",
              "GitHub has temporarily limited requests from your address (60 per hour). Try later or upload the file from your phone.",
              "GitHub暂时限制了请求（每小时60次）。请稍后再试，或从手机上传文件。");
  if (code == 404) return L3("Не найдено (404).", "Not found (404).", "未找到（404）。");
  return String(L3("Не удалось связаться с GitHub (код ", "Cannot reach GitHub (code ", "无法连接GitHub（代码 ")) + String(code) +
         L3("). Если не выходит - загрузите файл с телефона.", "). If it keeps failing, upload the file from your phone.", "）。如果一直失败，请从手机上传文件。");
}

// Скачать файл пульта из базы. Пустая строка - успех.
String irDownload(const std::string& path) {
  int slot = irFreeSlot();
  if (slot < 0) return L3("Уже 12 пультов. Удалите лишний.", "12 remotes already. Delete one.", "已有12个遥控器，请先删除。");
  if (!irRoom()) return L3("В памяти платы нет места.", "No free memory on the device.", "设备存储空间不足。");
  IrFileSink sink;
  sink.f = LittleFS.open(irPath(slot).c_str(), "w");
  if (!sink.f) return L3("Не удалось записать файл.", "Cannot write the file.", "无法写入文件。");
  std::string head = "# title: " + ir::titleFromPath(path) + "\n";
  sink.f.write((const uint8_t*)head.data(), head.size());
  String url = String("https://raw.githubusercontent.com/") + IR_DB_REPO + "/" + IR_DB_BRANCH + "/" + fromStd(ir::urlPath(path));
  int code = httpGetSink(url, &sink);
  sink.f.close();
  String err;
  if (code != 200) err = irNetErr(code);
  else if (sink.badType || sink.n < 30) err = L3("Это не файл пульта.", "This is not a remote file.", "这不是遥控器文件。");
  else if (sink.tooBig) err = L3("Файл слишком большой.", "The file is too big.", "文件太大。");
  else if (sink.failed) err = L3("Не удалось записать файл.", "Cannot write the file.", "无法写入文件。");
  if (err.length()) LittleFS.remove(irPath(slot).c_str());
  return err;
}

// Выбор пульта в базе: тип техники -> марка -> файл. true - пульт скачан.
bool runRemoteDownload() {
  if (!waitWifi(false)) return false;
  std::vector<std::string> pathParts;
  std::vector<std::vector<String>> lists;                // уже полученные списки по уровням (чтобы не запрашивать заново)
  std::vector<int> sels;
  for (;;) {
    std::string path;
    for (size_t i = 0; i < pathParts.size(); i++) path += (i ? "/" : "") + pathParts[i];
    if (lists.size() <= pathParts.size()) {
      showMsg(L3("Загружаю список...", "Loading the list...", "正在加载列表……"), C_YELLOW);
      IrDirSink sink;
      int code = httpGetSink(String("https://api.github.com/repos/") + IR_DB_REPO + "/contents/" + fromStd(ir::urlPath(path)) + "?ref=" + IR_DB_BRANCH, &sink);
      sink.scan.finish();
      if (code != 200) {
        viewAnswer(irNetErr(code), C_RED);
        if (pathParts.empty()) return false;
        pathParts.pop_back();
        continue;
      }
      std::vector<String> items;
      for (const std::string& it : sink.scan.items) items.push_back(fromStd(it));
      lists.push_back(items);
      sels.push_back(0);
    }
    std::vector<String> items = lists.back();
    int n = (int)items.size();
    items.push_back(T().menuBack);
    String title = pathParts.empty() ? String(L3("База пультов", "Remote database", "遥控器数据库")) : fromStd(pathParts.back());
    int k = listScreen(title.c_str(), items, std::vector<String>(), sels.back());
    if (k < 0 || k >= n) {                               // назад на уровень выше
      if (pathParts.empty()) return false;
      pathParts.pop_back();
      lists.pop_back();
      sels.pop_back();
      continue;
    }
    sels.back() = k;
    std::string name = toStd(items[(size_t)k]);
    if (!name.empty() && name.back() == '/') {
      name.pop_back();
      pathParts.push_back(name);
      continue;
    }
    showMsg(L3("Скачиваю пульт...", "Downloading the remote...", "正在下载遥控器……"), C_YELLOW);
    String err = irDownload(path.empty() ? name : path + "/" + name);
    if (err.length()) {
      viewAnswer(err, C_RED);
      continue;
    }
    sndOk();
    showMsg(String(L3("Пульт сохранён: ", "Remote saved: ", "遥控器已保存：")) + fromStd(ir::titleFromPath(name)), C_GREEN);
    delay(1200);
    return true;
  }
}

#if BOARD_S3
// ---------- Запись кнопок с родного пульта (S3) ----------
// ИК-приёмник на 38 кГц (VS1838B, TSOP1838...): OUT -> резистор 1 кОм -> GPIO18 (площадка фонарика), VCC -> 3V3, GND -> GND.
// Приёмник в покое держит 1, во время вспышки - 0. Ловим моменты переключений и сохраняем как сырой сигнал (type: raw).
const int PIN_IRRX = PIN_LED;
const int IRRX_MAX = 400;
static volatile uint32_t irRxT[IRRX_MAX];
static volatile int irRxN = 0;
void IRAM_ATTR irRxIsr() {
  int n = irRxN;
  if (n < IRRX_MAX) {
    irRxT[n] = micros();
    irRxN = n + 1;
  }
}

// 1 - сигнал пойман (длительности: вспышка, пауза, вспышка...), 0 - отмена кнопкой гаджета, -1 - слишком короткий.
int irCapture(std::vector<uint32_t>& dur) {
  dur.clear();
  pinMode(PIN_IRRX, INPUT_PULLUP);
  delay(20);
  irRxN = 0;
  attachInterrupt(digitalPinToInterrupt(PIN_IRRX), irRxIsr, CHANGE);
  int res = 0;
  for (;;) {
    BtnEvent e = btnEvent();
    if (e == EV_CLICK || e == EV_LONG) break;
    int n = irRxN;
    if (n > 0 && micros() - irRxT[n - 1] > 150000) {   // 150 мс тишины - пульт закончил
      res = 1;
      break;
    }
    delay(3);
  }
  detachInterrupt(digitalPinToInterrupt(PIN_IRRX));
  pinMode(PIN_IRRX, INPUT);
  if (res != 1) return res;
  int n = irRxN;
  if (n > 0 && digitalRead(PIN_IRRX) == LOW) n--;    // не должно быть, но последняя вспышка без конца не нужна
  for (int i = 1; i < n; i++) {
    uint32_t d = irRxT[i] - irRxT[i - 1];
    if ((i & 1) == 0 && d > 30000 && dur.size() >= 8) break;   // пауза >30 мс после кадра - дальше повторы, их не берём
    dur.push_back(d);
  }
  if (!dur.empty() && (dur.size() & 1) == 0) dur.pop_back();  // кадр кончается вспышкой
  return dur.size() >= 8 ? 1 : -1;
}

String irLearnTitle() { return L3("Мой пульт", "My remote", "我的遥控器"); }

// Дописать кнопку в «Мой пульт» (создаётся при первой записи).
bool irSaveLearned(String name, const std::vector<uint32_t>& d) {
  int slot = -1;
  for (int i = 0; i < IR_SLOTS; i++)
    if (LittleFS.exists(irPath(i).c_str()) && irTitle(i) == irLearnTitle()) slot = i;
  bool fresh = slot < 0;
  if (fresh) slot = irFreeSlot();
  if (slot < 0 || !irRoom()) return false;
  File f = LittleFS.open(irPath(slot).c_str(), fresh ? "w" : "a");
  if (!f) return false;
  if (fresh) f.print(String("# title: ") + irLearnTitle() + "\nFiletype: IR signals file\nVersion: 1\n");
  name.trim();
  name.replace(' ', '_');
  if (!name.length()) name = "Button";
  f.print(String("#\nname: ") + name + "\ntype: raw\nfrequency: 38000\nduty_cycle: 0.330000\ndata:");
  for (uint32_t t : d) f.print(String(" ") + String((unsigned long)t));
  f.print("\n");
  f.close();
  return true;
}

void runRemoteLearn() {
  if (ENABLE_TORCH && torchOn) {                     // фонарик и приёмник на одном выводе
    torchOn = false;
    digitalWrite(PIN_LED, LOW);
  }
  int saved = 0;
  for (;;) {
    showMsg(String(L3("Запись с пульта\n\nНаправьте пульт на ИК-приёмник (вывод 18) и нажмите кнопку на пульте.\n\n"
                      "Выход - кнопка гаджета.",
                      "Record from a remote\n\nPoint the remote at the IR receiver (pin 18) and press a button on it.\n\nExit - the device button.",
                      "从遥控器录制\n\n把遥控器对准红外接收器（18脚）并按键。\n\n按本机按键退出。")) +
                (saved ? String(L3("\n\nЗаписано кнопок: ", "\n\nButtons saved: ", "\n\n已保存: ")) + String(saved) : String("")),
            C_WHITE);
    std::vector<uint32_t> d;
    int r = irCapture(d);
    if (r == 0) return;
    if (r < 0) {
      showMsg(L3("Сигнал слишком короткий - нажмите кнопку пульта ещё раз, поближе к приёмнику.",
                 "The signal is too short - press the remote button again, closer to the receiver.", "信号太短，请再试一次。"), C_YELLOW);
      delay(1800);
      continue;
    }
    beep(1, 2400, 15);
    for (;;) {
      std::vector<String> names = splitBar(L3("Power|Громкость +|Громкость -|Без звука|Вверх|Вниз|Влево|Вправо|OK|Назад|Домой|Меню|Канал +|Канал -|Источник|Своё название...|Проверить: отправить обратно|Не сохранять",
                                              "Power|Volume +|Volume -|Mute|Up|Down|Left|Right|OK|Back|Home|Menu|Channel +|Channel -|Input|Custom name...|Test: send it back|Don't save",
                                              "电源|音量+|音量-|静音|上|下|左|右|OK|返回|主页|菜单|频道+|频道-|信号源|自定义名称...|测试：发送|不保存"));
      String title = String(L3("Пойман сигнал (", "Signal caught (", "已接收信号（")) + String((int)d.size()) + L3(" импульсов). Что это за кнопка?", " pulses). Which button is it?", "）。这是哪个键？");
      int k = listScreen(title.c_str(), names, std::vector<String>(), 0);
      int nn = (int)names.size();
      if (k == nn - 2) {                             // проверка: отправить записанное на телевизор
        ir::Signal s;
        s.hasType = true;
        s.raw = true;
        s.freq = 38000;
        s.data = d;
        irSend(s);
        continue;
      }
      if (k < 0 || k == nn - 1) break;
      String name = names[(size_t)k];
      if (k == nn - 3) {
        name = inputText();
        if (!name.length()) continue;
      }
      if (irSaveLearned(name, d)) {
        saved++;
        showMsg(String(L3("Сохранено: «", "Saved: \"", "已保存：“")) + name + L3("» в «", "\" to \"", "”到“") + irLearnTitle() + L3("».", "\".", "”。"), C_GREEN);
      } else {
        showMsg(L3("Не удалось сохранить: нет места для пультов.", "Could not save: no room for remotes.", "保存失败：空间不足。"), C_RED);
      }
      delay(1200);
      break;
    }
  }
}
#endif

void runRemote() {
  if (!ENABLE_IR) {
    notInEdition();
    return;
  }
  if (cfgSd && !BOARD_S3) {
    viewAnswer(L3("Вывод ИК-светодиода занят SD-картой.", "The IR LED pin is used by the SD card.", "红外引脚已被SD卡占用。"), C_YELLOW);
    return;
  }
  int sel = 0;
  for (;;) {
    std::vector<int> slots;
    std::vector<String> items;
    for (int i = 0; i < IR_SLOTS; i++)
      if (LittleFS.exists(irPath(i).c_str())) {
        slots.push_back(i);
        items.push_back(irTitle(i));
      }
    int n = (int)slots.size();
    items.push_back(L3("+ Скачать из интернета", "+ Download from the internet", "+ 从网上下载"));
    items.push_back(L3("+ Загрузить с телефона", "+ Upload from phone", "+ 从手机上传"));
#if BOARD_S3
    items.push_back(L3("+ Записать с пульта", "+ Record from a remote", "+ 从遥控器录制"));
#endif
    items.push_back(T().menuBack);
    sel = listScreen(L3("Пульт", "Remote", "遥控器"), items, std::vector<String>(), sel);
#if BOARD_S3
    if (sel == n + 2) {
      runRemoteLearn();
      continue;
    }
#endif
    if (sel >= 0 && sel < n) {
      runRemoteButtons(slots[(size_t)sel]);
    } else if (sel == n) {
      if (runRemoteDownload()) sel = irCount() - 1;
    } else if (sel == n + 1) {
      viewAnswer(L3("Сейчас плата создаст сеть Wi-Fi. Подключитесь к ней с телефона, внизу страницы будет раздел «Пульты (.ir)» - выберите там файл. "
                    "Когда закончите - нажмите кнопку на устройстве.",
                    "The device will now create a Wi-Fi network. Join it from your phone; the \"Remotes (.ir)\" section is at the bottom of the page. "
                    "Press the button on the device when done.",
                    "设备将创建Wi-Fi网络。用手机连接，页面底部有“遥控器（.ir）”部分，在那里选择文件。完成后按设备上的按钮。"), C_WHITE);
      runPortal();
      netIdx = 0;
      wifiStart();
    } else {
      return;
    }
  }
}

void runTools() {
  int sel = 0;
  for (;;) {
    std::vector<String> items = splitBar(L3("Калькулятор|Википедия|Картинки|Галерея|Погода|Курсы валют|Часы|Таймер|Секундомер|Пульт|Назад",
                                            "Calculator|Wikipedia|Images|Gallery|Weather|Exchange rates|Clock|Timer|Stopwatch|Remote|Back",
                                            "计算器|维基百科|图片|相册|天气|汇率|时钟|计时器|秒表|遥控器|返回"));
    sel = listScreen(L3("Инструменты", "Tools", "工具"), items, std::vector<String>(), sel);
    switch (sel) {
      case 0: runCalc(); break;
      case 1: runWiki(); break;
      case 2: runImages(); break;
      case 3: runGallery(); break;
      case 4: runWeather(); break;
      case 5: runRates(); break;
      case 6: runClock(); break;
      case 7: runTimer(); break;
      case 8: runStopwatch(); break;
      case 9: runRemote(); break;
      default: return;
    }
  }
}

// ---------- Обновление прошивки по воздуху ----------
// Из интернета: в репозитории OTA_REPO лежат firmware/version.txt (номер версии) и firmware/firmware.bin.
// С телефона: файл .bin загружается через страницу гаджета (без интернета).
void otaBar(size_t done, size_t total) {
  static int shown = -1;
  int pct = total ? (int)((uint64_t)done * 100 / total) : (int)((done / 20000) % 101);
  if (pct == shown) return;
  shown = pct;
  tft.drawRect(20, 150, 200, 16, C_WHITE);
  tft.fillRect(22, 152, pct * 196 / 100, 12, C_GREEN);
}

// Приёмник: скачиваемая прошивка сразу пишется во вторую половину памяти.
class OtaSink : public Stream {
 public:
  size_t n = 0, total = 0;
  bool failed = false, badType = false;
  uint8_t head[8] = {0};                             // первые байты - чтобы показать, что пришло вместо прошивки
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* b, size_t len) override {
    if (n == 0) memcpy(head, b, len < 8 ? len : 8);
    if (n == 0 && (len == 0 || b[0] != 0xE9)) badType = true;      // прошивка ESP32 начинается с байта E9
    if (!badType && !failed && Update.write((uint8_t*)b, len) != len) failed = true;
    n += len;
    otaBar(n, total);
    return len;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
};

// Проверить и поставить обновление из интернета. Возвращает текст для показа (при успехе плата перезапускается).
int otaTarget = -1;                                  // какую сборку поставить; -1 - ту же, что стоит
bool otaNoAsk = false;                               // true - не спрашивать «Обновить?» (обновление по просьбе админа)
String otaFromInternet() {
  if (!waitWifi(false)) return "";
  showMsg(L3("Проверяю обновление...", "Checking for an update...", "正在检查更新……"), C_YELLOW);
  String base = String("https://raw.githubusercontent.com/") + OTA_REPO + "/" + OTA_BRANCH + "/firmware/";
  String body;
  int code = httpGetText(base + "version.txt", body);
  if (code != 200)
    return String(L3("Не удалось проверить (код ", "Cannot check (code ", "无法检查（代码 ")) + String(code) +
           L3("). Можно обновиться с телефона.", "). You can update from your phone.", "）。可以从手机更新。");
  body.trim();
  int remote = atoi(body.c_str());
  int want = otaTarget >= 0 ? otaTarget : EDITION_ID;
  bool otaSwitch = want != EDITION_ID;
  const char* fwFile = edFile(want);
  if (!otaSwitch && remote <= FW_VERSION)
    return String(L3("У вас последняя версия: ", "You have the latest version: ", "已是最新版本：")) + String(FW_VERSION);
  std::vector<String> yn = splitBar(L3("Обновить|Не сейчас", "Update|Not now", "更新|以后再说"));
  if (otaSwitch && choiceScreen(String(edAbout(want)) + L3(" Поставить? Настройки сохранятся.", " Install? Settings are kept.", " 安装？设置会保留。"), yn) != 0)
    return "";
  if (!otaSwitch && !otaNoAsk && choiceScreen(String(L3("Есть новая версия ", "New version ", "有新版本 ")) + String(remote) + L3(" (у вас ", " (you have ", "（当前 ") + String(FW_VERSION) +
                   L3("). Не выключайте гаджет, пока идёт обновление.", "). Do not switch the device off during the update.", "）。更新时请勿关机。"), yn) != 0)
    return "";
  fwBeforeInstall();
  // Скачать и записать. Иногда сеть отдаёт не файл (страницу-заглушку, обрыв) - тогда пробуем ещё раз.
  String lastErr;
  bool done = false;
  for (int attempt = 1; attempt <= 3 && !done; attempt++) {
    showMsg(String(L3("Скачиваю прошивку...", "Downloading the firmware...", "正在下载固件……")) + (attempt > 1 ? String(" (") + String(attempt) + "/3)" : String("")),
            C_YELLOW);
    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(15);
    HTTPClient http;
    http.setUserAgent(APP_UA);
    http.setConnectTimeout(15000);
    http.setTimeout(15000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    // ?a=N - чтобы промежуточный кэш не отдал тот же испорченный ответ
    if (!http.begin(client, base + fwFile + "?a=" + String(attempt) + String(millis() % 100000))) {
      lastErr = "begin";
      continue;
    }
    code = http.GET();
    int len = http.getSize();
    if (code != 200 || len < 100000) {
      http.end();
      lastErr = String(L3("Файл прошивки не найден (код ", "Firmware file not found (code ", "找不到固件文件（代码 ")) + String(code) + ", " + String(len) + ")";
      delay(1500);
      continue;
    }
    if (!Update.begin((size_t)len)) {
      http.end();
      return String(L3("Прошивка не помещается: ", "The firmware does not fit: ", "固件放不下：")) + Update.errorString();
    }
    OtaSink sink;
    sink.total = (size_t)len;
    int got = http.writeToStream(&sink);
    http.end();
    if (got < 0 || sink.failed || sink.badType || sink.n != (size_t)len || !Update.end(true)) {
      Update.abort();
      char hx[40];
      snprintf(hx, sizeof(hx), "%02X %02X %02X %02X %02X %02X", sink.head[0], sink.head[1], sink.head[2], sink.head[3], sink.head[4], sink.head[5]);
      Serial.printf("[ota] attempt %d: got %d of %d, bad %d, failed %d, head %s\n", attempt, (int)sink.n, len, sink.badType, sink.failed, hx);
      lastErr = sink.badType ? String(L3("Пришёл не файл прошивки (начало: ", "Received something that is not firmware (starts with: ", "收到的不是固件（开头：")) + hx + ")"
                             : String(L3("Обновление оборвалось (", "The update was interrupted (", "更新中断（")) + String((int)sink.n) + " / " + String(len) + ")";
      delay(1500);
      continue;
    }
    done = true;
  }
  if (!done)
    return lastErr + L3(". Старая прошивка осталась на месте. Попробуйте позже или обновитесь с телефона.",
                        ". The old firmware is still in place. Try later or update from your phone.", "。旧固件仍保留。");
  showMsg(L3("Готово! Перезапускаюсь...", "Done! Restarting...", "完成！正在重启……"), C_GREEN);
  delay(1500);
  ESP.restart();
  return "";
}

// Приём прошивки с телефона.
bool otaUpOk = false, otaUpStarted = false;
String otaUpErr;
size_t otaUpSize = 0;

void handleFwUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    otaUpOk = false;
    otaUpErr = "";
    otaUpSize = 0;
    otaUpStarted = Update.begin(UPDATE_SIZE_UNKNOWN);
    if (!otaUpStarted) otaUpErr = Update.errorString();
    showMsg(L3("Принимаю прошивку с телефона...", "Receiving the firmware...", "正在接收固件……"), C_YELLOW);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!otaUpStarted) return;
    if (otaUpSize == 0 && (up.currentSize == 0 || up.buf[0] != 0xE9)) {
      Update.abort();
      otaUpStarted = false;
      otaUpErr = L3("это не файл прошивки (.bin)", "this is not a firmware file (.bin)", "这不是固件文件（.bin）");
      return;
    }
    if (Update.write(up.buf, up.currentSize) != up.currentSize) {
      otaUpErr = Update.errorString();
      Update.abort();
      otaUpStarted = false;
      return;
    }
    otaUpSize += up.currentSize;
    otaBar(otaUpSize, 0);
  } else if (up.status == UPLOAD_FILE_END) {
    if (!otaUpStarted) return;
    otaUpStarted = false;
    if (otaUpSize < 100000) {
      Update.abort();
      otaUpErr = L3("файл слишком маленький", "the file is too small", "文件太小");
    } else if (Update.end(true)) {
      otaUpOk = true;
    } else {
      otaUpErr = Update.errorString();
    }
  } else if (otaUpStarted) {
    Update.abort();
    otaUpStarted = false;
    otaUpErr = L3("загрузка прервана", "the upload was interrupted", "上传中断");
  }
}

void handleFwDone() {
  if (!otaUpOk) {
    server.send(200, "text/plain; charset=utf-8", String(L3("Ошибка: ", "Error: ", "错误：")) + (otaUpErr.length() ? otaUpErr : String("?")));
    showMsg(String(L3("Обновление не удалось: ", "The update failed: ", "更新失败：")) + otaUpErr, C_RED);
    return;
  }
  server.send(200, "text/plain; charset=utf-8", L3("Прошивка принята. Гаджет перезапускается.", "Firmware received. The device is restarting.", "固件已接收。设备正在重启。"));
  showMsg(L3("Готово! Перезапускаюсь...", "Done! Restarting...", "完成！正在重启……"), C_GREEN);
  delay(1500);
  ESP.restart();
}

// ---------- Сборки прошивки на SD-карте ----------
// Обе сборки (полная и с Bluetooth) скачиваются с GitHub на карту; переключаться между ними можно без интернета.
#if BOARD_S3
String sdFwPath(int e) { return e == 1 ? "/fw/s3-ble.bin" : e == 2 ? "/fw/s3-games.bin" : "/fw/s3-full.bin"; }
#else
String sdFwPath(int e) { return e == 1 ? "/fw/ble.bin" : e == 2 ? "/fw/games.bin" : "/fw/full.bin"; }
#endif

int sdFwVersion() {
  SdBus b;
  File f = SD.open("/fw/version.txt", "r");
  int v = f ? f.readString().toInt() : 0;
  if (f) f.close();
  return v;
}

bool sdFwHas(int e) {
  SdBus b;
  File f = SD.open(sdFwPath(e).c_str(), "r");
  bool ok = f && f.size() > 100000;
  if (f) f.close();
  return ok;
}

// Скачать файл с GitHub на карту. Пустая строка - успех.
String sdDownload(const String& url, const String& path) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(15);
  HTTPClient http;
  http.setUserAgent(APP_UA);
  http.setConnectTimeout(15000);
  http.setTimeout(15000);
  if (!http.begin(client, url)) return "begin";
  int code = http.GET();
  int len = http.getSize();
  if (code != 200 || len < 100000) {
    http.end();
    return String(L3("файл не найден (код ", "file not found (code ", "找不到文件（代码 ")) + String(code) + ")";
  }
  String tmp = path + ".part";
  File f;
  {
    SdBus b;
    SD.remove(tmp.c_str());
    f = SD.open(tmp.c_str(), "w");
  }
  if (!f) {
    http.end();
    return L3("не удалось создать файл на карте", "cannot create the file on the card", "无法在卡上创建文件");
  }
  uint8_t* buf = (uint8_t*)malloc(4096);
  WiFiClient* st = http.getStreamPtr();
  size_t done = 0;
  bool ok = buf != nullptr;
  uint32_t lastData = millis();
  tft.fillRect(20, 150, 200, 16, C_BLACK);
  while (ok && done < (size_t)len) {
    size_t av = st->available();
    if (!av) {
      if (!http.connected() || millis() - lastData > 20000) ok = false;
      delay(2);
      continue;
    }
    int n = st->readBytes(buf, av > 4096 ? 4096 : av);
    if (n <= 0) continue;
    lastData = millis();
    size_t w;
    {
      SdBus b;
      w = f.write(buf, (size_t)n);
    }
    if (w != (size_t)n) ok = false;
    done += (size_t)n;
    otaBar(done, (size_t)len);
  }
  if (buf) free(buf);
  http.end();
  SdBus b;
  f.close();
  if (!ok || done != (size_t)len) {
    SD.remove(tmp.c_str());
    return L3("загрузка оборвалась", "the download was interrupted", "下载中断");
  }
  SD.remove(path.c_str());
  SD.rename(tmp.c_str(), path.c_str());
  return "";
}

String sdDownloadEditions() {
  if (!waitWifi(false)) return "";
  String base = String("https://raw.githubusercontent.com/") + OTA_REPO + "/" + OTA_BRANCH + "/firmware/";
  String body;
  int code = httpGetText(base + "version.txt", body);
  if (code != 200) return String(L3("Не удалось узнать версию (код ", "Cannot get the version (code ", "无法获取版本（代码 ")) + String(code) + ")";
  int remote = body.toInt();
  for (int k = 0; k < 3; k++) {
    showMsg(String(L3("Скачиваю на карту: сборка ", "Downloading to the card: edition ", "正在下载到卡：")) + edName(k) + " v" + String(remote) + " (" +
                String(k + 1) + "/3)",
            C_YELLOW);
    String err = sdDownload(base + edFile(k), sdFwPath(k));
    if (err.length()) return String(L3("Не получилось: ", "Failed: ", "失败：")) + err;
  }
  SdBus b;
  File f = SD.open("/fw/version.txt", "w");
  if (f) {
    f.print(remote);
    f.close();
  }
  return String(L3("Готово: на карте все три сборки версии ", "Done: all three editions of version ", "完成：卡上有三个版本 ")) + String(remote);
}

// Поставить сборку с карты (без интернета). При успехе плата перезапускается.
String sdInstallPath(const String& path);
String sdInstall(int e) { return sdInstallPath(sdFwPath(e)); }
String sdInstallPath(const String& path) {
  File f;
  size_t len = 0;
  {
    SdBus b;
    f = SD.open(path.c_str(), "r");
    len = f ? (size_t)f.size() : 0;
  }
  if (!f || len < 100000) return L3("Этой сборки на карте нет.", "This edition is not on the card.", "卡上没有这个版本。");
  if (!Update.begin(len)) {
    SdBus b;
    f.close();
    return String(L3("Прошивка не помещается: ", "The firmware does not fit: ", "固件放不下：")) + Update.errorString();
  }
  showMsg(L3("Ставлю прошивку с карты... Не выключайте гаджет.", "Installing from the card... Do not switch off.", "正在从卡安装……请勿关机。"), C_YELLOW);
  uint8_t* buf = (uint8_t*)malloc(4096);
  size_t done = 0;
  bool ok = buf != nullptr;
  while (ok && done < len) {
    int n;
    {
      SdBus b;
      n = f.read(buf, 4096);
    }
    if (n <= 0 || (done == 0 && buf[0] != 0xE9) || Update.write(buf, (size_t)n) != (size_t)n) ok = false;
    done += n > 0 ? (size_t)n : 0;
    otaBar(done, len);
  }
  if (buf) free(buf);
  {
    SdBus b;
    f.close();
  }
  if (!ok || done != len || !Update.end(true)) {
    Update.abort();
    return L3("Не удалось поставить прошивку. Старая осталась на месте.", "Could not install the firmware. The old one is still in place.", "安装失败，旧固件仍保留。");
  }
  showMsg(L3("Готово! Перезапускаюсь...", "Done! Restarting...", "完成！正在重启……"), C_GREEN);
  delay(1200);
  ESP.restart();
  return "";
}

#if ENABLE_FWBACKUP
void runSdEditions() {
  for (;;) {
    int v = sdFwVersion();
    std::vector<String> o;
    o.push_back(L3("Скачать все сборки на карту", "Download all editions to the card", "下载全部版本到卡"));
    std::vector<int> eds;
    for (int e = 0; e < 3; e++)
      if (sdFwHas(e)) {
        o.push_back(String(L3("Поставить с карты: ", "Install from the card: ", "从卡安装：")) + edName(e));
        eds.push_back(e);
      }
    o.push_back(L3("Назад", "Back", "返回"));
    String head = String(L3("Сборки на SD-карте. ", "Editions on the SD card. ", "SD卡上的版本。")) +
                  (v ? String(L3("На карте версия ", "Card version ", "卡上版本 ")) + String(v) : String(L3("На карте пока ничего нет.", "Nothing on the card yet.", "卡上还没有。"))) +
                  String(L3(". У вас: ", ". Installed: ", "。当前：")) + String(FW_VERSION) + ", " + edName(EDITION_ID);
    int k = choiceScreen(head, o);
    String msg;
    if (k == 0) msg = sdDownloadEditions();
    else if (k >= 1 && k <= (int)eds.size()) {
      int e = eds[(size_t)(k - 1)];
      std::vector<String> yn = splitBar(L3("Поставить|Отмена", "Install|Cancel", "安装|取消"));
      if (choiceScreen(String(edAbout(e)) + L3(" Поставить с карты? Настройки сохранятся. Около 20 секунд.", " Install from the card? Settings are kept. About 20 seconds.",
                                               " 从卡安装？约20秒。"), yn) == 0) {
        fwBeforeInstall();
        msg = sdInstall(e);
      }
    } else return;
    if (msg.length()) viewAnswer(msg, C_WHITE);
  }
}
#else
void runSdEditions() { notInEdition(); }
#endif

// ---------- Откат прошивки ----------
// 1) В памяти у платы два места под прошивку: после обновления прошлая версия остаётся во втором,
//    вернуться на неё можно мгновенно (пока её не затёрло следующее обновление).
// 2) Перед каждым обновлением текущая прошивка копируется на SD-карту (папка fw/, три последние).
// 3) Если новая прошивка 3 раза подряд не запустилась дольше чем на 20 секунд - плата сама вернёт прошлую.
const char* edTag(int e) {
#if BOARD_S3
  return e == 1 ? "s3-ble" : e == 2 ? "s3-games" : "s3-full";
#else
  return e == 1 ? "ble" : e == 2 ? "games" : "full";
#endif
}

uint32_t fwImageSize(const esp_partition_t* p) {
#if !ENABLE_FWBACKUP
  (void)p;
  return 0;
#else
  if (!p) return 0;
  esp_partition_pos_t pos = {p->address, p->size};
  esp_image_metadata_t md;
  if (esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &pos, &md) == ESP_OK) return md.image_len;
  return 0;
#endif
}

// Копия работающей прошивки на карту. "" - сделано (или карты нет), иначе ошибка.
String fwBackupToSd() {
  if (!sdOk || !ENABLE_FWBACKUP) return "";
  const esp_partition_t* run = esp_ota_get_running_partition();
  uint32_t n = fwImageSize(run);
  if (!n) return L3("не удалось прочитать текущую прошивку", "cannot read the current firmware", "无法读取当前固件");
  String path = String("/fw/bak-") + edTag(EDITION_ID) + "-v" + String(FW_VERSION) + ".bin";
  {
    SdBus b;
    SD.mkdir("/fw");
    File old = SD.open(path.c_str(), "r");
    bool same = old && old.size() == n;
    if (old) old.close();
    if (same) return "";                             // такая копия уже есть
  }
  showMsg(L3("Сохраняю текущую прошивку на карту...", "Saving the current firmware to the card...", "正在把当前固件保存到卡……"), C_YELLOW);
  uint8_t* buf = (uint8_t*)malloc(4096);
  if (!buf) return L3("не хватило памяти", "out of memory", "内存不足");
  String tmp = path + ".part";
  bool ok;
  {
    SdBus b;
    File f = SD.open(tmp.c_str(), "w");
    ok = (bool)f;
    for (uint32_t off = 0; ok && off < n; off += 4096) {
      uint32_t k = n - off < 4096 ? n - off : 4096;
      ok = esp_partition_read(run, off, buf, k) == ESP_OK && f.write(buf, k) == k;
      if ((off & 0x3FFFF) == 0) otaBar(off, n);
    }
    if (f) f.close();
    if (ok) {
      SD.remove(path.c_str());
      ok = SD.rename(tmp.c_str(), path.c_str());
    } else {
      SD.remove(tmp.c_str());
    }
    // оставить три последние копии
    std::vector<String> baks;
    File dir = SD.open("/fw");
    if (dir) {
      for (int g = 0; g < 100; g++) {
        bool isDir = false;
        String nm = dir.getNextFileName(&isDir);
        if (!nm.length()) break;
        int sl = nm.lastIndexOf('/');
        if (sl >= 0) nm = nm.substring(sl + 1);
        if (!isDir && nm.startsWith("bak-") && nm.endsWith(".bin")) baks.push_back(nm);
      }
      dir.close();
    }
    auto ver = [](const String& nm) { int v = nm.lastIndexOf("-v"); return v >= 0 ? nm.substring(v + 2).toInt() : 0; };
    std::sort(baks.begin(), baks.end(), [&](const String& a, const String& b) { return ver(a) > ver(b); });
    for (size_t i = 3; i < baks.size(); i++) SD.remove((String("/fw/") + baks[i]).c_str());
  }
  free(buf);
  return ok ? "" : String(L3("не удалось записать копию на карту", "cannot write the copy to the card", "无法写入卡"));
}

// Перед установкой новой прошивки: копия на карту и запомнить, что стояло.
void fwBeforeInstall() {
  String e = fwBackupToSd();
  if (e.length()) Serial.printf("[backup] %s\n", e.c_str());
  prefs.begin("dsk", false);
  prefs.putInt("prevver", FW_VERSION);
  prefs.putUChar("preved", (uint8_t)EDITION_ID);
  prefs.end();
}

void runRollback() {
  for (;;) {
    std::vector<String> o;
    std::vector<String> paths;                       // "" - прошлая версия из памяти платы
    prefs.begin("dsk", true);
    int pv = prefs.getInt("prevver", 0);
    int pe = prefs.getUChar("preved", 0);
    prefs.end();
    if (Update.canRollBack()) {
      o.push_back(String(L3("Прошлая версия из памяти", "Previous version from memory", "内存中的上一版本")) +
                  (pv ? String(" v") + String(pv) + ", " + edName(pe) : String("")));
      paths.push_back("");
    }
    if (sdOk && ENABLE_FWBACKUP) {
      SdBus b;
      File dir = SD.open("/fw");
      if (dir) {
        for (int g = 0; g < 100; g++) {
          bool isDir = false;
          String nm = dir.getNextFileName(&isDir);
          if (!nm.length()) break;
          int sl = nm.lastIndexOf('/');
          if (sl >= 0) nm = nm.substring(sl + 1);
          if (isDir || !nm.startsWith("bak-") || !nm.endsWith(".bin")) continue;
          String tag = nm.substring(4, nm.lastIndexOf("-v"));
          int e = -1;
          for (int k = 0; k < 3; k++)
            if (tag == edTag(k)) e = k;
          if (e < 0) continue;                       // копия для другой платы
          o.push_back(String(L3("С карты: v", "From card: v", "从卡：v")) + nm.substring(nm.lastIndexOf("-v") + 2, nm.length() - 4) + ", " + edName(e));
          paths.push_back(String("/fw/") + nm);
        }
        dir.close();
      }
    }
    if (o.empty()) {
      viewAnswer(L3("Откатываться пока не на что. Прошлая версия появится в памяти после следующего обновления, "
                    "а с SD-картой перед каждым обновлением текущая прошивка ещё и копируется на карту.",
                    "Nothing to roll back to yet. The previous version appears in memory after the next update; with an SD card the current "
                    "firmware is also copied to the card before every update.", "暂时没有可回退的版本。"),
                 C_WHITE);
      return;
    }
    if (ENABLE_FWBACKUP) o.push_back(L3("Сохранить текущую на карту", "Save the current one to the card", "把当前固件保存到卡"));
    o.push_back(L3("Назад", "Back", "返回"));
    int k = choiceScreen(String(L3("Откат прошивки. Сейчас: v", "Roll back firmware. Now: v", "固件回退。当前：v")) + String(FW_VERSION) + ", " + edName(EDITION_ID), o);
    if (k < 0 || k == (int)o.size() - 1) return;
    if (ENABLE_FWBACKUP && k == (int)o.size() - 2) {
      if (!sdOk) viewAnswer(L3("SD-карта не подключена.", "No SD card.", "没有SD卡。"), C_YELLOW);
      else {
        String e = fwBackupToSd();
        viewAnswer(e.length() ? e : String(L3("Готово: текущая прошивка на карте.", "Done: the current firmware is on the card.", "完成。")), e.length() ? C_RED : C_GREEN);
      }
      continue;
    }
    std::vector<String> yn = splitBar(L3("Вернуть|Отмена", "Roll back|Cancel", "回退|取消"));
    if (choiceScreen(String(L3("Поставить «", "Install \"", "安装“")) + o[(size_t)k] + L3("»? Настройки и файлы сохранятся.", "\"? Settings and files are kept.", "”？设置会保留。"), yn) != 0)
      continue;
    if (paths[(size_t)k].length() == 0) {
      prefs.begin("dsk", false);
      prefs.putInt("prevver", FW_VERSION);
      prefs.putUChar("preved", (uint8_t)EDITION_ID);
      prefs.end();
      if (Update.rollBack()) {
        showMsg(L3("Возвращаю прошлую версию...", "Rolling back...", "正在回退……"), C_GREEN);
        delay(1200);
        ESP.restart();
      }
      viewAnswer(L3("Не получилось: прошлая версия в памяти испорчена или уже затёрта.", "Failed: the previous version in memory is damaged or overwritten.", "失败。"), C_RED);
      continue;
    }
    fwBeforeInstall();
    String msg = sdInstallPath(paths[(size_t)k]);   // при успехе плата перезапускается
    if (msg.length()) viewAnswer(msg, C_RED);
  }
}

// Новая прошивка не запускается: счётчик неудачных запусков. Сбрасывается через 20 с нормальной работы.
void bootGuard() {
  prefs.begin("dsk", false);
  int bf = prefs.getUChar("bootfail", 0);
  if (bf >= 3 && Update.canRollBack()) {
    prefs.putUChar("bootfail", 0);
    prefs.putUChar("rolledback", 1);
    prefs.end();
    Serial.println("3 failed starts in a row - rolling back to the previous firmware");
    Update.rollBack();
    ESP.restart();
  }
  prefs.putUChar("bootfail", (uint8_t)(bf + 1));
  prefs.end();
  xTaskCreate(
      [](void*) {
        vTaskDelay(pdMS_TO_TICKS(20000));
        Preferences p;
        p.begin("dsk", false);
        p.putUChar("bootfail", 0);
        p.end();
        vTaskDelete(nullptr);
      },
      "bootok", 3072, nullptr, 1, nullptr);
}

void runUpdate() {
  for (;;) {
    std::vector<String> o = splitBar(L3("Проверить в интернете|Сменить сборку|С телефона (файл .bin)|Откат прошивки|Назад",
                                        "Check online|Switch edition|From phone (.bin file)|Roll back firmware|Back", "联网检查|切换版本|从手机（.bin文件）|固件回退|返回"));
    if (sdOk && ENABLE_FWBACKUP) o.insert(o.begin() + 4, L3("Сборки на SD-карте", "Editions on the SD card", "SD卡上的版本"));
    int k = choiceScreen(String(L3("Обновление прошивки. Сейчас стоит версия ", "Firmware update. Installed version: ", "固件更新。当前版本：")) + String(FW_VERSION) +
                             String(L3(", сборка: ", ", edition: ", "，版本：")) + edName(EDITION_ID),
                         o);
    if (k == 0 || k == 1) {
      otaTarget = -1;
      if (k == 1) {                                  // выбрать другую сборку
        std::vector<String> eo;
        std::vector<int> eds;
        for (int e = 0; e < 3; e++)
          if (e != EDITION_ID) {
            eo.push_back(String(L3("Сборка ", "Edition: ", "版本：")) + edName(e));
            eds.push_back(e);
          }
        eo.push_back(L3("Назад", "Back", "返回"));
        int c = choiceScreen(String(L3("Сейчас стоит сборка ", "Installed edition: ", "当前版本：")) + edName(EDITION_ID) + L3(". На какую сменить?", ". Switch to?", "。切换到？"), eo);
        if (c < 0 || c >= (int)eds.size()) continue;
        otaTarget = eds[(size_t)c];
      }
      String msg = otaFromInternet();
      otaTarget = -1;
      if (msg.length()) viewAnswer(msg, C_WHITE);
    } else if (k == 3) {
      runRollback();
    } else if (k == 4 && sdOk && ENABLE_FWBACKUP) {
      runSdEditions();
    } else if (k == 2) {
      fwBeforeInstall();
      viewAnswer(L3("Сейчас плата создаст сеть Wi-Fi. Подключитесь к ней с телефона, внизу страницы будет раздел «Обновление прошивки» - выберите там файл .bin. "
                    "Чтобы отменить - нажмите кнопку на устройстве.",
                    "The device will now create a Wi-Fi network. Join it from your phone; the \"Firmware update\" section is at the bottom of the page. "
                    "Press the button on the device to cancel.",
                    "设备将创建Wi-Fi网络。用手机连接，页面底部有“固件更新”部分，在那里选择.bin文件。按设备上的按钮可取消。"), C_WHITE);
      runPortal();
      netIdx = 0;
      wifiStart();
    } else {
      return;
    }
  }
}

// ---------- Меню «Настройки» ----------
// true - поменялся язык или сеть (нужно перерисовать клавиатуру).
// ========================= BLUETOOTH =========================
// Телефон подключается к гаджету «DS-<метка>»: печатает текст, присылает уведомления (страница
// ble.html в Chrome на Android или приложение Serial Bluetooth Terminal), а гаджет служит пультом
// для музыки на телефоне. Bluetooth включается в настройках. (Игры вдвоём рядом - ESP-NOW.)
const char* BLE_PAGE = "yacovgopnik.github.io/deepseek-pocket/ble.html";

void bleSetEnabled(bool on) {
  if (on) {
    showMsg(L3("Включаю Bluetooth...", "Turning Bluetooth on...", "正在打开蓝牙……"), C_YELLOW);
    ble::begin(toStd(roomId));
  } else {
    ble::end();
  }
  cfgBle = on ? 1 : 0;
  saveScreenSettings();
  Serial.printf("Bluetooth %s, heap %u, largest block %u\n", on ? "on" : "off", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
}

// Уведомления и текст с телефона, пришедшие не в поле ввода. true - экран занят.
bool bleService() {
  std::string line;
  while (phoneTakeLine(line)) {
    if (line.compare(0, 2, "N:") == 0 || line.compare(0, 2, "T:") == 0 || line.compare(0, 2, "S:") == 0) line = line.substr(2);
    if (line.size()) bleNotes.push_back(fromStd(line));
  }
  if (bleNotes.empty()) return false;
  String t = bleNotes.front();
  bleNotes.erase(bleNotes.begin());
  wakeScreen();
  sndMessage();
  viewAnswer(String(L3("Сообщение с телефона\n\n", "Message from the phone\n\n", "来自手机的消息\n\n")) + t, C_WHITE);
  ble::sendPhone("OK\n");
  return true;
}

void drawRemoteScreen() {
  tft.fillScreen(C_BLACK);
  tft.fillRect(0, 0, 240, 28, C_BAR);
  drawUtf8(L3("Пульт для телефона", "Phone remote", "手机遥控器"), 6, 2, C_WHITE, C_BAR);
  const int cx = 120, cy = 118;
  tft.fillCircle(cx, cy, 30, C_SEL);                 // пуск/пауза
  tft.fillTriangle(cx - 8, cy - 12, cx - 8, cy + 12, cx + 4, cy, C_WHITE);
  tft.fillRect(cx + 6, cy - 11, 3, 22, C_WHITE);
  tft.fillRect(cx + 11, cy - 11, 3, 22, C_WHITE);
  tft.fillTriangle(cx, cy - 72, cx - 14, cy - 50, cx + 14, cy - 50, C_GREEN);    // громче
  tft.fillTriangle(cx, cy + 72, cx - 14, cy + 50, cx + 14, cy + 50, C_YELLOW);   // тише
  for (int k = 0; k < 2; k++) {                      // предыдущий / следующий
    tft.fillTriangle(cx - 52 - k * 12, cy, cx - 40 - k * 12, cy - 10, cx - 40 - k * 12, cy + 10, C_WHITE);
    tft.fillTriangle(cx + 52 + k * 12, cy, cx + 40 + k * 12, cy - 10, cx + 40 + k * 12, cy + 10, C_WHITE);
  }
  SmallFont small;
  drawUtf8(L3("громче", "vol +", "音量+"), cx + 20, cy - 66, C_LIGHT, C_BLACK);
  drawUtf8(L3("тише", "vol -", "音量-"), cx + 20, cy + 54, C_LIGHT, C_BLACK);
  drawUtf8(L3("Нажать - пауза, держать - выход", "Click - pause, hold - exit", "按下：暂停，长按：退出"), 4, 222, C_LIGHT, C_BLACK);
}

void runPhoneRemote() {
  if (!ble::active()) bleSetEnabled(true);
  KeepAwake awake;
  uint32_t t0 = millis();
  while (!ble::remoteReady()) {                      // ждём, пока телефон подключит гаджет как пульт
    if (millis() - t0 < 300 || (millis() - t0) % 4000 < 30)
      showMsg(String(L3("На телефоне: Настройки -> Bluetooth -> найдите ", "On the phone: Settings -> Bluetooth -> find ", "在手机上：设置 -> 蓝牙 -> 找到 ")) +
                  "DS-" + roomId +
                  L3(" и подключитесь (подтвердите сопряжение). Держать кнопку - выход.", " and connect (confirm pairing). Hold the button - exit.",
                     " 并连接（确认配对）。长按按钮：退出。"),
              C_YELLOW);
    if (btnEvent() == EV_LONG) return;
    delay(20);
  }
  sndOk();
  drawRemoteScreen();
  for (;;) {
    if (!ble::remoteReady()) {
      showMsg(L3("Телефон отключился.", "The phone disconnected.", "手机已断开。"), C_YELLOW);
      delay(1500);
      return;
    }
    uint8_t j = joyEvent();
    if (j == J_UP) ble::media(ble::KEY_VOL_UP);
    else if (j == J_DOWN) ble::media(ble::KEY_VOL_DOWN);
    else if (j == J_LEFT) ble::media(ble::KEY_PREV);
    else if (j == J_RIGHT) ble::media(ble::KEY_NEXT);
    BtnEvent e = btnEvent();
    if (e == EV_LONG) return;
    if (e == EV_CLICK) ble::media(ble::KEY_PLAY);
    if (j != J_NONE || e == EV_CLICK) beep(2, 2200, 5);
    noteActivity();
    delay(10);
  }
}

// Приложение «Bluetooth».
void runBluetooth() {
  int sel = 0;
  for (;;) {
    std::vector<String> items = splitBar(L3("Пульт для телефона|Связь с телефоном|Bluetooth|Назад",
                                            "Phone remote|Phone link|Bluetooth|Back", "手机遥控器|手机连接|蓝牙|返回"));
    std::vector<String> right(items.size());
    right[1] = ble::phoneConnected() ? L3("подключён", "connected", "已连接") : "";
    right[2] = ble::active() ? L3("вкл", "on", "开") : L3("выкл", "off", "关");
    sel = listScreen((String("Bluetooth  DS-") + roomId).c_str(), items, right, sel);
    if (sel == 0) {
      runPhoneRemote();
    } else if (sel == 1) {
      if (!ble::active()) bleSetEnabled(true);
      viewAnswer(String(L3("Гаджет виден по Bluetooth как ", "The device is visible over Bluetooth as ", "设备的蓝牙名称：")) + "DS-" + roomId +
                     L3(".\n\nНа Android откройте в Chrome страницу\n", ".\n\nOn Android open this page in Chrome:\n", "。\n\n在安卓手机的Chrome中打开：\n") + BLE_PAGE +
                     L3("\nи нажмите «Подключить». Можно и приложение Serial Bluetooth Terminal (режим Bluetooth LE).\n\n"
                        "Текст с телефона попадает в поле ввода (например, вопрос DeepSeek) или всплывает как уведомление, если поле ввода не открыто. "
                        "Строка, начатая с N:, - всегда уведомление; с T: - только вставить, не отправлять.",
                        "\nand press Connect. The Serial Bluetooth Terminal app (Bluetooth LE mode) works too.\n\n"
                        "Text from the phone goes into the open input field (e.g. a DeepSeek question) or pops up as a notification. "
                        "A line starting with N: is always a notification; T: only inserts the text without sending.",
                        "\n并点击“连接”。也可以用 Serial Bluetooth Terminal 应用（蓝牙LE模式）。\n\n手机发来的文字会进入输入框，或在没有输入框时作为通知弹出。"),
                 C_WHITE);
    } else if (sel == 2) {
      bleSetEnabled(!ble::active());
    } else {
      return;
    }
  }
}

// ========================= АДМИНИСТРАТОР =========================
// Админ входит по логину и паролю (Настройки -> Администратор). Из них получается ключ подписи; гаджеты
// принимают только команды с верной подписью (открытый ключ - ADMIN_PUBKEY в начале скетча).
// Команды идут через тот же сервер, что и «Комната»: их получают гаджеты, у которых комната на связи
// (задано имя и включены «Сообщения в фоне»). Владелец всегда видит, что сделал админ, и может отменить
// обновление или перезапуск.
const char* ADM_TOPIC = "dspocket/v1/adm/";
struct AdmDev {
  String id, nick, room;
  int fw, bat;
  uint32_t seen;
};
std::vector<AdmDev> admDevs;                         // гаджеты, которые видит админ (до 30)
adm::Guard admGuard;
uint32_t admAnnTs = 0, admBansTs = 0;                // время последнего принятого объявления и списка блокировок
String admPendingAnn;                                // объявление: показать, когда будет удобно
int admPendingCmd = 0;                               // 1 - обновиться, 2 - перезапуститься
bool admStateLoaded = false;

int admRng(void*, unsigned char* buf, size_t len) {
  esp_fill_random(buf, len);
  return 0;
}

uint32_t unixNow() {
  struct tm t;
  if (!timeNow(t)) return 0;
  time_t now = time(nullptr);
  return now > 1600000000 ? (uint32_t)now : 0;
}

bool admBanned(const String& id) {
  std::string s = toStd(id);
  if (s.size() > 3 && s.compare(s.size() - 3, 3, "-ai") == 0) s.resize(s.size() - 3);   // ответы DeepSeek от гаджета
  for (const std::string& b : roomBans)
    if (b == s) return true;
  return false;
}

void admLoadState() {
  if (admStateLoaded) return;
  admStateLoaded = true;
  prefs.begin("dsk", true);
  admAnnTs = prefs.getULong("annts", 0);
  admBansTs = prefs.getULong("bansts", 0);
  adm::splitList(toStd(prefs.getString("bans", "")), roomBans);
  prefs.end();
}

void admSaveState() {
  prefs.begin("dsk", false);
  prefs.putULong("annts", admAnnTs);
  prefs.putULong("bansts", admBansTs);
  prefs.putString("bans", fromStd(adm::joinList(roomBans)));
  prefs.end();
}

// Проверка подписи занимает около секунды, поэтому чужой мусор не должен её запускать часто:
// не больше 5 проверок подряд, потом одна в 2 секунды.
bool admVerifyBudget() {
  static int tokens = 5;
  static uint32_t last = 0;
  uint32_t now = millis();
  int add = (int)((now - last) / 2000);
  if (add > 0) {
    tokens = tokens + add > 5 ? 5 : tokens + add;
    last = now;
  }
  if (tokens <= 0) return false;
  tokens--;
  return true;
}

void admReceive(const std::string& sub, const std::string& payload) {
  if (!strlen(ADMIN_PUBKEY) || payload.size() > 1200) return;
  admLoadState();
  std::string b, sig;
  adm::Cmd c;
  if (!adm::unwrap(payload, b, sig) || !adm::parse(b, c)) return;
  bool isCmd = sub == "cmd", isAnn = sub == "ann", isBans = sub == "bans";
  bool isPin = sub == "pin/" + mqtt::cleanRoom(toStd(cfgRoom));
  if (!isCmd && !isAnn && !isBans && !isPin) return;
  // Дешёвые проверки - до подписи.
  if (isCmd) {
    if (c.target != adm::ALL && c.target != toStd(roomId)) return;
    if (!admGuard.fresh(c, unixNow())) return;
    if (admOn && c.target == adm::ALL && (c.cmd == "upd" || c.cmd == "rbt")) return;   // свой массовый перезапуск админ не ловит
  }
  if (isAnn && c.ts <= admAnnTs) return;
  if (isBans && c.ts <= admBansTs) return;
  if (isPin && c.ts <= roomPinTs) return;
  if (!admVerifyBudget() || !adm::verify(ADMIN_PUBKEY, b, sig)) return;
  Serial.printf("[admin] %s %s -> %s\n", sub.c_str(), c.cmd.c_str(), c.target.c_str());
  if (isAnn) {
    admAnnTs = c.ts;
    admSaveState();
    if (c.arg.size()) admPendingAnn = fromStd(mqtt::clip(c.arg, 600));
  } else if (isBans) {
    admBansTs = c.ts;
    adm::splitList(c.arg, roomBans);
    admSaveState();
    for (size_t i = roomMsgs.size(); i-- > 0;)
      if (admBanned(roomMsgs[i].id)) roomMsgs.erase(roomMsgs.begin() + (long)i);
    roomDirty = true;
  } else if (isPin) {
    roomPinTs = c.ts;
    roomPin = fromStd(mqtt::clip(c.arg, 200));
    roomDirty = true;
  } else {
    admGuard.remember(c);
    if (c.cmd == "upd") admPendingCmd = 1;
    else if (c.cmd == "rbt") admPendingCmd = 2;
    else if (c.cmd == "kick") roomKicked = true;
    else if (c.cmd == "ping") {                     // админ ищет гаджеты: ответить «я здесь» (вразнобой, чтобы не все сразу)
      delay(esp_random() % 1500);
      if (mqUp) sendHere(true);
    } else if (c.cmd == "del") {                    // удалить сообщение: «метка:хеш»
      size_t k = c.arg.find(':');
      if (k != std::string::npos) {
        std::string id = c.arg.substr(0, k), h = c.arg.substr(k + 1);
        for (size_t i = roomMsgs.size(); i-- > 0;)
          if (toStd(roomMsgs[i].id) == id && adm::msgHash(id, toStd(roomMsgs[i].text)) == h) roomMsgs.erase(roomMsgs.begin() + (long)i);
        roomDirty = true;
      }
    }
  }
}

// Присутствие гаджета (для списка у админа): «метка \t имя \t 1 \t прошивка \t заряд».
void admPresence(const std::string& room, const std::string& payload) {
  std::vector<std::string> f;
  size_t a = 0;
  for (;;) {
    size_t t = payload.find('\t', a);
    f.push_back(payload.substr(a, t == std::string::npos ? std::string::npos : t - a));
    if (t == std::string::npos) break;
    a = t + 1;
  }
  if (f.size() < 3 || f[0].empty() || f[0].size() > 16) return;
  String id = fromStd(f[0]);
  size_t i = 0;
  while (i < admDevs.size() && admDevs[i].id != id) i++;
  if (f[2] == "0") {                                 // ушёл
    if (i < admDevs.size()) admDevs.erase(admDevs.begin() + (long)i);
    return;
  }
  if (i == admDevs.size()) {
    if (admDevs.size() >= 30) {                      // вытеснить самого давнего
      size_t old = 0;
      for (size_t k = 1; k < admDevs.size(); k++)
        if ((int32_t)(admDevs[k].seen - admDevs[old].seen) < 0) old = k;
      admDevs.erase(admDevs.begin() + (long)old);
      i = admDevs.size();
    }
    AdmDev d;
    d.id = id;
    admDevs.push_back(d);
  }
  AdmDev& d = admDevs[i];
  d.nick = fromStd(mqtt::clip(f[1], 24));
  d.room = fromStd(mqtt::clip(room, 24));
  d.fw = f.size() > 3 ? atoi(f[3].c_str()) : 0;
  d.bat = f.size() > 4 ? atoi(f[4].c_str()) : -2;
  d.seen = millis();
}

// Обратный отсчёт с возможностью отмены. true - не отменили.
bool admCountdown(const String& what) {
  KeepAwake awake;
  wakeScreen();
  sndMessage();
  for (int sec = 10; sec > 0; sec--) {
    showMsg(what + "\n\n" + L3("Через ", "In ", "") + String(sec) + L3(" с. Нажмите кнопку, чтобы отменить.", " s. Press the button to cancel.", " 秒。按按钮取消。"), C_YELLOW);
    uint32_t t0 = millis();
    while (millis() - t0 < 1000) {
      if (btnEvent() != EV_NONE) return false;
      delay(10);
    }
  }
  return true;
}

// Показать объявление и выполнить команду админа. Вызывается там, где можно занять экран
// (главный экран, ввод текста, комната). true - экран занят, его нужно перерисовать.
bool admService() {
  sdRetryService();
  bool used = bleService();                          // заодно: уведомления с телефона по Bluetooth
  if (admPendingAnn.length()) {
    String t = admPendingAnn;
    admPendingAnn = "";
    wakeScreen();
    sndMessage();
    viewAnswer(String(L3("Объявление администратора\n\n", "Announcement from the administrator\n\n", "管理员公告\n\n")) + t, C_YELLOW);
    used = true;
  }
  if (admPendingCmd) {
    int k = admPendingCmd;
    admPendingCmd = 0;
    if (k == 1 && admCountdown(L3("Администратор просит обновить прошивку.", "The administrator asks to update the firmware.", "管理员要求更新固件。"))) {
      otaNoAsk = true;
      String r = otaFromInternet();
      otaNoAsk = false;
      if (r.length()) viewAnswer(r, C_WHITE);
    }
    if (k == 2 && admCountdown(L3("Администратор просит перезапустить гаджет.", "The administrator asks to restart the device.", "管理员要求重启设备。"))) {
      showMsg(L3("Перезапускаюсь...", "Restarting...", "正在重启……"), C_YELLOW);
      delay(500);
      ESP.restart();
    }
    used = true;
  }
  return used;
}

// Подписать и отправить команду. sub - «cmd», «ann», «bans» или «pin/<комната>».
bool admSend(const char* sub, const String& target, const char* cmd, const String& arg, bool retained) {
  if (!admOn) return false;
  if (!mqUp) {
    if (!waitWifi(false)) return false;
    showMsg(L3("Подключаюсь к серверу...", "Connecting to the server...", "正在连接服务器……"), C_YELLOW);
    mqConnect(6000);
  }
  adm::Cmd c;
  c.ts = unixNow();
  if (!mqUp || !c.ts) {
    viewAnswer(c.ts ? L3("Нет связи с сервером.", "No connection to the server.", "无法连接服务器。")
                    : L3("Нет точного времени (нужен интернет). Попробуйте через минуту.", "The clock is not set yet (needs internet). Try again in a minute.", "时间未同步（需要网络），请稍后再试。"),
               C_RED);
    return false;
  }
  c.nonce = esp_random();
  c.target = toStd(target);
  c.cmd = cmd;
  c.arg = toStd(arg);
  std::string b = adm::body(c), sig;
  if (!adm::sign(admKey, b, sig, admRng, nullptr)) {
    viewAnswer(L3("Не удалось подписать команду.", "Could not sign the command.", "无法签名命令。"), C_RED);
    return false;
  }
  std::string topic = std::string(ADM_TOPIC) + sub, pl = adm::wrap(b, sig);
  bool ok = mqWrite(retained ? mqtt::publishRetained(topic, pl) : mqtt::publish(topic, pl));
  showMsg(ok ? L3("Отправлено.", "Sent.", "已发送。") : L3("Не отправилось: нет связи.", "Not sent: no connection.", "发送失败：无连接。"), ok ? C_GREEN : C_RED);
  delay(900);
  return ok;
}

void admSendBans() { admSend("bans", adm::ALL, "bans", fromStd(adm::joinList(roomBans)), true); }

void admToggleBan(const String& id) {
  admLoadState();
  std::string s = toStd(id);
  bool was = false;
  for (size_t i = 0; i < roomBans.size(); i++)
    if (roomBans[i] == s) {
      roomBans.erase(roomBans.begin() + (long)i);
      was = true;
      break;
    }
  if (!was) roomBans.push_back(s);
  admSendBans();
}

// Логин и пароль -> закрытый ключ. false - отменено.
bool admAskKey(uint8_t key[32]) {
  showMsg(L3("Логин администратора. Наберите и нажмите OK.", "Administrator login. Type it and press OK.", "管理员用户名，输入后按OK。"), C_YELLOW);
  delay(1300);
  String login = inputText();
  login.trim();
  if (!login.length()) return false;
  showMsg(L3("Пароль. Он виден на экране, пока вы его набираете - не делайте этого при посторонних.",
             "Password. It is visible while you type it - do not do this with people around.", "密码。输入时会显示在屏幕上，请避开他人。"), C_YELLOW);
  delay(2000);
  String pass = inputText();
  if (!pass.length()) return false;
  showMsg(L3("Проверяю...", "Checking...", "正在验证……"), C_YELLOW);
  bool ok = adm::deriveKey(toStd(login), toStd(pass), key);
  pass = "";
  return ok;
}

void admLogin() {
  if (!strlen(ADMIN_PUBKEY)) {
    viewAnswer(L3("Ключ администратора ещё не задан в прошивке. Сначала: Администратор -> Создать ключ.",
                  "The administrator key is not set in the firmware yet. First: Administrator -> Create key.", "固件中尚未设置管理员密钥。请先：管理员 -> 创建密钥。"),
               C_YELLOW);
    return;
  }
  uint8_t k[32];
  if (!admAskKey(k)) return;
  std::string pub;
  if (!adm::publicKey(k, pub, admRng, nullptr) || pub != ADMIN_PUBKEY) {
    memset(k, 0, sizeof(k));
    delay(2000);                                     // подбирать пароль медленно
    viewAnswer(L3("Неверный логин или пароль.", "Wrong login or password.", "用户名或密码错误。"), C_RED);
    return;
  }
  memcpy(admKey, k, 32);
  memset(k, 0, sizeof(k));
  admOn = true;
  admLoadState();
  if (mqUp) mqWrite(mqtt::subscribe(4, "dspocket/v1/+/p"));
  sndOk();
  showMsg(L3("Вы вошли как администратор (до перезапуска).", "Signed in as administrator (until restart).", "已以管理员身份登录（重启前有效）。"), C_GREEN);
  delay(1200);
}

void admCreateKey() {
  viewAnswer(L3("Придумайте логин и надёжный пароль администратора. Из них получится ключ: его нужно вписать в прошивку (ADMIN_PUBKEY). "
                "Ключ появится на экране и в мониторе порта (115200) - скопируйте его оттуда. Сам пароль никуда не записывается: не забудьте его.",
                "Choose an administrator login and a strong password. They give a key that goes into the firmware (ADMIN_PUBKEY). "
                "The key appears on the screen and in the serial monitor (115200) - copy it from there. The password is not stored anywhere: do not forget it.",
                "设置管理员用户名和强密码。由此生成的密钥需要写入固件（ADMIN_PUBKEY）。密钥会显示在屏幕和串口监视器（115200）中。密码不会被保存，请牢记。"),
             C_WHITE);
  uint8_t k[32];
  if (!admAskKey(k)) return;
  std::string pub;
  bool ok = adm::publicKey(k, pub, admRng, nullptr);
  memset(k, 0, sizeof(k));
  if (!ok) {
    viewAnswer(L3("Не удалось создать ключ.", "Could not create the key.", "无法创建密钥。"), C_RED);
    return;
  }
  Serial.println();
  Serial.print("ADMIN_PUBKEY: ");
  Serial.println(pub.c_str());
  viewAnswer(String(L3("Открытый ключ (он же - в мониторе порта):\n\n", "Public key (also in the serial monitor):\n\n", "公钥（串口监视器中也有）：\n\n")) + fromStd(pub) +
                 L3("\n\nЕго можно показывать кому угодно - по нему нельзя узнать пароль.", "\n\nIt is safe to share - the password cannot be recovered from it.", "\n\n公钥可以公开，无法据此推出密码。"),
             C_GREEN);
}

String admBatText(int b) { return b == -1 ? String("USB") : b >= 0 ? String(b) + "%" : String(""); }

// Действия с одним гаджетом.
void admDeviceMenu(const String& id, const String& nick) {
  for (;;) {
    bool banned = admBanned(id);
    std::vector<String> o = splitBar(L3("Обновить прошивку|Перезапустить|Выгнать из комнаты|Заблокировать в комнате|Назад",
                                        "Update the firmware|Restart|Remove from the room|Block in the room|Back", "更新固件|重启|移出房间|在房间中屏蔽|返回"));
    if (banned) o[3] = L3("Разблокировать", "Unblock", "解除屏蔽");
    int k = listScreen((nick + " " + id).c_str(), o, std::vector<String>(), 0);
    if (k == 0) admSend("cmd", id, "upd", "", false);
    else if (k == 1) admSend("cmd", id, "rbt", "", false);
    else if (k == 2) admSend("cmd", id, "kick", "", false);
    else if (k == 3) admToggleBan(id);
    else return;
  }
}

void admDevices() {
  admSend("cmd", adm::ALL, "ping", "", false);       // все гаджеты на связи ответят «я здесь»
  showMsg(L3("Ищу гаджеты...", "Looking for devices...", "正在查找设备……"), C_YELLOW);
  uint32_t t0 = millis();
  while (millis() - t0 < 4000) {
    mqPoll();
    delay(20);
  }
  int sel = 0;
  for (;;) {
    std::vector<String> items, right;
    std::vector<size_t> idx;
    for (size_t i = 0; i < admDevs.size(); i++) {
      if (millis() - admDevs[i].seen > 180000UL) continue;
      String n = admDevs[i].nick.length() ? admDevs[i].nick : admDevs[i].id;
      if (admDevs[i].id == roomId) n += L3(" (вы)", " (you)", "（你）");
      if (admBanned(admDevs[i].id)) n = String("x ") + n;
      items.push_back(n);
      String r = String("#") + admDevs[i].room;
      if (admDevs[i].fw) r += String(" v") + String(admDevs[i].fw);
      String bt = admBatText(admDevs[i].bat);
      if (bt.length()) r += String(" ") + bt;
      right.push_back(r);
      idx.push_back(i);
    }
    if (items.empty()) {
      viewAnswer(L3("Гаджетов на связи не видно. Команды получают гаджеты, у которых в Комнате задано имя и включены «Сообщения в фоне».",
                    "No devices online. Commands reach devices that have a name in the Room and \"Background messages\" turned on.",
                    "没有在线设备。只有在房间中设置了名字并开启“后台接收消息”的设备才能收到命令。"),
                 C_YELLOW);
      return;
    }
    items.push_back(L3("Назад", "Back", "返回"));
    right.push_back("");
    sel = listScreen(L3("Гаджеты на связи", "Devices online", "在线设备"), items, right, sel);
    if (sel < 0 || sel >= (int)idx.size()) return;
    AdmDev d = admDevs[idx[(size_t)sel]];
    admDeviceMenu(d.id, d.nick);
  }
}

void admRoomMessages() {
  int sel = 0;
  for (;;) {
    std::vector<String> items;
    std::vector<size_t> idx;
    for (size_t i = roomMsgs.size(); i-- > 0;) {     // новые сверху
      if (roomMsgs[i].mine) continue;
      items.push_back(roomMsgs[i].nick + ": " + roomMsgs[i].text);
      idx.push_back(i);
    }
    if (items.empty()) {
      viewAnswer(L3("Чужих сообщений в комнате пока нет. Список пополняется, пока гаджет на связи с комнатой.",
                    "No messages from others in the room yet. The list fills while the device is connected to the room.", "房间里还没有其他人的消息。"),
                 C_YELLOW);
      return;
    }
    items.push_back(L3("Назад", "Back", "返回"));
    sel = listScreen((String("#") + cfgRoom).c_str(), items, std::vector<String>(), sel);
    if (sel < 0 || sel >= (int)idx.size()) return;
    RoomMsg m = roomMsgs[idx[(size_t)sel]];
    std::vector<String> o = splitBar(L3("Удалить у всех|Заблокировать автора|Назад", "Delete for everyone|Block the author|Back", "为所有人删除|屏蔽作者|返回"));
    int k = choiceScreen(m.nick + ": " + m.text, o);
    if (k == 0) admSend("cmd", adm::ALL, "del", m.id + ":" + fromStd(adm::msgHash(toStd(m.id), toStd(m.text))), false);
    if (k == 1) {
      String id = m.id;
      if (id.endsWith("-ai")) id = id.substring(0, id.length() - 3);
      if (!admBanned(id)) admToggleBan(id);
    }
  }
}

void admBansMenu() {
  admLoadState();
  for (;;) {
    std::vector<String> items;
    for (const std::string& b : roomBans) {
      String n = fromStd(b);
      for (const AdmDev& d : admDevs)
        if (d.id == n && d.nick.length()) n = d.nick + " " + n;
      items.push_back(n);
    }
    if (items.empty()) {
      viewAnswer(L3("Заблокированных нет.", "Nobody is blocked.", "没有被屏蔽的人。"), C_WHITE);
      return;
    }
    items.push_back(L3("Назад", "Back", "返回"));
    int k = listScreen(L3("Заблокированные (нажать - снять)", "Blocked (click to unblock)", "已屏蔽（点击解除）"), items, std::vector<String>(), 0);
    if (k < 0 || k >= (int)roomBans.size()) return;
    admToggleBan(fromStd(roomBans[(size_t)k]));
  }
}

void runAdminPanel() {
  int sel = 0;
  for (;;) {
    std::vector<String> items = splitBar(L3("Гаджеты на связи|Объявление всем|Сообщения комнаты|Закрепить в комнате|Заблокированные|Обновить все гаджеты|Перезапустить все|Выйти из админа|Назад",
                                            "Devices online|Announcement to all|Room messages|Pin in the room|Blocked|Update all devices|Restart all|Sign out|Back",
                                            "在线设备|全体公告|房间消息|房间置顶|已屏蔽|更新所有设备|重启所有设备|退出管理员|返回"));
    sel = listScreen(L3("Администратор", "Administrator", "管理员"), items, std::vector<String>(), sel);
    if (sel == 0) {
      admDevices();
    } else if (sel == 1) {
      showMsg(L3("Текст объявления. Его увидят все гаджеты на связи (и те, кто подключится позже).",
                 "Announcement text. All online devices will see it (and those that connect later).", "公告内容。所有在线设备都会看到（稍后连接的也会）。"),
              C_YELLOW);
      delay(1600);
      String t = inputText();
      t.trim();
      if (t.length()) admSend("ann", adm::ALL, "ann", t, true);
    } else if (sel == 2) {
      admRoomMessages();
    } else if (sel == 3) {
      showMsg(L3("Закреплённое сообщение для этой комнаты. Пустое - снять.", "Pinned message for this room. Empty - remove it.", "本房间置顶消息。留空则取消。"), C_YELLOW);
      delay(1400);
      pendingInput = roomPin;
      String t = inputText();
      t.trim();
      String room = fromStd(mqtt::cleanRoom(toStd(cfgRoom)));
      admSend((String("pin/") + room).c_str(), room, "pin", t, true);
    } else if (sel == 4) {
      admBansMenu();
    } else if (sel == 5 || sel == 6) {
      std::vector<String> yn = splitBar(L3("Да|Нет", "Yes|No", "是|否"));
      String q = sel == 5 ? L3("Попросить все гаджеты обновить прошивку? Владельцы смогут отменить.", "Ask all devices to update the firmware? Owners can cancel.", "要求所有设备更新固件？设备主人可以取消。")
                          : L3("Перезапустить все гаджеты? Владельцы смогут отменить.", "Restart all devices? Owners can cancel.", "重启所有设备？设备主人可以取消。");
      if (choiceScreen(q, yn) == 0) admSend("cmd", adm::ALL, sel == 5 ? "upd" : "rbt", "", false);
    } else if (sel == 7) {
      admOn = false;
      memset(admKey, 0, sizeof(admKey));
      showMsg(L3("Вы вышли из режима администратора.", "Signed out.", "已退出管理员。"), C_GREEN);
      delay(1000);
      return;
    } else {
      return;
    }
  }
}

void runAdmin() {
  if (admOn) {
    runAdminPanel();
    return;
  }
  std::vector<String> o = splitBar(L3("Войти|Создать ключ|Назад", "Sign in|Create key|Back", "登录|创建密钥|返回"));
  int k = listScreen(L3("Администратор", "Administrator", "管理员"), o, std::vector<String>(), 0);
  if (k == 0) {
    admLogin();
    if (admOn) runAdminPanel();
  } else if (k == 1) {
    admCreateKey();
  }
}

#if BOARD_S3
// Настройки -> Наклон: уровень-«пузырёк», калибровка осей, управление наклоном в играх.
void accSaveCal() {
  prefs.begin("dsk", false);
  prefs.putShort("az0", accZero[0]);
  prefs.putShort("az1", accZero[1]);
  prefs.putShort("az2", accZero[2]);
  prefs.putChar("aax", accAxX);
  prefs.putChar("asx", accSgX);
  prefs.putChar("aay", accAxY);
  prefs.putChar("asy", accSgY);
  prefs.putChar("gax", gAxX);
  prefs.putChar("gsx", gSgX);
  prefs.putChar("gay", gAxY);
  prefs.putChar("gsy", gSgY);
  prefs.end();
}

// Ждать нажатия, показывая подсказку; вернуть средние показания за полсекунды.
float gyroTurn[3];                                  // на сколько градусов повернули гаджет, пока ждали нажатия
bool accCapture(const char* hint, int out[3]) {
  showMsg(hint, C_WHITE);
  for (int k = 0; k < 3; k++) gyroTurn[k] = 0;
  uint32_t t = millis();
  for (;;) {
    BtnEvent e = btnEvent();
    joyEvent();
    if (e == EV_LONG) return false;
    if (e == EV_CLICK) break;
    float g[3];
    uint32_t now = millis();
    if (gyroRead(g))
      for (int k = 0; k < 3; k++) gyroTurn[k] += g[k] * (now - t) / 1000.0f;
    t = now;
    delay(10);
  }
  long sum[3] = {0, 0, 0};
  int n = 0;
  for (int i = 0; i < 25; i++) {
    int v[3];
    if (accRaw(v)) {
      for (int k = 0; k < 3; k++) sum[k] += v[k];
      n++;
    }
    delay(20);
  }
  if (!n) return false;
  for (int k = 0; k < 3; k++) out[k] = (int)(sum[k] / n);
  return true;
}

void accCalibrate() {
  int flat[3], right[3], down[3];
  if (!accCapture(L3("1/3. Положите гаджет ровно экраном вверх и нажмите кнопку.", "1/3. Lay the device flat, screen up, and press the button.", "1/3. 平放后按键。"), flat)) return;
  if (gyroType) {                                    // гаджет лежит - заодно смещение покоя гироскопа
    float sum[3] = {0, 0, 0}, g[3];
    int n = 0;
    float save[3] = {gyroBias[0], gyroBias[1], gyroBias[2]};
    for (int k = 0; k < 3; k++) gyroBias[k] = 0;
    for (int i = 0; i < 60; i++) {
      if (gyroRead(g)) {
        for (int k = 0; k < 3; k++) sum[k] += g[k];
        n++;
      }
      delay(5);
    }
    for (int k = 0; k < 3; k++) gyroBias[k] = n ? sum[k] / n : save[k];
  }
  if (!accCapture(L3("2/3. Наклоните гаджет ВПРАВО (градусов на 30-45), держите и нажмите кнопку.", "2/3. Tilt the device to the RIGHT (30-45 degrees), hold and press.",
                     "2/3. 向右倾斜后按键。"), right))
    return;
  float turnRight[3] = {gyroTurn[0], gyroTurn[1], gyroTurn[2]};
  if (!accCapture(L3("3/3. Верните гаджет ровно, потом наклоните К СЕБЕ (нижним краем вниз), держите и нажмите кнопку.",
                     "3/3. Level the device again, then tilt it TOWARDS you (bottom edge down), hold and press.", "3/3. 放平后向自己倾斜，然后按键。"), down))
    return;
  float turnDown[3];
  for (int k = 0; k < 3; k++) turnDown[k] = gyroTurn[k] + turnRight[k];   // за шаг 3: -вправо + к себе
  auto pick = [&](const int a[3], int skip, int8_t& ax, int8_t& sg) {
    int best = -1, bv = 0;
    for (int k = 0; k < 3; k++) {
      if (k == skip) continue;
      int d = a[k] - flat[k];
      if (abs(d) > bv) { bv = abs(d); best = k; }
    }
    if (best < 0 || bv < 40) return false;          // почти не наклонили
    ax = (int8_t)best;
    sg = (int8_t)(a[best] - flat[best] > 0 ? 1 : -1);
    return true;
  };
  int8_t ax, sx, ay, sy;
  if (!pick(right, -1, ax, sx) || !pick(down, ax, ay, sy)) {
    viewAnswer(L3("Не понял наклон - наклоняйте сильнее. Попробуйте ещё раз.", "Could not detect the tilt - tilt more. Try again.", "未检测到倾斜，请再试一次。"), C_YELLOW);
    return;
  }
  for (int k = 0; k < 3; k++) accZero[k] = flat[k];
  accAxX = ax;
  accSgX = sx;
  accAxY = ay;
  accSgY = sy;
  if (gyroType) {                                    // оси гироскопа: где больше всего повернули
    int bx = 0, by = -1;
    for (int k = 1; k < 3; k++)
      if (fabsf(turnRight[k]) > fabsf(turnRight[bx])) bx = k;
    for (int k = 0; k < 3; k++)
      if (k != bx && (by < 0 || fabsf(turnDown[k]) > fabsf(turnDown[by]))) by = k;
    if (fabsf(turnRight[bx]) > 10 && fabsf(turnDown[by]) > 10) {
      gAxX = (int8_t)bx;
      gSgX = (int8_t)(turnRight[bx] > 0 ? 1 : -1);
      gAxY = (int8_t)by;
      gSgY = (int8_t)(turnDown[by] > 0 ? 1 : -1);
    } else {
      gAxX = gAxY = -1;                              // повернули слишком мало - без гироскопа
    }
  }
  accSaveCal();
  viewAnswer(L3("Готово! Наклон откалиброван.", "Done! Tilt calibrated.", "完成！"), C_GREEN);
}

void runTilt() {
  if (!accOk) {
    viewAnswer(BOARD_S3 ? L3("Датчик наклона не найден (ADXL345 или MPU6050). Подключение: SDA - 38, SCL - 39, VCC - 3V3, GND - GND; у ADXL345 ещё CS - 3V3, SDO - GND. Потом перезапустите гаджет.",
                             "ADXL345 not found. Wiring: SDA 38, SCL 39, VCC 3V3, GND GND, CS to 3V3 (I2C mode), SDO to GND. Then restart.", "未找到ADXL345。")
                        : L3("Датчик наклона подключается только к гаджету на ESP32-S3.", "The tilt sensor works only on the ESP32-S3 gadget.", "倾斜传感器仅支持ESP32-S3。"),
               C_YELLOW);
    return;
  }
  tft.fillScreen(C_BLACK);
  int bx = -1, by = -1;
  uint32_t lastTxt = 0;
  for (;;) {
    int tx, ty;
    if (accTilt(tx, ty)) {
      // пузырёк: круг радиуса 90, наклон 45 градусов (~181) - до края
      int nx = 120 + constrain(tx * 90 / 181, -90, 90), ny = 120 + constrain(ty * 90 / 181, -90, 90);
      if (nx != bx || ny != by) {
        if (bx >= 0) tft.fillCircle(bx, by, 12, C_BLACK);
        tft.drawCircle(120, 120, 102, C_LIGHT);
        tft.drawCircle(120, 120, 16, C_LIGHT);
        tft.drawFastHLine(18, 120, 204, 0x31A6);
        tft.drawFastVLine(120, 18, 204, 0x31A6);
        bool level = abs(tx) < 9 && abs(ty) < 9;
        tft.fillCircle(nx, ny, 12, level ? C_GREEN : C_YELLOW);
        bx = nx;
        by = ny;
      }
      if (millis() - lastTxt > 250) {
        lastTxt = millis();
        SmallFont small;
        char b[48];
        snprintf(b, sizeof(b), "X %+3d  Y %+3d     ", (int)(asinf(constrain(tx / 256.0f, -1.f, 1.f)) * 57.3f),
                 (int)(asinf(constrain(ty / 256.0f, -1.f, 1.f)) * 57.3f));
        drawUtf8(b, 4, 2, C_WHITE, C_BLACK);
        drawUtf8(cfgTilt ? L3("игры: наклон ВКЛ ", "games: tilt ON  ", "游戏：开") : L3("игры: наклон выкл", "games: tilt off ", "游戏：关"), 4, 222, cfgTilt ? C_GREEN : C_LIGHT, C_BLACK);
        String sn = String(accSrc == 1 ? "ADXL345" : "") + (gyroType ? String(accSrc == 1 ? "+" : "") + gyroName : String("")) +
                    (gyroType && gAxX < 0 ? L3(" (калибр.)", " (calib.)", "") : "");
        drawUtf8(sn.c_str(), 236 - textWidth(sn.c_str()), 2, 0x8C71, C_BLACK);
      }
    }
    joyEvent();
    BtnEvent e = btnEvent();
    if (e == EV_LONG) return;
    if (e == EV_CLICK) {
      std::vector<String> o = splitBar(cfgTilt ? L3("Выключить наклон в играх|Калибровать|Выйти", "Turn tilt off in games|Calibrate|Leave", "关闭|校准|退出")
                                               : L3("Включить наклон в играх|Калибровать|Выйти", "Turn tilt on in games|Calibrate|Leave", "开启|校准|退出"));
      int k = choiceScreen(L3("Наклон", "Tilt", "倾斜"), o);
      if (k == 0) {
        cfgTilt = !cfgTilt;
        saveScreenSettings();
      } else if (k == 1) {
        accCalibrate();
      } else if (k == 2) {
        return;
      }
      tft.fillScreen(C_BLACK);
      bx = by = -1;
    }
    delay(10);
  }
}

#else
void runTilt() {
  viewAnswer(L3("Датчик наклона подключается только к гаджету на ESP32-S3.", "The tilt sensor works only on the ESP32-S3 gadget.", "倾斜传感器仅支持ESP32-S3。"), C_YELLOW);
}
#endif

#if ENABLE_JOYCAL
// ---------- Калибровка джойстика ----------
// 1) ручку не трогать - центр и дрожание; 2) покрутить по кругу до упора - края хода; 3) проверка точкой.
void joyCalScreen() {
  // шаг 1: покой
  showMsg(L3("Калибровка джойстика\n\n1/2. Отпустите ручку и не трогайте 2 секунды...",
             "Joystick calibration\n\n1/2. Release the stick and do not touch it for 2 seconds...", "摇杆校准\n\n1/2. 请松开摇杆2秒……"), C_WHITE);
  delay(1200);
  long sx = 0, sy = 0;
  int nx0 = 4095, nx1 = 0, ny0 = 4095, ny1 = 0;
  const int N = 200;
  for (int i = 0; i < N; i++) {
    int x = analogRead(PIN_JOY_X), y = analogRead(PIN_JOY_Y);   // сырые замеры: нужно видеть и выбросы
    sx += x;
    sy += y;
    nx0 = min(nx0, x); nx1 = max(nx1, x);
    ny0 = min(ny0, y); ny1 = max(ny1, y);
    delay(5);
  }
  int cx = (int)(sx / N), cy = (int)(sy / N);
  int noiseX = (nx1 - nx0) / 2, noiseY = (ny1 - ny0) / 2;
  // шаг 2: края хода
  tft.fillScreen(C_BLACK);
  {
    SmallFont small;
    drawUtf8(L3("2/2. Покрутите ручку по кругу", "2/2. Rotate the stick in circles", "2/2. 转动摇杆画圈"), 4, 4, C_WHITE, C_BLACK);
    drawUtf8(L3("до упора 3-4 раза, потом нажмите.", "to the edge 3-4 times, then press.", "到底3-4圈，然后按下。"), 4, 22, C_WHITE, C_BLACK);
  }
  int x0 = cx, x1 = cx, y0 = cy, y1 = cy;
  uint32_t t0 = millis();
  while (btnEvent() != EV_NONE) delay(5);            // хвост прошлого нажатия
  for (;;) {
    int x = joyRead(PIN_JOY_X), y = joyRead(PIN_JOY_Y);
    x0 = min(x0, x); x1 = max(x1, x);
    y0 = min(y0, y); y1 = max(y1, y);
    static uint32_t tDraw = 0;
    if (millis() - tDraw > 80) {
      tDraw = millis();
      // рамка 200x150: прямоугольник - уже пройденный ход, точка - ручка сейчас
      const int FX = 20, FY = 60, FW = 200, FH = 150;
      tft.fillRect(FX, FY, FW, FH, C_BLACK);
      tft.drawRect(FX, FY, FW, FH, C_GRAY);
      auto mx = [&](int v) { return FX + (int)((long)v * (FW - 1) / 4095); };
      auto my = [&](int v) { return FY + (int)((long)v * (FH - 1) / 4095); };
      tft.drawRect(mx(x0), my(y0), max(1, mx(x1) - mx(x0)), max(1, my(y1) - my(y0)), C_GREEN);
      tft.fillCircle(mx(x), my(y), 4, C_YELLOW);
      char b[40];
      snprintf(b, sizeof(b), "X %4d..%4d  Y %4d..%4d", x0, x1, y0, y1);
      SmallFont small;
      drawUtf8(b, 4, 218, C_LIGHT, C_BLACK);
    }
    BtnEvent e = btnEvent();
    if (e == EV_LONG) return;                        // передумали - ничего не меняем
    if (e == EV_CLICK || millis() - t0 > 30000) break;
    delay(5);
  }
  // проверка и сохранение
  String rep;
  char b[96];
  bool okX = x1 - cx > 500 && cx - x0 > 500, okY = y1 - cy > 500 && cy - y0 > 500;
  if (!okX || !okY) {
    rep = L3("Калибровка не сохранена: ", "Calibration not saved: ", "校准未保存：");
    rep += !okY ? L3("ось Y почти не двигается в одну из сторон. ", "the Y axis barely moves to one side. ", "Y轴几乎不动。")
                : L3("ось X почти не двигается в одну из сторон. ", "the X axis barely moves to one side. ", "X轴几乎不动。");
    rep += L3("Крутите до упора; если не помогает - проверьте провод этой оси и питание джойстика (3V3, не 5V).",
              "Push to the edge; if it does not help, check this axis wire and the joystick power (3V3, not 5V).", "请检查接线。");
    viewAnswer(rep, C_YELLOW);
    return;
  }
  joyCx = cx; joyCy = cy;
  joyX0 = x0; joyX1 = x1; joyY0 = y0; joyY1 = y1;
  prefs.begin("dsk", false);
  prefs.putInt("jcx", cx); prefs.putInt("jcy", cy);
  prefs.putInt("jx0", x0); prefs.putInt("jx1", x1);
  prefs.putInt("jy0", y0); prefs.putInt("jy1", y1);
  prefs.end();
  rep = L3("Готово, калибровка сохранена.\n\n", "Done, calibration saved.\n\n", "完成，已保存。\n\n");
  snprintf(b, sizeof(b), "X: %d..%d..%d\nY: %d..%d..%d\n", x0, cx, x1, y0, cy, y1);
  rep += b;
  snprintf(b, sizeof(b), "%s X ±%d, Y ±%d\n", L3("Дрожание в покое:", "Rest noise:", "静止抖动:"), noiseX, noiseY);
  rep += b;
  int thrRaw = joyThr();                             // порог срабатывания в той же шкале (из 2048)
  int worst = max(noiseX * 2048 / max(1, min(x1 - cx, cx - x0)), noiseY * 2048 / max(1, min(y1 - cy, cy - y0)));
  if (worst > thrRaw / 2) {
    rep += L3("\nДжойстик сильно шумит",
              "\nThe joystick is noisy", "\n摇杆噪声大");
    rep += noiseY >= noiseX ? L3(" по оси Y", " on the Y axis", "（Y轴）") : L3(" по оси X", " on the X axis", "（X轴）");
    rep += L3(": это плохой контакт провода или изношенный резистор в ручке. Пропаяйте провод этой оси, "
              "поставьте конденсатор 0,1 мкФ с вывода оси на GND. Пока помогает чувствительность пониже (Настройки -> Джойстик).",
              ": a bad wire contact or a worn pot. Resolder that axis wire and add a 0.1 uF capacitor from the axis pin to GND. "
              "Lower sensitivity helps meanwhile.", "：请检查焊点，并加0.1uF电容。");
  } else {
    rep += L3("\nШум в норме.", "\nNoise is fine.", "\n噪声正常。");
  }
  viewAnswer(rep, C_GREEN);
}

// Проверка ручки: точка ходит за ручкой, рамка - порог срабатывания. Выход - удержание кнопки.
void joyTestScreen() {
  tft.fillScreen(C_BLACK);
  {
    SmallFont small;
    drawUtf8(L3("Проверка. Выход - удержание.", "Test. Hold the button to exit.", "测试。长按退出。"), 4, 4, C_WHITE, C_BLACK);
  }
  int px = -1, py = -1;
  for (;;) {
    int dx, dy;
    joyAnalog(dx, dy);
    const int CX = 120, CY = 128, R = 90;
    int x = CX + dx * R / 2048, y = CY + dy * R / 2048;
    if (x != px || y != py) {
      tft.fillRect(CX - R - 6, CY - R - 6, 2 * R + 13, 2 * R + 13, C_BLACK);
      tft.drawRect(CX - R, CY - R, 2 * R + 1, 2 * R + 1, C_GRAY);
      int t = joyThr() * R / 2048;
      tft.drawRect(CX - t, CY - t, 2 * t + 1, 2 * t + 1, 0x03FF);
      tft.fillCircle(x, y, 5, (abs(dx) > joyThr() || abs(dy) > joyThr()) ? C_GREEN : C_YELLOW);
      px = x;
      py = y;
      char b[32];
      snprintf(b, sizeof(b), "dx %5d  dy %5d", dx, dy);
      SmallFont small;
      drawUtf8(b, 4, 222, C_LIGHT, C_BLACK);
    }
    if (btnEvent() == EV_LONG) return;
    delay(20);
  }
}
#endif

bool runSettings() {
  bool changed = false;
  int sel = 0;
  for (;;) {
    std::vector<String> items = splitBar(L3("Язык|Яркость|Звук|Гасить экран|Заставка|Часовой пояс|Джойстик|Фонарик|Wi-Fi и ключ|Забыть слова|Обновление|Ось Y джойстика|Администратор|Поворот экрана|Bluetooth|SD-карта|Наклон|Сенсорные кнопки|Назад",
                                            "Language|Brightness|Sound|Screen off|Screensaver|Time zone|Joystick|Flashlight|Wi-Fi and key|Forget words|Update|Joystick Y axis|Administrator|Screen rotation|Bluetooth|SD card|Tilt|Touch buttons|Back",
                                            "语言|亮度|声音|自动熄屏|屏保|时区|摇杆|手电筒|Wi-Fi和密钥|清除词语|更新|摇杆Y轴|管理员|屏幕旋转|蓝牙|SD卡|倾斜|触摸按键|返回"));
    std::vector<String> right;
    right.push_back(T().langName);
    right.push_back(String(BRIGHT_PCT[cfgBright]) + "%");
    right.push_back(cfgSound == 0 ? L3("нет", "off", "关") : cfgSound == 1 ? L3("сигналы", "signals", "提示音") : L3("всё", "all", "全部"));
    static const char* IDLE_RU[] = {"нет", "30 с", "1 мин", "2 мин", "5 мин"};
    static const char* IDLE_EN[] = {"never", "30 s", "1 min", "2 min", "5 min"};
    right.push_back(cfgLang == LANG_RU ? IDLE_RU[cfgIdle] : IDLE_EN[cfgIdle]);
    right.push_back(cfgSaver ? L3("часы", "clock", "时钟") : L3("нет", "off", "关"));
    right.push_back(String("UTC") + (cfgTz >= 0 ? "+" : "") + String(cfgTz));
    right.push_back(String(L3("чувств. ", "sens. ", "灵敏度 ")) + String(cfgJoy + 1) + "/5");
    right.push_back(torchOn ? L3("вкл", "on", "开") : L3("выкл", "off", "关"));
    right.push_back(String((int)netSsid.size()) + L3(" сет.", " net.", " 个"));
    right.resize(items.size());
    right[11] = cfgJoyInvY ? L3("обратная", "inverted", "反向") : L3("обычная", "normal", "正常");
    right[12] = admOn ? L3("вы админ", "signed in", "已登录") : "";
    right[13] = String(cfgRot * 90) + "\xC2\xB0";
    prefs.begin("dsk", true);
    int sdDiag = prefs.getInt("sddiag", -9);
    prefs.end();
    right[15] = !cfgSd ? (sdDiag != -9 ? String(L3("сбой ", "fail ", "失败 ")) + String(sdDiag) : String(L3("нет", "off", "关"))) : sdOk ? L3("работает", "ok", "正常") : L3("не найдена", "not found", "未找到");
    right[14] = !ENABLE_BLE ? L3("нет", "n/a", "无") : cfgBle ? L3("вкл", "on", "开") : L3("выкл", "off", "关");
    right[16] = !accOk ? L3("нет датчика", "no sensor", "无传感器") : cfgTilt ? L3("в играх", "in games", "游戏中") : L3("выкл", "off", "关");
    right[17] = !BOARD_S3 ? L3("нет", "n/a", "无") : cfgTouch ? L3("вкл", "on", "开") : L3("выкл", "off", "关");
    sel = listScreen(L3("Настройки", "Settings", "设置"), items, right, sel);
    switch (sel) {
      case 0:
        cfgLang = (cfgLang + 1) % (ENABLE_CJK ? N_LANGS : 2);
        saveLang();
        changed = true;
        break;
      case 1:
        cfgBright = (cfgBright + 1) % 5;
        applyBrightness();
        saveScreenSettings();
        break;
      case 2:
        cfgSound = (cfgSound + 1) % 3;
        saveScreenSettings();
        sndOk();                                     // сразу слышно, что выбрано
        break;
      case 3:
        cfgIdle = (cfgIdle + 1) % 5;
        saveScreenSettings();
        break;
      case 4:
        cfgSaver = !cfgSaver;
        saveScreenSettings();
        break;
      case 5:
        cfgTz = cfgTz >= 12 ? -11 : cfgTz + 1;
        saveScreenSettings();
        if (ntpStarted) startNtp();
        break;
      case 6: {
#if !ENABLE_JOYCAL
        cfgJoy = (cfgJoy + 1) % 5;
        saveScreenSettings();
#else
        for (;;) {
          std::vector<String> o;
          o.push_back(String(L3("Чувствительность: ", "Sensitivity: ", "灵敏度: ")) + String(cfgJoy + 1) + "/5");
          o.push_back(L3("Калибровка", "Calibrate", "校准"));
          o.push_back(L3("Проверка", "Test", "测试"));
          o.push_back(L3("Назад", "Back", "返回"));
          int k = choiceScreen(L3("Джойстик", "Joystick", "摇杆"), o);
          if (k == 0) {
            cfgJoy = (cfgJoy + 1) % 5;               // 1 - отклонять до упора, 5 - хватает лёгкого наклона
            saveScreenSettings();
          } else if (k == 1) {
            joyCalScreen();
          } else if (k == 2) {
            joyTestScreen();
          } else {
            break;
          }
        }
#endif
        break;
      }
      case 7:
        if (!ENABLE_TORCH) {
          notInEdition();
          break;
        }
        if (cfgSd && !BOARD_S3) {
          viewAnswer(L3("Вывод фонарика занят SD-картой.", "The torch pin is used by the SD card.", "手电筒引脚已被SD卡占用。"), C_YELLOW);
          break;
        }
        torchOn = !torchOn;                          // после перезапуска фонарик всегда выключен
        digitalWrite(PIN_LED, torchOn ? HIGH : LOW);
        break;
      case 8:
        runPortal();
        netIdx = 0;
        wifiStart();
        return true;
      case 9:
        sugg.clearLearned();
        saveLearned();
        showMsg(T().forgetDone, C_GREEN);
        delay(1200);
        break;
      case 10:
        runUpdate();
        break;
      case 11:
        cfgJoyInvY = !cfgJoyInvY;
        saveScreenSettings();
        break;
      case 12:
        runAdmin();
        break;
      case 15: {
#if BOARD_S3
        if (cfgSd && !sdOk) {                        // карта включена, но не нашлась - предлагаем проверку
          std::vector<String> ch = splitBar(L3("Проверить карту и провода|Выключить SD-карту|Назад",
                                               "Check the card and wires|Turn the SD card off|Back", "检查|关闭|返回"));
          int c = choiceScreen(L3("SD-карта не найдена", "SD card not found", "未找到SD卡"), ch);
          if (c == 0) {
            sdWiringTest();
            break;
          }
          if (c != 1) break;
        }
#endif
        std::vector<String> yn = splitBar(L3("Да, перезапустить|Нет", "Yes, restart|No", "是，重启|否"));
        String q = cfgSd ? (BOARD_S3 ? L3("Выключить SD-карту? Нужен перезапуск.", "Turn the SD card off? Needs a restart.", "关闭SD卡？需要重启。")
                                     : L3("Выключить SD-карту? Вернутся пищалка, фонарик и ИК-пульт (если они подключены). Нужен перезапуск.",
                              "Turn the SD card off? The buzzer, torch and IR remote come back (if wired). Needs a restart.", "关闭SD卡？需要重启。"))
                         : BOARD_S3 ? L3("Включить SD-карту? Её выводы: CS - GPIO4, SCK - GPIO5, MOSI - GPIO6, MISO - GPIO13. "
                              "Сенсорные кнопки на этих выводах отключатся. Нужен перезапуск.",
                              "Turn the SD card on? Pins: CS GPIO4, SCK GPIO5, MOSI GPIO6, MISO GPIO13. "
                              "Touch buttons on these pins are turned off. Needs a restart.", "开启SD卡？需要重启。")
                         : L3("Включить SD-карту? Её выводы: SCK - GPIO8, MOSI - GPIO2, MISO - GPIO20, CS - GPIO9 (+10 кОм от GPIO2 и GPIO9 к 3V3). "
                              "Пищалка, фонарик и ИК-пульт отключатся. Нужен перезапуск.",
                              "Turn the SD card on? Pins: SCK GPIO8, MOSI GPIO2, MISO GPIO20, CS GPIO9 (+10k from GPIO2 and GPIO9 to 3V3). "
                              "The buzzer, torch and IR remote are turned off. Needs a restart.", "开启SD卡？需要重启。");
        if (choiceScreen(q, yn) == 0) {
          cfgSd = !cfgSd;
          saveScreenSettings();
          showMsg(L3("Перезапускаюсь...", "Restarting...", "正在重启……"), C_YELLOW);
          delay(800);
          ESP.restart();
        }
        break;
      }
      case 14:
        if (ENABLE_BLE) bleSetEnabled(!cfgBle);
        else viewAnswer(L3("Bluetooth не помещается в эту прошивку (ENABLE_BLE 0).", "Bluetooth does not fit into this firmware (ENABLE_BLE 0).", "此固件不包含蓝牙。"), C_YELLOW);
        break;
      case 16:
        runTilt();
        break;
      case 17:
        if (!BOARD_S3) {
          viewAnswer(L3("Сенсорные кнопки работают только на гаджете с ESP32-S3.", "Touch buttons work only on the ESP32-S3 gadget.", "仅ESP32-S3支持。"), C_YELLOW);
          break;
        }
        if (cfgSd && !cfgTouch) {
          viewAnswer(L3("Площадки на выводах 4, 5, 6, 13 - сейчас они у SD-карты. Выключите SD-карту, чтобы проверить сенсорные кнопки.",
                        "The pads use pins 4, 5, 6, 13, now taken by the SD card. Turn the SD card off to try touch buttons.", "引脚被SD卡占用。"),
                     C_YELLOW);
          break;
        }
        cfgTouch = !cfgTouch;
        saveScreenSettings();
        if (cfgTouch) {
          viewAnswer(L3("Сенсорные кнопки: вверх - 4, вниз - 5, влево - 6, вправо - 13 (квадратики медного скотча ~12x12 мм). "
                        "Не трогайте площадки - сейчас запомню их «покой». Вибромотор - вывод 40 через транзистор.",
                        "Touch buttons: up 4, down 5, left 6, right 13 (copper tape squares ~12x12 mm). Do not touch them now - calibrating. Vibration motor: pin 40 via a transistor.",
                        "触摸按键：上4 下5 左6 右13。"),
                     C_WHITE);
          touchInit();
        } else {
          touchReady = false;
        }
        break;
      case 13:                                     // повернуть на 90° (экран квадратный - всё помещается)
        cfgRot = (cfgRot + 1) & 3;
        tft.setRotation((TFT_ROTATION + cfgRot) % 4);
        saveScreenSettings();
        break;
      default:
        return changed;
    }
  }
}

// ========================= ПОВТОРИ =========================
games2::simon::Game smn;
// Кнопки: 0 вверх (зелёная), 1 вправо (красная), 2 вниз (синяя), 3 влево (жёлтая).
const uint16_t SIMON_TONE[4] = {392, 330, 262, 196};
void drawSimonPad(int d, bool lit) {
  static const uint16_t DIM[4] = {0x0320, 0x6000, 0x000C, 0x6300}, LIT[4] = {0x07E0, 0xF800, 0x441F, 0xFFE0};
  static const int X[4] = {85, 160, 85, 10}, Y[4] = {30, 100, 170, 100};
  tft.fillRect(X[d], Y[d], 70, 60, lit ? LIT[d] : DIM[d]);
}

void drawSimonCenter(const char* s, uint16_t col) {
  tft.fillRect(85, 100, 70, 60, C_BLACK);
  drawUtf8(s, 120 - textWidth(s) / 2, 118, col, C_BLACK);
}

void runSimon() {
  smn.rnd = gameRandom;
  for (;;) {
    smn.reset();
    tft.fillScreen(C_BLACK);
    for (int d = 0; d < 4; d++) drawSimonPad(d, false);
    drawSmallCentered(L3("Запомните и повторите", "Watch and repeat", "记住并重复"), 2, C_LIGHT);
    bool quit = false;
    while (!smn.over && !quit) {
      {                                              // показ последовательности
        KeepAwake awake;
        char b[8];
        snprintf(b, sizeof(b), "%d", smn.len);
        drawSimonCenter(b, C_WHITE);
        delay(500);
        for (int i = 0; i < smn.len; i++) {
          drawSimonPad(smn.seq[i], true);
          if (cfgSound >= 1 && !screenAsleep) sndTone(SIMON_TONE[smn.seq[i]]);   // у каждой кнопки своя нота
          delay((uint32_t)smn.showMs());
          sndTone(0);
          drawSimonPad(smn.seq[i], false);
          delay(120);
          if (btnEvent() == EV_LONG) return;
        }
        while (joyRaw() != J_NONE) delay(5);         // ручка должна вернуться в центр
      }
      for (;;) {                                     // ответ игрока
        if (btnEvent() == EV_LONG) return;
        uint8_t j = joyEvent();
        if (j != J_NONE && !g_joyRepeated) {
          int d = j == J_UP ? 0 : j == J_RIGHT ? 1 : j == J_DOWN ? 2 : 3;
          drawSimonPad(d, true);
          if (cfgSound >= 1 && !screenAsleep) sndTone(SIMON_TONE[d]);
          delay(140);
          sndTone(0);
          drawSimonPad(d, false);
          int r = smn.press(d);
          if (r == 0 || r == 2) break;
        }
        delay(2);
      }
      if (!smn.over) {
        drawSimonCenter("OK", C_GREEN);
        delay(350);
      }
    }
    saveBestKey("bsimon", (uint32_t)smn.score());
    char b[48];
    snprintf(b, sizeof(b), T().gameScore, (unsigned)smn.score());
    if (!resultBox(T().gameOver, b, C_RED)) return;
  }
}

// ========================= САПЁР =========================
games2::mines::Game mns;
const int MN_CELL = 22, MN_X0 = 21, MN_Y0 = 20;      // поле 9x9: 198x198

void drawMineCell(int x, int y, bool cur) {
  int px = MN_X0 + x * MN_CELL, py = MN_Y0 + y * MN_CELL;
  bool open = mns.open[y][x];
  uint16_t bg = open ? 0x2104 : 0x6B4D;
  if (open && mns.num[y][x] == games2::mines::MINE) bg = (x == mns.boomX && y == mns.boomY) ? C_RED : 0x2104;
  tft.fillRect(px, py, MN_CELL - 1, MN_CELL - 1, bg);
  if (open) {
    uint8_t n = mns.num[y][x];
    if (n == games2::mines::MINE) {
      tft.fillCircle(px + 10, py + 10, 6, C_BLACK);
      tft.fillRect(px + 8, py + 7, 2, 2, C_WHITE);
    } else if (n) {
      static const uint16_t NC[9] = {0, 0x5D7F, 0x07E0, 0xFA08, 0xA2BF, 0xFC00, 0x07FF, 0xFFFF, 0xAD55};
      SmallFont small;
      drawCp((uint16_t)('0' + n), px + 7, py + 3, NC[n], bg);
    }
  } else if (mns.flag[y][x]) {
    tft.fillRect(px + 9, py + 4, 2, 14, C_BLACK);
    tft.fillTriangle(px + 11, py + 4, px + 11, py + 11, px + 17, py + 7, C_RED);
  } else if (mns.over && !mns.won && mns.num[y][x] == games2::mines::MINE) {
    tft.fillCircle(px + 10, py + 10, 5, C_BLACK);     // остальные мины после взрыва
  }
  if (cur) {
    tft.drawRect(px, py, MN_CELL - 1, MN_CELL - 1, C_YELLOW);
    tft.drawRect(px + 1, py + 1, MN_CELL - 3, MN_CELL - 3, C_YELLOW);
  }
}

void drawMineBar(uint32_t secs, bool exitSel) {
  SmallFont small;
  tft.fillRect(0, 0, 240, 18, C_BLACK);
  char b[48];
  snprintf(b, sizeof(b), "%s %d", L3("Мины:", "Mines:", "雷："), games2::mines::MINES - mns.flags);
  drawUtf8(b, 2, 1, C_WHITE, C_BLACK);
  snprintf(b, sizeof(b), "%u:%02u", (unsigned)(secs / 60), (unsigned)(secs % 60));
  drawUtf8(b, 240 - textWidth(b) - 2, 1, C_YELLOW, C_BLACK);
  tft.fillRect(0, 220, 240, 20, C_BLACK);
  const char* ex = L3("[ Выход ]", "[ Exit ]", "[ 退出 ]");
  int w = textWidth(ex);
  if (exitSel) tft.fillRect(236 - w - 4, 221, w + 8, 18, C_BLUE);
  drawUtf8(ex, 236 - w, 222, C_WHITE, exitSel ? C_BLUE : C_BLACK);
  if (!exitSel) drawUtf8(L3("Держать - флажок", "Hold - flag", "长按：插旗"), 2, 222, C_LIGHT, C_BLACK);
}

void runMines() {
  mns.rnd = gameRandom;
  const int W = games2::mines::W, H = games2::mines::H;
  for (;;) {
    mns.reset();
    int cx = 4, cy = 4;                              // cy == H - курсор на кнопке «Выход»
    uint32_t t0 = 0, shown = 0;
    tft.fillScreen(C_BLACK);
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++) drawMineCell(x, y, x == cx && y == cy);
    drawMineBar(0, false);
    while (!mns.over) {
      uint8_t j = joyEvent();
      if (j != J_NONE) {
        int ox = cx, oy = cy;
        if (j == J_LEFT) cx = (cx + W - 1) % W;
        if (j == J_RIGHT) cx = (cx + 1) % W;
        if (j == J_UP) cy = (cy + H) % (H + 1);
        if (j == J_DOWN) cy = (cy + 1) % (H + 1);
        if (oy < H) drawMineCell(ox, oy, false);
        if (cy < H) drawMineCell(cx, cy, true);
        if ((oy == H) != (cy == H)) drawMineBar(shown, cy == H);
      }
      BtnEvent e = btnEvent();
      if (e == EV_CLICK && cy == H) return;
      if (e == EV_CLICK && cy < H) {
        bool wasOpen = mns.open[cy][cx];
        int n = mns.reveal(cx, cy);
        if (!t0 && mns.placed) t0 = millis();
        if (n > 1 || wasOpen || mns.over) {
          for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) drawMineCell(x, y, x == cx && y == cy);
        } else {
          drawMineCell(cx, cy, true);
        }
      }
      if (e == EV_LONG && cy < H) {
        if (mns.toggleFlag(cx, cy)) {
          drawMineCell(cx, cy, true);
          drawMineBar(shown, false);
        }
      }
      if (t0 && (millis() - t0) / 1000 != shown) {
        shown = (millis() - t0) / 1000;
        drawMineBar(shown, cy == H);
      }
      delay(2);
    }
    uint32_t secs = t0 ? (millis() - t0) / 1000 : 0;
    delay(900);
    char b[48];
    snprintf(b, sizeof(b), "%u:%02u", (unsigned)(secs / 60), (unsigned)(secs % 60));
    if (mns.won) saveMinKey("bmines", secs ? secs : 1);
    if (!resultBox(mns.won ? L3("Победа!", "You win!", "胜利！") : L3("Взрыв!", "Boom!", "爆炸！"), b, mns.won ? C_GREEN : C_RED)) return;
  }
}

// ========================= АРКАНОИД =========================
games2::breakout::Game brk;
const int BRK_Y = 20;                                // поле начинается под строкой счёта

void drawBrick(int r, int c) {
  static const uint16_t COL[6] = {0xF800, 0xFC00, 0xFFE0, 0x07E0, 0x07FF, 0x441F};
  int x = games2::breakout::Game::brickX(c), y = BRK_Y + games2::breakout::Game::brickY(r);
  uint8_t v = brk.brick[r][c];
  tft.fillRect(x, y, games2::breakout::BW, games2::breakout::BH, v == 0 ? C_BLACK : v > 1 ? C_WHITE : COL[r]);
}

// Мяч стёр кусок кирпича - дорисовать кирпичи, которых коснулся прямоугольник.
void redrawBricksAt(int x, int y, int w, int h) {
  using namespace games2::breakout;
  for (int r = 0; r < NROWS; r++)
    for (int c = 0; c < NCOLS; c++) {
      if (!brk.brick[r][c]) continue;
      int bx = Game::brickX(c), by = Game::brickY(r);
      if (x + w > bx && x < bx + BW && y + h > by && y < by + BH) drawBrick(r, c);
    }
}

void drawBrkBar() {
  SmallFont small;
  tft.fillRect(0, 0, 240, 18, C_BLACK);
  char b[48];
  snprintf(b, sizeof(b), T().gameScore, (unsigned)brk.score);
  drawUtf8(b, 2, 1, C_WHITE, C_BLACK);
  snprintf(b, sizeof(b), "%s %d  \xE2\x80\xA2%d", L3("Ур.", "Lv.", "关"), brk.level, brk.lives);
  drawUtf8(b, 240 - textWidth(b) - 2, 1, C_YELLOW, C_BLACK);
}

void runBreakout() {
  using namespace games2::breakout;
  brk.rnd = gameRandom;
  for (;;) {
    brk.reset();
    bool redraw = true;
    int opx = 0, obx = 0, oby = 0;
    bool fire = false;
    uint32_t next = millis();
    while (!brk.over) {
      if (redraw) {
        tft.fillScreen(C_BLACK);
        drawBrkBar();
        for (int r = 0; r < NROWS; r++)
          for (int c = 0; c < NCOLS; c++) drawBrick(r, c);
        opx = (int)brk.px;
        obx = (int)brk.bx;
        oby = (int)brk.by;
        tft.fillRect(opx, BRK_Y + PY, PW, PH, C_WHITE);
        tft.fillRect(obx, BRK_Y + oby, BALL, BALL, C_YELLOW);
        drawSmallCentered(L3("Нажмите - запуск мяча", "Click to launch", "按下发球"), 150, C_LIGHT);
        redraw = false;
        next = millis();
      }
      BtnEvent e = btnEvent();
      if (e == EV_LONG) {
        saveBestKey("bbrk", brk.score);
        return;
      }
      if (e == EV_CLICK) fire = true;                 // нажатие не потеряется между кадрами
      if ((int32_t)(millis() - next) < 0) {
        delay(1);
        continue;
      }
      next += 16;
      int dx, dy;
      joyAnalog(dx, dy);
      float mv = dx / 1500.0f;
      if (mv > 1) mv = 1;
      if (mv < -1) mv = -1;
      if (mv > -0.25f && mv < 0.25f) mv = 0;
      bool wasStuck = brk.stuck;
      int res = brk.step(mv, fire);
      fire = false;
      if (wasStuck && !brk.stuck) tft.fillRect(0, 150, 240, 16, C_BLACK);
      if (res == 1) {
        drawBrick(brk.hitR, brk.hitC);
        drawBrkBar();
        beep(1, 1400 + brk.hitR * 120, 8);
      }
      if (res == 2 || res == 3) {
        drawBrkBar();
        delay(600);
        redraw = true;
        continue;
      }
      int npx = (int)brk.px, nbx = (int)brk.bx, nby = (int)brk.by;
      if (npx != opx) {
        tft.fillRect(opx, BRK_Y + PY, PW, PH, C_BLACK);
        tft.fillRect(npx, BRK_Y + PY, PW, PH, C_WHITE);
        opx = npx;
      }
      if (nbx != obx || nby != oby) {
        tft.fillRect(obx, BRK_Y + oby, BALL, BALL, C_BLACK);
        redrawBricksAt(obx, oby, BALL, BALL);
        if (oby + BALL >= PY) tft.fillRect(npx, BRK_Y + PY, PW, PH, C_WHITE);
        if (BRK_Y + nby + BALL <= 240) tft.fillRect(nbx, BRK_Y + nby, BALL, BALL, C_YELLOW);
        obx = nbx;
        oby = nby;
      }
    }
    saveBestKey("bbrk", brk.score);
    if (!gameOverBox(brk.score)) return;
  }
}

// ========================= ПРИШЕЛЬЦЫ =========================
games2::invaders::Game inv;
const int INV_Y = 20;

void drawAlien(int x, int y, int row, bool on) {
  using namespace games2::invaders;
  static const uint16_t COL[4] = {0xF81F, 0x07FF, 0x07E0, 0xFFE0};
  y += INV_Y;
  if (!on) {
    tft.fillRect(x, y, AW, AH, C_BLACK);
    return;
  }
  uint16_t c = COL[row % 4];
  tft.fillRect(x + 2, y, AW - 4, AH - 3, c);          // тело
  tft.fillRect(x, y + 3, AW, 3, c);                   // «руки»
  tft.fillRect(x + 4, y + 2, 2, 2, C_BLACK);          // глаза
  tft.fillRect(x + AW - 6, y + 2, 2, 2, C_BLACK);
  tft.fillRect(x + 2, y + AH - 3, 3, 3, c);           // ноги
  tft.fillRect(x + AW - 5, y + AH - 3, 3, 3, c);
}

void drawShip(int x, uint16_t col) {
  using namespace games2::invaders;
  tft.fillRect(x, INV_Y + SHIPY + 4, SHIPW, SHIPH - 4, col);
  tft.fillRect(x + SHIPW / 2 - 2, INV_Y + SHIPY, 4, 4, col);
}

// После стирания выстрела дорисовать пришельца, которого он задел.
void redrawAliensAt(int x, int y, int w, int h) {
  using namespace games2::invaders;
  for (int r = 0; r < AR; r++)
    for (int c = 0; c < AC; c++) {
      if (!inv.alive[r][c]) continue;
      int ax = inv.alienX(c), ay = inv.alienY(r);
      if (x + w > ax && x < ax + AW && y + h > ay && y < ay + AH) drawAlien(ax, ay, r, true);
    }
}

void drawInvBar() {
  SmallFont small;
  tft.fillRect(0, 0, 240, 18, C_BLACK);
  char b[48];
  snprintf(b, sizeof(b), T().gameScore, (unsigned)inv.score);
  drawUtf8(b, 2, 1, C_WHITE, C_BLACK);
  snprintf(b, sizeof(b), "%s %d  \xE2\x80\xA2%d", L3("Волна", "Wave", "波"), inv.wave, inv.lives);
  drawUtf8(b, 240 - textWidth(b) - 2, 1, C_YELLOW, C_BLACK);
}

void runInvaders() {
  using namespace games2::invaders;
  inv.rnd = gameRandom;
  for (;;) {
    inv.reset();
    bool redraw = true;
    int ogx = 0, ogy = 0, osx = 0;
    bool wasAlive[AR][AC];
    Shot oshot = {0, 0, false}, obomb[MAXSHOTS];
    uint32_t next = millis();
    bool fireHeld = false;
    while (!inv.over) {
      if (redraw) {
        tft.fillScreen(C_BLACK);
        drawInvBar();
        for (int r = 0; r < AR; r++)
          for (int c = 0; c < AC; c++) {
            wasAlive[r][c] = inv.alive[r][c];
            if (inv.alive[r][c]) drawAlien(inv.alienX(c), inv.alienY(r), r, true);
          }
        ogx = inv.gx;
        ogy = inv.gy;
        osx = inv.shipX;
        drawShip(osx, C_WHITE);
        oshot.on = false;
        for (auto& b : obomb) b.on = false;
        redraw = false;
        next = millis();
      }
      BtnEvent e = btnEvent();
      if (e == EV_LONG) {
        saveBestKey("binv", inv.score);
        return;
      }
      if (e == EV_CLICK) fireHeld = true;             // выстрел не потеряется между кадрами
      if ((int32_t)(millis() - next) < 0) {
        delay(1);
        continue;
      }
      next += 33;                                     // 30 кадров в секунду
      uint8_t raw = joyRaw();
      int mv = raw == J_LEFT ? -1 : raw == J_RIGHT ? 1 : 0;
      int res = inv.step(mv, fireHeld);
      fireHeld = false;
      if (res == 3 || (res == 2 && !inv.over)) {
        if (res == 2) {                               // попали в игрока - мигнуть
          drawShip(osx, C_RED);
          delay(500);
        } else {
          delay(400);
        }
        redraw = true;
        continue;
      }
      // выстрел игрока
      if (oshot.on) {
        tft.fillRect(oshot.x, INV_Y + oshot.y, 2, 6, C_BLACK);
        redrawAliensAt(oshot.x, oshot.y, 2, 6);
      }
      // сетка пришельцев
      if (inv.gridMoved) {
        for (int r = 0; r < AR; r++)
          for (int c = 0; c < AC; c++)
            if (wasAlive[r][c]) drawAlien(ogx + c * ASX, ogy + r * ASY, r, false);
        for (int r = 0; r < AR; r++)
          for (int c = 0; c < AC; c++) {
            wasAlive[r][c] = inv.alive[r][c];
            if (inv.alive[r][c]) drawAlien(inv.alienX(c), inv.alienY(r), r, true);
          }
        ogx = inv.gx;
        ogy = inv.gy;
      } else if (res == 1) {
        for (int r = 0; r < AR; r++)
          for (int c = 0; c < AC; c++)
            if (wasAlive[r][c] && !inv.alive[r][c]) {
              drawAlien(inv.alienX(c), inv.alienY(r), r, false);
              wasAlive[r][c] = false;
            }
      }
      if (res == 1) {
        drawInvBar();
        beep(1, 900, 10);
      }
      if (inv.shot.on) tft.fillRect(inv.shot.x, INV_Y + inv.shot.y, 2, 6, C_WHITE);
      oshot = inv.shot;
      // бомбы
      for (int i = 0; i < MAXSHOTS; i++) {
        if (obomb[i].on) {
          tft.fillRect(obomb[i].x, INV_Y + obomb[i].y, 2, 6, C_BLACK);
          redrawAliensAt(obomb[i].x, obomb[i].y, 2, 6);
        }
        if (inv.bomb[i].on && INV_Y + inv.bomb[i].y + 6 <= 240) tft.fillRect(inv.bomb[i].x, INV_Y + inv.bomb[i].y, 2, 6, C_RED);
        obomb[i] = inv.bomb[i];
      }
      // корабль
      if (inv.shipX != osx) {
        drawShip(osx, C_BLACK);
        osx = inv.shipX;
      }
      drawShip(osx, C_WHITE);
    }
    saveBestKey("binv", inv.score);
    if (!gameOverBox(inv.score)) return;
  }
}

// ========================= КРЕСТИКИ-НОЛИКИ =========================
const int TT_X0 = 24, TT_Y0 = 36, TT_CELL = 64;

void drawTttCell(const uint8_t* b, int i, bool cur, uint16_t markCol) {
  int x = TT_X0 + (i % 3) * TT_CELL, y = TT_Y0 + (i / 3) * TT_CELL;
  tft.fillRect(x + 2, y + 2, TT_CELL - 4, TT_CELL - 4, cur ? 0x18C3 : C_BLACK);
  if (cur) tft.drawRect(x + 2, y + 2, TT_CELL - 4, TT_CELL - 4, C_YELLOW);
  if (b[i] == 1) {                                    // крестик
    uint16_t c = markCol ? markCol : 0x5D7F;
    for (int k = -2; k <= 2; k++) {
      tft.drawLine(x + 14 + k, y + 14, x + TT_CELL - 14 + k, y + TT_CELL - 14, c);
      tft.drawLine(x + TT_CELL - 14 + k, y + 14, x + 14 + k, y + TT_CELL - 14, c);
    }
  } else if (b[i] == 2) {                             // нолик
    uint16_t c = markCol ? markCol : 0xFA08;
    tft.fillCircle(x + TT_CELL / 2, y + TT_CELL / 2, 20, c);
    tft.fillCircle(x + TT_CELL / 2, y + TT_CELL / 2, 15, cur ? 0x18C3 : C_BLACK);
  }
}

void runTtt() {
  std::vector<String> lv = splitBar(L3("Легко|Нормально|Сложно|Назад", "Easy|Normal|Hard|Back", "简单|普通|困难|返回"));
  int level = listScreen(L3("Крестики-нолики", "Tic-tac-toe", "井字棋"), lv, std::vector<String>(), 1);
  if (level < 0 || level > 2) return;
  int wins = 0, losses = 0;
  bool playerFirst = true;
  for (;;) {
    uint8_t b[9] = {0};
    int cur = 4;
    tft.fillScreen(C_BLACK);
    {
      SmallFont small;
      char s[64];
      snprintf(s, sizeof(s), "%s %d : %d %s", T().pongYou, wins, losses, T().pongCpu);
      drawUtf8(s, (240 - textWidth(s)) / 2, 4, C_WHITE, C_BLACK);
    }
    for (int k = 1; k < 3; k++) {                     // сетка
      tft.fillRect(TT_X0 + k * TT_CELL - 1, TT_Y0, 3, 3 * TT_CELL, C_LIGHT);
      tft.fillRect(TT_X0, TT_Y0 + k * TT_CELL - 1, 3 * TT_CELL, 3, C_LIGHT);
    }
    if (!playerFirst) b[games2::ttt::cpuMove(b, level, gameRandom)] = 2;
    for (int i = 0; i < 9; i++) drawTttCell(b, i, i == cur, 0);
    int w = 0;
    while (!(w = games2::ttt::winner(b))) {
      uint8_t j = joyEvent();
      if (j != J_NONE) {
        int old = cur, x = cur % 3, y = cur / 3;
        if (j == J_LEFT) x = (x + 2) % 3;
        if (j == J_RIGHT) x = (x + 1) % 3;
        if (j == J_UP) y = (y + 2) % 3;
        if (j == J_DOWN) y = (y + 1) % 3;
        cur = y * 3 + x;
        drawTttCell(b, old, false, 0);
        drawTttCell(b, cur, true, 0);
      }
      BtnEvent e = btnEvent();
      if (e == EV_LONG) return;
      if (e == EV_CLICK && !b[cur]) {
        b[cur] = 1;
        drawTttCell(b, cur, true, 0);
        if (games2::ttt::winner(b)) continue;
        delay(350);
        int m = games2::ttt::cpuMove(b, level, gameRandom);
        b[m] = 2;
        drawTttCell(b, m, m == cur, 0);
      }
      delay(2);
    }
    int line[3];
    if (games2::ttt::winLine(b, line))
      for (int k = 0; k < 3; k++) drawTttCell(b, line[k], false, C_GREEN);
    delay(900);
    if (w == 1) {
      wins++;
      saveBestKey("bttt", loadBestKey("bttt") + 1);
    }
    if (w == 2) losses++;
    playerFirst = !playerFirst;
    const char* title = w == 1 ? T().pongWin : w == 2 ? T().pongLose : L3("Ничья", "Draw", "平局");
    char s[24];
    snprintf(s, sizeof(s), "%d : %d", wins, losses);
    if (!resultBox(title, s, w == 1 ? C_GREEN : w == 2 ? C_RED : C_YELLOW)) return;
  }
}

// ========================= РЕКОРДЫ ИГРОКОВ =========================
// Лучшие результаты хранятся на сервере комнаты («сохраняемые» сообщения), по одному на игрока и игру:
// dspocket/v1/hs/<игра>/<метка>  =  «имя \t число».
struct HiScore {
  String key, id, nick;
  uint32_t value;
};
std::vector<HiScore> hiScores;
const char* const HS_KEYS[] = {"best", "b2048", "bsnake", "bfall", "bmaze", "bmem", "bpong", "bflap", "brace", "bbrk", "binv",
                               "bmines", "bsimon", "bttt", "bchess", "brider", "bfps", "bquiz", "bguess"};
const int HS_COUNT = sizeof(HS_KEYS) / sizeof(HS_KEYS[0]);

bool hsLowerBetter(const String& key) { return key == "bmem" || key == "bmines"; }

const int HS_COUNT_FWD = HS_COUNT;
uint32_t hsSent[HS_COUNT];                           // что уже отправлено серверу в этом подключении
String hsSentNick;

// Отправить очередной свой рекорд (если он изменился). По одному за вызов - сервер не любит залпы.
void hsSendNext() {
  if (!mqUp || !cfgNick.length()) {
    hsNext = HS_COUNT;
    return;
  }
  if (hsSentNick != cfgNick) {                       // сменилось имя - отправить всё заново
    hsSentNick = cfgNick;
    for (int i = 0; i < HS_COUNT; i++) hsSent[i] = 0;
  }
  while (hsNext < HS_COUNT) {
    int i = hsNext++;
    uint32_t v = loadBestKey(HS_KEYS[i]);
    if (!v || v == hsSent[i]) continue;
    char b[16];
    snprintf(b, sizeof(b), "%u", (unsigned)v);
    mqWrite(mqtt::publishRetained(std::string("dspocket/v1/hs/") + HS_KEYS[i] + "/" + toStd(roomId), toStd(cfgNick) + "\t" + b));
    hsSent[i] = v;
    hsLastSend = millis();
    return;
  }
}

// Отправить все свои рекорды сразу (перед показом таблицы).
void hsPublishAll() {
  hsDirty = false;
  hsNext = 0;
  while (hsNext < HS_COUNT) hsSendNext();
}

// Пришёл чей-то рекорд: «<игра>/<метка>» и «имя \t число».
void hsReceive(const std::string& rest, const std::string& payload) {
  size_t sl = rest.find('/'), tab = payload.find('\t');
  if (sl == std::string::npos || sl == 0 || sl > 10 || rest.size() - sl - 1 > 16 || rest.size() == sl + 1 || tab == std::string::npos) return;
  String key = fromStd(rest.substr(0, sl)), id = fromStd(rest.substr(sl + 1));
  uint32_t v = (uint32_t)strtoul(payload.c_str() + tab + 1, nullptr, 10);
  if (!v) return;
  String nick = fromStd(mqtt::clip(payload.substr(0, tab), 24));
  for (HiScore& h : hiScores)
    if (h.key == key && h.id == id) {
      h.nick = nick;
      h.value = v;
      return;
    }
  if (hiScores.size() >= 150) return;                // защита памяти
  HiScore h;
  h.key = key;
  h.id = id;
  h.nick = nick;
  h.value = v;
  hiScores.push_back(h);
}

const char* gameTitle(const char* key);              // название игры по ключу рекорда

void runHiScores() {
  if (!waitWifi(false)) return;
  if (!cfgNick.length() && !askNick()) return;
  if (!mqUp) {
    showMsg(L3("Загружаю рекорды...", "Loading records...", "正在加载记录……"), C_YELLOW);
    mqConnect(6000);
  }
  if (!mqUp) {
    viewAnswer(L3("Нет связи с сервером рекордов. Попробуйте позже.", "No connection to the records server. Try later.", "无法连接记录服务器。请稍后再试。"), C_RED);
    return;
  }
  hsPublishAll();
  uint32_t t0 = millis();
  while (millis() - t0 < 1500) {                     // даём серверу прислать сохранённые рекорды
    mqPoll();
    delay(20);
  }
  int sel = 0;
  for (;;) {
    std::vector<String> items, right;
    for (int i = 0; i < HS_COUNT; i++) {
      items.push_back(gameTitle(HS_KEYS[i]));
      int n = 0;
      for (const HiScore& h : hiScores)
        if (h.key == HS_KEYS[i]) n++;
      right.push_back(n ? String(n) : String(""));
    }
    items.push_back(T().menuBack);
    sel = listScreen(L3("Рекорды игроков", "Players' records", "玩家记录"), items, right, sel);
    if (sel < 0 || sel >= HS_COUNT) return;
    mqPoll();
    String key = HS_KEYS[sel];
    std::vector<int> idx;
    for (size_t i = 0; i < hiScores.size(); i++)
      if (hiScores[i].key == key) idx.push_back((int)i);
    bool low = hsLowerBetter(key);
    for (size_t a = 1; a < idx.size(); a++)             // сортировка: лучший сверху
      for (size_t b = a; b > 0; b--) {
        uint32_t x = hiScores[(size_t)idx[b - 1]].value, y = hiScores[(size_t)idx[b]].value;
        if (low ? x > y : x < y) std::swap(idx[b - 1], idx[b]);
        else break;
      }
    if (idx.empty()) {
      viewAnswer(L3("В этой игре рекордов пока нет. Сыграйте - и ваш результат появится здесь.", "No records in this game yet. Play - and your result will appear here.",
                    "这个游戏还没有记录。玩一局，你的成绩就会出现在这里。"), C_YELLOW);
      continue;
    }
    std::vector<String> rows, vals;
    for (size_t i = 0; i < idx.size() && i < 20; i++) {
      const HiScore& h = hiScores[(size_t)idx[i]];
      rows.push_back(String((int)i + 1) + ". " + h.nick + (h.id == roomId ? " *" : ""));
      vals.push_back(String((unsigned)h.value));
    }
    listScreen(gameTitle(HS_KEYS[sel]), rows, vals, 0);
  }
}

// ========================= РАЙДЕР =========================
// Рисование без мерцания: любой кусок экрана сначала целиком собирается в памяти
// (небо, земля, мотоцикл), а потом одним махом уходит на экран. Земля не пересчитывается
// каждый кадр: столбцы просто сдвигаются, считаются только новые справа.
rider::Game rdr;
const uint16_t C_RD_SKY = 0x1926, C_RD_GROUND = 0x3D03;
const int RD_BIKE_X = 70, RD_TOP = 18;
const int RD_BUF = 1536;
static uint16_t rdBuf[RD_BUF];
static int16_t rdCol[240];                               // высота земли в каждом столбце экрана
static int rdRX, rdRY, rdRW, rdRH;                       // кусок, который сейчас собирается
static int rdBikeY, rdWx, rdWy, rdUx, rdUy;              // мотоцикл: центр колёс и наклон
static long rdBarScore = -1;
static int rdBarSec = -1, rdBarW = 0;

void rdPlot(int x, int y, uint16_t c) {
  x -= rdRX;
  y -= rdRY;
  if (x >= 0 && x < rdRW && y >= 0 && y < rdRH) rdBuf[y * rdRW + x] = c;
}

void rdDisc(int cx, int cy, int r, uint16_t c) {
  for (int y = -r; y <= r; y++)
    for (int x = -r; x <= r; x++)
      if (x * x + y * y <= r * r) rdPlot(cx + x, cy + y, c);
}

void rdLine(int x0, int y0, int x1, int y1, uint16_t c) {
  int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
  int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
  for (;;) {
    rdPlot(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * e;
    if (e2 >= dy) {
      e += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      e += dx;
      y0 += sy;
    }
  }
}

void rdSetBike(int sy, float angle) {
  float c = cosf(angle), s = sinf(angle);
  rdWx = (int)(7 * c);                                   // колёса вдоль наклона
  rdWy = (int)(7 * s);
  rdUx = (int)(9 * s);                                   // «вверх» от мотоцикла
  rdUy = (int)(-9 * c);
  rdBikeY = sy - 4;                                      // центр колёс чуть выше земли
}

// Собрать кусок экрана в памяти и вывести. Строку счёта не трогает.
void rdRegion(int x0, int y0, int w, int h) {
  if (x0 < 0) {
    w += x0;
    x0 = 0;
  }
  if (y0 < RD_TOP) {
    h -= RD_TOP - y0;
    y0 = RD_TOP;
  }
  if (x0 + w > 240) w = 240 - x0;
  if (y0 + h > 240) h = 240 - y0;
  if (w <= 0 || h <= 0 || w > RD_BUF) return;
  int rows = RD_BUF / w;
  for (int y = y0; y < y0 + h; y += rows) {
    int n = y0 + h - y < rows ? y0 + h - y : rows;
    rdRX = x0;
    rdRY = y;
    rdRW = w;
    rdRH = n;
    for (int i = 0; i < w; i++) {
      int g = rdCol[x0 + i] - y;                         // с какой строки куска начинается земля
      uint16_t* p = rdBuf + i;
      for (int r = 0; r < n; r++, p += w) *p = r >= g ? C_RD_GROUND : C_RD_SKY;
    }
    const int sx = RD_BIKE_X, cy = rdBikeY;
    if (x0 <= sx + 14 && x0 + w > sx - 14 && y <= cy + 13 && y + n > cy - 16) {
      rdDisc(sx - rdWx, cy - rdWy, 4, C_WHITE);
      rdDisc(sx + rdWx, cy + rdWy, 4, C_WHITE);
      rdDisc(sx - rdWx, cy - rdWy, 2, C_BLACK);
      rdDisc(sx + rdWx, cy + rdWy, 2, C_BLACK);
      rdLine(sx - rdWx, cy - rdWy, sx + rdWx, cy + rdWy, C_YELLOW);
      rdLine(sx - rdWx, cy - rdWy, sx + rdUx / 2, cy + rdUy / 2, C_YELLOW);
      rdLine(sx + rdWx, cy + rdWy, sx + rdUx / 2, cy + rdUy / 2, C_YELLOW);
      rdDisc(sx + rdUx, cy + rdUy, 3, C_RED);
    }
    tft.startWrite();
    tft.setAddrWindow(x0, y, w, n);
    tft.writePixels(rdBuf, (uint32_t)(w * n));
    tft.endWrite();
  }
}

// Строка счёта: перерисовывается только то, что изменилось (full - целиком).
void drawRiderBar(bool full) {
  SmallFont small;
  if (full) {
    tft.fillRect(0, 0, 240, RD_TOP, C_BLACK);
    rdBarScore = -1;
    rdBarSec = -1;
    rdBarW = 0;
  }
  char b[48];
  long sc = (long)rdr.score();
  if (sc != rdBarScore) {
    rdBarScore = sc;
    snprintf(b, sizeof(b), T().gameScore, (unsigned)sc);
    drawUtf8(b, 2, 1, C_WHITE, C_BLACK);
  }
  int sec = (rdr.timeLeft + rider::FPS - 1) / rider::FPS;
  bool red = rdr.timeLeft < 10 * rider::FPS;
  if (sec * 2 + (red ? 1 : 0) != rdBarSec) {
    rdBarSec = sec * 2 + (red ? 1 : 0);
    snprintf(b, sizeof(b), "%d %s", sec, L3("с", "s", "秒"));
    int w = textWidth(b);
    if (w < rdBarW) tft.fillRect(238 - rdBarW, 0, rdBarW - w, RD_TOP, C_BLACK);   // число стало короче
    rdBarW = w;
    drawUtf8(b, 238 - w, 1, red ? C_RED : C_YELLOW, C_BLACK);
  }
}

void runRider() {
  if (!ENABLE_BIGGAMES) {
    notInEdition();
    return;
  }
  rdr.best = loadBestKey("brider");
  for (;;) {
    rdr.reset();
    tft.fillScreen(C_RD_SKY);
    int cam = (int)rdr.x - RD_BIKE_X;                    // камера двигается целыми точками
    for (int sx = 0; sx < 240; sx++) {
      int gy = (int)rider::ground((float)(cam + sx));
      rdCol[sx] = (int16_t)gy;
      tft.fillRect(sx, gy, 1, 240 - gy, C_RD_GROUND);
    }
    drawRiderBar(true);
    drawSmallCenteredBg(L3("Ручку в сторону - газ", "Push the stick - gas", "推摇杆：油门"), 26, C_WHITE, C_RD_SKY);
    drawSmallCenteredBg(L3("В воздухе газ = сальто", "In the air gas = flip", "空中油门=翻转"), 44, C_WHITE, C_RD_SKY);
    rdSetBike((int)rdr.y, rdr.angle);
    int oby = rdBikeY;
    rdRegion(RD_BIKE_X - 14, oby - 16, 29, 30);
    while (joyRaw() == J_NONE) {                        // старт по первому движению ручки
      if (btnEvent() == EV_LONG) return;
      delay(5);
    }
    tft.fillRect(0, 26, 240, 36, C_RD_SKY);
    uint32_t next = millis();
    int frame = 0, msgFrames = 0;
    while (!rdr.over) {
      if (btnEvent() == EV_LONG) {
        saveBestKey("brider", rdr.best);
        return;
      }
      int32_t late = (int32_t)(millis() - next);
      if (late < 0) {
        delay(1);
        continue;
      }
      if (late > 100) next = millis();                  // сильно отстали - не догоняем рывком
      next += 1000 / rider::FPS;
      int dx, dy;
      joyAnalog(dx, dy);
      int gt = joyThr() * 4 / 5;
      bool gas = dx > gt || dx < -gt || dy > gt || dy < -gt;
      int res = rdr.step(gas);
      if (res == 1) beep(1, 1000, 6);
      if (res == 2) beep(1, rdr.lastFlips ? 1700 : 500, rdr.lastFlips ? 40 : 8);
      // земля: сдвигаем столбцы, считаем только новые справа, перерисовываем узкие полоски, где что-то изменилось
      int ncam = (int)rdr.x - RD_BIKE_X;
      int shift = ncam - cam;
      cam = ncam;
      rdSetBike((int)rdr.y, rdr.angle);
      if (shift < 0 || shift > 240) shift = 240;
      for (int cx = 0; cx < 240; cx += 8) {
        int lo = 999, hi = -1;
        for (int sx = cx; sx < cx + 8; sx++) {
          int py = rdCol[sx];
          int gy = sx + shift < 240 ? rdCol[sx + shift] : (int)rider::ground((float)(cam + sx));
          if (gy != py) {
            int a = gy < py ? gy : py, b2 = gy < py ? py : gy;
            if (a < lo) lo = a;
            if (b2 > hi) hi = b2;
            rdCol[sx] = (int16_t)gy;
          }
        }
        if (hi > lo) rdRegion(cx, lo, 8, hi - lo);
      }
      // мотоцикл: один кусок, накрывающий и старое место, и новое
      int top = (oby < rdBikeY ? oby : rdBikeY) - 16, bot = (oby > rdBikeY ? oby : rdBikeY) + 14;
      rdRegion(RD_BIKE_X - 14, top, 29, bot - top);
      oby = rdBikeY;
      if (res == 2 && rdr.lastFlips) {
        char b[32];
        snprintf(b, sizeof(b), "%s x%d  +2 %s", L3("Сальто", "Flip", "翻转"), rdr.lastFlips, L3("с", "s", "秒"));
        drawSmallCenteredBg(b, 26, C_YELLOW, C_RD_SKY);
        msgFrames = rider::FPS;
      } else if (msgFrames && --msgFrames == 0) {
        tft.fillRect(0, 26, 240, 16, C_RD_SKY);
      }
      if (++frame % 6 == 0) drawRiderBar(false);
    }
    saveBestKey("brider", rdr.best);
    drawRiderBar(false);
    delay(500);
    char b[48];
    snprintf(b, sizeof(b), T().gameScore, (unsigned)rdr.score());
    if (!resultBox(rdr.crashed ? L3("Авария!", "Crash!", "摔车！") : L3("Время вышло", "Time is up", "时间到"), b, rdr.crashed ? C_RED : C_YELLOW)) return;
  }
}

// ========================= ПОДЗЕМЕЛЬЕ 3D =========================
// Стрелялка от первого лица. Картинка собирается в памяти полосками по 8 точек и сразу уходит на экран.
fps3d::Game f3;
const int F3_W = 240, F3_H = 180, F3_RAYS = 120, F3_SPR = 24;
static uint16_t f3Tex[3][2][256];                        // рисунки стен: 3 вида, светлая и тёмная грань
static uint16_t f3Row[F3_H];                             // цвет потолка и пола по строкам
static int32_t f3Dist[F3_RAYS];
static int16_t f3LineH[F3_RAYS];
static uint8_t f3Kind[F3_RAYS], f3U[F3_RAYS];
static int16_t f3SprLeft[F3_SPR], f3SprTop[F3_SPR], f3SprSize[F3_SPR];
static int32_t f3SprDepth[F3_SPR];
static const char* const* f3SprArt[F3_SPR];
static bool f3SprWhite[F3_SPR];
static uint8_t f3SprOrder[F3_SPR];
static int f3SprN = 0;
static uint32_t f3Frame = 0;
static bool f3Walking = false;
static int f3HudHp = -1, f3HudAmmo = -1;
static long f3HudInfo = -1;

void f3Init() {
  for (int t = 0; t < 3; t++)
    for (int v = 0; v < 16; v++)
      for (int u = 0; u < 16; u++) {
        uint16_t c = fps3d::wallTexel(t + 1, u, v);
        f3Tex[t][0][v * 16 + u] = c;
        f3Tex[t][1][v * 16 + u] = (uint16_t)(((c >> 1) & 0x7BEF) + ((c >> 2) & 0x39E7));   // 3/4 яркости
      }
  for (int y = 0; y < F3_H; y++) {
    int d = y < F3_H / 2 ? F3_H / 2 - y : y - F3_H / 2 + 1;    // чем дальше от горизонта, тем светлее
    int k = 6 + d * 20 / (F3_H / 2);                            // 6..26
    if (y < F3_H / 2) f3Row[y] = (uint16_t)(((k / 3) << 11) | ((k * 2 / 3) << 5) | (k / 2));            // потолок: серо-синий
    else f3Row[y] = (uint16_t)(((k * 2 / 3) << 11) | ((k * 3 / 4) << 5) | (k / 4));                     // пол: бурый
  }
}

// Посчитать лучи и то, что видно (враги, вещи).
void f3Cast() {
  float c = cosf(f3.pa), s = sinf(f3.pa);
  float plx = -s * 0.66f, ply = c * 0.66f;
  int32_t px = (int32_t)(f3.px * 65536.0f), py = (int32_t)(f3.py * 65536.0f);
  int32_t dirX = (int32_t)(c * 65536.0f), dirY = (int32_t)(s * 65536.0f);
  int32_t plX = (int32_t)(plx * 65536.0f), plY = (int32_t)(ply * 65536.0f);
  fps3d::Hit h;
  for (int r = 0; r < F3_RAYS; r++) {
    int32_t cam = ((2 * r + 1 - F3_RAYS) * 65536) / F3_RAYS;
    int32_t rdx = dirX + (int32_t)(((int64_t)plX * cam) >> 16), rdy = dirY + (int32_t)(((int64_t)plY * cam) >> 16);
    fps3d::castRay(f3.map, px, py, rdx, rdy, h);
    f3Dist[r] = h.dist;
    int lh = (int)((((int64_t)F3_H) << 16) / h.dist);
    f3LineH[r] = (int16_t)(lh > 2000 ? 2000 : lh < 1 ? 1 : lh);
    f3Kind[r] = (uint8_t)((h.type - 1) * 2 + h.side);
    f3U[r] = h.u;
  }
  f3SprN = 0;
  for (int i = 0; i < f3.nThings && f3SprN < F3_SPR; i++) {
    const fps3d::Thing& t = f3.things[i];
    bool enemy = t.kind <= fps3d::K_MAGE;
    if (!t.alive && !(enemy && t.deadT)) continue;
    float rx = t.x - f3.px, ry = t.y - f3.py;
    float tx = (s * rx - c * ry) / -0.66f;
    float ty = (-ply * rx + plx * ry) / -0.66f;          // глубина
    if (ty < 0.2f) continue;
    float full = F3_H / ty;
    float k = enemy ? 0.78f : t.kind == fps3d::K_EXIT ? 0.8f : 0.42f;
    int size = (int)(full * k);
    if (size < 2) continue;
    if (size > 500) size = 500;
    int cx = (int)((F3_W / 2) * (1.0f + tx / ty));
    int bottom = F3_H / 2 + (int)(full / 2);
    if (t.kind == fps3d::K_MAGE && t.alive) bottom -= (int)(full * 0.12f);   // колдун парит
    if (t.kind == fps3d::K_EXIT) bottom -= (int)(full * 0.08f);
    int left = cx - size / 2;
    if (left >= F3_W || left + size <= 0) continue;
    int n = f3SprN++;
    f3SprLeft[n] = (int16_t)left;
    f3SprTop[n] = (int16_t)(bottom - size);
    f3SprSize[n] = (int16_t)size;
    f3SprDepth[n] = (int32_t)(ty * 65536.0f);
    f3SprWhite[n] = enemy && t.alive && (t.hurt > 0 || t.attack > 3);
    f3SprArt[n] = !t.alive ? fps3d::ART_SPLAT : t.kind == fps3d::K_GRUNT ? fps3d::ART_GRUNT : t.kind == fps3d::K_MAGE ? fps3d::ART_MAGE
                  : t.kind == fps3d::K_MED ? fps3d::ART_MED : t.kind == fps3d::K_AMMO ? fps3d::ART_AMMO : fps3d::ART_EXIT;
  }
  for (int i = 0; i < f3SprN; i++) f3SprOrder[i] = (uint8_t)i;
  for (int i = 1; i < f3SprN; i++)                       // дальние рисуются первыми
    for (int j = i; j > 0 && f3SprDepth[f3SprOrder[j]] > f3SprDepth[f3SprOrder[j - 1]]; j--) {
      uint8_t t = f3SprOrder[j];
      f3SprOrder[j] = f3SprOrder[j - 1];
      f3SprOrder[j - 1] = t;
    }
}

// Столбец картинки 16x16, растянутый на size точек, в полоску (шаг по памяти 8).
void f3ArtColumn(const char* const* art, int tx, int top, int size, bool white, uint16_t* col) {
  uint16_t pal[16];
  for (int v = 0; v < 16; v++) {
    uint16_t c = fps3d::artColor(art[v][tx]);
    pal[v] = c && white ? 0xFFFF : c;
  }
  int y0 = top < 0 ? 0 : top, y1 = top + size > F3_H ? F3_H : top + size;
  uint32_t st = (16u << 16) / (uint32_t)size, tp = (uint32_t)(y0 - top) * st;
  uint16_t* p = col + y0 * 8;
  for (int y = y0; y < y1; y++, p += 8, tp += st) {
    uint16_t c = pal[(tp >> 16) & 15];
    if (c) *p = c;
  }
}

void f3Draw() {
  f3Cast();
  int gunY = F3_H - 64 + (f3.shotFlash ? 8 : 0) + (f3Walking && ((f3Frame >> 2) & 1) ? 3 : 0);
  for (int sx0 = 0; sx0 < F3_W; sx0 += 8) {
    for (int i = 0; i < 8; i += 2) {                     // стены, потолок, пол: луч на две точки
      int r = (sx0 + i) / 2;
      int h = f3LineH[r], top = (F3_H - h) / 2;
      const uint16_t* tex = f3Tex[f3Kind[r] >> 1][f3Kind[r] & 1] + f3U[r];
      bool farWall = f3Dist[r] > (6 << 16);
      uint32_t st = (16u << 16) / (uint32_t)h, tp = top < 0 ? (uint32_t)(-top) * st : 0;
      int y0 = top < 0 ? 0 : top, y1 = top + h > F3_H ? F3_H : top + h;
      uint16_t* p = rdBuf + i;
      int y = 0;
      for (; y < y0; y++, p += 8) p[0] = p[1] = f3Row[y];
      for (; y < y1; y++, p += 8, tp += st) {
        uint16_t c = tex[((tp >> 16) & 15) << 4];
        if (farWall) c = (c >> 1) & 0x7BEF;
        p[0] = p[1] = c;
      }
      for (; y < F3_H; y++, p += 8) p[0] = p[1] = f3Row[y];
    }
    for (int i = 0; i < 8; i++) {
      int x = sx0 + i;
      for (int k = 0; k < f3SprN; k++) {                 // враги и вещи, если не за стеной
        int n = f3SprOrder[k];
        int rel = x - f3SprLeft[n];
        if (rel < 0 || rel >= f3SprSize[n] || f3SprDepth[n] >= f3Dist[x / 2]) continue;
        f3ArtColumn(f3SprArt[n], rel * 16 / f3SprSize[n], f3SprTop[n], f3SprSize[n], f3SprWhite[n], rdBuf + i);
      }
      if (x >= 88 && x < 152) {                          // оружие
        if (f3.shotFlash == 2) {                         // вспышка выстрела
          int dx = x - 120;
          for (int dy = -9; dy <= 9; dy++) {
            int yy = gunY + 8 + dy;
            if (dx * dx + dy * dy <= 81 && yy >= 0 && yy < F3_H) rdBuf[yy * 8 + i] = dx * dx + dy * dy < 25 ? 0xFFFF : 0xFFE0;
          }
        }
        f3ArtColumn(fps3d::ART_GUN, (x - 88) / 4, gunY, 64, false, rdBuf + i);
      }
      if (x >= 116 && x <= 123) {                        // прицел
        if (x == 119 || x == 120) {
          rdBuf[86 * 8 + i] = rdBuf[87 * 8 + i] = rdBuf[92 * 8 + i] = rdBuf[93 * 8 + i] = 0xFFFF;
        } else if (x <= 117 || x >= 122) {
          rdBuf[89 * 8 + i] = rdBuf[90 * 8 + i] = 0xFFFF;
        }
      }
    }
    if (f3.hurtFlash)                                    // ранили - экран краснеет
      for (int k = 0; k < 8 * F3_H; k++) rdBuf[k] = (uint16_t)(((rdBuf[k] >> 1) & 0x7BEF) + 0x7800);
    tft.startWrite();
    tft.setAddrWindow(sx0, 0, 8, F3_H);
    tft.writePixels(rdBuf, (uint32_t)(8 * F3_H));
    tft.endWrite();
  }
  f3Frame++;
}

// Нижняя панель: здоровье, патроны, уровень и счёт. Рисуется только изменившееся.
void f3Hud(bool full) {
  if (full) {
    tft.fillRect(0, F3_H, 240, 240 - F3_H, C_BLACK);
    tft.fillRect(0, F3_H, 240, 2, 0x4208);
    f3HudHp = f3HudAmmo = -1;
    f3HudInfo = -1;
  }
  char b[64];
  if (f3.hp != f3HudHp) {
    f3HudHp = f3.hp;
    tft.fillRect(0, F3_H + 5, 118, 26, C_BLACK);
    snprintf(b, sizeof(b), "%s %d", L3("Жизнь", "HP", "生命"), f3.hp);
    drawUtf8(b, 4, F3_H + 6, f3.hp < 30 ? C_RED : C_GREEN, C_BLACK);
  }
  if (f3.ammo != f3HudAmmo) {
    f3HudAmmo = f3.ammo;
    tft.fillRect(118, F3_H + 5, 122, 26, C_BLACK);
    snprintf(b, sizeof(b), "%s %d", L3("Патр.", "Ammo", "子弹"), f3.ammo);
    drawUtf8(b, 236 - textWidth(b), F3_H + 6, f3.ammo ? C_YELLOW : C_RED, C_BLACK);
  }
  long info = (long)f3.score * 100 + f3.level;
  if (info != f3HudInfo) {
    f3HudInfo = info;
    SmallFont small;
    tft.fillRect(0, F3_H + 36, 240, 18, C_BLACK);
    snprintf(b, sizeof(b), "%s %d", L3("Уровень", "Level", "关卡"), f3.level + 1);
    drawUtf8(b, 4, F3_H + 38, C_WHITE, C_BLACK);
    snprintf(b, sizeof(b), T().gameScore, (unsigned)f3.score);
    drawUtf8(b, 236 - textWidth(b), F3_H + 38, C_WHITE, C_BLACK);
  }
}

void runFps3d() {
  if (!ENABLE_BIGGAMES) {
    notInEdition();
    return;
  }
  static bool inited = false;
  if (!inited) {
    f3Init();
    inited = true;
  }
  f3.best = loadBestKey("bfps");
  for (;;) {
    f3.reset(millis() | 1);
    f3Walking = false;
    f3Hud(true);
    f3Draw();
    drawSmallCenteredBg(L3("Ручка: идти и поворачивать", "Stick: walk and turn", "摇杆：移动和转向"), 30, C_WHITE, C_BLACK);
    drawSmallCenteredBg(L3("Кнопка: выстрел", "Button: shoot", "按键：射击"), 50, C_WHITE, C_BLACK);
    drawSmallCenteredBg(L3("Найдите синий портал", "Find the blue portal", "找到蓝色传送门"), 70, C_YELLOW, C_BLACK);
    drawSmallCenteredBg(L3("Держать кнопку: выход", "Hold the button: exit", "长按：退出"), 90, 0xAD55, C_BLACK);
    for (;;) {                                           // старт по ручке или кнопке
      BtnEvent e = btnEvent();
      if (e == EV_LONG) return;
      if (e == EV_CLICK || joyRaw() != J_NONE) break;
      delay(5);
    }
    const uint32_t TICK = 1000 / fps3d::TICKS;
    uint32_t last = millis();
    bool wasDown = false, fire = false;
    while (!f3.over) {
      if (btnEvent() == EV_LONG) {
        saveBestKey("bfps", f3.best);
        return;
      }
      bool down = swDown();                              // выстрел сразу по нажатию, не дожидаясь отпускания
      if (down && !wasDown) fire = true;
      wasDown = down;
      uint32_t behind = millis() - last;
      if (behind < TICK) {
        delay(1);
        continue;
      }
      int steps = (int)(behind / TICK);
      if (steps > 3) {                                   // экран не успевает - не копим долг
        steps = 3;
        last = millis() - 3 * TICK;
      }
      last += (uint32_t)steps * TICK;
      int dx, dy;
      joyAnalog(dx, dy);
      int dead = joyThr() / 4, lim = joyThr();
      float turn = dx > dead ? (float)(dx - dead) / (lim - dead) : dx < -dead ? (float)(dx + dead) / (lim - dead) : 0;
      float fwd = dy < -dead ? (float)(-dy - dead) / (lim - dead) : dy > dead ? -(float)(dy - dead) / (lim - dead) : 0;
      if (turn > 1) turn = 1;
      if (turn < -1) turn = -1;
      if (fwd > 1) fwd = 1;
      if (fwd < -1) fwd = -1;
      f3Walking = fwd > 0.3f || fwd < -0.3f;
      int ev = 0;
      for (int i = 0; i < steps && !f3.over; i++) {
        ev |= f3.step(fwd, turn, fire);
        fire = false;
      }
      if (ev & fps3d::EV_LEVEL) {
        saveBestKey("bfps", f3.best);
        sndWin();
        last = millis();
      } else if (ev & fps3d::EV_HURT) beep(1, 220, 14);
      else if (ev & fps3d::EV_KILL) beep(1, 1900, 10);
      else if (ev & fps3d::EV_SHOT) beep(1, 700, 5);
      else if (ev & fps3d::EV_EMPTY) beep(1, 300, 4);
      else if (ev & fps3d::EV_PICK) beep(1, 1500, 10);
      f3Hud(false);
      f3Draw();
    }
    saveBestKey("bfps", f3.best);
    sndLose();
    delay(600);
    char b[48];
    snprintf(b, sizeof(b), T().gameScore, (unsigned)f3.score);
    if (!resultBox(L3("Вы погибли", "You died", "你死了"), b, C_RED)) return;
  }
}

// ========================= КРЕСТИКИ-НОЛИКИ ПО СЕТИ =========================
// Два гаджета в одной комнате находят друг друга сами. Сообщения идут в <комната>/g:
// «метка \t имя \t ttt \t команда \t кому \t число».
// ---- Прямая связь гаджет-гаджет (ESP-NOW): без роутера и интернета, до ~100 м ----
// На время игры Wi-Fi отключается от сети (оба гаджета должны быть на одном канале - берём 1-й),
// после игры подключается снова.
namespace enow {
std::mutex mu;
std::vector<std::string> inbox;
struct Peer {
  std::string id;
  uint8_t mac[6];
};
std::vector<Peer> peers;
bool on = false;
const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len <= 0 || len > 250) return;
  std::string m((const char*)data, (size_t)len);
  std::lock_guard<std::mutex> lk(mu);
  size_t t = m.find('\t');
  if (t != std::string::npos && t > 0 && t <= 16 && info) {   // запомнить адрес отправителя по его метке
    std::string id = m.substr(0, t);
    bool known = false;
    for (Peer& p : peers)
      if (p.id == id) {
        memcpy(p.mac, info->src_addr, 6);
        known = true;
      }
    if (!known && peers.size() < 8) {
      Peer p;
      p.id = id;
      memcpy(p.mac, info->src_addr, 6);
      peers.push_back(p);
    }
  }
  if (inbox.size() < 16) inbox.push_back(m);
}

void addPeer(const uint8_t* mac) {
  if (esp_now_is_peer_exist(mac)) return;
  esp_now_peer_info_t pi;
  memset(&pi, 0, sizeof(pi));
  memcpy(pi.peer_addr, mac, 6);
  pi.channel = 0;
  pi.encrypt = false;
  esp_now_add_peer(&pi);
}

bool begin() {
  if (on) return true;
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false);
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) return false;
  esp_now_register_recv_cb(onRecv);
  addPeer(BCAST);
  on = true;
  return true;
}

void end() {
  if (!on) return;
  esp_now_deinit();
  on = false;
  std::lock_guard<std::mutex> lk(mu);
  inbox.clear();
  peers.clear();
  WiFi.setAutoReconnect(true);
  wifiStarted = false;                               // фоновый Wi-Fi подключится заново
}

// to - метка получателя (если известен его адрес - отправляем лично, с подтверждением доставки).
void send(const std::string& m, const std::string& to) {
  if (!on) return;
  const uint8_t* mac = BCAST;
  uint8_t buf[6];
  {
    std::lock_guard<std::mutex> lk(mu);
    for (const Peer& p : peers)
      if (to.size() && p.id == to) {
        memcpy(buf, p.mac, 6);
        mac = buf;
      }
  }
  if (mac != BCAST) addPeer(mac);
  esp_now_send(mac, (const uint8_t*)m.data(), m.size() > 250 ? 250 : m.size());
}

bool take(std::string& m) {
  std::lock_guard<std::mutex> lk(mu);
  if (inbox.empty()) return false;
  m = inbox.front();
  inbox.erase(inbox.begin());
  return true;
}
}  // namespace enow

bool g_netDirect = false;                            // игра идёт напрямую (ESP-NOW), а не через сервер
void netSend(const String& body) {
  std::string m = toStd(roomId) + "\t" + toStd(cfgNick) + "\t" + toStd(body);
  if (g_netDirect) {
    std::string b = toStd(body), to;                 // «игра \t команда \t кому \t ...»
    size_t t1 = b.find('\t'), t2 = t1 == std::string::npos ? t1 : b.find('\t', t1 + 1);
    if (t2 != std::string::npos) {
      size_t t3 = b.find('\t', t2 + 1);
      to = b.substr(t2 + 1, t3 == std::string::npos ? std::string::npos : t3 - t2 - 1);
    }
    enow::send(m, to);
  } else {
    mqWrite(mqtt::publish(roomBase() + "/g", m));
  }
}

// Принять новые сообщения игры в netInbox.
void netPump() {
  if (g_netDirect) {
    std::string m;
    while (enow::take(m)) netInbox.push_back(m);
  } else {
    wifiTick();
    roomTick();
  }
}

bool netLinkUp() { return g_netDirect ? enow::on : mqUp; }

// Разобрать очередное сообщение игры. false - очередь пуста.
bool netNext(String& id, String& nick, String& game, String& cmd, String& peer, int& arg) {
  while (!netInbox.empty()) {
    std::string m = netInbox.front();
    netInbox.erase(netInbox.begin());
    std::vector<std::string> f;
    size_t p = 0;
    for (;;) {
      size_t e = m.find('\t', p);
      f.push_back(m.substr(p, e == std::string::npos ? std::string::npos : e - p));
      if (e == std::string::npos) break;
      p = e + 1;
    }
    if (f.size() < 4 || f[0].empty() || f[0].size() > 16 || f[0] == toStd(roomId)) continue;
    id = fromStd(f[0]);
    nick = fromStd(mqtt::clip(f[1], 24));
    game = fromStd(f[2]);
    cmd = fromStd(f[3]);
    peer = f.size() > 4 ? fromStd(f[4]) : String("");
    arg = f.size() > 5 ? atoi(f[5].c_str()) : -1;
    return true;
  }
  return false;
}

void runTttNetVia(bool direct);
void runTttNet() { runTttNetVia(false); }
void runTttNear() { runTttNetVia(true); }

// Крестики-нолики вдвоём: через сервер комнаты или напрямую (гаджеты рядом, без интернета).
void runTttNetVia(bool direct) {
  if (direct) {
    if (!enow::begin()) {
      viewAnswer(L3("Не удалось включить прямую связь.", "Could not start the direct link.", "无法启动直连。"), C_RED);
      return;
    }
  } else {
    if (inRoom) return;
    if (!waitWifi(false)) return;
    if (!cfgNick.length() && !askNick()) return;
    if (!mqUp) {
      showMsg(L3("Подключаюсь...", "Connecting...", "正在连接……"), C_YELLOW);
      mqConnect(6000);
    }
    inRoom = true;                                   // связь поддерживается, пока идёт игра
  }
  g_netDirect = direct;
  struct Leave {
    bool direct;
    ~Leave() {
      if (direct) {
        enow::end();
        g_netDirect = false;
      } else {
        inRoom = false;
        if (!(cfgRoomBg && cfgNick.length())) mqDrop();
      }
    }
  } leave{direct};
  int wins = 0, losses = 0, round = 0;
  for (;;) {
    // --- поиск соперника ---
    String peer, peerNick;
    bool host = false, iAmX = true;
    netInbox.clear();
    if (direct)
      showMsg(L3("Ищу гаджет рядом (без интернета, до ~100 м). Пусть друг откроет «Крестики рядом». Держать - выход.",
                 "Looking for a device nearby (no internet, up to ~100 m). Ask a friend to open Tic-tac-toe nearby. Hold - exit.",
                 "正在寻找附近的设备（无需网络，约100米内）。请朋友打开“附近井字棋”。长按：退出。"), C_YELLOW);
    else
      showMsg(String(L3("Ищу соперника в комнате #", "Looking for an opponent in room #", "正在房间里寻找对手 #")) + cfgRoom +
              L3(". Пусть друг тоже откроет «Крестики по сети». Держать - выход.", ". Ask a friend to open Tic-tac-toe online too. Hold - exit.",
                 "。请朋友也打开“联网井字棋”。长按：退出。"), C_YELLOW);
    uint32_t lastSeek = 0, lastConn = millis();
    int shownLink = -1;
    while (!peer.length()) {
      if (btnEvent() == EV_LONG) return;
      netPump();
      if (!direct && !mqUp && millis() - lastConn > 5000) {   // связь с сервером пропала - переподключиться
        lastConn = millis();
        mqConnect(3000);
      }
      if ((int)netLinkUp() != shownLink) {
        shownLink = netLinkUp();
        SmallFont small;
        tft.fillRect(0, 222, 240, 18, C_BLACK);
        drawUtf8(shownLink ? (direct ? L3("радио: включено", "radio: on", "无线：开") : L3("сервер: подключено", "server: connected", "服务器：已连接"))
                           : (direct ? L3("радио: нет", "radio: off", "无线：关") : L3("сервер: нет связи, переподключаюсь", "server: no link, reconnecting", "服务器：重连中")),
                 4, 222, shownLink ? C_GREEN : C_RED, C_BLACK);
      }
      if (netLinkUp() && millis() - lastSeek > 2000) {
        lastSeek = millis();
        netSend("ttt\tseek");
      }
      String id, nick, game, cmd, to;
      int arg;
      while (netNext(id, nick, game, cmd, to, arg)) {
        if (game != "ttt") continue;
        if (cmd == "seek" && strcmp(roomId.c_str(), id.c_str()) < 0) {   // у кого метка меньше - тот начинает игру
          peer = id;
          peerNick = nick;
          host = true;
          iAmX = round % 2 == 0;                     // право первого хода чередуется
          netSend(String("ttt\tstart\t") + peer + "\t" + (iAmX ? "1" : "0"));
          break;
        }
        if (cmd == "start" && to == roomId) {
          peer = id;
          peerNick = nick;
          iAmX = arg == 0;
          break;
        }
      }
      delay(10);
    }
    sndOk();
    if (!peerNick.length()) peerNick = peer;
    // --- партия ---
    uint8_t b[9] = {0};
    int cur = 4, mine = iAmX ? 1 : 2;
    bool myTurn = iAmX;
    uint32_t lastHeard = millis();
    auto status = [&]() {
      SmallFont small;
      tft.fillRect(0, 0, 240, 30, C_BLACK);
      char s[80];
      snprintf(s, sizeof(s), "%s %d : %d %s", cfgNick.c_str(), wins, losses, peerNick.c_str());
      drawUtf8(s, (240 - textWidth(s)) / 2 < 0 ? 0 : (240 - textWidth(s)) / 2, 0, C_WHITE, C_BLACK);
      const char* t = myTurn ? L3("Ваш ход", "Your move", "轮到你") : L3("Ход соперника...", "Opponent's move...", "对手走棋……");
      drawUtf8(t, (240 - textWidth(t)) / 2, 16, myTurn ? C_GREEN : C_YELLOW, C_BLACK);
    };
    tft.fillScreen(C_BLACK);
    for (int k = 1; k < 3; k++) {
      tft.fillRect(TT_X0 + k * TT_CELL - 1, TT_Y0, 3, 3 * TT_CELL, C_LIGHT);
      tft.fillRect(TT_X0, TT_Y0 + k * TT_CELL - 1, 3 * TT_CELL, 3, C_LIGHT);
    }
    for (int i = 0; i < 9; i++) drawTttCell(b, i, i == cur, 0);
    status();
    int w = 0;
    bool gone = false;
    while (!(w = games2::ttt::winner(b)) && !gone) {
      netPump();
      String id, nick, game, cmd, to;
      int arg;
      while (netNext(id, nick, game, cmd, to, arg)) {
        if (game != "ttt" || id != peer) continue;
        lastHeard = millis();
        if (cmd == "bye") gone = true;
        if (cmd == "move" && to == roomId && !myTurn && arg >= 0 && arg < 9 && !b[arg]) {
          b[arg] = (uint8_t)(3 - mine);
          drawTttCell(b, arg, arg == cur, 0);
          myTurn = true;
          beep(1, 700, 40);
          status();
        }
      }
      if (millis() - lastHeard > 120000UL || (!netLinkUp() && millis() - lastHeard > (direct ? 5000UL : 30000UL))) gone = true;
      uint8_t j = joyEvent();
      if (j != J_NONE) {
        int old = cur, x = cur % 3, y = cur / 3;
        if (j == J_LEFT) x = (x + 2) % 3;
        if (j == J_RIGHT) x = (x + 1) % 3;
        if (j == J_UP) y = (y + 2) % 3;
        if (j == J_DOWN) y = (y + 1) % 3;
        cur = y * 3 + x;
        drawTttCell(b, old, false, 0);
        drawTttCell(b, cur, true, 0);
      }
      BtnEvent e = btnEvent();
      if (e == EV_LONG) {
        netSend(String("ttt\tbye\t") + peer);
        return;
      }
      if (e == EV_CLICK && myTurn && !b[cur]) {
        b[cur] = (uint8_t)mine;
        drawTttCell(b, cur, true, 0);
        netSend(String("ttt\tmove\t") + peer + "\t" + String(cur));
        myTurn = false;
        lastHeard = millis();
        status();
      }
      delay(5);
    }
    round++;
    if (gone) {
      showMsg(L3("Соперник вышел из игры.", "The opponent has left.", "对手已离开。"), C_YELLOW);
      delay(1800);
      continue;
    }
    int line[3];
    if (games2::ttt::winLine(b, line))
      for (int k = 0; k < 3; k++) drawTttCell(b, line[k], false, C_GREEN);
    delay(900);
    bool won = w == mine, draw = w == 3;
    if (won) wins++;
    else if (!draw) losses++;
    char s[24];
    snprintf(s, sizeof(s), "%d : %d", wins, losses);
    if (!resultBox(won ? T().pongWin : draw ? L3("Ничья", "Draw", "平局") : T().pongLose, s, won ? C_GREEN : draw ? C_YELLOW : C_RED)) {
      netSend(String("ttt\tbye\t") + peer);
      return;
    }
  }
}

// ========================= ШАХМАТЫ =========================
// Игрок - белые (внизу). Соперник - DeepSeek (если есть интернет) или сама плата.
chess::Board chs;
chess::Engine chsEng;
const int CH_CELL = 27, CH_X0 = 16;                 // доска 216x216, слева цифры рядов
const uint16_t C_SQ_LIGHT = 0xEE54, C_SQ_DARK = 0x7CAB, C_SQ_LAST = 0xCE60;

void chessPause() { delay(1); }

// Клетка: фон, отметки, фигура. mark: 0 - нет, 1 - сюда можно пойти, 2 - курсор, 4 - выбрана, 8 - последний ход.
bool chsFlip = false;                                // смотреть со стороны чёрных (сетевая партия)
void drawChessSquare(int s, int mark) {
  int r = s >> 3, f = s & 7;
  int x = CH_X0 + (chsFlip ? 7 - f : f) * CH_CELL, y = (chsFlip ? r : 7 - r) * CH_CELL;
  uint16_t bg = (mark & 8) ? C_SQ_LAST : ((r + f) & 1) ? C_SQ_LIGHT : C_SQ_DARK;
  tft.fillRect(x, y, CH_CELL, CH_CELL, bg);
  int8_t p = chs.sq[s];
  if (p) {
    int a = (p < 0 ? -p : p) - 1;
    uint16_t fill = p > 0 ? C_WHITE : C_BLACK, edge = p > 0 ? C_BLACK : 0xAD55;
    for (int row = 0; row < 24; row++) {
      uint32_t fm = ((uint32_t)pgm_read_byte(&CHESS_FILL[a][row * 3]) << 16) | ((uint32_t)pgm_read_byte(&CHESS_FILL[a][row * 3 + 1]) << 8) |
                    pgm_read_byte(&CHESS_FILL[a][row * 3 + 2]);
      uint32_t om = ((uint32_t)pgm_read_byte(&CHESS_OUTLINE[a][row * 3]) << 16) |
                    ((uint32_t)pgm_read_byte(&CHESS_OUTLINE[a][row * 3 + 1]) << 8) | pgm_read_byte(&CHESS_OUTLINE[a][row * 3 + 2]);
      for (int c = 0; c < 24; c++) {
        uint32_t bit = 0x800000UL >> c;
        cbuf[row * 24 + c] = (fm & bit) ? fill : (om & bit) ? edge : bg;
      }
    }
    blit(x + 1, y + 2, 24, 24);
  }
  if (mark & 1) {
    if (p) {
      tft.drawRect(x + 1, y + 1, CH_CELL - 2, CH_CELL - 2, C_RED);
      tft.drawRect(x + 2, y + 2, CH_CELL - 4, CH_CELL - 4, C_RED);
    } else {
      tft.fillCircle(x + CH_CELL / 2, y + CH_CELL / 2, 4, 0x39E7);
    }
  }
  if (mark & 6) {
    uint16_t c = (mark & 2) ? C_YELLOW : 0x07E0;
    tft.drawRect(x, y, CH_CELL, CH_CELL, c);
    tft.drawRect(x + 1, y + 1, CH_CELL - 2, CH_CELL - 2, c);
    if ((mark & 6) == 6) tft.drawRect(x + 2, y + 2, CH_CELL - 4, CH_CELL - 4, 0x07E0);
  }
}

void chessStatus(const String& s, uint16_t col) {
  SmallFont small;
  tft.fillRect(0, 216, 240, 24, C_BLACK);
  drawUtf8(s.c_str(), 2, 221, col, C_BLACK);
}

void saveChess(int opp) {
  prefs.begin("dsk", false);
  prefs.putString("chfen", fromStd(chs.fen()));
  prefs.putUChar("chopp", (uint8_t)opp);
  prefs.end();
}

void clearChess() {
  prefs.begin("dsk", false);
  prefs.remove("chfen");
  prefs.end();
}

// Ход соперника. opp: 0 - DeepSeek, 1..3 - плата (легко, нормально, сложно). say - что показать в строке внизу.
chess::Move chessOpponentMove(int opp, String& say) {
  chess::Move list[chess::MAX_MOVES];
  int n = chs.legal(list);
  if (!n) return 0;
  chsEng.pause = chessPause;
  chsEng.rnd = gameRandom;
  if (opp == 0 && wifiOk() && cfgKey.length()) {
    chessStatus(L3("DeepSeek думает...", "DeepSeek is thinking...", "DeepSeek思考中……"), C_YELLOW);
    std::string sys = std::string("You are playing chess as Black against a human. You get the position in FEN and the list of "
                                  "legal moves. Choose a strong move. Reply in exactly this format:\nMOVE: one move copied from "
                                  "the list\nSAY: a short remark about your move in ") + T().promptLang + ", at most 20 characters\nNo other text.";
    std::string q = "FEN: " + chs.fen() + "\nLegal moves:";
    for (int i = 0; i < n; i++) q += " " + chess::moveStr(list[i]);
    std::vector<ChatMsg> none;
    for (int attempt = 0; attempt < 2; attempt++) {
      String reply;
      if (!askWith(sys.c_str(), none, fromStd(q), 60, reply)) break;
      std::string r = toStd(reply);
      size_t sp = r.find("SAY");
      chess::Move m = chess::findMove(sp == std::string::npos ? r : r.substr(0, sp), list, n);
      if (m) {
        std::string remark;
        if (sp != std::string::npos) {
          size_t a = sp + 3;
          while (a < r.size() && (r[a] == ':' || r[a] == ' ')) a++;
          size_t e = r.find('\n', a);
          remark = r.substr(a, e == std::string::npos ? std::string::npos : e - a);
        }
        say = String("DS ") + chess::moveStr(m).c_str() + (remark.empty() ? "" : ": ") + remark.c_str();
        noteActivity();
        return m;
      }
      q += "\nYour previous answer did not contain a legal move. Copy one move exactly from the list.";
    }
    say = L3("DeepSeek молчит - ходит плата", "DeepSeek failed, board moves", "DeepSeek没有回应，由设备走棋");
  } else if (opp == 0) {
    say = L3("Нет интернета - ходит плата", "No internet, board moves", "没有网络，由设备走棋");
  }
  if (opp != 0) chessStatus(L3("Плата думает...", "The board is thinking...", "设备思考中……"), C_YELLOW);
  int level = opp == 0 ? 2 : opp;
  chess::Move m = chsEng.best(chs, level, level == 1 ? 60 : 10);
  if (opp != 0) say = String(L3("Плата: ", "Board: ", "设备：")) + chess::moveStr(m).c_str();
  noteActivity();
  return m;
}

void runChess() {
  if (!ENABLE_CHESS) {
    notInEdition();
    return;
  }
  int opp = 2;
  prefs.begin("dsk", true);
  String saved = prefs.getString("chfen", "");
  opp = prefs.getUChar("chopp", 2);
  prefs.end();
  bool cont = false;
  if (saved.length() && chs.setFen(toStd(saved)) && chs.white && chs.result() == chess::PLAYING) {
    std::vector<String> o = splitBar(L3("Продолжить партию|Новая партия|Назад", "Continue the game|New game|Back", "继续对局|新对局|返回"));
    int k = choiceScreen(L3("Шахматы", "Chess", "国际象棋"), o);
    if (k < 0 || k == 2) return;
    cont = k == 0;
  }
  for (;;) {                                         // одна партия
    if (!cont) {
      std::vector<String> o = splitBar(L3("DeepSeek|Плата: легко|Плата: нормально|Плата: сложно|Назад",
                                          "DeepSeek|Board: easy|Board: normal|Board: hard|Back", "DeepSeek|设备：简单|设备：普通|设备：困难|返回"));
      int k = listScreen(L3("Соперник", "Opponent", "对手"), o, std::vector<String>(), opp);
      if (k < 0 || k > 3) return;
      opp = k;
      chs.reset();
      saveChess(opp);
    }
    cont = false;
    int cur = 12, sel = -1, lastFrom = -1, lastTo = -1;   // курсор на e2
    uint64_t targets = 0;
    chess::Move legal[chess::MAX_MOVES];
    int nLegal = chs.legal(legal);
    auto markOf = [&](int s) {
      return ((targets >> s) & 1 ? 1 : 0) | (s == cur ? 2 : 0) | (s == sel ? 4 : 0) | ((s == lastFrom || s == lastTo) ? 8 : 0);
    };
    auto drawAll = [&]() {
      tft.fillRect(0, 0, CH_X0, 216, C_BLACK);
      tft.fillRect(CH_X0 + 216, 0, 240 - CH_X0 - 216, 216, C_BLACK);
      {
        SmallFont small;
        for (int r = 0; r < 8; r++) drawCp((uint16_t)('1' + r), 4, (7 - r) * CH_CELL + 6, C_LIGHT, C_BLACK);
      }
      for (int s = 0; s < 64; s++) drawChessSquare(s, markOf(s));
    };
    auto yourTurn = [&]() {
      if (chs.inCheck(true)) chessStatus(L3("Вам шах!", "You are in check!", "将军！"), C_RED);
    };
    tft.fillScreen(C_BLACK);
    drawAll();
    chessStatus(L3("Ваш ход. Держать - меню", "Your move. Hold - menu", "轮到你。长按：菜单"), C_LIGHT);
    yourTurn();
    chess::Result res = chs.result();
    bool again = false;
    while (res == chess::PLAYING) {
      uint8_t j = joyEvent();
      if (j != J_NONE) {
        int old = cur, r = cur >> 3, f = cur & 7;
        if (j == J_LEFT) f = (f + 7) % 8;
        if (j == J_RIGHT) f = (f + 1) % 8;
        if (j == J_UP) r = (r + 1) % 8;
        if (j == J_DOWN) r = (r + 7) % 8;
        cur = r * 8 + f;
        drawChessSquare(old, markOf(old));
        drawChessSquare(cur, markOf(cur));
      }
      BtnEvent e = btnEvent();
      if (e == EV_LONG) {
        std::vector<String> o = splitBar(L3("Продолжить|Новая партия|Выйти (партия сохранится)", "Continue|New game|Exit (the game is saved)",
                                            "继续|新对局|退出（对局已保存）"));
        int k = choiceScreen(L3("Шахматы", "Chess", "国际象棋"), o);
        if (k == 2) return;
        if (k == 1) {
          again = true;
          break;
        }
        tft.fillScreen(C_BLACK);
        drawAll();
        chessStatus(L3("Ваш ход (белые)", "Your move (White)", "轮到你（白方）"), C_LIGHT);
        yourTurn();
      }
      if (e == EV_CLICK) {
        if (sel >= 0 && ((targets >> cur) & 1)) {      // ход игрока
          chess::Move mv = 0;
          for (int i = 0; i < nLegal; i++)
            if (chess::mFrom(legal[i]) == sel && chess::mTo(legal[i]) == cur &&
                (!chess::mPromo(legal[i]) || chess::mPromo(legal[i]) == chess::QUEEN))
              mv = legal[i];
          chess::Undo u;
          chs.make(mv, u);
          lastFrom = sel;
          lastTo = cur;
          sel = -1;
          targets = 0;
          drawAll();
          res = chs.result();
          if (res != chess::PLAYING) break;
          String say;
          chess::Move om = chessOpponentMove(opp, say);  // ответ соперника
          if (om) {
            chs.make(om, u);
            lastFrom = chess::mFrom(om);
            lastTo = chess::mTo(om);
          }
          saveChess(opp);
          nLegal = chs.legal(legal);
          drawAll();
          chessStatus(say, 0x07FF);
          beep(1, 700, 40);
          res = chs.result();
          if (res == chess::PLAYING && chs.inCheck(true)) {
            delay(900);
            yourTurn();
          }
          while (btnEvent() != EV_NONE) delay(5);         // нажатия во время раздумий не считаются
        } else if (chs.sq[cur] > 0 && cur != sel) {     // выбрать свою фигуру
          uint64_t t = 0;
          for (int i = 0; i < nLegal; i++)
            if (chess::mFrom(legal[i]) == cur) t |= 1ULL << chess::mTo(legal[i]);
          if (t) {
            sel = cur;
            targets = t;
            drawAll();
          } else {
            chessStatus(L3("Этой фигурой сейчас не пойти", "This piece cannot move now", "这个棋子现在不能走"), C_YELLOW);
          }
        } else if (sel >= 0) {                          // снять выбор
          sel = -1;
          targets = 0;
          drawAll();
        }
      }
      delay(2);
    }
    if (again) {
      clearChess();
      continue;
    }
    clearChess();
    delay(700);
    const char* title;
    uint16_t col = C_YELLOW;
    if (res == chess::MATE_WHITE_WINS) {
      title = L3("Мат! Вы победили", "Checkmate! You win", "将死！你赢了");
      col = C_GREEN;
      saveBestKey("bchess", loadBestKey("bchess") + 1);
    } else if (res == chess::MATE_BLACK_WINS) {
      title = L3("Вам мат", "Checkmate. You lose", "你被将死了");
      col = C_RED;
    } else if (res == chess::STALEMATE) {
      title = L3("Пат - ничья", "Stalemate - draw", "逼和");
    } else {
      title = L3("Ничья", "Draw", "和棋");
    }
    char b[32];
    snprintf(b, sizeof(b), "%s %u", L3("Ходов:", "Moves:", "回合："), (unsigned)chs.full);
    if (!resultBox(title, b, col)) return;
  }
}

// ---------- Шахматы вдвоём: через сервер комнаты или напрямую по радио ----------
// Сообщения: «chs seek», «chs start <кому> <1 - ты белые>», «chs move <кому> <ход>», «chs bye <кому>», «chs resign <кому>».
void runChessNet(bool direct) {
  if (!ENABLE_CHESS) {
    notInEdition();
    return;
  }
  if (direct) {
    if (!enow::begin()) {
      viewAnswer(L3("Не удалось включить прямую связь.", "Could not start the direct link.", "无法启动直连。"), C_RED);
      return;
    }
  } else {
    if (inRoom) return;
    if (!waitWifi(false)) return;
    if (!cfgNick.length() && !askNick()) return;
    if (!mqUp) {
      showMsg(L3("Подключаюсь...", "Connecting...", "正在连接……"), C_YELLOW);
      mqConnect(6000);
    }
    inRoom = true;
  }
  g_netDirect = direct;
  struct Leave {
    bool direct;
    ~Leave() {
      chsFlip = false;
      if (direct) {
        enow::end();
        g_netDirect = false;
      } else {
        inRoom = false;
        if (!(cfgRoomBg && cfgNick.length())) mqDrop();
      }
    }
  } leave{direct};
  int wins = 0, losses = 0, round = 0;
  for (;;) {
    // --- поиск соперника (как в «Крестиках по сети») ---
    String peer, peerNick;
    bool myWhite = true;
    netInbox.clear();
    showMsg(direct ? L3("Ищу гаджет рядом. Пусть друг откроет «Шахматы рядом». Держать - выход.",
                        "Looking for a device nearby. Ask a friend to open Chess nearby. Hold - exit.", "正在寻找附近的设备。长按：退出。")
                   : String(L3("Ищу соперника в комнате #", "Looking for an opponent in room #", "正在房间里寻找对手 #")) + cfgRoom +
                         L3(". Пусть друг откроет «Шахматы по сети». Держать - выход.", ". Ask a friend to open Chess online. Hold - exit.", "。长按：退出。"),
            C_YELLOW);
    uint32_t lastSeek = 0, lastConn = millis();
    int shownLink = -1;
    while (!peer.length()) {
      if (btnEvent() == EV_LONG) return;
      netPump();
      if (!direct && !mqUp && millis() - lastConn > 5000) {
        lastConn = millis();
        mqConnect(3000);
      }
      if ((int)netLinkUp() != shownLink) {
        shownLink = netLinkUp();
        SmallFont small;
        tft.fillRect(0, 222, 240, 18, C_BLACK);
        drawUtf8(shownLink ? (direct ? L3("радио: включено", "radio: on", "无线：开") : L3("сервер: подключено", "server: connected", "服务器：已连接"))
                           : (direct ? L3("радио: нет", "radio: off", "无线：关") : L3("сервер: нет связи, переподключаюсь", "server: no link, reconnecting", "服务器：重连中")),
                 4, 222, shownLink ? C_GREEN : C_RED, C_BLACK);
      }
      if (netLinkUp() && millis() - lastSeek > 2000) {
        lastSeek = millis();
        netSend("chs\tseek");
      }
      String id, nick, game, cmd, to;
      int arg;
      while (netNext(id, nick, game, cmd, to, arg)) {
        if (game != "chs") continue;
        if (cmd == "seek" && strcmp(roomId.c_str(), id.c_str()) < 0) {
          peer = id;
          peerNick = nick;
          myWhite = round % 2 == 0;                  // цвет чередуется от партии к партии
          netSend(String("chs\tstart\t") + peer + "\t" + (myWhite ? "0" : "1"));
          break;
        }
        if (cmd == "start" && to == roomId) {
          peer = id;
          peerNick = nick;
          myWhite = arg == 1;
          break;
        }
      }
      delay(10);
    }
    sndOk();
    if (!peerNick.length()) peerNick = peer;
    // --- партия ---
    chs.reset();
    chsFlip = !myWhite;
    int cur = myWhite ? 12 : 52, sel = -1, lastFrom = -1, lastTo = -1;
    uint64_t targets = 0;
    chess::Move legal[chess::MAX_MOVES];
    int nLegal = chs.legal(legal);
    auto mine = [&](int s) { return myWhite ? chs.sq[s] > 0 : chs.sq[s] < 0; };
    auto myTurn = [&]() { return chs.white == myWhite; };
    auto markOf = [&](int s) {
      return ((targets >> s) & 1 ? 1 : 0) | (s == cur ? 2 : 0) | (s == sel ? 4 : 0) | ((s == lastFrom || s == lastTo) ? 8 : 0);
    };
    auto drawAll = [&]() {
      tft.fillRect(0, 0, CH_X0, 216, C_BLACK);
      tft.fillRect(CH_X0 + 216, 0, 240 - CH_X0 - 216, 216, C_BLACK);
      {
        SmallFont small;
        for (int r = 0; r < 8; r++) drawCp((uint16_t)('1' + r), 4, (chsFlip ? r : 7 - r) * CH_CELL + 6, C_LIGHT, C_BLACK);
      }
      for (int sq = 0; sq < 64; sq++) drawChessSquare(sq, markOf(sq));
    };
    auto status = [&]() {
      String t = myTurn() ? String(L3("Ваш ход", "Your move", "轮到你")) + (myWhite ? L3(" (белые)", " (White)", "（白）") : L3(" (чёрные)", " (Black)", "（黑）"))
                          : String(L3("Ход: ", "Move: ", "对手：")) + peerNick;
      if (myTurn() && chs.inCheck(myWhite)) t = L3("Вам шах!", "You are in check!", "将军！");
      chessStatus(String(wins) + ":" + String(losses) + "  " + t, myTurn() ? (chs.inCheck(myWhite) ? C_RED : C_GREEN) : C_YELLOW);
    };
    tft.fillScreen(C_BLACK);
    drawAll();
    status();
    chess::Result res = chs.result();
    bool gone = false, resigned = false, oppResigned = false;
    uint32_t lastHeard = millis();
    while (res == chess::PLAYING && !gone && !resigned && !oppResigned) {
      netPump();
      String id, nick, game, cmd, to;
      int arg;
      while (netNext(id, nick, game, cmd, to, arg)) {
        if (game != "chs" || id != peer) continue;
        lastHeard = millis();
        if (cmd == "bye") gone = true;
        if (cmd == "resign" && to == roomId) oppResigned = true;
        if (cmd == "move" && to == roomId && !myTurn() && arg >= 0) {
          chess::Move om = 0;                        // ход соперника - только если он есть среди возможных
          for (int i = 0; i < nLegal; i++)
            if (legal[i] == (chess::Move)arg) om = legal[i];
          if (om) {
            chess::Undo u;
            chs.make(om, u);
            lastFrom = chess::mFrom(om);
            lastTo = chess::mTo(om);
            nLegal = chs.legal(legal);
            sel = -1;
            targets = 0;
            drawAll();
            beep(1, 700, 40);
            res = chs.result();
            status();
          }
        }
      }
      if (millis() - lastHeard > 600000UL || (!netLinkUp() && millis() - lastHeard > (direct ? 8000UL : 60000UL))) gone = true;
      if (myTurn() && millis() - lastHeard > 30000UL) lastHeard = millis();   // думаю я - соперник может молчать
      uint8_t j = joyEvent();
      if (j != J_NONE) {
        int old = cur, r = cur >> 3, f = cur & 7;
        int dir = chsFlip ? -1 : 1;                  // у чёрных доска перевёрнута
        if (j == J_LEFT) f = (f + 8 - dir) % 8;
        if (j == J_RIGHT) f = (f + 8 + dir) % 8;
        if (j == J_UP) r = (r + 8 + dir) % 8;
        if (j == J_DOWN) r = (r + 8 - dir) % 8;
        cur = r * 8 + f;
        drawChessSquare(old, markOf(old));
        drawChessSquare(cur, markOf(cur));
      }
      BtnEvent e = btnEvent();
      if (e == EV_LONG) {
        std::vector<String> o = splitBar(L3("Продолжить|Сдаться|Выйти", "Continue|Resign|Leave", "继续|认输|退出"));
        int k = choiceScreen(L3("Шахматы по сети", "Chess online", "联网国际象棋"), o);
        if (k == 1) {
          netSend(String("chs\tresign\t") + peer);
          resigned = true;
          break;
        }
        if (k == 2) {
          netSend(String("chs\tbye\t") + peer);
          return;
        }
        tft.fillScreen(C_BLACK);
        drawAll();
        status();
      }
      if (e == EV_CLICK && myTurn()) {
        if (sel >= 0 && ((targets >> cur) & 1)) {      // мой ход
          chess::Move mv = 0;
          for (int i = 0; i < nLegal; i++)
            if (chess::mFrom(legal[i]) == sel && chess::mTo(legal[i]) == cur &&
                (!chess::mPromo(legal[i]) || chess::mPromo(legal[i]) == chess::QUEEN))
              mv = legal[i];
          chess::Undo u;
          chs.make(mv, u);
          netSend(String("chs\tmove\t") + peer + "\t" + String((unsigned)mv));
          lastFrom = sel;
          lastTo = cur;
          sel = -1;
          targets = 0;
          nLegal = chs.legal(legal);
          lastHeard = millis();
          drawAll();
          res = chs.result();
          status();
        } else if (mine(cur) && cur != sel) {
          uint64_t t = 0;
          for (int i = 0; i < nLegal; i++)
            if (chess::mFrom(legal[i]) == cur) t |= 1ULL << chess::mTo(legal[i]);
          if (t) {
            sel = cur;
            targets = t;
            drawAll();
          } else {
            chessStatus(L3("Этой фигурой сейчас не пойти", "This piece cannot move now", "这个棋子现在不能走"), C_YELLOW);
          }
        } else if (sel >= 0) {
          sel = -1;
          targets = 0;
          drawAll();
        }
      }
      delay(3);
    }
    round++;
    if (gone) {
      showMsg(L3("Соперник вышел из игры.", "The opponent has left.", "对手已离开。"), C_YELLOW);
      delay(1800);
      continue;
    }
    delay(700);
    bool won = oppResigned || (res == chess::MATE_WHITE_WINS && myWhite) || (res == chess::MATE_BLACK_WINS && !myWhite);
    bool lost = resigned || (res == chess::MATE_WHITE_WINS && !myWhite) || (res == chess::MATE_BLACK_WINS && myWhite);
    if (won) {
      wins++;
      saveBestKey("bchnet", loadBestKey("bchnet") + 1);
    } else if (lost) {
      losses++;
    }
    const char* title = oppResigned ? L3("Соперник сдался!", "The opponent resigned!", "对手认输！")
                        : resigned  ? L3("Вы сдались", "You resigned", "你认输了")
                        : won       ? L3("Мат! Вы победили", "Checkmate! You win", "将死！你赢了")
                        : lost      ? L3("Вам мат", "Checkmate. You lose", "你被将死了")
                                    : L3("Ничья", "Draw", "和棋");
    char b[24];
    snprintf(b, sizeof(b), "%d : %d", wins, losses);
    if (!resultBox(title, b, won ? C_GREEN : lost ? C_RED : C_YELLOW)) {
      netSend(String("chs\tbye\t") + peer);
      return;
    }
  }
}

// ========================= ИГРЫ С DEEPSEEK =========================

// Тема/жанр/категория: из списка или своя (набрать на клавиатуре). Пустая строка - выход.
String chooseTopic(const char* title, const char* list) {
  std::vector<String> items = splitBar(list);
  int k = listScreen(title, items, std::vector<String>(), 0);
  if (k < 0) return "";
  if (k == (int)items.size() - 1) {                  // «Своя тема...»
    showMsg(T().typeTopic, C_YELLOW);
    delay(1200);
    String t = inputText();
    t.trim();
    return t;
  }
  return items[(size_t)k];
}

// Запрос с одной повторной попыткой, если DeepSeek ответил не по формату.
// parse(reply) возвращает true, если ответ подходит. false - ошибка (уже показана).
bool askGame(const std::string& system, const std::vector<ChatMsg>& history, const String& q, int maxTokens,
             String& reply, std::function<bool(const std::string&)> parse) {
  for (int attempt = 0; attempt < 2; attempt++) {
    if (!waitWifi(false)) return false;
    showMsg(T().thinking, C_YELLOW);
    if (!askWith(system.c_str(), history, q, maxTokens, reply)) {
      viewAnswer(String(T().errPrefix) + reply, C_RED);
      return false;
    }
    if (parse(toStd(reply))) return true;
  }
  viewAnswer(T().aiBadFormat, C_RED);
  return false;
}

void keepLast(std::vector<ChatMsg>& h, size_t n) {
  while (h.size() > n) h.erase(h.begin(), h.begin() + 2);
}

// ---------- Викторина: 10 вопросов на выбранную тему ----------
void runQuiz() {
  if (!ENABLE_AIGAMES) {
    notInEdition();
    return;
  }
  if (!waitWifi(false)) return;                       // без связи эти игры не работают
  String topic = chooseTopic(T().topicTitle, T().quizTopics);
  if (!topic.length()) return;
  std::string sys = aigames::quizSystem(T().promptLang, toStd(topic));
  for (;;) {                                         // один раунд
    std::vector<ChatMsg> hist;
    int score = 0;
    for (int qn = 1; qn <= 10; qn++) {
      aigames::Quiz q;
      String reply;
      if (!askGame(sys, hist, "Next question.", 350, reply, [&](const std::string& r) { return aigames::parseQuiz(r, q); }))
        return;
      hist.push_back({true, "Next question."});
      hist.push_back({false, reply});
      keepLast(hist, 16);
      char head[48];
      snprintf(head, sizeof(head), T().quizNum, qn);
      std::vector<String> opts;
      for (int i = 0; i < 4; i++) opts.push_back(String((char)('A' + i)) + ") " + fromStd(q.option[i]));
      int k = choiceScreen(String(head) + "\n" + fromStd(q.question), opts);
      if (k < 0) return;
      bool ok = k == q.answer;
      if (ok) score++;
      String msg = ok ? String(T().right) : String(T().wrongIs) + String((char)('A' + q.answer)) + ") " +
                                                fromStd(q.option[q.answer]);
      if (q.why.size()) msg += "\n\n" + fromStd(q.why);
      viewAnswer(msg, ok ? C_GREEN : C_RED);
    }
    saveBestKey("bquiz", (uint32_t)score);
    char b[48];
    snprintf(b, sizeof(b), T().quizResult, score);
    if (!resultBox(T().gQuiz, b, score >= 7 ? C_GREEN : C_YELLOW)) return;
  }
}

// ---------- Приключение: DeepSeek ведёт историю, вы выбираете действия ----------
void runStory() {
  if (!ENABLE_AIGAMES) {
    notInEdition();
    return;
  }
  if (!waitWifi(false)) return;                       // без связи эти игры не работают
  String genre = chooseTopic(T().genreTitle, T().storyGenres);
  if (!genre.length()) return;
  std::string sys = aigames::storySystem(T().promptLang, toStd(genre));
  for (;;) {                                         // одна история
    std::vector<ChatMsg> hist;
    String say = "Start the story.";
    for (;;) {
      aigames::StoryTurn t;
      String reply;
      if (!askGame(sys, hist, say, 450, reply, [&](const std::string& r) { return aigames::parseStory(r, t); }))
        return;
      hist.push_back({true, say});
      hist.push_back({false, reply});
      keepLast(hist, 20);
      viewAnswer(fromStd(t.story), C_WHITE);
      if (t.end) {
        if (!resultBox(T().storyEnd, T().gStory, C_GREEN)) return;
        break;
      }
      std::vector<String> opts;
      for (size_t i = 0; i < t.choices.size(); i++) opts.push_back(String((int)(i + 1)) + ") " + fromStd(t.choices[i]));
      int k = choiceScreen(T().storyWhat, opts);
      if (k < 0) return;
      say = fromStd(t.choices[(size_t)k]);
    }
  }
}

// ---------- «Угадай, кто я»: 5 раундов, чем меньше подсказок - тем больше очков ----------
void runGuess() {
  if (!waitWifi(false)) return;                       // без связи эти игры не работают
  String cat = chooseTopic(T().guessTitle, T().guessCats);
  if (!cat.length()) return;
  std::string sys = aigames::guessSystem(T().promptLang, toStd(cat));
  for (;;) {                                         // одна игра
    int total = 0;
    String used;                                     // что уже загадывали - чтобы не повторялось
    for (int round = 1; round <= 5; round++) {
      aigames::Guess g;
      String reply;
      String q = "New round.";
      if (used.length()) q += " Do not pick any of: " + used;
      if (!askGame(sys, std::vector<ChatMsg>(), q, 400, reply,
                   [&](const std::string& r) { return aigames::parseGuess(r, g, (unsigned)gameRandom()); }))
        return;
      used += (used.length() ? ", " : "") + fromStd(g.secret);
      int shown = 1;
      for (;;) {
        char head[48];
        snprintf(head, sizeof(head), T().guessRound, round);
        String h = head;
        for (int i = 0; i < shown; i++) h += "\n" + String(i + 1) + ". " + fromStd(g.hints[(size_t)i]);
        std::vector<String> opts;
        for (const std::string& o : g.options) opts.push_back(fromStd(o));
        bool more = shown < (int)g.hints.size();
        if (more) opts.push_back(T().guessMore);
        int k = choiceScreen(h, opts);
        if (k < 0) return;
        if (more && k == (int)opts.size() - 1) {
          shown++;
          continue;
        }
        bool ok = k == g.answer;
        int pts = aigames::guessPoints(shown, ok);
        total += pts;
        char b[48];
        snprintf(b, sizeof(b), T().guessRight, pts);
        viewAnswer(ok ? String(b) : String(T().wrongIs) + fromStd(g.secret), ok ? C_GREEN : C_RED);
        break;
      }
    }
    saveBestKey("bguess", (uint32_t)total);
    char b[48];
    snprintf(b, sizeof(b), T().guessTotal, total);
    if (!resultBox(T().gGuess, b, C_GREEN)) return;
  }
}

// ========================= МЕНЮ ИГР =========================
// Игры по порядку меню: ключ рекорда (или "") и название.
const char* gameTitle(const char* key) {
  static String keep;
  String k = key;
  if (k == "best") return T().gBlocks;
  if (k == "b2048") return "2048";
  if (k == "bsnake") return T().gSnake;
  if (k == "bfall") return T().gFall;
  if (k == "bmaze") return T().gMaze;
  if (k == "bmem") return T().gMemory;
  if (k == "bpong") return T().gPong;
  if (k == "bflap") return T().gFlappy;
  if (k == "brace") return T().gRace;
  if (k == "bbrk") return L3("Арканоид", "Breakout", "打砖块");
  if (k == "binv") return L3("Пришельцы", "Invaders", "太空侵略者");
  if (k == "bmines") return L3("Сапёр", "Minesweeper", "扫雷");
  if (k == "bsimon") return L3("Повтори", "Repeat", "记忆重复");
  if (k == "bttt") return L3("Крестики-нолики", "Tic-tac-toe", "井字棋");
  if (k == "bchess") return L3("Шахматы", "Chess", "国际象棋");
  if (k == "brider") return L3("Райдер", "Rider", "摩托骑手");
  if (k == "bfps") return L3("Подземелье 3D", "Dungeon 3D", "地牢3D");
  if (k == "bquiz") return T().gQuiz;
  if (k == "bguess") return T().gGuess;
  return key;
}

// ========================= ПЕСОЧНИЦА =========================
// Логика - в sandbox_game.h. Здесь картинка, управление, меню и сохранение мира.
#if ENABLE_SANDBOX
const int SB_TOP = 16, SB_VH = 224;                  // сверху 16 точек - строка состояния, ниже - мир
struct SbGfx {
  uint16_t tex[sbx::BLOCKS][5][64];                  // текстуры блоков при 5 уровнях света
  uint16_t wall[5][64];                              // стена пещеры (фон под землёй)
  uint16_t line[240 * 8];                            // полоса экрана 240x8
  uint8_t light[34][32];                             // свет видимых блоков
};
sbx::Game* sbG = nullptr;
SbGfx* sbX = nullptr;
const uint16_t SB_KEY = 0xF81F;                      // «прозрачный» цвет в текстуре факела

uint16_t sbRgb(int r, int g, int b) {
  r = r < 0 ? 0 : r > 255 ? 255 : r;
  g = g < 0 ? 0 : g > 255 ? 255 : g;
  b = b < 0 ? 0 : b > 255 ? 255 : b;
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
uint16_t sbShade(uint16_t c, int lvl) {              // 4 - как есть, 0 - почти черно
  static const int K[5] = {40, 90, 145, 200, 256};
  if (c == SB_KEY) return c;
  int r = (c >> 11) << 3, g = ((c >> 5) & 63) << 2, b = (c & 31) << 3;
  return sbRgb(r * K[lvl] >> 8, g * K[lvl] >> 8, b * K[lvl] >> 8);
}
uint32_t sbHash(uint32_t a) {
  a ^= a >> 16;
  a *= 0x7feb352d;
  a ^= a >> 15;
  a *= 0x846ca68b;
  return a ^ (a >> 16);
}

// Простые узоры для блоков: основной цвет с шумом и детали.
void sbMakeTex(uint8_t b, uint16_t* out) {
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++) {
      int n = (int)(sbHash(b * 977 + y * 8 + x) & 31) - 16;   // шум -16..15
      int r = 0, g = 0, bl = 0;
      switch (b) {
        case sbx::DIRT: r = 134; g = 90; bl = 54; break;
        case sbx::GRASS:
          if (y < 2 || (y == 2 && (x & 1))) { r = 70; g = 170; bl = 60; }
          else { r = 134; g = 90; bl = 54; }
          break;
        case sbx::STONE: r = g = bl = 120; if ((x + y * 3) % 7 == 0) n -= 30; break;
        case sbx::LOG: r = 110; g = 74; bl = 40; if (x == 2 || x == 5) n -= 35; break;
        case sbx::LEAVES: r = 40; g = 130; bl = 45; if (sbHash(x * 31 + y * 7) % 5 == 0) { r = 25; g = 90; bl = 30; } break;
        case sbx::PLANKS: r = 180; g = 134; bl = 80; if (y == 3 || y == 7) n -= 45; if ((y < 4 && x == 6) || (y >= 4 && x == 2)) n -= 35; break;
        case sbx::COAL: case sbx::IRON: case sbx::GOLD: {
          r = g = bl = 120;
          bool speck = sbHash(b * 13 + x * 5 + y * 11) % 4 == 0 && x > 0 && x < 7 && y > 0 && y < 7;
          if (speck) {
            if (b == sbx::COAL) { r = g = bl = 30; }
            else if (b == sbx::IRON) { r = 210; g = 160; bl = 120; }
            else { r = 250; g = 210; bl = 40; }
          }
          break;
        }
        case sbx::SAND: r = 220; g = 200; bl = 130; break;
        case sbx::BEDROCK: r = g = bl = 45; if ((x ^ y) & 2) n -= 20; break;
        case sbx::BENCH:
          if (y < 3) { r = 170; g = 120; bl = 70; }
          else if (x == 1 || x == 6) { r = 120; g = 80; bl = 45; }
          else { out[y * 8 + x] = SB_KEY; continue; }
          break;
        case sbx::BRICK: r = 150; g = 150; bl = 155; if (y == 3 || y == 7 || (y < 4 && x == 3) || (y >= 4 && x == 7)) { r = g = bl = 80; n = 0; } break;
        case sbx::TORCH:
          if (x >= 3 && x <= 4 && y >= 3) { r = 120; g = 80; bl = 40; n = 0; }
          else if (x >= 3 && x <= 4 && y >= 1) { r = 255; g = y == 1 ? 240 : 170; bl = 40; n = 0; }
          else { out[y * 8 + x] = SB_KEY; continue; }
          break;
        default: r = g = bl = 255;
      }
      out[y * 8 + x] = sbRgb(r + n, g + n, bl + n);
    }
}

bool sbGfxInit() {
  if (!sbX) sbX = (SbGfx*)malloc(sizeof(SbGfx));
  if (!sbX) return false;
  uint16_t base[64];
  for (int b = 1; b < sbx::BLOCKS; b++) {
    sbMakeTex((uint8_t)b, base);
    for (int l = 0; l < 5; l++)
      for (int i = 0; i < 64; i++) sbX->tex[b][l][i] = sbShade(base[i], l);
  }
  for (int i = 0; i < 64; i++) {                     // стена пещеры: тёмный камень
    int n = (int)(sbHash(i * 7 + 3) & 15);
    base[i] = sbRgb(52 + n, 44 + n, 40 + n);
  }
  for (int l = 0; l < 5; l++)
    for (int i = 0; i < 64; i++) sbX->wall[l][i] = sbShade(base[i], l);
  return true;
}

uint16_t sbSky() {                                   // цвет неба по времени суток
  static const uint16_t SKY[5] = {0x0000, 0x08A6, 0x31AD, 0x4C1A, 0x6D7F};
  return SKY[sbG->skyLight()];
}

// Свет для видимых блоков: небо сверху, глубже - темнее; факелы светят вокруг себя.
void sbLight(int tx0, int ty0) {
  sbx::Game& g = *sbG;
  int sky = g.skyLight();
  for (int j = 0; j < 30; j++)
    for (int i = 0; i < 32; i++) {
      int tx = tx0 + i, ty = ty0 + j;
      int l;
      if (tx < 0 || tx >= sbx::W) l = 0;
      else {
        int d = ty - g.surf[tx];
        l = d < 0 ? sky : sky - (d + 1) / 2;
        if (l < 0) l = 0;
      }
      sbX->light[j][i] = (uint8_t)l;
    }
  for (int ty = ty0 - 5; ty < ty0 + 35; ty++)
    for (int tx = tx0 - 5; tx < tx0 + 37; tx++) {
      if (g.at(tx, ty) != sbx::TORCH) continue;
      for (int j = ty - 5; j <= ty + 5; j++)
        for (int i = tx - 5; i <= tx + 5; i++) {
          int jj = j - ty0, ii = i - tx0;
          if (jj < 0 || jj >= 30 || ii < 0 || ii >= 32) continue;
          int d = abs(i - tx) + abs(j - ty);
          int l = 4 - d / 2;
          if (l > sbX->light[jj][ii]) sbX->light[jj][ii] = (uint8_t)l;
        }
    }
}

// Человечек 8x16 (свой рисунок): каска, лицо, рубашка, штаны. 0 - прозрачно.
static const char* const SB_MAN[16] = {
    "..yyyy..", ".yyyyyy.", ".ssssss.", ".sksskk.", ".ssssss.", "..ssss..", ".bbbbbb.", "bbbbbbbb",
    "sbbbbbbs", "sbbbbbbs", "s.bbbb.s", "..pppp..", "..pp.pp.", "..pp.pp.", "..pp.pp.", ".hh..hh."};
uint16_t sbManColor(char c) {
  switch (c) {
    case 'y': return 0xFE60;
    case 's': return 0xF5B0;
    case 'k': return 0x2104;
    case 'b': return 0x2B5F;
    case 'p': return 0x5A69;
    case 'h': return 0x3186;
  }
  return SB_KEY;
}

void sbFrame() {
  sbx::Game& g = *sbG;
  int camX = (int)(g.px + sbx::PW / 2) - 120, camY = (int)(g.py + sbx::PH / 2) - SB_VH / 2;
  if (camX < 0) camX = 0;
  if (camX > sbx::W * sbx::T - 240) camX = sbx::W * sbx::T - 240;
  if (camY < 0) camY = 0;
  if (camY > sbx::H * sbx::T - SB_VH) camY = sbx::H * sbx::T - SB_VH;
  int tx0 = camX >> 3, ty0 = camY >> 3;
  sbLight(tx0, ty0);
  uint16_t sky = sbSky();
  int atx, aty;
  {
    int t1, t2;
    g.target(t1, t2, false);
    if (g.at(t1, t2) == sbx::AIR) g.target(t1, t2, true);
    atx = t1;
    aty = t2;
  }
  tft.startWrite();
  tft.setAddrWindow(0, SB_TOP, 240, SB_VH);
  for (int s = 0; s < SB_VH / 8; s++) {
    uint16_t* L = sbX->line;
    for (int r = 0; r < 8; r++) {
      int wy = camY + s * 8 + r, ty = wy >> 3, fy = wy & 7;
      uint16_t* row = L + r * 240;
      for (int x = 0; x < 240;) {
        int wx = camX + x, tx = wx >> 3, fx = wx & 7;
        int n = 8 - fx;
        if (x + n > 240) n = 240 - x;
        uint8_t b = g.at(tx, ty);
        int lv = sbX->light[ty - ty0][tx - tx0];
        bool under = tx >= 0 && tx < sbx::W && ty > g.g0[tx];
        const uint16_t* bg = under ? &sbX->wall[lv][fy * 8] : nullptr;
        if (b == sbx::AIR || b == sbx::TORCH || b == sbx::BENCH) {
          const uint16_t* t = b != sbx::AIR ? &sbX->tex[b][b == sbx::TORCH ? 4 : lv][fy * 8] : nullptr;
          for (int k = 0; k < n; k++) {
            uint16_t c = t ? t[fx + k] : SB_KEY;
            row[x + k] = c != SB_KEY ? c : bg ? bg[fx + k] : sky;
          }
        } else {
          memcpy(row + x, &sbX->tex[b][lv][fy * 8 + fx], (size_t)n * 2);
        }
        x += n;
      }
    }
    // поверх: прицел, слизни, игрок
    int y0 = camY + s * 8;
    auto put = [&](int wx, int wy, uint16_t c) {
      int sx = wx - camX, sy = wy - y0;
      if (sx >= 0 && sx < 240 && sy >= 0 && sy < 8) L[sy * 240 + sx] = c;
    };
    if (aty * 8 + 8 > y0 && aty * 8 < y0 + 8) {
      uint16_t c = g.hits ? 0xFC00 : 0xFFFF;
      for (int k = 0; k < 8; k++) {
        put(atx * 8 + k, aty * 8, c);
        put(atx * 8 + k, aty * 8 + 7, c);
        put(atx * 8, aty * 8 + k, c);
        put(atx * 8 + 7, aty * 8 + k, c);
      }
    }
    for (const sbx::Slime& m : g.sl) {
      if (!m.alive) continue;
      int mx = (int)m.x, my = (int)m.y;
      if (my + 6 <= y0 || my >= y0 + 8) continue;
      for (int j = 0; j < 6; j++)
        for (int i = 0; i < 8; i++) {
          bool in = (j == 0 && i > 1 && i < 6) || (j == 1 && i > 0 && i < 7) || j > 1;
          if (!in) continue;
          uint16_t c = (j == 2 && (i == 2 || i == 5)) ? 0x0000 : (j == 1 && i == 2) ? 0xBFF7 : (g.night() ? 0x2E9F : 0x3E67);
          put(mx + i, my + j, c);
        }
    }
    int pxi = (int)g.px - 1, pyi = (int)g.py - 2;
    if (pyi + 16 > y0 && pyi < y0 + 8 && (g.hurtT == 0 || (g.hurtT / 3) % 2 == 0)) {
      for (int j = 0; j < 16; j++) {
        if (pyi + j < y0 || pyi + j >= y0 + 8) continue;
        for (int i = 0; i < 8; i++) {
          char ch = SB_MAN[j][g.facing > 0 ? i : 7 - i];
          uint16_t c = sbManColor(ch);
          if (c != SB_KEY) put(pxi + i, pyi + j, c);
        }
      }
    }
    tft.writePixels(L, 240 * 8, true);
  }
  tft.endWrite();
}

const char* sbItemName(uint8_t it) {
  switch (it) {
    case sbx::DIRT: return L3("Земля", "Dirt", "泥土");
    case sbx::GRASS: return L3("Дёрн", "Grass", "草");
    case sbx::STONE: return L3("Камень", "Stone", "石头");
    case sbx::LOG: return L3("Бревно", "Log", "原木");
    case sbx::LEAVES: return L3("Листва", "Leaves", "树叶");
    case sbx::PLANKS: return L3("Доски", "Planks", "木板");
    case sbx::COAL: return L3("Уголь", "Coal", "煤");
    case sbx::IRON: return L3("Железо", "Iron", "铁");
    case sbx::GOLD: return L3("Золото", "Gold", "金");
    case sbx::SAND: return L3("Песок", "Sand", "沙子");
    case sbx::TORCH: return L3("Факел", "Torch", "火把");
    case sbx::BENCH: return L3("Верстак", "Workbench", "工作台");
    case sbx::BRICK: return L3("Кирпич", "Brick", "砖");
    case sbx::I_GEL: return L3("Слизь", "Gel", "凝胶");
    case sbx::I_PICK_WOOD: return L3("Деревянная кирка", "Wooden pickaxe", "木镐");
    case sbx::I_PICK_STONE: return L3("Каменная кирка", "Stone pickaxe", "石镐");
    case sbx::I_PICK_IRON: return L3("Железная кирка", "Iron pickaxe", "铁镐");
    case sbx::I_SWORD_WOOD: return L3("Деревянный меч", "Wooden sword", "木剑");
    case sbx::I_SWORD_IRON: return L3("Железный меч", "Iron sword", "铁剑");
    case sbx::I_CROWN: return L3("Золотая корона", "Golden crown", "金冠");
  }
  return "?";
}

// Значок предмета 8x8 (x2) для строки состояния.
void sbIcon(uint8_t it, int x, int y) {
  if (it > sbx::AIR && it < sbx::BLOCKS) {
    for (int j = 0; j < 8; j++)
      for (int i = 0; i < 8; i++) {
        uint16_t c = sbX->tex[it][4][j * 8 + i];
        tft.fillRect(x + i * 2, y + j * 2, 2, 2, c == SB_KEY ? C_BAR : c);
      }
    return;
  }
  tft.fillRect(x, y, 16, 16, C_BAR);
  if (it == sbx::I_GEL) tft.fillRoundRect(x + 3, y + 6, 10, 8, 3, 0x2E9F);
  else if (it == sbx::I_CROWN) {
    tft.fillRect(x + 2, y + 8, 12, 5, 0xFEA0);
    for (int k = 0; k < 3; k++) tft.fillTriangle(x + 2 + k * 5, y + 8, x + 4 + k * 5, y + 3, x + 6 + k * 5, y + 8, 0xFEA0);
  } else if (it != sbx::I_NONE) {
    uint16_t head = it == sbx::I_PICK_WOOD || it == sbx::I_SWORD_WOOD ? 0xB408 : it == sbx::I_PICK_STONE ? 0x8410 : 0xD69A;
    tft.drawLine(x + 3, y + 13, x + 12, y + 4, 0x8A22);
    if (it == sbx::I_SWORD_WOOD || it == sbx::I_SWORD_IRON) tft.drawLine(x + 6, y + 10, x + 13, y + 3, head), tft.drawLine(x + 7, y + 10, x + 14, y + 3, head);
    else tft.fillTriangle(x + 6, y + 2, x + 15, y + 5, x + 12, y + 8, head);
  }
}

String sbHudKey;                                     // что сейчас нарисовано в строке состояния
void sbHud(bool force) {
  sbx::Game& g = *sbG;
  const sbx::Slot& s = g.inv[g.sel];
  String key = String(g.hp) + "/" + String(s.item) + "/" + String(s.n) + "/" + String(g.night());
  if (!force && key == sbHudKey) return;
  sbHudKey = key;
  tft.fillRect(0, 0, 240, SB_TOP, C_BAR);
  for (int k = 0; k < sbx::MAX_HP / 2; k++) {        // сердца
    int x = 2 + k * 13, v = g.hp - k * 2;
    uint16_t c = v >= 2 ? 0xF904 : v == 1 ? 0xFB4D : 0x4208;
    tft.fillCircle(x + 3, 4, 3, c);
    tft.fillCircle(x + 8, 4, 3, c);
    tft.fillTriangle(x, 4 + 1, x + 11, 4 + 1, x + 5, 4 + 8, c);
  }
  sbIcon(s.item, 150, 0);
  SmallFont small;
  if (s.item) drawUtf8(String(s.n).c_str(), 168, 0, C_WHITE, C_BAR);
  if (g.night()) tft.fillCircle(230, 8, 5, 0xCE79), tft.fillCircle(232, 6, 4, C_BAR);
  else tft.fillCircle(230, 8, 5, 0xFEA0);
}

String sbPath() { return "/sbx.bin"; }
fs::FS& sbFs() {
  if (sdOk) return SD;
  return LittleFS;
}

String sbSave() {
  size_t n = sbx::Game::saveSize();
  uint8_t* buf = (uint8_t*)malloc(n);
  if (!buf) return L3("не хватило памяти", "out of memory", "内存不足");
  sbG->saveTo(buf);
  PicBus b;
  File f = sbFs().open(sbPath().c_str(), "w");
  size_t w = f ? f.write(buf, n) : 0;
  if (f) f.close();
  free(buf);
  if (w != n) {
    sbFs().remove(sbPath().c_str());
    return L3("не удалось записать (нет места?)", "cannot write (no space?)", "无法写入（空间不足？）");
  }
  return "";
}

bool sbLoad() {
  size_t n = sbx::Game::saveSize();
  uint8_t* buf = (uint8_t*)malloc(n);
  if (!buf) return false;
  bool ok = false;
  {
    PicBus b;
    File f = sbFs().open(sbPath().c_str(), "r");
    if (f && f.size() == n) ok = f.read(buf, n) == n;
    if (f) f.close();
  }
  ok = ok && sbG->loadFrom(buf, n);
  free(buf);
  return ok;
}

bool sbHasSave() {
  PicBus b;
  return sbFs().exists(sbPath().c_str());
}

void sbInventory() {
  sbx::Game& g = *sbG;
  std::vector<String> items, right;
  std::vector<int> idx;
  for (int i = 0; i < sbx::SLOTS; i++)
    if (g.inv[i].item) {
      items.push_back(String(i == g.sel ? "> " : "") + sbItemName(g.inv[i].item));
      right.push_back(String(g.inv[i].n));
      idx.push_back(i);
    }
  if (items.empty()) {
    viewAnswer(L3("Пока пусто. Копайте землю, рубите деревья - всё добытое попадает сюда.",
                  "Empty for now. Dig dirt and chop trees - everything you get goes here.", "还是空的。挖土、砍树，得到的东西都会放在这里。"), C_WHITE);
    return;
  }
  int k = listScreen(L3("Что ставить", "Block to place", "放置的方块"), items, right, 0);
  if (k < 0) return;
  uint8_t it = g.inv[idx[(size_t)k]].item;
  if (sbx::placeable(it)) g.sel = idx[(size_t)k];
  else viewAnswer(L3("Это не ставится. Инструменты работают сами, пока лежат в рюкзаке: лучшая кирка копает, лучший меч бьёт.",
                     "This cannot be placed. Tools work by themselves while in the backpack: the best pickaxe digs, the best sword hits.",
                     "这个不能放置。工具放在背包里就会自动使用。"), C_WHITE);
}

void sbCraft() {
  sbx::Game& g = *sbG;
  int sel = 0;
  for (;;) {
    std::vector<String> items, right;
    for (int r = 0; r < sbx::RECIPE_COUNT; r++) {
      const sbx::Recipe& c = sbx::RECIPES[r];
      items.push_back(String(sbItemName(c.out)) + (c.outN > 1 ? String(" x") + String(c.outN) : String("")));
      int st = g.canCraft(r);
      right.push_back(st == 0 ? L3("можно", "ok", "可以") : st == 2 ? L3("верстак", "bench", "工作台") : "-");
    }
    sel = listScreen(L3("Мастерить", "Craft", "制作"), items, right, sel);
    if (sel < 0) return;
    const sbx::Recipe& c = sbx::RECIPES[sel];
    int st = g.canCraft(sel);
    if (st == 0) {
      g.craft(sel);
      beep(1, 1500, 40);
      if (c.out == sbx::I_CROWN) {
        viewAnswer(L3("Золотая корона! Вы прошли Песочницу. Мир остаётся - стройте дальше.",
                      "A golden crown! You have beaten the Sandbox. The world stays - keep building.", "金冠！你通关了沙盒。世界会保留，继续建造吧。"), C_GREEN);
      }
      continue;
    }
    String need = String(L3("Нужно: ", "Needs: ", "需要：")) + sbItemName(c.a) + " " + String(c.an) + " (" + String(g.count(c.a)) + ")";
    if (c.b) need += String(", ") + sbItemName(c.b) + " " + String(c.bn) + " (" + String(g.count(c.b)) + ")";
    if (c.bench) need += L3(". И верстак рядом (поставьте его и встаньте возле).", ". And a workbench nearby (place one and stand next to it).", "。并且旁边要有工作台。");
    viewAnswer(need, C_YELLOW);
  }
}

void runSandbox() {
  if (!sbG) sbG = new (std::nothrow) sbx::Game();
  if (!sbG || !sbGfxInit()) {
    delete sbG;
    sbG = nullptr;
    viewAnswer(L3("Не хватило памяти для мира. Перезапустите гаджет и попробуйте ещё раз.", "Not enough memory for the world. Restart and try again.",
                  "内存不足。请重启后再试。"), C_RED);
    return;
  }
  struct Free {                                      // выход из игры - отдать память обратно
    ~Free() {
      delete sbG;
      sbG = nullptr;
      free(sbX);
      sbX = nullptr;
    }
  } freeOnExit;
  bool have = sbHasSave();
  std::vector<String> o = splitBar(have ? L3("Продолжить мир|Новый мир|Как играть|Выйти", "Continue the world|New world|How to play|Leave", "继续|新世界|玩法|退出")
                                        : L3("Новый мир|Как играть|Выйти", "New world|How to play|Leave", "新世界|玩法|退出"));
  for (;;) {
    int k = choiceScreen(L3("Песочница", "Sandbox", "沙盒"), o);
    if (!have) k = k < 0 ? -1 : k + 1;
    if (k == 2) {
      viewAnswer(L3("Ручка влево/вправо - идти, вверх - прыжок. Куда наклонена ручка - туда и цель (белая рамка).\n"
                    "Нажатие - ударить по блоку (копать), поставить выбранный блок в пустое место или ударить слизня.\n"
                    "Держать кнопку - меню: что ставить, мастерить, сохранить.\n\n"
                    "С чего начать: срубите дерево (бейте по стволу), сделайте доски и верстак, у верстака - деревянную кирку. "
                    "Ей уже можно копать камень и уголь, из угля - факелы. Глубже - железо, у самого дна - золото. "
                    "Ночью выходят слизни: стройте дом и делайте меч. Цель - золотая корона.",
                    "Stick left/right - walk, up - jump. Where the stick points is the target (white frame).\n"
                    "Press - hit a block (dig), place the selected block in an empty spot or hit a slime.\n"
                    "Hold the button - menu: block to place, craft, save.\n\n"
                    "Start: chop a tree (hit the trunk), make planks and a workbench, then a wooden pickaxe at the bench. "
                    "It digs stone and coal; coal makes torches. Iron is deeper, gold is at the very bottom. "
                    "Slimes come out at night: build a house and craft a sword. The goal is a golden crown.",
                    "摇杆左右走，上跳。摇杆指向的方向就是目标（白框）。按下：挖方块、放方块或攻击史莱姆。长按：菜单。"),
                 C_WHITE);
      continue;
    }
    if (k == 0) {
      if (!sbLoad()) {
        viewAnswer(L3("Сохранение не читается - начинаю новый мир.", "The save cannot be read - starting a new world.", "存档无法读取，开始新世界。"), C_YELLOW);
        sbG->generate(esp_random() | 1);
      }
      break;
    }
    if (k == 1) {
      showMsg(L3("Создаю мир...", "Creating the world...", "正在生成世界……"), C_YELLOW);
      sbG->generate(esp_random() | 1);
      break;
    }
    return;
  }
  KeepAwake awake;
  tft.fillScreen(C_BLACK);
  sbHud(true);
  const uint32_t TICK = 1000 / sbx::TICKS;
  uint32_t last = millis(), lastSave = millis();
  bool act = false;
  for (;;) {
    BtnEvent e = btnEvent();
    if (e == EV_CLICK) act = true;
    if (e == EV_LONG) {
      std::vector<String> m = splitBar(L3("Продолжить|Что ставить|Мастерить|Сохранить и выйти|Выйти без сохранения",
                                          "Continue|Block to place|Craft|Save and leave|Leave without saving", "继续|放置的方块|制作|保存并退出|不保存退出"));
      int k = choiceScreen(L3("Песочница", "Sandbox", "沙盒"), m);
      if (k == 1) sbInventory();
      if (k == 2) sbCraft();
      if (k == 3) {
        showMsg(L3("Сохраняю мир...", "Saving the world...", "正在保存……"), C_YELLOW);
        String err = sbSave();
        if (err.length()) {
          viewAnswer(String(L3("Не сохранилось: ", "Not saved: ", "未保存：")) + err, C_RED);
          continue;
        }
        return;
      }
      if (k == 4) {
        std::vector<String> yn = splitBar(L3("Выйти|Остаться", "Leave|Stay", "退出|留下"));
        if (choiceScreen(L3("Выйти без сохранения? Всё, что сделано после последнего сохранения, пропадёт.",
                            "Leave without saving? Everything since the last save will be lost.", "不保存退出？"), yn) == 0)
          return;
      }
      tft.fillScreen(C_BLACK);
      sbHud(true);
      last = millis();
      act = false;
      continue;
    }
    uint32_t behind = millis() - last;
    if (behind < TICK) {
      delay(1);
      continue;
    }
    int steps = (int)(behind / TICK);
    if (steps > 3) {
      steps = 3;
      last = millis() - 3 * TICK;
    }
    last += (uint32_t)steps * TICK;
    int dx, dy;
    joyAnalog(dx, dy);
    int thr = joyThr(), dead = thr / 4;
    sbx::Input in;
    in.mx = dx > dead ? (float)(dx - dead) / (thr - dead) : dx < -dead ? (float)(dx + dead) / (thr - dead) : 0;
    if (in.mx > 1) in.mx = 1;
    if (in.mx < -1) in.mx = -1;
    int ax = abs(dx), ay = abs(dy);
    if (ay > thr && ay >= ax) in.aim = dy < 0 ? sbx::A_UP : sbx::A_DOWN;
    else if (ax > thr / 2) in.aim = dx > 0 ? sbx::A_RIGHT : sbx::A_LEFT;
    in.jump = dy < -thr;
    int ev = 0;
    for (int i = 0; i < steps; i++) {
      in.act = act && i == 0;
      ev |= sbG->step(in);
    }
    act = false;
    if (ev & sbx::E_MINE) beep(2, 700, 6);
    else if (ev & sbx::E_HIT) beep(2, 400, 4);
    if (ev & sbx::E_PLACE) beep(2, 1100, 5);
    if (ev & (sbx::E_ATTACK | sbx::E_KILL)) beep(2, 1800, 6);
    if (ev & sbx::E_HURT) beep(1, 220, 30);
    sbFrame();
    sbHud(false);
    if (ev & sbx::E_DEAD) {
      drawSmallCenteredBg(L3("Вы погибли... снова дома", "You died... back home", "你死了……回到出生点"), 110, C_RED, C_BLACK);
      delay(1500);
      last = millis();
    }
    if (millis() - lastSave > 180000) {               // раз в 3 минуты - само сохраняется
      sbSave();
      lastSave = millis();
      last = millis();
    }
    if (admService()) {
      tft.fillScreen(C_BLACK);
      sbHud(true);
      last = millis();
    }
  }
}
#else
void runSandbox() { notInEdition(); }
#endif

void runGamesMenu() {
  int sel = 0;
  for (;;) {
    std::vector<String> names = {T().gBlocks, "2048", T().gSnake, T().gFall, T().gMaze, T().gMemory,
                                 T().gPong, T().gFlappy, T().gRace, L3("Райдер", "Rider", "摩托骑手"),
                                 L3("Арканоид", "Breakout", "打砖块"), L3("Пришельцы", "Invaders", "太空侵略者"),
                                 L3("Сапёр", "Minesweeper", "扫雷"), L3("Повтори", "Repeat", "记忆重复"),
                                 L3("Крестики-нолики", "Tic-tac-toe", "井字棋"), L3("Крестики по сети", "Tic-tac-toe online", "联网井字棋"),
                                 L3("Крестики рядом", "Tic-tac-toe nearby", "附近井字棋"), L3("Шахматы", "Chess", "国际象棋"),
                                 L3("Шахматы по сети", "Chess online", "联网国际象棋"), L3("Шахматы рядом", "Chess nearby", "附近国际象棋"),
                                 T().gQuiz, T().gStory, T().gGuess, L3("Подземелье 3D", "Dungeon 3D", "地牢3D"),
                                 L3("Песочница", "Sandbox", "沙盒"), L3("Рекорды игроков", "Players' records", "玩家记录"), T().menuBack};
    const char* keys[] = {"best", "b2048", "bsnake", "bfall", "bmaze", "bmem", "bpong", "bflap", "brace", "brider",
                          "bbrk", "binv", "bmines", "bsimon", "bttt", "", "", "bchess", "bchnet", "", "bquiz", "", "bguess", "bfps", "", "", ""};
    std::vector<String> right;
    for (size_t i = 0; i < names.size(); i++) {
      uint32_t v = keys[i][0] ? loadBestKey(keys[i]) : 0;
      right.push_back(v ? String((unsigned)v) : String(""));
    }
    sel = listScreen(T().menuGames, names, right, sel);
    InGame inGame;                                   // наклон гаджета работает только внутри игр
    switch (sel) {
      case 0: runGame(); break;
      case 1: run2048(); break;
      case 2: runSnake(); break;
      case 3: runFalling(); break;
      case 4: runMaze(); break;
      case 5: runMemory(); break;
      case 6: runPong(); break;
      case 7: runFlappy(); break;
      case 8: runRace(); break;
      case 9: runRider(); break;
      case 10: runBreakout(); break;
      case 11: runInvaders(); break;
      case 12: runMines(); break;
      case 13: runSimon(); break;
      case 14: runTtt(); break;
      case 15: runTttNet(); break;
      case 16: runTttNear(); break;
      case 17: runChess(); break;
      case 18: runChessNet(false); break;
      case 19: runChessNet(true); break;
      case 20: runQuiz(); break;
      case 21: runStory(); break;
      case 22: runGuess(); break;
      case 23: runFps3d(); break;
      case 24: runSandbox(); break;
      case 25: runHiScores(); break;
      default: return;                               // «Назад» или удержание
    }
  }
}

#if ENABLE_PCSCREEN
// ========================= ЭКРАН ПК (картинки с компьютера по USB) =========================
// На компьютере страница pc.html в Chrome пишет в USB-порт гаджета (Web Serial).
// ПК -> "DSJ1" + длина JPEG (4 байта, младший первым) + флаги (1 = ещё и сохранить в галерею);
// гаджет -> "DSPC GO"; ПК шлёт JPEG кусками по 2048 байт, после каждого куска кроме последнего гаджет
// отвечает "DSPC A"; в конце "DSPC OK" / "DSPC SAVED" / "DSPC ERR <причина>".
// Пока открыто приложение, гаджет раз в 2 с пишет "DSPC READY 240 240".
const size_t PC_CHUNK = 2048, PC_MAX = 60000;

bool pcJpgOut(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bmp) {
  if (y >= 240 || x >= 240 || x + (int)w <= 0 || y + (int)h <= 0) return true;
  int x0 = x < 0 ? 0 : x, x1 = x + w > 240 ? 240 : x + w;
  int y0 = y < 0 ? 0 : y, y1 = y + h > 240 ? 240 : y + h;
  tft.startWrite();
  tft.setAddrWindow(x0, y0, x1 - x0, y1 - y0);
  for (int yy = y0; yy < y1; yy++) tft.writePixels(bmp + (yy - y) * w + (x0 - x), (uint32_t)(x1 - x0), true);
  tft.endWrite();
  return true;
}

// Прочитать ровно n байт; ms - сколько ждать, если данные перестали приходить.
bool pcRead(uint8_t* dst, size_t n, uint32_t ms) {
  size_t got = 0;
  uint32_t t = millis();
  while (got < n) {
    int a = Serial.available();
    if (a > 0) {
      got += Serial.read(dst + got, n - got < (size_t)a ? n - got : (size_t)a);
      t = millis();
    } else if (millis() - t > ms) {
      return false;
    } else {
      delay(1);
    }
  }
  return true;
}

void pcIntro() {
  tft.fillScreen(C_BLACK);
  SmallFont small;
  drawUtf8(L3("Экран ПК", "PC screen", "电脑屏幕"), 8, 10, C_WHITE, C_BLACK);
  drawUtf8(L3("1. Подключите USB к ПК.", "1. Plug USB into the PC.", "1. 用USB连接电脑。"), 8, 44, C_LIGHT, C_BLACK);
  drawUtf8(L3("2. На ПК откройте в Chrome:", "2. On the PC open in Chrome:", "2. 在电脑的Chrome中打开："), 8, 66, C_LIGHT, C_BLACK);
  drawUtf8("yacovgopnik.github.io/", 8, 90, C_YELLOW, C_BLACK);
  drawUtf8("deepseek-pocket/pc.html", 8, 108, C_YELLOW, C_BLACK);
  drawUtf8(L3("3. Нажмите «Подключить» и", "3. Press Connect and pick", "3. 点“连接”，"), 8, 134, C_LIGHT, C_BLACK);
  drawUtf8(L3("выберите порт гаджета.", "the gadget's port.", "选择设备端口。"), 8, 152, C_LIGHT, C_BLACK);
  drawUtf8(L3("Нажатие ручки - выход", "Press the stick to leave", "按下摇杆退出"), 8, 214, C_LIGHT, C_BLACK);
}

// Принять один кадр (метка "DSJ1" уже прочитана).
void pcFrame(int& lastW, int& lastH) {
  uint8_t hdr[5];
  if (!pcRead(hdr, 5, 500)) {
    Serial.println("DSPC ERR header");
    return;
  }
  uint32_t n = hdr[0] | (hdr[1] << 8) | ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
  uint8_t flags = hdr[4];
  if (n < 100 || n > PC_MAX) {
    Serial.println("DSPC ERR size");
    return;
  }
  uint8_t* buf = (uint8_t*)malloc(n);
  if (!buf) {
    Serial.println("DSPC ERR memory");
    return;
  }
  Serial.println("DSPC GO");
  size_t got = 0;
  while (got < n) {
    size_t k = n - got < PC_CHUNK ? n - got : PC_CHUNK;
    if (!pcRead(buf + got, k, 1500)) {
      free(buf);
      Serial.println("DSPC ERR timeout");
      return;
    }
    got += k;
    if (got < n) Serial.println("DSPC A");
  }
  if (screenAsleep) wakeScreen();
  noteActivity();
  uint16_t w = 0, h = 0;
  bool jpeg = buf[0] == 0xFF && buf[1] == 0xD8;
  if (jpeg) TJpgDec.getJpgSize(&w, &h, buf, n);
  int sc = 1;
  while (sc < 8 && ((int)w / sc > 240 || (int)h / sc > 240)) sc *= 2;
  int dw = w / sc, dh = h / sc;
  if (dw != lastW || dh != lastH) tft.fillScreen(C_BLACK);   // новый размер - убрать остатки прошлого кадра
  lastW = dw;
  lastH = dh;
  TJpgDec.setSwapBytes(false);
  TJpgDec.setJpgScale((uint8_t)sc);
  TJpgDec.setCallback(pcJpgOut);
  int rc = (jpeg && w && h) ? TJpgDec.drawJpg((240 - dw) / 2, (240 - dh) / 2, buf, n) : -1;
  TJpgDec.setCallback(jpgOutput);
  TJpgDec.setJpgScale(1);
  if (rc != 0) {
    free(buf);
    Serial.println("DSPC ERR decode");
    return;
  }
  if (flags & 1) {
    String err = savePic(buf, n);
    free(buf);
    if (err.length()) Serial.println("DSPC ERR " + err);
    else Serial.println("DSPC SAVED");
    return;
  }
  free(buf);
  Serial.println("DSPC OK");
}

void runPcScreen() {
  pcIntro();
  KeepAwake awake;
  static const char MAGIC[4] = {'D', 'S', 'J', '1'};
  int mi = 0, lastW = -1, lastH = -1;
  uint32_t lastReady = 0;
  for (;;) {
    BtnEvent e = btnEvent();
    joyEvent();
    if (e != EV_NONE) break;
    if (millis() - lastReady > 2000) {
      Serial.println("DSPC READY 240 240");
      lastReady = millis();
    }
    while (Serial.available() > 0) {
      int c = Serial.read();
      if (c == MAGIC[mi]) mi++;
      else mi = c == 'D' ? 1 : 0;
      if (mi == 4) {
        mi = 0;
        pcFrame(lastW, lastH);
        lastReady = millis();
      }
    }
    delay(2);
  }
  Serial.println("DSPC BYE");
}
#else
void runPcScreen() { notInEdition(); }
#endif

// ========================= РАСПИСАНИЕ (с сайта вуза) =========================
// Страница группы скачивается и разбирается по кусочкам (rasp.h), результат хранится в /rasp.txt -
// смотреть можно и без интернета. Обновляется само при открытии, если прошло больше 6 часов.
#if ENABLE_RASP
const char* RASP_URL_DEFAULT = "http://rasp2.achgaa.ru/4.html";
String raspUrl() {
  prefs.begin("dsk", true);
  String u = prefs.getString("raspurl", RASP_URL_DEFAULT);
  prefs.end();
  return u.length() ? u : String(RASP_URL_DEFAULT);
}
fs::FS& raspFs() {
  if (sdOk) return SD;
  return LittleFS;
}

// Скачать и разобрать. "" - получилось, иначе причина.
String raspDownload() {
  if (!wifiOk()) return L3("нет Wi-Fi", "no Wi-Fi", "没有Wi-Fi");
  String url = raspUrl();
  WiFiClientSecure sc;
  sc.setInsecure();
  WiFiClient pc;
  HTTPClient http;
  http.useHTTP10(true);                              // без «chunked»: поток читаем как есть
  http.setUserAgent("Mozilla/5.0 (DeepSeek pocket)");
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setConnectTimeout(15000);
  http.setTimeout(15000);
  bool ok = url.startsWith("https://") ? http.begin(sc, url) : http.begin(pc, url);
  if (!ok) return L3("неверный адрес страницы", "bad page address", "地址错误");
  int code = http.GET();
  if (code != 200) {
    String e = httpErr(code, code < 0 ? HTTPClient::errorToString(code) : String(""));
    http.end();
    return e;
  }
  WiFiClient* st = http.getStreamPtr();
  int total = http.getSize();
  rasp::Parser p;
  std::string head;                                  // начало страницы - показать, если не разобралась
  String tmp = "/rasp.tmp";
  size_t got = 0;
  bool wrOk = true;
  {
    PicBus b;
    File f = raspFs().open(tmp.c_str(), "w");
    if (!f) wrOk = false;
    uint8_t buf[512];
    uint32_t last = millis();
    while (wrOk && (http.connected() || st->available()) && (total < 0 || (int)got < total)) {
      int a = st->available();
      if (a <= 0) {
        if (millis() - last > 15000) break;
        delay(2);
        continue;
      }
      int n = st->readBytes(buf, a > (int)sizeof(buf) ? sizeof(buf) : a);
      if (n <= 0) continue;
      last = millis();
      got += n;
      if (head.size() < 400)
        for (int k = 0; k < n && head.size() < 400; k++)
          if (buf[k] >= 0x20 || buf[k] == '\n') rasp::cp1251(buf[k], head);
      p.feed(buf, n);
      if (!p.out.empty()) {
        if (f.write((const uint8_t*)p.out.data(), p.out.size()) != p.out.size()) wrOk = false;
        p.out.clear();
      }
    }
    p.finish();
    if (f && !p.out.empty() && f.write((const uint8_t*)p.out.data(), p.out.size()) != p.out.size()) wrOk = false;
    p.out.clear();
    if (f) f.close();
  }
  http.end();
  Serial.printf("[rasp] %u bytes, %d weeks, %d days\n", (unsigned)got, p.weeks, p.days);
  PicBus b;
  if (!wrOk) {
    raspFs().remove(tmp.c_str());
    return L3("не удалось записать (нет места?)", "cannot write (no space?)", "无法写入");
  }
  if (p.weeks == 0 || p.days == 0) {
    raspFs().remove(tmp.c_str());
    return String(L3("на странице не нашлось расписания (", "no schedule on the page (", "页面上没有课表（")) + String((unsigned)got) +
           L3(" байт, недель ", " bytes, weeks ", " 字节，周 ") + String(p.weeks) + L3(", дней ", ", days ", "，天 ") + String(p.days) +
           "; td " + String(p.nTd) + ", tr " + String(p.nTr) + ", table " + String(p.nTable) + ", <!-- " + String(p.nComment) + "/" +
           String((unsigned)p.maxComment) + (p.inComment ? " !" : "") + L3("). Начало страницы:\n", "). Page starts with:\n", "）。页面开头：\n") +
           String(head.substr(0, 300).c_str());
  }
  raspFs().remove("/rasp.txt");
  if (!raspFs().rename(tmp.c_str(), "/rasp.txt")) return L3("не удалось сохранить файл расписания", "cannot save the schedule file", "无法保存课表文件");
  struct tm t;
  prefs.begin("dsk", false);
  prefs.putULong("raspts", timeNow(t) ? (uint32_t)time(nullptr) : 1);
  prefs.end();
  return "";
}

bool raspLoad(rasp::Schedule& s, long fromDay) {
  std::string txt;
  {
    PicBus b;
    File f = raspFs().open("/rasp.txt", "r");
    if (!f) return false;
    txt.reserve(f.size() + 1);
    uint8_t buf[512];
    for (;;) {
      int n = f.read(buf, sizeof(buf));
      if (n <= 0) break;
      txt.append((const char*)buf, n);
    }
    f.close();
  }
  rasp::load(txt, s, fromDay);
  return !s.days.empty();
}

// Тип занятия из начала названия: «лек.Физика» -> «лекция», «Физика»
String raspType(const std::string& subj, std::string& rest) {
  struct T { const char* p; const char* ru; const char* en; };
  static const T TYPES[] = {{"лек.", "лекция", "lecture"}, {"пр.", "практика", "practice"}, {"лаб.", "лабораторная", "lab"},
                            {"сем.", "семинар", "seminar"}, {"зач.", "зачёт", "test"}, {"экз.", "экзамен", "exam"}, {"конс.", "консультация", "consult"}};
  for (const T& t : TYPES)
    if (subj.rfind(t.p, 0) == 0) {
      rest = subj.substr(strlen(t.p));
      while (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
      return cfgLang == LANG_RU ? t.ru : t.en;
    }
  rest = subj;
  return "";
}

// Минуты от полуночи по «08:05-09:40»: начало и конец
bool raspSpan(const std::string& t, int& a, int& b) {
  int h1, m1, h2, m2;
  if (sscanf(t.c_str(), "%d:%d-%d:%d", &h1, &m1, &h2, &m2) != 4) return false;
  a = h1 * 60 + m1;
  b = h2 * 60 + m2;
  return true;
}

void raspAddWrapped(std::vector<rasp::VLine>& out, const String& s, uint16_t c, int indent) {
  std::vector<uint16_t> cps;
  utf8ToCps(s.c_str(), cps);
  std::vector<Line> lines;
  wrapCps(cps, 30 - indent, lines);
  for (const Line& l : lines) {
    String t;
    for (int i = 0; i < indent; i++) t += ' ';
    t += cpsToUtf8(cps.data() + l.start, l.len).c_str();
    out.push_back({std::string(t.c_str()), c});
  }
}

void raspBuild(const rasp::Schedule& s, int di, long today, int nowMin, std::vector<rasp::VLine>& out) {
  out.clear();
  const rasp::Day& d = s.days[(size_t)di];
  bool isToday = rasp::dayNum(d.y, d.m, d.d) == today;
  if (d.pairs.empty()) {
    out.push_back({std::string(L3("Пар нет", "No classes", "没有课")), C_GREEN});
    return;
  }
  bool nextMarked = false;
  for (const rasp::Pair& p : d.pairs) {
    std::string tm = p.num >= 1 && p.num <= 7 ? s.times[p.num] : "";
    int a = 0, b = 0;
    bool span = raspSpan(tm, a, b);
    uint16_t hc = C_LIGHT;
    String mark;
    if (isToday && span && nowMin >= a && nowMin < b) {
      hc = C_GREEN;
      mark = L3("  сейчас", "  now", "  现在");
    } else if (isToday && span && nowMin < a && !nextMarked) {
      hc = C_YELLOW;
      mark = L3("  далее", "  next", "  下一节");
      nextMarked = true;
    }
    std::string rest;
    String type = raspType(p.subj, rest);
    String head = String(p.num) + ". " + String(tm.c_str()) + (type.length() ? String("  ") + type : String("")) + mark;
    out.push_back({std::string(head.c_str()), hc});
    raspAddWrapped(out, String(rest.c_str()), C_WHITE, 2);
    if (!p.who.empty()) raspAddWrapped(out, String(p.who.c_str()), 0x8C71, 2);
    out.push_back({"", C_BLACK});
  }
}

void raspDraw(const rasp::Schedule& s, int di, const std::vector<rasp::VLine>& lines, int top, bool isToday) {
  const rasp::Day& d = s.days[(size_t)di];
  tft.fillRect(0, 0, 240, 28, isToday ? C_SEL : C_BAR);
  char date[8];
  snprintf(date, sizeof(date), "%02d.%02d", d.d, d.m);
  static const char* const WD_RU[] = {"Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс"};
  static const char* const WD_EN[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
  String h = String(d.wd >= 0 ? (cfgLang == LANG_RU ? WD_RU[d.wd] : WD_EN[d.wd]) : "?") + " " + date;
  drawUtf8(h.c_str(), 6, 2, C_WHITE, isToday ? C_SEL : C_BAR);
  SmallFont small;
  String w = String(L3("нед.", "wk ", "周")) + String(d.week);
  drawUtf8(w.c_str(), 234 - textWidth(w.c_str()), 6, C_LIGHT, isToday ? C_SEL : C_BAR);
  if (isToday) drawUtf8(L3("сегодня", "today", "今天"), 110, 6, C_YELLOW, C_SEL);
  const int Y0 = 32, ROWS_V = 12;
  for (int r = 0; r < ROWS_V; r++) {
    int y = Y0 + r * 16;
    size_t i = (size_t)(top + r);
    tft.fillRect(0, y, 240, 16, C_BLACK);
    if (i < lines.size()) drawUtf8(lines[i].s.c_str(), 0, y, lines[i].c, C_BLACK);
  }
  tft.fillRect(0, Y0 + ROWS_V * 16, 240, 240 - (Y0 + ROWS_V * 16), C_BLACK);
  drawUtf8(L3("< > день  ^v листать  OK меню", "< > day  ^v scroll  OK menu", "< > 日  ^v 滚动  OK 菜单"), 0, 224, 0x52AA, C_BLACK);
}

void runRasp() {
  rasp::Schedule s;
  struct tm t;
  bool haveTime = timeNow(t);
  long today = haveTime ? rasp::dayNum(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday) : 0;
  int nowMin = haveTime ? t.tm_hour * 60 + t.tm_min : -1;
  prefs.begin("dsk", true);
  uint32_t ts = prefs.getULong("raspts", 0);
  prefs.end();
  bool have = raspLoad(s, today ? today - 7 : 0);
  bool stale = !have || (haveTime && ts > 1 && (uint32_t)time(nullptr) - ts > 6 * 3600) || ts == 1;
  if (!have && !waitWifi(false)) return;             // расписания нет совсем - ждём сеть (можно отменить)
  if (stale && wifiOk()) {
    showMsg(L3("Скачиваю расписание...", "Downloading the schedule...", "正在下载课表……"), C_YELLOW);
    String err = raspDownload();
    if (err.length() && !have) {
      viewAnswer(String(L3("Не получилось скачать расписание: ", "Could not download the schedule: ", "无法下载课表：")) + err, C_RED);
      return;
    }
    if (!err.length()) {
      have = raspLoad(s, 0);                         // всё, что есть на сайте
      if (have && today) {
        rasp::Schedule cur;
        if (raspLoad(cur, today - 7)) s = cur;       // а если есть текущие недели - только их
      }
    }
  }
  if (!have) {
    viewAnswer(L3("Расписание скачалось, но не читается. Нажмите «Обновить с сайта» ещё раз или проверьте адрес страницы.",
                  "The schedule was downloaded but cannot be read. Try updating again or check the page address.", "课表无法读取。"),
               C_YELLOW);
    return;
  }
  // открыть на сегодня, а если сегодня пар нет в расписании (воскресенье) - на ближайший следующий день
  int di = 0;
  if (today)
    for (size_t i = 0; i < s.days.size(); i++)
      if (rasp::dayNum(s.days[i].y, s.days[i].m, s.days[i].d) >= today) {
        di = (int)i;
        break;
      }
  std::vector<rasp::VLine> lines;
  int top = 0;
  bool redraw = true;
  uint32_t lastTick = millis();
  for (;;) {
    if (redraw) {
      if (haveTime && timeNow(t)) nowMin = t.tm_hour * 60 + t.tm_min;
      raspBuild(s, di, today, nowMin, lines);
      const rasp::Day& d = s.days[(size_t)di];
      raspDraw(s, di, lines, top, today && rasp::dayNum(d.y, d.m, d.d) == today);
      redraw = false;
    }
    uint8_t j = joyEvent();
    if (j == J_RIGHT && di + 1 < (int)s.days.size()) { di++; top = 0; redraw = true; }
    if (j == J_LEFT && di > 0) { di--; top = 0; redraw = true; }
    if (j == J_DOWN && top + 12 < (int)lines.size()) { top += 3; redraw = true; }
    if (j == J_UP && top > 0) { top = top > 3 ? top - 3 : 0; redraw = true; }
    BtnEvent e = btnEvent();
    if (e == EV_LONG) return;
    if (e == EV_CLICK) {
      std::vector<String> o = splitBar(L3("Сегодня|Выбрать неделю|Обновить с сайта|Адрес страницы|Выйти", "Today|Pick a week|Update from the site|Page address|Leave",
                                          "今天|选择周|从网站更新|页面地址|退出"));
      int k = choiceScreen(String(L3("Расписание ", "Schedule ", "课表 ")) + String(s.group.c_str()), o);
      if (k == 0 && today) {
        for (size_t i = 0; i < s.days.size(); i++)
          if (rasp::dayNum(s.days[i].y, s.days[i].m, s.days[i].d) >= today) {
            di = (int)i;
            break;
          }
      } else if (k == 1) {
        std::vector<String> items, right;
        std::vector<int> first;
        int lastW = -1;
        for (size_t i = 0; i < s.days.size(); i++)
          if (s.days[i].week != lastW) {
            lastW = s.days[i].week;
            first.push_back((int)i);
            char r[16];
            snprintf(r, sizeof(r), "%02d.%02d", s.days[i].d, s.days[i].m);
            items.push_back(String(L3("Неделя ", "Week ", "第")) + String(lastW));
            right.push_back(r);
          }
        int sel = 0;
        for (size_t w = 0; w < first.size(); w++)
          if (first[w] <= di) sel = (int)w;
        int w = listScreen(L3("Неделя", "Week", "周"), items, right, sel);
        if (w >= 0) di = first[(size_t)w];
      } else if (k == 2) {
        showMsg(L3("Скачиваю расписание...", "Downloading the schedule...", "正在下载课表……"), C_YELLOW);
        String err = raspDownload();
        if (err.length()) viewAnswer(String(L3("Не получилось: ", "Failed: ", "失败：")) + err, C_RED);
        else {
          raspLoad(s, today ? today - 7 : 0);
          if (di >= (int)s.days.size()) di = 0;
        }
      } else if (k == 3) {
        viewAnswer(String(L3("Сейчас: ", "Now: ", "当前：")) + raspUrl() +
                       L3("\n\nДля другой группы откройте на сайте её расписание и введите адрес страницы (например http://rasp2.achgaa.ru/4.html).",
                          "\n\nFor another group open its schedule on the site and enter that page address.", "\n\n输入其他班级的页面地址。"),
                   C_WHITE);
        pendingInput = raspUrl();
        String u = inputText();
        u.trim();
        if (u.length() > 10 && (u.startsWith("http://") || u.startsWith("https://"))) {
          prefs.begin("dsk", false);
          prefs.putString("raspurl", u);
          prefs.putULong("raspts", 0);
          prefs.end();
          showMsg(L3("Скачиваю расписание...", "Downloading the schedule...", "正在下载课表……"), C_YELLOW);
          String err = raspDownload();
          if (err.length()) viewAnswer(String(L3("Не получилось: ", "Failed: ", "失败：")) + err, C_RED);
          else {
            raspLoad(s, today ? today - 7 : 0);
            di = 0;
          }
        }
      } else if (k == 4) {
        return;
      }
      top = 0;
      redraw = true;
    }
    if (millis() - lastTick > 30000) {               // «сейчас/далее» меняются со временем
      lastTick = millis();
      redraw = true;
    }
    if (admService()) redraw = true;
    delay(5);
  }
}
#else
void runRasp() { notInEdition(); }
#endif

// ========================= ТЕЛЕФОН (пульт и клавиатура по Wi-Fi) =========================
// Сам пульт работает всегда, когда есть Wi-Fi; это приложение только показывает адрес.
void drawPhonePad(bool wifi, bool conn) {
  tft.fillScreen(C_BLACK);
  tft.fillRect(0, 0, 240, 28, C_BAR);
  drawUtf8(L3("Телефон как пульт", "Phone as a remote", "手机遥控"), 6, 2, C_WHITE, C_BAR);
  SmallFont small;
  if (!wifi) {
    drawUtf8(L3("Нет Wi-Fi. Подключите гаджет", "No Wi-Fi. Connect the device", "没有Wi-Fi。请先在设置中"), 8, 50, C_YELLOW, C_BLACK);
    drawUtf8(L3("к сети в Настройках.", "to a network in Settings.", "连接网络。"), 8, 70, C_YELLOW, C_BLACK);
  } else {
    drawUtf8(L3("Телефон - в ту же сеть Wi-Fi.", "Phone on the same Wi-Fi.", "手机连接同一Wi-Fi，"), 8, 40, C_LIGHT, C_BLACK);
    drawUtf8(L3("Откройте в браузере:", "Open in a browser:", "在浏览器中打开："), 8, 60, C_LIGHT, C_BLACK);
    drawUtf8("http://", 8, 88, C_LIGHT, C_BLACK);
    String ip = WiFi.localIP().toString();
    g_big = true;                                    // адрес - крупно
    drawUtf8(ip.c_str(), 8, 106, C_YELLOW, C_BLACK);
    g_big = false;
    drawUtf8(conn ? L3("Телефон подключён", "Phone connected", "手机已连接") : L3("Телефон не подключён", "No phone connected", "手机未连接"), 8, 150,
             conn ? C_GREEN : C_LIGHT, C_BLACK);
    drawUtf8(L3("Пульт работает везде: в играх,", "Works everywhere: in games,", "在游戏、菜单中都能用，"), 8, 176, C_LIGHT, C_BLACK);
    drawUtf8(L3("меню и при вводе текста.", "menus and text input.", "也能输入文字。"), 8, 194, C_LIGHT, C_BLACK);
  }
  drawUtf8(L3("Нажатие - выход", "Press to leave", "按下退出"), 8, 218, C_LIGHT, C_BLACK);
}

void runPhonePad() {
  int shownWifi = -1, shownConn = -1;
  for (;;) {
    int w = wifiOk() ? 1 : 0, c = wpad::connected() ? 1 : 0;
    if (w != shownWifi || c != shownConn) {
      drawPhonePad(w, c);
      shownWifi = w;
      shownConn = c;
    }
    joyEvent();
    if (btnEvent() != EV_NONE) return;
    if (admService()) shownWifi = -1;
    delay(5);
  }
}

// ========================= ГЛАВНЫЙ ЭКРАН =========================
void runChat();
void runAdmin();
// Как у телефона: строка состояния сверху, иконки приложений 3x3, несколько страниц.
// Ручка - выбор (влево/вправо у края - соседняя страница), нажатие - открыть приложение.
enum AppId : uint8_t {
  A_CHAT, A_ROOM, A_GAMES, A_RASP, A_CALC, A_REF, A_NOTES, A_SAVED, A_WIKI, A_IMAGES, A_GALLERY,
  A_WEATHER, A_RATES, A_CLOCK, A_TIMER, A_STOPW, A_REMOTE, A_BT, A_PCSCR, A_PHONE, A_SETTINGS, A_ADMIN, A_COUNT
};
const char* const APP_RU[A_COUNT] = {"DeepSeek", "Комната", "Игры", "Расписание", "Калькул.", "Справочник", "Конспекты", "Сохранено", "Википедия", "Картинки",
                                     "Галерея", "Погода", "Курсы", "Часы", "Таймер", "Секундомер", "Пульт", "Bluetooth", "Экран ПК", "Телефон", "Настройки", "Админ"};
const char* const APP_EN[A_COUNT] = {"DeepSeek", "Room", "Games", "Schedule", "Calculator", "Reference", "Notes", "Saved", "Wikipedia", "Images",
                                     "Gallery", "Weather", "Rates", "Clock", "Timer", "Stopwatch", "Remote", "Bluetooth", "PC screen", "Phone", "Settings", "Admin"};
const uint16_t APP_COLOR[A_COUNT] = {0x2B3E, 0x04B2, 0x8219, 0x1A7B, 0xF460, 0x2D4A, 0xE5E5, 0xD9E7, 0x7BD0, 0x3CBC,
                                     0xE2D4, 0x5DBE, 0x13C8, 0x3188, 0xF2C5, 0x5239, 0x424A, 0x041F, 0x2A2F, 0x0C87, 0x636F, 0xB0A6};
const int HOME_TOP = 22, CELL_W = 80, CELL_H = 70, TILE = 46;

const char* appName(int a) { return cfgLang == LANG_RU ? APP_RU[a] : APP_EN[a]; }

// Значок приложения: цветная плитка 46x46 со скруглёнными углами и белый рисунок.
void drawAppIcon(int a, int x, int y) {
  const uint16_t W = C_WHITE, col = APP_COLOR[a];
  const int cx = x + TILE / 2, cy = y + TILE / 2;
  tft.fillRoundRect(x, y, TILE, TILE, 10, col);
  switch (a) {
    case A_CHAT:                                     // облачко с тремя точками
      tft.fillRoundRect(x + 7, y + 9, 32, 21, 7, W);
      tft.fillTriangle(x + 13, y + 28, x + 22, y + 28, x + 11, y + 37, W);
      for (int k = -1; k <= 1; k++) tft.fillCircle(cx + k * 8, y + 19, 2, col);
      break;
    case A_ROOM:                                     // два человечка
      tft.fillCircle(x + 30, y + 16, 6, 0xC618);
      tft.fillRoundRect(x + 21, y + 25, 19, 13, 6, 0xC618);
      tft.fillCircle(x + 17, y + 18, 6, W);
      tft.fillRoundRect(x + 8, y + 27, 19, 12, 6, W);
      break;
    case A_GAMES:                                    // геймпад
      tft.fillRoundRect(x + 5, y + 14, 36, 19, 9, W);
      tft.fillRect(x + 11, y + 22, 11, 3, col);
      tft.fillRect(x + 15, y + 18, 3, 11, col);
      tft.fillCircle(x + 31, y + 20, 2, col);
      tft.fillCircle(x + 35, y + 26, 2, col);
      break;
    case A_CALC:                                     // калькулятор
      tft.fillRoundRect(x + 11, y + 6, 24, 34, 4, W);
      tft.fillRect(x + 14, y + 9, 18, 7, col);
      for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) tft.fillRect(x + 14 + i * 7, y + 20 + j * 6, 4, 4, col);
      break;
    case A_REF:                                      // раскрытая книга
      tft.fillRect(x + 8, y + 11, 14, 25, W);
      tft.fillRect(x + 24, y + 11, 14, 25, W);
      for (int k = 0; k < 4; k++) {
        tft.drawFastHLine(x + 11, y + 16 + k * 5, 8, col);
        tft.drawFastHLine(x + 27, y + 16 + k * 5, 8, col);
      }
      break;
    case A_NOTES:                                    // листок с линейками
      tft.fillRect(x + 12, y + 7, 22, 32, W);
      for (int k = 0; k < 5; k++) tft.drawFastHLine(x + 15, y + 13 + k * 5, 16, col);
      break;
    case A_SAVED:                                    // закладка
      tft.fillRect(x + 15, y + 7, 16, 30, W);
      tft.fillTriangle(x + 15, y + 38, x + 31, y + 38, x + 23, y + 30, col);
      break;
    case A_WIKI:
      drawCp('W', cx - 6, cy - 12, W, col);
      break;
    case A_IMAGES:                                   // картинка: горы и солнце
    case A_GALLERY:
      if (a == A_GALLERY) tft.drawRect(x + 11, y + 7, 28, 24, W);
      tft.fillRect(x + 7, y + 13, 28, 24, W);
      tft.fillTriangle(x + 9, y + 35, x + 18, y + 22, x + 27, y + 35, col);
      tft.fillTriangle(x + 21, y + 35, x + 27, y + 27, x + 33, y + 35, col);
      tft.fillCircle(x + 28, y + 19, 3, col);
      break;
    case A_WEATHER:                                  // солнце за облаком
      tft.fillCircle(x + 17, y + 17, 8, C_YELLOW);
      tft.fillCircle(x + 22, y + 28, 7, W);
      tft.fillCircle(x + 31, y + 25, 8, W);
      tft.fillRoundRect(x + 12, y + 28, 28, 10, 5, W);
      break;
    case A_RATES:
      drawCp(0x20BD, cx - 6, cy - 12, W, col);       // знак рубля
      break;
    case A_RASP:                                     // календарь
      tft.fillRoundRect(x + 7, y + 10, 32, 28, 3, W);
      tft.fillRect(x + 7, y + 10, 32, 7, 0xF2C5);
      tft.fillRect(x + 13, y + 6, 3, 7, W);
      tft.fillRect(x + 30, y + 6, 3, 7, W);
      for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++) tft.fillRect(x + 10 + c * 7, y + 20 + r * 6, 5, 4, col);
      break;
    case A_CLOCK:                                    // циферблат
      tft.fillCircle(cx, cy, 15, W);
      tft.drawLine(cx, cy, cx, cy - 10, col);
      tft.drawLine(cx + 1, cy, cx + 1, cy - 10, col);
      tft.drawLine(cx, cy, cx + 8, cy, col);
      tft.drawLine(cx, cy + 1, cx + 8, cy + 1, col);
      break;
    case A_TIMER:                                    // песочные часы
      tft.fillRect(x + 11, y + 7, 24, 3, W);
      tft.fillRect(x + 11, y + 36, 24, 3, W);
      tft.fillTriangle(x + 13, y + 10, x + 33, y + 10, cx, cy, W);
      tft.fillTriangle(x + 13, y + 36, x + 33, y + 36, cx, cy, W);
      break;
    case A_STOPW:                                    // секундомер
      tft.fillCircle(cx, cy + 3, 14, W);
      tft.fillRect(cx - 3, y + 4, 6, 5, W);
      tft.drawLine(cx, cy + 3, cx + 6, cy - 6, col);
      tft.drawLine(cx + 1, cy + 3, cx + 7, cy - 6, col);
      break;
    case A_REMOTE:                                   // пульт и волны
      tft.fillRoundRect(x + 17, y + 12, 13, 28, 4, W);
      tft.fillCircle(cx, y + 18, 2, C_RED);
      for (int k = 0; k < 3; k++) tft.fillRect(x + 20, y + 24 + k * 5, 7, 2, col);
      tft.drawFastHLine(x + 18, y + 8, 11, W);
      tft.drawFastHLine(x + 20, y + 5, 7, W);
      break;
    case A_BT:                                       // знак Bluetooth
      for (int k = 0; k < 3; k++) {
        tft.drawLine(cx - 1 + k, y + 7, cx - 1 + k, y + 39, W);
        tft.drawLine(cx - 1 + k, y + 7, cx + 10 + k, y + 16, W);
        tft.drawLine(cx + 10 + k, y + 16, cx - 10 + k, y + 31, W);
        tft.drawLine(cx - 1 + k, y + 39, cx + 10 + k, y + 30, W);
        tft.drawLine(cx + 10 + k, y + 30, cx - 10 + k, y + 15, W);
      }
      break;
    case A_PCSCR:                                    // монитор на подставке
      tft.fillRoundRect(x + 6, y + 8, 34, 24, 3, W);
      tft.fillRect(x + 9, y + 11, 28, 18, col);
      tft.fillRect(x + 21, y + 32, 4, 5, W);
      tft.fillRoundRect(x + 14, y + 36, 18, 3, 1, W);
      break;
    case A_PHONE:                                    // телефон
      tft.fillRoundRect(x + 14, y + 5, 18, 36, 4, W);
      tft.fillRect(x + 16, y + 10, 14, 24, col);
      tft.fillCircle(cx, y + 37, 2, col);
      break;
    case A_SETTINGS:                                 // шестерёнка
      for (int k = 0; k < 8; k++) {
        float an = k * 0.785398f;
        tft.fillCircle(cx + (int)(13 * cosf(an)), cy + (int)(13 * sinf(an)), 4, W);
      }
      tft.fillCircle(cx, cy, 12, W);
      tft.fillCircle(cx, cy, 5, col);
      break;
    case A_ADMIN:                                    // щит с галочкой
      tft.fillRect(x + 12, y + 8, 22, 17, W);
      tft.fillTriangle(x + 12, y + 25, x + 34, y + 25, cx, y + 39, W);
      for (int k = 0; k < 2; k++) {
        tft.drawLine(x + 17, y + 21 + k, x + 21, y + 26 + k, col);
        tft.drawLine(x + 21, y + 26 + k, x + 29, y + 15 + k, col);
      }
      break;
  }
}

// Какие приложения есть на главном экране (Админ - только после входа).
void homeApps(std::vector<uint8_t>& apps) {
  apps.clear();
  for (int a = 0; a < A_COUNT; a++) {
    if (a == A_ADMIN && !admOn) continue;
    if ((a == A_IMAGES || a == A_GALLERY) && !ENABLE_IMAGES) continue;
    if (a == A_BT && !ENABLE_BLE) continue;
    if (a == A_PHONE && !ENABLE_WIFIPAD) continue;
    if (a == A_RASP && !ENABLE_RASP) continue;
    if (a == A_PCSCR && !ENABLE_PCSCREEN) continue;
    if (GAMES_EDITION && a != A_GAMES && a != A_ROOM && a != A_PHONE && a != A_SETTINGS && a != A_ADMIN) continue;
    if ((a == A_REMOTE && !ENABLE_IR) || (a == A_IMAGES && !ENABLE_WEBIMG)) continue;
    apps.push_back((uint8_t)a);
  }
}

// Строка состояния главного экрана: время, админ, комната, Wi-Fi, заряд.
void drawHomeStatus() {
  tft.fillRect(0, 0, 240, 20, C_BAR);
  SmallFont small;
  struct tm t;
  if (timeNow(t)) {
    char b[8];
    snprintf(b, sizeof(b), "%02d:%02d", t.tm_hour, t.tm_min);
    drawUtf8(b, 4, 2, C_WHITE, C_BAR);
  }
  if (admOn) drawUtf8("ADMIN", 56, 2, 0xFB2C, C_BAR);
  if (roomUnread > 0) {                              // непрочитанные в комнате: конвертик и число
    int ex = 112, ey = 5;
    tft.drawRect(ex, ey, 14, 10, C_YELLOW);
    tft.drawLine(ex, ey, ex + 6, ey + 5, C_YELLOW);
    tft.drawLine(ex + 13, ey, ex + 7, ey + 5, C_YELLOW);
    String n = roomUnread > 9 ? String("9+") : String(roomUnread);
    drawUtf8(n.c_str(), ex + 17, 2, C_YELLOW, C_BAR);
  }
  if (ble::active())                                 // Bluetooth: «B», голубая - телефон подключён
    drawUtf8("B", 164, 2, ble::phoneConnected() ? 0x5DFF : 0x8410, C_BAR);
  int ws = wifiState();                              // Wi-Fi: три полоски
  uint16_t wc = ws == 0 ? C_WHITE : ws == 1 ? C_YELLOW : 0x8410;
  for (int k = 0; k < 3; k++) tft.fillRect(178 + k * 5, 14 - k * 4, 3, 3 + k * 4, ws == 0 || k == 0 ? wc : 0x4208);
  if (BATTERY_ENABLED && batShownPct >= -1) {        // батарейка
    tft.drawRect(204, 5, 22, 11, C_WHITE);
    tft.fillRect(226, 8, 2, 5, C_WHITE);
    if (batShownPct == -1) {
      tft.fillTriangle(216, 6, 211, 11, 215, 11, C_YELLOW);   // молния: от USB
      tft.fillTriangle(214, 15, 219, 10, 215, 10, C_YELLOW);
    } else {
      int w = batShownPct * 18 / 100;
      tft.fillRect(206, 7, w < 1 ? 1 : w, 7, batShownPct <= 15 ? C_RED : C_GREEN);
    }
  }
}

void drawHomeCell(const std::vector<uint8_t>& apps, int page, int i, bool sel) {
  int idx = page * 9 + i;
  int cx = (i % 3) * CELL_W, cy = HOME_TOP + (i / 3) * CELL_H;
  int tx = cx + (CELL_W - TILE) / 2, ty = cy + 3;
  if (idx >= (int)apps.size()) {
    tft.fillRect(cx, cy, CELL_W, CELL_H, C_BLACK);
    return;
  }
  uint16_t frame = sel ? C_WHITE : C_BLACK;
  tft.drawRoundRect(tx - 3, ty - 3, TILE + 6, TILE + 6, 12, frame);
  tft.drawRoundRect(tx - 2, ty - 2, TILE + 4, TILE + 4, 11, frame);
  int a = apps[(size_t)idx];
  SmallFont small;
  const char* name = appName(a);
  int w = textWidth(name);
  tft.fillRect(cx, ty + TILE + 3, CELL_W, 16, C_BLACK);
  drawUtf8(name, cx + (CELL_W - w) / 2, ty + TILE + 3, sel ? C_YELLOW : C_WHITE, C_BLACK);
  if (a == A_ROOM && roomUnread > 0) {               // красный кружок с числом новых сообщений
    tft.fillCircle(tx + TILE - 3, ty + 3, 8, C_RED);
    String n = roomUnread > 9 ? String("9") : String(roomUnread);
    drawUtf8(n.c_str(), tx + TILE - 7, ty - 5, C_WHITE, C_RED);
  }
}

void drawHomeIcons(const std::vector<uint8_t>& apps, int page, int selCell) {
  tft.fillRect(0, HOME_TOP, 240, 240 - HOME_TOP, C_BLACK);
  for (int i = 0; i < 9; i++) {
    int idx = page * 9 + i;
    if (idx >= (int)apps.size()) break;
    drawAppIcon(apps[(size_t)idx], (i % 3) * CELL_W + (CELL_W - TILE) / 2, HOME_TOP + (i / 3) * CELL_H + 3);
    drawHomeCell(apps, page, i, i == selCell);
  }
  int pages = ((int)apps.size() + 8) / 9;
  if (pages > 1)                                     // точки страниц
    for (int p = 0; p < pages; p++) tft.fillCircle(120 - (pages - 1) * 6 + p * 12, 235, p == page ? 3 : 2, p == page ? C_WHITE : 0x6B4D);
}

void launchApp(int a) {
  switch (a) {
    case A_CHAT: if (!GAMES_EDITION) runChat(); break;
    case A_ROOM: runRoom(); break;
    case A_GAMES: runGamesMenu(); break;
    case A_CALC: if (!GAMES_EDITION) runCalc(); break;
    case A_REF: if (!GAMES_EDITION) runReference(); break;
    case A_NOTES: if (!GAMES_EDITION) runNotes(); break;
    case A_SAVED: if (!GAMES_EDITION) runSaved(); break;
    case A_WIKI: if (!GAMES_EDITION) runWiki(); break;
    case A_IMAGES: if (!GAMES_EDITION) runImages(); break;
    case A_GALLERY: if (!GAMES_EDITION) runGallery(); break;
    case A_WEATHER: if (!GAMES_EDITION) runWeather(); break;
    case A_RATES: if (!GAMES_EDITION) runRates(); break;
    case A_RASP: runRasp(); break;
    case A_CLOCK: if (!GAMES_EDITION) runClock(); break;
    case A_TIMER: if (!GAMES_EDITION) runTimer(); break;
    case A_STOPW: if (!GAMES_EDITION) runStopwatch(); break;
    case A_REMOTE: if (!GAMES_EDITION) runRemote(); break;
    case A_BT: runBluetooth(); break;
    case A_PCSCR: if (!GAMES_EDITION) runPcScreen(); break;
    case A_PHONE: runPhonePad(); break;
    case A_SETTINGS: runSettings(); break;
    case A_ADMIN: runAdmin(); break;
  }
}

void runHome() {
  static int sel = 0;                                // номер приложения в списке (помнится между запусками)
  std::vector<uint8_t> apps;
  bool redraw = true;
  uint32_t nextCheck = 0, nextWifi = 0;
  int shownMin = -1, shownWifi = -1, shownBat = -9, shownUnread = -1;
  bool shownAdm = false;
  SaverArm arm;                                      // долго не трогали - заставка с часами
  for (;;) {
    if (saverDue) {
      runSaver();
      saverDue = false;
      redraw = true;
    }
    if (redraw) {
      homeApps(apps);
      if (sel >= (int)apps.size()) sel = (int)apps.size() - 1;
      tft.fillScreen(C_BLACK);
      updateBattery();
      drawHomeStatus();
      drawHomeIcons(apps, sel / 9, sel % 9);
      struct tm t;
      shownMin = timeNow(t) ? t.tm_min : -1;
      shownWifi = wifiState();
      shownBat = batShownPct;
      shownUnread = roomUnread;
      shownAdm = admOn;
      redraw = false;
    }
    if (g_bgTick) g_bgTick();                        // комната принимает сообщения в фоне
    if ((int32_t)(millis() - nextWifi) >= 0) {
      nextWifi = millis() + 500;
      wifiTick();
    }
    if (admService()) {
      redraw = true;
      continue;
    }
    if ((int32_t)(millis() - nextCheck) >= 0) {      // раз в секунду: не изменилось ли что-то в строке состояния
      nextCheck = millis() + 1000;
      updateBattery();
      struct tm t;
      int m = timeNow(t) ? t.tm_min : -1;
      if (m != shownMin || wifiState() != shownWifi || batShownPct != shownBat || roomUnread != shownUnread || admOn != shownAdm) {
        bool icons = roomUnread != shownUnread || admOn != shownAdm;
        shownMin = m;
        shownWifi = wifiState();
        shownBat = batShownPct;
        shownUnread = roomUnread;
        shownAdm = admOn;
        drawHomeStatus();
        if (icons) redraw = true;
      }
    }
    uint8_t j = joyEvent();
    if (j != J_NONE) {
      int n = (int)apps.size(), page = sel / 9, cell = sel % 9, old = sel;
      int col = cell % 3, row = cell / 3;
      if (j == J_LEFT) {
        if (col > 0) sel--;
        else if (page > 0) sel = (page - 1) * 9 + row * 3 + 2;
      } else if (j == J_RIGHT) {
        if (col < 2 && sel + 1 < n) sel++;
        else if ((page + 1) * 9 < n) sel = (page + 1) * 9 + row * 3;
      } else if (j == J_UP) {
        if (row > 0) sel -= 3;
      } else if (j == J_DOWN) {
        if (row < 2 && sel + 3 < n) sel += 3;
      }
      if (sel >= n) sel = n - 1;
      if (sel != old) {
        if (sel / 9 != old / 9) {
          drawHomeIcons(apps, sel / 9, sel % 9);
        } else {
          drawHomeCell(apps, page, old % 9, false);
          drawHomeCell(apps, page, sel % 9, true);
        }
        beep(2, 2200, 5);                             // щелчок (если в настройках звука «всё»)
      }
    }
    BtnEvent e = btnEvent();
    if (e == EV_CLICK && sel < (int)apps.size()) {
      saverArmed = false;
      launchApp(apps[(size_t)sel]);
      saverArmed = true;
      saverDue = false;
      noteActivity();
      redraw = true;
    }
    delay(5);
  }
}

// Строка состояния на заставке: видно, на каком шаге запуск (если что-то зависнет).
void bootStage(const char* t) {
  SmallFont small;
  tft.fillRect(0, 214, 240, 20, C_BLACK);
  drawUtf8(t, 120 - textWidth(t) / 2, 216, C_LIGHT, C_BLACK);
}

// Заставка при включении.
void drawBoot() {
  tft.fillScreen(C_BLACK);
  drawAppIcon(A_CHAT, 97, 52);
  const char* t = "DeepSeek OS";
  drawUtf8(t, 120 - textWidth(t) / 2, 112, C_WHITE, C_BLACK);
  SmallFont small;
  String v = String("v") + String(FW_VERSION);
  drawUtf8(v.c_str(), 120 - textWidth(v.c_str()) / 2, 142, C_LIGHT, C_BLACK);
  drawUtf8(T().starting, 120 - textWidth(T().starting) / 2, 190, C_YELLOW, C_BLACK);
}

// ========================= ОСНОВНАЯ ПРОГРАММА =========================
// Шаги запуска - переживают перезапуск: если плата упала при запуске, на следующем видно, на каком шаге,
// и этот шаг один раз пропускается (гаджет запустится без звука / датчика / Wi-Fi, но запустится).
RTC_NOINIT_ATTR uint32_t initStage;
const char* const INIT_NAMES[] = {"", "zvuk", "datchik naklona", "fajly", "SD-karta", "Wi-Fi", "Bluetooth"};
int initSkip = 0;                                    // какой шаг пропустить в этот раз
void initStep(int n) { initStage = 0x1A170000u | (uint32_t)n; }
bool initRun(int n) {
  initStep(n);
  if (initSkip == n) {
    Serial.printf("init step %d (%s) skipped after a crash\n", n, INIT_NAMES[n]);
    return false;
  }
  return true;
}

void setup() {
  pinMode(PIN_LED, OUTPUT);          // первым делом гасим фонарик (с SD-картой здесь MOSI карты - тоже безопасно)
  digitalWrite(PIN_LED, LOW);
  Serial.setRxBufferSize(4096);      // «Экран ПК» присылает картинки кусками по 2 КБ
  Serial.begin(115200);
  delay(3000);                       // время на USB и питание
  Serial.println("1: start");
  {
    esp_reset_reason_t why = esp_reset_reason();
    bool crashed = why == ESP_RST_PANIC || why == ESP_RST_INT_WDT || why == ESP_RST_TASK_WDT || why == ESP_RST_WDT;
    if (crashed && (initStage >> 16) == 0x1A17 && (initStage & 0xFF) > 0 && (initStage & 0xFF) < 7) initSkip = (int)(initStage & 0xFF);
    Serial.printf("reset reason %d, last init step %u, skip %d\n", (int)why, (unsigned)(initStage & 0xFF), initSkip);
    initStep(0);
  }
  bootGuard();
  loadSettings();
  pinMode(PIN_BLK, OUTPUT);
  applyBrightness();                 // подсветка с выбранной яркостью
  g_bgTick = roomTick;               // комната принимает сообщения в фоне, пока вы на экране ввода
  noteActivity();

  displayReset();                    // сброс экрана на обеих ножках + есть ли батарея
  SPI.begin(PIN_SCK, -1, PIN_MOSI, -1);
  tft.init(240, 240, SPI_MODE3);     // модули без CS работают в режиме 3
  tft.setRotation((TFT_ROTATION + cfgRot) % 4);   // поворот из настроек
  tft.setSPISpeed(TFT_SPI_HZ);       // быстрее шина - быстрее экран (особенно в гонке)
  drawBoot();                        // заставка «DeepSeek OS»
  Serial.println("2: display ok");
  if (initSkip) {
    char b[64];
    snprintf(b, sizeof(b), "Upalo na shage: %s - propuskayu", INIT_NAMES[initSkip]);
    bootStage(b);
    delay(3000);
  }
  bootStage("Zvuk");
  if (initRun(1)) soundInit();       // после подсветки: у пищалки свой канал
  bootStage("Datchiki");
  if (initRun(2)) accInit();         // датчик наклона (S3)
  touchInit();                       // сенсорные кнопки и вибромотор (S3)

  analogReadResolution(12);
  if (BATTERY_ENABLED) analogSetPinAttenuation(PIN_BAT, ADC_11db);
  pinMode(PIN_SW, INPUT_PULLUP);
  bool afterHoldReset = holdReset == HOLD_RESET_MAGIC;
  holdReset = 0;
  if (afterHoldReset) Serial.println("restarted by holding the button");
  xTaskCreate(holdWatchTask, "hold", 2048, nullptr, 2, nullptr);
  joyCalibrate();
  Serial.printf("3: joystick center %d / %d\n", joyCx, joyCy);

  sugg.addDict(DICT_RU);
  sugg.addDict(DICT_EN);
  bootStage("Fajly");
  if (initRun(3) && LittleFS.begin(true)) {  // true = отформатировать при первом запуске
    loadLearned();
    loadAvatar();
  }
  // SD-карта (если включена в настройках). Защита: если прошлый запуск с картой не дошёл до конца
  // или при включении зажата кнопка джойстика - карта выключается, чтобы гаджет точно загрузился.
  if (cfgSd) {
    pinMode(PIN_SW, INPUT_PULLUP);
    delay(5);
    prefs.begin("dsk", false);
    bool failed = prefs.getUChar("sdboot", 0) != 0;
    // кнопка всё ещё зажата после аварийного перезапуска - это не просьба выключить карту
    if (failed || (digitalRead(PIN_SW) == LOW && !afterHoldReset)) {
      cfgSd = 0;
      prefs.putUChar("sd", 0);
      prefs.putUChar("sdboot", 0);
      Serial.println(failed ? "SD card turned off: the previous start with it did not finish" : "SD card turned off: button held");
      if (failed) {                                  // показать, на каком шаге остановился прошлый запуск
        int st = (sdDiagStage >> 24) == 0x5D ? (int)(sdDiagStage & 0xFF) : -1;
        char b[48];
        snprintf(b, sizeof(b), "SD: zavis na shage %d - kartu vyklyuchil", st);
        prefs.putInt("sddiag", st);
        Serial.println(b);
        bootStage(b);
        delay(4000);
      }
    } else {
      prefs.putUChar("sdboot", 1);                 // снимем в конце запуска
    }
    prefs.end();
  }
  if (cfgSd && !initRun(4)) cfgSd = 0;
  if (cfgSd) {
    bootStage("SD: 1/3");
    { SdBus test; }                                  // переключение шины туда-обратно: экран должен остаться живым
    bootStage("SD: 2/3");
    sdMount();
    bootStage(sdOk ? "SD: 3/3 ok" : "SD: 3/3 -");
  }
  Serial.printf("4: learned words: %u\n", (unsigned)sugg.learnedCount());

  bootStage("Wi-Fi");
  if (initRun(5)) {
    if (netSsid.empty()) runPortal();               // первый запуск: настроить сеть (можно отменить кнопкой)
    wifiStart();                                     // дальше Wi-Fi подключается в фоне
  }
  if (ENABLE_BLE && cfgBle && initRun(6)) {          // Bluetooth включён в настройках
    ble::begin(toStd(roomId));
    Serial.printf("Bluetooth on, heap %u\n", (unsigned)ESP.getFreeHeap());
  }
  if (cfgSd) {                                       // запуск с картой прошёл
    SD_STAGE(0);
    prefs.begin("dsk", false);
    prefs.putUChar("sdboot", 0);
    prefs.end();
  }
  initStep(0);                                       // запуск прошёл
  Serial.println("5: ready (Wi-Fi in background)");
  prefs.begin("dsk", false);
  bool rolled = prefs.getUChar("rolledback", 0);
  if (rolled) prefs.putUChar("rolledback", 0);
  prefs.end();
  if (rolled)
    viewAnswer(L3("Новая прошивка 3 раза подряд не смогла запуститься, поэтому вернулась прошлая версия. Напишите автору, что случилось.",
                  "The new firmware failed to start 3 times in a row, so the previous version was restored.", "新固件连续3次无法启动，已恢复上一版本。"),
               C_YELLOW);
}

// Один вопрос DeepSeek и сколько угодно «по шагам» / «проще».
void chatAsk(String q) {
  if (!waitWifi(true)) {                             // нет связи - вопрос вернётся в поле ввода
    pendingInput = q;
    return;
  }
  int maxTokens = MAX_TOKENS;
  bool learn = true;
  for (;;) {                                         // вопрос, затем сколько угодно «по шагам» / «проще»
    Serial.print("Q: ");
    Serial.println(q);
    showMsg(T().thinking, C_YELLOW);
    String result;
    bool ok = askLen(q, maxTokens, result);
    if (ok) {
      Serial.println("answer ok");
      sndOk();
    } else {
      sndError();
      Serial.print("error: ");
      Serial.println(result);
    }
    if (learn) {                                     // учим подсказки: слова вопроса весят больше, чем слова ответа
      sugg.learnText(q.c_str());
      sugg.learnText(q.c_str());
    }
    if (ok) sugg.learnText(result.c_str());
    saveLearned();
    if (!ok) {
      viewAnswer(String(T().errPrefix) + result, C_RED);
      return;
    }
    chat.push_back({true, q});
    chat.push_back({false, result});
    trimHistory();
    int act = viewChat();
    if (act == 0) return;
    if (!waitWifi(false)) return;
    q = act == 1 ? L3("Распиши решение по шагам, подробно, с пояснением каждого шага.", "Show the solution step by step, explaining every step.",
                      "请一步一步详细解答，并解释每一步。")
                 : L3("Объясни это проще и короче, как новичку, с примером.", "Explain this more simply and briefly, as to a beginner, with an example.",
                      "请用更简单简短的方式解释，像对初学者一样，并举例。");
    maxTokens = act == 1 ? 900 : MAX_TOKENS;
    learn = false;                                   // служебные фразы в подсказки не добавляем
  }
}

// Приложение «DeepSeek»: чат. Выход - удержание кнопки на клавиатуре -> «На главный экран».
void runChat() {
  g_goHome = false;
  for (;;) {
    g_kbChat = true;
    String q = inputText();
    g_kbChat = false;
    if (g_goHome) {
      g_goHome = false;
      return;
    }
    q.trim();
    if (q.length()) chatAsk(q);
  }
}

void loop() { runHome(); }
