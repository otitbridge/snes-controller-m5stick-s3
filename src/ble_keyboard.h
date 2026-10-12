#pragma once
#include <stdint.h>

namespace BleKeyboard {
constexpr const char* kDeviceName = "SNES-Controller";
enum class Status { Unavailable, Starting, Advertising, Pairing, Connected, Resetting, ResetFailed };
bool begin(int batteryLevel);
// Called by the input task on every controller poll, not just input changes.
void update(uint16_t buttons);
void setBatteryLevel(int level);
// Called only by the input task. Disconnects and asynchronously deletes bonds.
void forgetPairing();
Status status();
const char* statusText(Status status);
void printDiagnostics();
}  // namespace BleKeyboard
