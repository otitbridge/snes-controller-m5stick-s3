#pragma once
#include <stdint.h>

namespace BleKeyboard {
// Transport readiness is separate from report delivery. Callers serialize all
// access to this state because GAP/GATT callbacks run on the Bluetooth task.
class Link {
 public:
  void connect() { reset(true); }
  void disconnect() { reset(false); }
  void setEncrypted(bool value) { change(encrypted_, value); }
  void setSubscribed(bool boot, bool value) {
    change(boot ? bootSubscribed_ : reportSubscribed_, value);
  }
  void setReportMode(bool value) { change(reportMode_, value); }
  void setSuspended(bool value) { change(suspended_, value); }
  void retry() { ++session_; }

  bool connected() const { return connected_; }
  bool reportMode() const { return reportMode_; }
  uint32_t session() const { return session_; }
  bool ready() const {
    return connected_ && encrypted_ && !suspended_ &&
        (reportMode_ ? reportSubscribed_ : bootSubscribed_);
  }

 private:
  void reset(bool connected) {
    const uint32_t next = session_ + 1;
    *this = Link{};
    session_ = next;
    connected_ = connected;
  }
  void change(bool& field, bool value) {
    if (field != value) {
      field = value;
      ++session_;
    }
  }
  uint32_t session_ = 0;
  bool connected_ = false;
  bool encrypted_ = false;
  bool reportSubscribed_ = false;
  bool bootSubscribed_ = false;
  bool reportMode_ = true;
  bool suspended_ = false;
};
}  // namespace BleKeyboard
