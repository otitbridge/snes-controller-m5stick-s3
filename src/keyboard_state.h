#pragma once
#include <stdint.h>
#include <string.h>

namespace Keyboard {
// Standard keyboard input report, without the separately supplied report ID.
struct Report {
  uint8_t modifiers = 0;
  uint8_t reserved = 0;
  uint8_t keys[6] = {};
};
static_assert(sizeof(Report) == 8, "USB keyboard report must be 8 bytes");

inline Report makeReport(uint16_t buttons) {
  // Raw HID usages in SNES serial order:
  // Z, A, Backspace, Enter, Up, Down, Left, Right, X, S, Q, W.
  constexpr uint8_t usages[] = {
      0x1D, 0x04, 0x2A, 0x28, 0x52, 0x51,
      0x50, 0x4F, 0x1B, 0x16, 0x14, 0x1A};
  Report report;
  uint8_t count = 0;
  for (uint8_t bit = 0; bit < sizeof(usages); ++bit) {
    if (buttons & (1U << bit)) {
      report.keys[count++] = usages[bit];
      if (count == sizeof(report.keys)) break;
    }
  }
  return report;
}

class State {
 public:
  // Called only by the input task. send returns true after a successful send.
  // A new USB/BLE session (including resume) first gets a neutral report; the
  // following poll sends the latest input, even if no buttons have changed.
  template <typename Sender>
  void update(uint16_t buttons, uint32_t session, bool active, Sender send) {
    if (session != session_ || !active) {
      resetPending_ = true;
      synchronized_ = false;
    }
    session_ = session;
    if (!active) return;

    if (resetPending_) {
      if (send(Report{})) resetPending_ = false;
      return;
    }

    const Report report = makeReport(buttons);
    if (!synchronized_ || memcmp(&report, &sent_, sizeof(report)) != 0) {
      if (send(report)) {
        sent_ = report;
        synchronized_ = true;
      }
    }
  }

 private:
  uint32_t session_ = 0;
  bool resetPending_ = true;
  bool synchronized_ = false;
  Report sent_;
};
}  // namespace Keyboard
