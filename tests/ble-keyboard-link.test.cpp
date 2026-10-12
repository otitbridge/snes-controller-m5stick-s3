#include "../src/ble_keyboard_link.h"
#include "../src/keyboard_state.h"
#include <cassert>
#include <cstdio>
#include <vector>

struct Host {
  BleKeyboard::Link link;
  Keyboard::State keyboard;
  uint16_t buttons = 1;
  bool accepts = true;
  std::vector<Keyboard::Report> reports;

  void poll() {
    keyboard.update(buttons, link.session(), link.ready(),
        [this](const Keyboard::Report& report) {
          if (!accepts) return false;
          reports.push_back(report);
          return true;
        });
  }
  void pair() {
    link.connect();
    link.setEncrypted(true);
    link.setSubscribed(false, true);
    poll();
    poll();
  }
};

int main() {
  {
    Host host;
    host.link.connect();
    host.poll();
    host.link.setSubscribed(false, true);
    host.poll();
    assert(host.reports.empty());  // Subscribed but not encrypted.
    host.link.setEncrypted(true);
    host.poll();
    assert(host.reports.size() == 1 && host.reports.back().keys[0] == 0);
    host.poll();
    assert(host.reports.back().keys[0] == 0x1D);
    puts("PASS BLE waits for encryption before synchronizing held input");
  }
  {
    Host host;
    host.link.connect();
    host.link.setEncrypted(true);
    host.poll();
    assert(host.reports.empty());  // Encrypted but not subscribed.
    host.link.setSubscribed(false, true);
    host.poll();
    host.poll();
    assert(host.reports.size() == 2 && host.reports.back().keys[0] == 0x1D);
    puts("PASS BLE waits for actual notification subscription");
  }
  {
    Host host;
    host.pair();
    host.link.disconnect();
    host.link.connect();
    host.poll();
    assert(!host.link.ready() && host.reports.size() == 2);
    host.buttons = 1U << 8;  // Old B released, A now held.
    host.link.setEncrypted(true);
    host.poll();
    assert(host.reports.size() == 2);
    host.link.setSubscribed(false, true);
    host.poll();
    host.poll();
    assert(host.reports[2].keys[0] == 0 && host.reports.back().keys[0] == 0x1B);
    puts("PASS reconnect resets old authentication and subscriptions");
  }
  {
    Host host;
    host.pair();
    host.link.setSuspended(true);
    host.buttons = 0;
    host.poll();
    assert(host.reports.size() == 2);
    host.link.setSuspended(false);
    host.poll();
    host.poll();
    assert(host.reports.size() == 4 && host.reports.back().keys[0] == 0);
    puts("PASS HID suspend pauses notifications and resume restores current input");
  }
  {
    Host host;
    host.pair();
    host.link.setSubscribed(false, false);
    host.poll();
    assert(host.reports.size() == 2);
    host.link.setSubscribed(false, true);
    host.poll();
    host.poll();
    assert(host.reports.size() == 4 && host.reports[2].keys[0] == 0);
    puts("PASS resubscription starts with a neutral report");
  }
  {
    Host host;
    host.pair();
    host.link.setReportMode(false);
    host.poll();
    assert(!host.link.ready() && host.reports.size() == 2);
    host.link.setSubscribed(true, true);
    host.poll();
    host.poll();
    assert(host.link.ready() && host.reports.size() == 4);
    host.link.setReportMode(true);
    host.poll();
    host.poll();
    assert(host.reports.size() == 6);
    puts("PASS boot and report protocols use their own subscriptions");
  }
  {
    Host host;
    host.pair();
    host.link.setEncrypted(false);
    host.buttons = 1U << 10;
    host.poll();
    assert(!host.link.ready() && host.reports.size() == 2);
    puts("PASS loss of encryption stops HID output");
  }
  {
    Host host;
    host.pair();
    host.link.retry();  // GATT reports an asynchronously failed send.
    host.accepts = false;
    host.poll();
    assert(host.reports.size() == 2);
    host.accepts = true;
    host.poll();
    host.buttons = 1U << 11;
    host.poll();
    assert(host.reports.size() == 4 && host.reports[2].keys[0] == 0);
    assert(host.reports.back().keys[0] == 0x1A);
    const uint32_t session = host.link.session();
    host.link.setEncrypted(true);
    host.link.setSubscribed(false, true);
    host.poll();
    assert(host.link.session() == session && host.reports.size() == 4);
    puts("PASS GATT retry restores latest input; duplicate events don't retrigger keys");
  }
}
