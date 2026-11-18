// Диагностика SD-карты: сама ищет, в чём беда, и пишет вывод на экран и в монитор порта (115200).
// Плата ESP32C3 Dev Module, USB CDC On Boot: Enabled. Библиотеки: Adafruit ST7735 and ST7789, Adafruit GFX.
// Экран: SCK 4, SDA 6, DC 5, RES 21, BLK 10.  Карта: SCK 8, MOSI 2, MISO 20, CS 9.
//
// Три серии по 10 попыток (около минуты):
//   A1 - только карта, медленно (1 МГц), экран выключен из шины;
//   A4 - только карта, 4 МГц (как в прошивке);
//   B4 - карта и экран по очереди, 4 МГц (как в прошивке).
// Каждая попытка: подключить карту заново, записать файл, прочитать обратно.

#include <SPI.h>
#include <SD.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "driver/gpio.h"

const int PIN_SCK = 4, PIN_MOSI = 6, PIN_DC = 5, PIN_RST = 21, PIN_BLK = 10;
const int SD_SCK = 8, SD_MOSI = 2, SD_MISO = 20, SD_CS = 9;
const int TRIES = 10;

Adafruit_ST7789 tft = Adafruit_ST7789(&SPI, -1, PIN_DC, -1);
bool screenOn = false;

void toCard() {
  spi_t* b = SPI.bus();
  gpio_set_level((gpio_num_t)PIN_SCK, 1);
  gpio_set_level((gpio_num_t)PIN_MOSI, 1);
  spiDetachSCK(b);
  spiDetachMOSI(b);
  pinMode(PIN_SCK, OUTPUT);
  digitalWrite(PIN_SCK, HIGH);
  pinMode(PIN_MOSI, OUTPUT);
  digitalWrite(PIN_MOSI, HIGH);
  spiAttachSCK(b, SD_SCK);
  spiAttachMISO(b, SD_MISO);
  spiAttachMOSI(b, SD_MOSI);
}

void toScreen() {
  spi_t* b = SPI.bus();
  spiDetachSCK(b);
  spiDetachMISO(b);
  spiDetachMOSI(b);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE3));
  SPI.transfer(0xFF);
  SPI.endTransaction();
  spiAttachSCK(b, PIN_SCK);
  spiAttachMOSI(b, PIN_MOSI);
}

// Одна попытка: подключить, записать, прочитать. true - всё прошло.
bool tryCard(uint32_t hz, const char* tag, int n) {
  SD.end();
  bool ok = SD.begin(SD_CS, SPI, hz, "/sd", 4);
  const char* why = "SD.begin";
  if (ok) {
    char msg[32];
    snprintf(msg, sizeof(msg), "diag %s %d", tag, n);
    File f = SD.open("/sd_diag.txt", "w");
    bool wr = f && f.print(msg) > 0;
    if (f) f.close();
    f = SD.open("/sd_diag.txt", "r");
    String back = f ? f.readString() : "";
    if (f) f.close();
    if (!wr) { ok = false; why = "write"; }
    else if (back != msg) { ok = false; why = "read"; }
  }
  Serial.printf("%s #%d: %s\n", tag, n, ok ? "OK" : (String("FAIL at ") + why).c_str());
  return ok;
}

int series(uint32_t hz, const char* tag, bool withScreen) {
  int ok = 0;
  for (int i = 1; i <= TRIES; i++) {
    if (withScreen) toCard();
    if (tryCard(hz, tag, i)) ok++;
    if (withScreen) {
      toScreen();
      tft.fillRect(0, 200, 240, 40, ST77XX_BLACK);
      tft.setCursor(10, 210);
      tft.printf("B4: %d/%d", ok, i);
    }
    delay(300);
  }
  return ok;
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n=== SD diag ===");
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  pinMode(PIN_BLK, OUTPUT);
  digitalWrite(PIN_BLK, HIGH);
  pinMode(PIN_SCK, OUTPUT);                          // экран пока не трогаем: его линии в покое
  digitalWrite(PIN_SCK, HIGH);
  pinMode(PIN_MOSI, OUTPUT);
  digitalWrite(PIN_MOSI, HIGH);
  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_RST, HIGH);

  // Серии A: шина только у карты
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, -1);
  int a1 = series(1000000, "A1", false);
  int a4 = series(4000000, "A4", false);
  SD.end();

  // Теперь экран: сброс, переключить шину на него, включить
  digitalWrite(PIN_RST, LOW);
  delay(100);
  digitalWrite(PIN_RST, HIGH);
  delay(120);
  toScreen();
  tft.init(240, 240, SPI_MODE3);
  tft.setSPISpeed(20000000);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.printf("A1 1MHz: %d/%d", a1, TRIES);
  tft.setCursor(10, 35);
  tft.printf("A4 4MHz: %d/%d", a4, TRIES);

  // Серия B: как в прошивке
  int b4 = series(4000000, "B4", true);
  SD.end();

  const char* l1;
  const char* l2;
  uint16_t col;
  if (a1 < TRIES - 1) {
    l1 = "BAD: power/wire/card";
    l2 = "even slow fails";
    col = ST77XX_RED;
  } else if (a4 < TRIES - 1) {
    l1 = "BAD: long wires";
    l2 = "slow OK, 4MHz fails";
    col = ST77XX_YELLOW;
  } else if (b4 < TRIES - 1) {
    l1 = "BAD: firmware";
    l2 = "card OK, swap fails";
    col = ST77XX_YELLOW;
  } else {
    l1 = "ALL OK";
    l2 = "card works";
    col = ST77XX_GREEN;
  }
  tft.fillRect(0, 60, 240, 180, ST77XX_BLACK);
  tft.setCursor(10, 60);
  tft.printf("B4 swap: %d/%d", b4, TRIES);
  tft.setTextColor(col, ST77XX_BLACK);
  tft.setCursor(10, 110);
  tft.print(l1);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setCursor(10, 140);
  tft.print(l2);
  tft.setCursor(10, 200);
  tft.print("RST - again");
  Serial.printf("\nA1 %d/%d  A4 %d/%d  B4 %d/%d\n=> %s (%s)\n", a1, TRIES, a4, TRIES, b4, TRIES, l1, l2);
}

void loop() {
  delay(1000);
}
