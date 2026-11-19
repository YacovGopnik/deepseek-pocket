// Проверка ESP32-S3-Zero: встроенный цветной светодиод (WS2812 на GPIO21) и связь по USB.
// Больше ничего к плате подключать не нужно.
//
// Arduino IDE: плата ESP32S3 Dev Module, USB CDC On Boot: Enabled, Flash Size: 4MB, PSRAM: QSPI PSRAM.
// Первая заливка: зажать B, подключить USB, отпустить B -> Загрузить -> после загрузки нажать R.
//
// Что должно быть: светодиод по кругу красный -> зелёный -> синий -> белый -> выключен,
// в мониторе порта (115200) раз в секунду строка с номером шага и временем работы.
// Если цвета перепутаны (например, вместо красного зелёный) - это нормально: у разных партий
// светодиодов разный порядок цветов.

const int LED_PIN = 21;
const int BRIGHT = 40;                               // яркость 0..255 (светодиод очень яркий)

struct Step {
  const char* name;
  uint8_t r, g, b;
};
const Step STEPS[] = {
    {"красный", BRIGHT, 0, 0},
    {"зелёный", 0, BRIGHT, 0},
    {"синий", 0, 0, BRIGHT},
    {"белый", BRIGHT, BRIGHT, BRIGHT},
    {"выключен", 0, 0, 0},
};

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== ESP32-S3: проверка светодиода ===");
  Serial.printf("Чип: %s, ядер: %d, частота: %u МГц\n", ESP.getChipModel(), ESP.getChipCores(), (unsigned)ESP.getCpuFreqMHz());
  Serial.printf("Флеш: %u МБ, PSRAM: %u КБ\n", (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)), (unsigned)(ESP.getPsramSize() / 1024));
}

void loop() {
  static int i = 0;
  const Step& s = STEPS[i];
  rgbLedWrite(LED_PIN, s.r, s.g, s.b);
  Serial.printf("[%lu с] светодиод: %s\n", (unsigned long)(millis() / 1000), s.name);
  i = (i + 1) % (int)(sizeof(STEPS) / sizeof(STEPS[0]));
  delay(1000);
}
