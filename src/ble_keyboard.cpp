#include "ble_keyboard.h"
#include "ble_keyboard_link.h"
#include "keyboard_state.h"
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLESecurity.h>
#include <atomic>
#include <vector>
#include <esp_gatt_common_api.h>
#include <esp_bt_main.h>
#include <esp_attr.h>

namespace BleKeyboard {
namespace {
constexpr uint8_t kReportId = 1;
// Standard six-key keyboard report, identical to the USB report payload.
uint8_t reportMap[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, kReportId,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00,
    0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01,
    0x29, 0x05, 0x91, 0x02, 0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
    0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0};

BLEServer* server = nullptr;
BLEHIDDevice* hid = nullptr;
BLECharacteristic* input = nullptr;
BLECharacteristic* bootInput = nullptr;
BLEDescriptor* inputCccd = nullptr;
BLEDescriptor* bootCccd = nullptr;
Keyboard::State keyboardState;
portMUX_TYPE linkLock = portMUX_INITIALIZER_UNLOCKED;
struct Connection {
  Link link;
  esp_gatt_if_t interface = ESP_GATT_IF_NONE;
  uint16_t id = 0;
  esp_bd_addr_t address = {};
  bool congested = false;
} connection;
std::atomic<bool> restartAdvertising{false};
std::atomic<bool> advertisingActive{false};
std::atomic<bool> advertisingFailed{false};
std::atomic<int> pendingRemovals{0};
std::atomic<bool> removalFailed{false};
bool initialized = false;
bool resetRequested = false;
bool disconnectRequested = false;
bool removingBonds = false;
// Retain the last Bluetooth event across a software panic/watchdog reset.
// This uses RTC RAM, so diagnosing bonding never writes additional NVS data.
struct Trace {
  uint32_t magic;
  uint32_t gapEvent;
  uint32_t gattEvent;
  uint32_t authResult;
  uint32_t stackMinimum;
};
RTC_NOINIT_ATTR volatile Trace trace;
constexpr uint32_t kTraceMagic = 0x534E4553;

Connection snapshot() {
  portENTER_CRITICAL(&linkLock);
  const Connection result = connection;
  portEXIT_CRITICAL(&linkLock);
  return result;
}

class SecurityCallbacks : public BLESecurityCallbacks {
  uint32_t onPassKeyRequest() override { return 0; }
  void onPassKeyNotify(uint32_t) override {}
  bool onSecurityRequest() override { return true; }
  bool onConfirmPIN(uint32_t) override { return false; }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t result) override {
    trace.authResult = result.success ? 0 : result.fail_reason;
    bool failed = false;
    uint16_t id = 0;
    portENTER_CRITICAL(&linkLock);
    if (connection.link.connected() &&
        memcmp(result.bd_addr, connection.address, sizeof(esp_bd_addr_t)) == 0) {
      connection.link.setEncrypted(result.success);
      failed = !result.success;
      id = connection.id;
    }
    portEXIT_CRITICAL(&linkLock);
    if (failed) server->disconnect(id);
  }
} securityCallbacks;

void onGattEvent(esp_gatts_cb_event_t event, esp_gatt_if_t interface,
                 esp_ble_gatts_cb_param_t* param) {
  trace.gattEvent = event;
  trace.stackMinimum = uxTaskGetStackHighWaterMark(nullptr);
  // Handles are immutable after services start. Only this small POD state is
  // shared with the input task; no BLE operations happen inside the lock.
  portENTER_CRITICAL(&linkLock);
  switch (event) {
    case ESP_GATTS_CONNECT_EVT:
      connection.link.connect();
      connection.interface = interface;
      connection.id = param->connect.conn_id;
      memcpy(connection.address, param->connect.remote_bda, sizeof(esp_bd_addr_t));
      connection.congested = false;
      restartAdvertising.store(false);
      advertisingActive.store(false);
      break;
    case ESP_GATTS_DISCONNECT_EVT:
      if (param->disconnect.conn_id == connection.id) {
        connection.link.disconnect();
        restartAdvertising.store(true);
      }
      break;
    case ESP_GATTS_WRITE_EVT:
      if (connection.link.connected() && param->write.conn_id == connection.id &&
          !param->write.is_prep && param->write.offset == 0) {
        const uint16_t handle = param->write.handle;
        if (param->write.len == 2 && inputCccd && bootCccd) {
          const bool enabled = (param->write.value[0] & 1) != 0;
          if (handle == inputCccd->getHandle()) connection.link.setSubscribed(false, enabled);
          if (handle == bootCccd->getHandle()) connection.link.setSubscribed(true, enabled);
        } else if (param->write.len == 1 && hid) {
          const uint8_t value = param->write.value[0];
          if (handle == hid->protocolMode()->getHandle() && value <= 1)
            connection.link.setReportMode(value == 1);
          if (handle == hid->hidControl()->getHandle() && value <= 1)
            connection.link.setSuspended(value == 0);
        }
      }
      break;
    case ESP_GATTS_CONGEST_EVT:
      if (param->congest.conn_id == connection.id)
        connection.congested = param->congest.congested;
      break;
    case ESP_GATTS_CONF_EVT:
      if (param->conf.conn_id == connection.id && param->conf.status != ESP_GATT_OK)
        connection.link.retry();
      break;
    default:
      break;
  }
  portEXIT_CRITICAL(&linkLock);
  if (event == ESP_GATTS_CONNECT_EVT) {
    // Arduino BLE keeps characteristic/CCCD values in RAM across clients.
    // Do not let the previous peer's protocol or pressed keys leak into a new
    // connection. The host explicitly subscribes for this connection.
    uint8_t reportMode = 1;
    uint8_t cccd[] = {0, 0};
    Keyboard::Report neutral;
    hid->protocolMode()->setValue(&reportMode, 1);
    inputCccd->setValue(cccd, sizeof(cccd));
    bootCccd->setValue(cccd, sizeof(cccd));
    input->setValue(reinterpret_cast<uint8_t*>(&neutral), sizeof(neutral));
    bootInput->setValue(reinterpret_cast<uint8_t*>(&neutral), sizeof(neutral));
    // 15-30ms, no slave latency, 4s supervision timeout. The host decides the
    // final interval; these are requests, not a measured latency guarantee.
    server->updateConnParams(param->connect.remote_bda, 12, 24, 0, 400);
  }
}

void onGapEvent(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
  trace.gapEvent = event;
  trace.stackMinimum = uxTaskGetStackHighWaterMark(nullptr);
  if (event == ESP_GAP_BLE_ADV_START_COMPLETE_EVT) {
    const bool success = param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS;
    advertisingActive.store(success);
    advertisingFailed.store(!success);
  } else if (event == ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT) {
    advertisingActive.store(false);
  } else if (event == ESP_GAP_BLE_REMOVE_BOND_DEV_COMPLETE_EVT) {
    if (param->remove_bond_dev_cmpl.status != ESP_BT_STATUS_SUCCESS)
      removalFailed.store(true);
    pendingRemovals.fetch_sub(1);
  }
}

void serviceReset() {
  if (!resetRequested) return;
  const Connection current = snapshot();
  if (current.link.connected()) {
    if (!disconnectRequested) {
      server->disconnect(current.id);
      disconnectRequested = true;
    }
    return;
  }
  if (!removingBonds) {
    BLEDevice::getAdvertising()->stop();
    const int count = esp_ble_get_bond_device_num();
    std::vector<esp_ble_bond_dev_t> devices(count > 0 ? count : 0);
    int obtained = count;
    if (count < 0 || (count > 0 &&
        esp_ble_get_bond_device_list(&obtained, devices.data()) != ESP_OK)) {
      removalFailed.store(true);
      obtained = 0;
    }
    pendingRemovals.store(obtained);
    removingBonds = true;
    for (int index = 0; index < obtained; ++index) {
      if (esp_ble_remove_bond_device(devices[index].bd_addr) != ESP_OK) {
        removalFailed.store(true);
        pendingRemovals.fetch_sub(1);
      }
    }
    return;
  }
  if (pendingRemovals.load() != 0) return;
  resetRequested = false;
  removingBonds = false;
  disconnectRequested = false;
  restartAdvertising.store(true);
  Serial.println(removalFailed.load() ? "BLE: pairing reset failed"
                                     : "BLE: pairing data cleared; forget device on iPhone too");
}
}  // namespace

bool begin(int batteryLevel) {
  if (trace.magic == kTraceMagic) {
    Serial.printf("BLE previous boot: gap=%u gatt=%u auth=%u stack_min=%u\n",
                  trace.gapEvent, trace.gattEvent, trace.authResult, trace.stackMinimum);
  }
  trace.magic = kTraceMagic;
  trace.gapEvent = UINT32_MAX;
  trace.gattEvent = UINT32_MAX;
  trace.authResult = UINT32_MAX;
  trace.stackMinimum = UINT32_MAX;
  BLEDevice::init(kDeviceName);
  if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_ENABLED) return false;
  BLEDevice::setSecurityCallbacks(&securityCallbacks);
  BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
  BLEDevice::setCustomGattsHandler(onGattEvent);
  BLEDevice::setCustomGapHandler(onGapEvent);
  BLESecurity security;
  security.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
  security.setCapability(ESP_IO_CAP_NONE);  // Just Works; no PIN entry required.
  security.setKeySize(16);
  security.setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  security.setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

  server = BLEDevice::createServer();
  hid = new BLEHIDDevice(server);
  hid->manufacturer()->setValue("SNES Controller Project");
  // Avoid impersonating a commercial game controller's VID/PID.
  uint8_t pnp[] = {0x02, 0x3A, 0x30, 0x01, 0x00, 0x00, 0x01};
  auto* pnpCharacteristic = hid->deviceInfo()->getCharacteristic(BLEUUID(uint16_t(0x2A50)));
  pnpCharacteristic->setValue(pnp, sizeof(pnp));
  hid->hidInfo(0, 0x02);  // Normally connectable, no remote wake.
  hid->reportMap(reportMap, sizeof(reportMap));
  input = hid->inputReport(kReportId);
  auto* output = hid->outputReport(kReportId);
  inputCccd = input->getDescriptorByUUID(BLEUUID(uint16_t(0x2902)));
  // Report references must be discoverable before the host encrypts the link.
  input->getDescriptorByUUID(BLEUUID(uint16_t(0x2908)))->setAccessPermissions(ESP_GATT_PERM_READ);
  output->getDescriptorByUUID(BLEUUID(uint16_t(0x2908)))->setAccessPermissions(ESP_GATT_PERM_READ);
  bootInput = hid->bootInput();
  bootInput->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED);
  bootCccd = bootInput->getDescriptorByUUID(BLEUUID(uint16_t(0x2902)));
  bootCccd->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED);
  hid->bootOutput()->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED);
  Keyboard::Report neutral;
  input->setValue(reinterpret_cast<uint8_t*>(&neutral), sizeof(neutral));
  bootInput->setValue(reinterpret_cast<uint8_t*>(&neutral), sizeof(neutral));
  setBatteryLevel(batteryLevel);
  hid->startServices();

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  // Keep the HID service/appearance in the advertising packet, and the full
  // name in scan response, so neither payload exceeds the 31-byte BLE limit.
  BLEAdvertisementData advertisement;
  advertisement.setFlags(ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);
  advertisement.setAppearance(HID_KEYBOARD);
  advertisement.setCompleteServices(hid->hidService()->getUUID());
  BLEAdvertisementData scanResponse;
  scanResponse.setName(kDeviceName);
  advertising->setAdvertisementData(advertisement);
  advertising->setScanResponseData(scanResponse);
  advertising->setMinInterval(160);  // 100ms advertising, in 0.625ms units.
  advertising->setMaxInterval(160);
  advertising->start();
  initialized = true;
  return true;
}

void setBatteryLevel(int level) {
  if (!hid || level < 0 || level > 100) return;
  uint8_t value = static_cast<uint8_t>(level);
  hid->batteryService()->getCharacteristic(BLEUUID(uint16_t(0x2A19)))->setValue(&value, 1);
}

void update(uint16_t buttons) {
  if (!initialized) return;
  serviceReset();
  const Connection current = snapshot();
  if (!resetRequested && !current.link.connected() && restartAdvertising.exchange(false))
    BLEDevice::startAdvertising();
  const bool active = current.link.ready() && !resetRequested;
  keyboardState.update(buttons, current.link.session(), active,
      [&current](const Keyboard::Report& report) {
        if (current.congested || esp_ble_get_cur_sendable_packets_num(current.id) == 0)
          return false;
        // Recheck after building the report, so an intervening disconnect or
        // subscription/protocol change cannot send an old session's input.
        const Connection latest = snapshot();
        if (!latest.link.ready() || latest.link.session() != current.link.session())
          return false;
        auto* characteristic = current.link.reportMode() ? input : bootInput;
        Keyboard::Report value = report;
        characteristic->setValue(reinterpret_cast<uint8_t*>(&value), sizeof(value));
        return esp_ble_gatts_send_indicate(current.interface, current.id,
            characteristic->getHandle(), sizeof(value),
            reinterpret_cast<uint8_t*>(&value), false) == ESP_OK;
      });
}

void forgetPairing() {
  if (!initialized || resetRequested) return;
  resetRequested = true;
  disconnectRequested = false;
  removingBonds = false;
  removalFailed.store(false);
}

Status status() {
  if (!initialized) return Status::Unavailable;
  if (resetRequested) return Status::Resetting;
  const Connection current = snapshot();
  if (current.link.ready()) return Status::Connected;
  if (current.link.connected()) return Status::Pairing;
  if (removalFailed.load()) return Status::ResetFailed;
  if (advertisingFailed.load()) return Status::Unavailable;
  return advertisingActive.load() ? Status::Advertising : Status::Starting;
}

const char* statusText(Status value) {
  switch (value) {
    case Status::Starting: return "BLE: starting";
    case Status::Advertising: return "BLE: SNES-Controller";
    case Status::Pairing: return "BLE: waiting for host";
    case Status::Connected: return "BLE: connected";
    case Status::Resetting: return "BLE: clearing pairing";
    case Status::ResetFailed: return "BLE: pairing reset failed";
    default: return "BLE: unavailable";
  }
}

void printDiagnostics() {
  if (!initialized) return;
  Serial.printf("BLE diag: gap=%u gatt=%u auth=%u stack_min=%u\n",
                trace.gapEvent, trace.gattEvent, trace.authResult, trace.stackMinimum);
}
}  // namespace BleKeyboard
