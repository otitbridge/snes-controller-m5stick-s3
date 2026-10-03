#include "web_monitor.h"
#include "web_page.h"
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

namespace WebMonitor {
namespace {
struct State {
  uint32_t sequence;
  uint16_t buttons;
};
State latest = {};
portMUX_TYPE stateLock = portMUX_INITIALIZER_UNLOCKED;
QueueHandle_t events = nullptr;
bool running = false;
WebServer http(80);
WebSocketsServer sockets(81);

State snapshot() {
  portENTER_CRITICAL(&stateLock);
  const State result = latest;
  portEXIT_CRITICAL(&stateLock);
  return result;
}

void sendState(const State& state, int client = -1) {
  char message[64];
  snprintf(message, sizeof(message), "{\"seq\":%lu,\"mask\":%u}",
           static_cast<unsigned long>(state.sequence), state.buttons);
  if (client < 0) {
    sockets.broadcastTXT(message);
  } else {
    sockets.sendTXT(static_cast<uint8_t>(client), message);
  }
}

void networkTask(void*) {
  // HTTP, WebSocket callbacks and writes have a single owner. Slow clients
  // cannot hold up the controller polling or the LCD on the Arduino task.
  http.on("/", HTTP_GET, [] {
    http.sendHeader("Cache-Control", "no-store");
    http.send_P(200, "text/html; charset=utf-8", kWebPage);
  });
  http.onNotFound([] { http.send(404, "text/plain", "Not found"); });
  sockets.onEvent([](uint8_t client, WStype_t type, uint8_t*, size_t) {
    if (type == WStype_CONNECTED) {
      sendState(snapshot(), client);
    }
  });
  http.begin();
  sockets.begin();
  sockets.enableHeartbeat(2000, 3000, 2);
  uint32_t lastHeartbeat = millis();
  for (;;) {
    sockets.loop();
    // Bound each batch so clients and HTTP continue to be serviced.
    State state;
    for (int i = 0; i < 16 && xQueueReceive(events, &state, 0) == pdTRUE; ++i) {
      sendState(state);
    }
    if (millis() - lastHeartbeat >= 1000) {
      sendState(snapshot());
      lastHeartbeat = millis();
    }
    http.handleClient();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
}  // namespace

bool begin(uint16_t initialButtons) {
  latest = {0, initialButtons};
  events = xQueueCreate(64, sizeof(State));
  if (!events) return false;
  WiFi.mode(WIFI_AP);
  const IPAddress address(192, 168, 4, 1);
  if (!WiFi.softAPConfig(address, address, IPAddress(255, 255, 255, 0)) ||
      !WiFi.softAP(kSsid, kPassword, 1, 0, 2)) {
    WiFi.mode(WIFI_OFF);
    vQueueDelete(events);
    events = nullptr;
    return false;
  }
  WiFi.setSleep(false);
  if (xTaskCreatePinnedToCore(networkTask, "snes-web", 8192, nullptr, 1,
                              nullptr, 0) != pdPASS) {
    WiFi.mode(WIFI_OFF);
    vQueueDelete(events);
    events = nullptr;
    return false;
  }
  running = true;
  return true;
}

void publish(uint16_t buttons) {
  if (!running) return;
  portENTER_CRITICAL(&stateLock);
  latest.buttons = buttons;
  ++latest.sequence;
  const State state = latest;
  portEXIT_CRITICAL(&stateLock);
  if (xQueueSend(events, &state, 0) != pdTRUE) {
    // Bounded memory under congestion: discard the oldest pending state.
    // The next event and periodic snapshot restore the current button state.
    State discarded;
    xQueueReceive(events, &discarded, 0);
    xQueueSend(events, &state, 0);
  }
}
}  // namespace WebMonitor
