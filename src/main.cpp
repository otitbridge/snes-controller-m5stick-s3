#include <Arduino.h>
#include <M5Unified.h>

namespace {
constexpr uint8_t kClockPin = 5;  // Yellow
constexpr uint8_t kLatchPin = 6;  // Orange
constexpr uint8_t kDataPin = 7;   // Red; white = 3V3_L2, brown = GND
constexpr uint32_t kPollIntervalMs = 10;
constexpr uint32_t kHeartbeatMs = 2000;
constexpr uint16_t kButtonMask = 0x0FFF;

// SNES serial order. A low DATA bit means the button is pressed.
constexpr const char* kButtonNames[] = {
    "B", "Y", "SELECT", "START", "UP", "DOWN",
    "LEFT", "RIGHT", "A", "X", "L", "R"};
constexpr uint8_t kButtonCount = sizeof(kButtonNames) / sizeof(kButtonNames[0]);
uint16_t previousButtons = 0;
uint32_t lastPollMs = 0;
uint32_t lastStatusMs = 0;
bool firstSample = true;

uint16_t readController() {
  uint16_t pressed = 0;
  digitalWrite(kClockPin, HIGH);
  digitalWrite(kLatchPin, HIGH);
  delayMicroseconds(12);
  digitalWrite(kLatchPin, LOW);
  delayMicroseconds(6);

  // The first bit is already present after latching. Clock advances the
  // register after each sample; read all 16 bits, including the unused tail.
  for (uint8_t bit = 0; bit < 16; ++bit) {
    if (digitalRead(kDataPin) == LOW) {
      pressed |= static_cast<uint16_t>(1U << bit);
    }
    digitalWrite(kClockPin, LOW);
    delayMicroseconds(6);
    digitalWrite(kClockPin, HIGH);
    delayMicroseconds(6);
  }
  return pressed & kButtonMask;
}

void drawButtons(uint16_t pressed) {
  auto& lcd = M5.Display;
  lcd.startWrite();
  lcd.setTextSize(1);
  for (uint8_t i = 0; i < kButtonCount; ++i) {
    const int cellWidth = lcd.width() / 4;
    const int x = (i % 4) * cellWidth + 2;
    const int y = 25 + (i / 4) * 28;
    const bool down = (pressed & (1U << i)) != 0;
    const uint16_t background = down ? TFT_GREEN : TFT_DARKGREY;
    lcd.fillRoundRect(x, y, cellWidth - 4, 24, 4, background);
    lcd.setTextColor(down ? TFT_BLACK : TFT_WHITE, background);
    lcd.drawCenterString(kButtonNames[i], x + (cellWidth - 4) / 2, y + 8);
  }
  lcd.fillRect(0, 112, lcd.width(), lcd.height() - 112, TFT_BLACK);
  lcd.setTextColor(pressed ? TFT_GREEN : TFT_LIGHTGREY, TFT_BLACK);
  lcd.setCursor(4, 119);
  lcd.print(pressed ? "GREEN = PRESSED" : "NO BUTTON PRESSED");
  lcd.endWrite();
}

void printStatus(uint16_t pressed) {
  Serial.printf("[%lu ms] STATE:", static_cast<unsigned long>(millis()));
  if (pressed == 0) {
    Serial.print(" NONE");
  } else {
    for (uint8_t i = 0; i < kButtonCount; ++i) {
      if (pressed & (1U << i)) {
        Serial.printf(" %s", kButtonNames[i]);
      }
    }
  }
  Serial.printf(" (mask=0x%03X)\n", static_cast<unsigned int>(pressed));
}

void printChanges(uint16_t before, uint16_t after) {
  const uint16_t changed = before ^ after;
  for (uint8_t i = 0; i < kButtonCount; ++i) {
    if (changed & (1U << i)) {
      Serial.printf("[%lu ms] %s %s\n",
                    static_cast<unsigned long>(millis()), kButtonNames[i],
                    (after & (1U << i)) ? "PRESSED" : "RELEASED");
    }
  }
}
}  // namespace

void setup() {
  auto config = M5.config();
  config.internal_imu = false;
  config.internal_rtc = false;
  config.internal_mic = false;
  config.internal_spk = false;
  config.output_power = false;  // No external 5V boost needed.
  M5.begin(config);
  M5.Power.M5pm1.setLDOOutput(true);  // Enable the 3.3V supply.

  Serial.begin(115200);
  // USB CDC may not yet be connected. Do not wait for it: LCD works alone.
  pinMode(kClockPin, OUTPUT);
  pinMode(kLatchPin, OUTPUT);
  pinMode(kDataPin, INPUT_PULLUP);
  digitalWrite(kClockPin, HIGH);
  digitalWrite(kLatchPin, LOW);

  M5.Display.setRotation(1);
  M5.Display.setBrightness(128);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setCursor(4, 4);
  M5.Display.print("SNES CONTROLLER / 3.3V");
  M5.Display.setCursor(4, 15);
  M5.Display.print("CLK:5  LATCH:6  DATA:7");
  drawButtons(0);
  Serial.println("\nSNES controller monitor / 3.3V");
  Serial.println("CLOCK=GPIO5 LATCH=GPIO6 DATA=GPIO7, serial=115200");
  delay(20);  // Allow controller power and logic to settle.
}

void loop() {
  M5.update();
  const uint32_t now = millis();
  if (now - lastPollMs >= kPollIntervalMs) {
    lastPollMs = now;
    const uint16_t pressed = readController();
    if (firstSample || pressed != previousButtons) {
      drawButtons(pressed);
      printChanges(previousButtons, pressed);
      printStatus(pressed);
      previousButtons = pressed;
      firstSample = false;
      lastStatusMs = now;
    }
  }
  // Periodic status also makes the state visible when USB is opened later.
  if (now - lastStatusMs >= kHeartbeatMs) {
    printStatus(previousButtons);
    lastStatusMs = now;
  }
  delay(1);
}
