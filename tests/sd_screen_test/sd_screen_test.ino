// Проверка: экран и SD-карта по очереди на одном SPI (как в прошивке, но больше ничего нет).
// Экран: SCK 4, SDA(MOSI) 6, DC 5, RES 21, BLK 10.  Карта: SCK 8, MOSI 2, MISO 20, CS 9.
// Плата ESP32C3 Dev Module, USB CDC On Boot: Enabled. Библиотеки: Adafruit ST7735 and ST7789, Adafruit GFX.
//
// Каждые 5 секунд: переключиться на карту, прочитать её, вернуться на экран и нарисовать результат.
// Если счётчик «round» на экране растёт - переключение работает и в прошивке тоже заработает.
// Если экран замер - в мониторе порта (115200) видно, на каком шаге.

#include <SPI.h>
#include <SD.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "driver/gpio.h"

const int PIN_SCK = 4, PIN_MOSI = 6, PIN_DC = 5, PIN_RST = 21, PIN_BLK = 10;
const int SD_SCK = 8, SD_MOSI = 2, SD_MISO = 20, SD_CS = 9;

Adafruit_ST7789 tft = Adafruit_ST7789(&SPI, -1, PIN_DC, -1);
int round_ = 0, okCount = 0;
bool mounted = false;

void step(const char* s) { Serial.printf("[%lu] %s\n", (unsigned long)millis(), s); Serial.flush(); }

void toCard() {
  spi_t* b = SPI.bus();
  gpio_set_level((gpio_num_t)PIN_SCK, 1);           // такт экрана в покое = 1 (режим 3): без лишнего фронта
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
  // spiSetDataMode на C3 не применяет режим сразу, поэтому пустая передача в режиме 3 без подключённых выводов:
  // после неё такт в покое = 1, и подключение экрана не даст лишнего фронта.
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE3));
  SPI.transfer(0xFF);
  SPI.endTransaction();
  spiAttachSCK(b, PIN_SCK);
  spiAttachMOSI(b, PIN_MOSI);
}

void setup() {
  Serial.begin(115200);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  pinMode(PIN_BLK, OUTPUT);
  digitalWrite(PIN_BLK, HIGH);
  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_RST, HIGH);
  delay(100);
  digitalWrite(PIN_RST, LOW);
  delay(100);
  digitalWrite(PIN_RST, HIGH);
  delay(120);
  SPI.begin(PIN_SCK, -1, PIN_MOSI, -1);
  tft.init(240, 240, SPI_MODE3);
  tft.setSPISpeed(20000000);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print("SD + screen test");
  delay(2000);
  step("start");
}

void loop() {
  round_++;
  Serial.printf("\n--- round %d ---\n", round_);
  step("1 switch to card");
  toCard();
  step("2 on card");
  // каждый круг карта подключается заново - так видно, насколько надёжно она отвечает
  SD.end();
  mounted = SD.begin(SD_CS, SPI, 4000000, "/sd", 4);
  step(mounted ? "3 SD.begin OK" : "3 SD.begin FAILED");
  if (mounted) okCount++;
  int files = -1;
  uint32_t mb = 0;
  if (mounted) {
    files = 0;
    File root = SD.open("/");
    for (File e = root.openNextFile(); e; e = root.openNextFile()) files++;
    root.close();
    mb = SD.cardSize() / (1024 * 1024);
    step("4 card read");
  }
  toScreen();
  step("5 back on screen");

  tft.fillRect(0, 40, 240, 200, ST77XX_BLACK);
  tft.setCursor(10, 50);
  tft.setTextColor(mounted ? ST77XX_GREEN : ST77XX_RED, ST77XX_BLACK);
  tft.print(mounted ? "card: OK" : "card: NOT FOUND");
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setCursor(10, 80);
  tft.printf("size: %lu MB", (unsigned long)mb);
  tft.setCursor(10, 110);
  tft.printf("files in /: %d", files);
  tft.setCursor(10, 150);
  tft.setTextSize(3);
  tft.printf("round %d", round_);
  tft.setTextSize(2);
  tft.setCursor(10, 190);
  tft.printf("OK %d of %d", okCount, round_);
  step("6 drawn");
  delay(5000);
}
