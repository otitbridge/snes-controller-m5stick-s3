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
M5Canvas controllerCanvas(&M5.Display);
bool canvasReady = false;

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
  // Compose the whole controller off-screen to avoid flicker on state changes.
  lgfx::LGFXBase& lcd = canvasReady
      ? static_cast<lgfx::LGFXBase&>(controllerCanvas)
      : static_cast<lgfx::LGFXBase&>(M5.Display);
  lcd.startWrite();
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.drawCenterString("SNES CONTROLLER", 120, 3);

  const auto buttonColor = [pressed](uint8_t bit) -> uint16_t {
    return (pressed & (1U << bit)) ? TFT_GREEN : TFT_DARKGREY;
  };
  const auto buttonTextColor = [pressed](uint8_t bit) -> uint16_t {
    return (pressed & (1U << bit)) ? TFT_BLACK : TFT_WHITE;
  };

  // Shoulder buttons sit above the two grips.
  lcd.fillRoundRect(20, 21, 58, 16, 5, buttonColor(10));
  lcd.setTextColor(buttonTextColor(10), buttonColor(10));
  lcd.drawCenterString("L", 49, 25);
  lcd.fillRoundRect(162, 21, 58, 16, 5, buttonColor(11));
  lcd.setTextColor(buttonTextColor(11), buttonColor(11));
  lcd.drawCenterString("R", 191, 25);

  // Grey shell with circular grips and recessed controls.
  lcd.fillRoundRect(8, 36, 224, 76, 32, TFT_LIGHTGREY);
  lcd.fillCircle(45, 74, 30, TFT_SILVER);
  lcd.fillCircle(195, 74, 33, TFT_SILVER);

  // D-pad: UP=bit4, DOWN=bit5, LEFT=bit6, RIGHT=bit7.
  lcd.fillRect(39, 68, 12, 12, TFT_DARKGREY);
  lcd.fillRect(39, 51, 12, 17, buttonColor(4));
  lcd.fillRect(39, 80, 12, 17, buttonColor(5));
  lcd.fillRect(22, 68, 17, 12, buttonColor(6));
  lcd.fillRect(51, 68, 17, 12, buttonColor(7));
  lcd.fillTriangle(45, 54, 41, 60, 49, 60, buttonTextColor(4));
  lcd.fillTriangle(45, 94, 41, 88, 49, 88, buttonTextColor(5));
  lcd.fillTriangle(25, 74, 31, 70, 31, 78, buttonTextColor(6));
  lcd.fillTriangle(65, 74, 59, 70, 59, 78, buttonTextColor(7));

  // SELECT and START are between the D-pad and the face buttons.
  lcd.setTextColor(TFT_BLACK, TFT_LIGHTGREY);
  lcd.drawCenterString("SELECT", 94, 63);
  lcd.drawCenterString("START", 136, 63);
  lcd.fillRoundRect(81, 78, 26, 10, 5, buttonColor(2));
  lcd.fillRoundRect(123, 78, 26, 10, 5, buttonColor(3));

  // Actual SNES diamond: X above, Y left, A right, B below.
  const auto faceButton = [&](uint8_t bit, int x, int y) {
    lcd.fillCircle(x, y, 10, buttonColor(bit));
    lcd.setTextColor(buttonTextColor(bit), buttonColor(bit));
    lcd.drawCenterString(kButtonNames[bit], x, y - 4);
  };
  faceButton(9, 195, 53);   // X
  faceButton(1, 174, 74);   // Y
  faceButton(8, 216, 74);   // A
  faceButton(0, 195, 95);   // B

  lcd.setTextColor(pressed ? TFT_GREEN : TFT_LIGHTGREY, TFT_BLACK);
  lcd.drawCenterString(pressed ? "GREEN = PRESSED" : "NO BUTTON PRESSED", 120, 121);
  lcd.endWrite();
  if (canvasReady) {
    controllerCanvas.pushSprite(0, 0);
  }
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
  controllerCanvas.setColorDepth(16);
  canvasReady = controllerCanvas.createSprite(M5.Display.width(), M5.Display.height()) != nullptr;
  controllerCanvas.setFont(&fonts::Font0);
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
