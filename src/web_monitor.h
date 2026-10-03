#pragma once
#include <Arduino.h>

namespace WebMonitor {
constexpr const char* kSsid = "SNES-Monitor";
constexpr const char* kPassword = "12345678";
constexpr const char* kAddress = "192.168.4.1";
bool begin(uint16_t initialButtons);
// Never waits for a network client. Called from the input/display task only.
void publish(uint16_t buttons);
}  // namespace WebMonitor
