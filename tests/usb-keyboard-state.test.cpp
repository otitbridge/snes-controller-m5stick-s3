#include "../src/usb_keyboard_state.h"
#include <cassert>
#include <cstdio>
#include <vector>

using UsbKeyboard::Report;

struct Host {
  UsbKeyboard::State state;
  uint16_t buttons = 0;
  uint32_t session = 1;
  bool active = true;
  bool accepts = true;
  std::vector<Report> reports;

  void poll() {
    state.update(buttons, session, active, [this](const Report& report) {
      if (!accepts) return false;
      reports.push_back(report);
      return true;
    });
  }

  void connect() {
    active = true;
    ++session;
    poll();
    poll();
  }
};

void expectKeys(const Report& report, std::initializer_list<uint8_t> keys) {
  assert(report.modifiers == 0 && report.reserved == 0);
  size_t index = 0;
  for (uint8_t key : keys) {
    assert(index < 6 && report.keys[index++] == key);
  }
  while (index < 6) assert(report.keys[index++] == 0);
}

int main() {
  {
    Host host;
    host.buttons = (1U << 0) | (1U << 8);  // B + A held at startup.
    host.poll();
    expectKeys(host.reports.back(), {});
    host.poll();
    expectKeys(host.reports.back(), {0x1D, 0x1B});
    host.poll();
    assert(host.reports.size() == 2);  // Holding doesn't repeat presses.
    host.buttons = 0;
    host.poll();
    expectKeys(host.reports.back(), {});
    puts("PASS startup-held keys, steady hold, and release");
  }
  {
    Host host;
    host.buttons = 1U << 0;
    host.connect();
    host.active = false;
    host.poll();
    const size_t count = host.reports.size();
    host.buttons = 1U << 9;  // Release B and press X while disconnected.
    host.poll();
    assert(host.reports.size() == count);
    host.connect();
    expectKeys(host.reports[count], {});
    expectKeys(host.reports.back(), {0x16});
    puts("PASS reconnect uses current input after offline changes");
  }
  {
    Host host;
    host.buttons = 1U << 4;
    host.connect();
    const size_t count = host.reports.size();
    // Reconnect/resume can occur between polls without an observed inactive poll.
    host.connect();
    assert(host.reports.size() == count + 2);
    expectKeys(host.reports[count], {});
    expectKeys(host.reports.back(), {0x52});
    puts("PASS session change resends unchanged held input");
  }
  {
    Host host;
    host.accepts = false;
    host.buttons = 1U << 10;
    host.poll();
    host.poll();
    assert(host.reports.empty());
    host.accepts = true;
    host.poll();
    expectKeys(host.reports.back(), {});
    host.buttons = 1U << 11;  // Input changes during initial synchronization.
    host.poll();
    expectKeys(host.reports.back(), {0x1A});
    puts("PASS failed neutral send retries and restores latest input");
  }
  {
    Host host;
    host.buttons = 1U << 1;
    host.connect();
    host.accepts = false;
    host.buttons = 0;
    host.poll();
    expectKeys(host.reports.back(), {0x04});
    host.accepts = true;
    host.poll();  // Release retried without any new input edge.
    expectKeys(host.reports.back(), {});
    host.accepts = false;
    host.buttons = 1U << 2;
    host.poll();
    host.buttons = 1U << 3;
    host.accepts = true;
    host.poll();
    expectKeys(host.reports.back(), {0x28});
    puts("PASS failed release retries; obsolete failed press isn't replayed");
  }
  {
    Host host;
    host.buttons = 0x007F;  // Seven held keys exceed the six-key report.
    host.connect();
    expectKeys(host.reports.back(), {0x1D, 0x04, 0x2A, 0x28, 0x52, 0x51});
    host.buttons &= ~(1U << 0);
    host.poll();
    expectKeys(host.reports.back(), {0x04, 0x2A, 0x28, 0x52, 0x51, 0x50});
    puts("PASS still-held overflow key becomes visible when a slot frees");
  }
}
