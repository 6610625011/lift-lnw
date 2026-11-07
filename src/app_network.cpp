#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include "app_network.h"
#include "secrets.h"

namespace AppNetwork {
namespace {
constexpr char kState[] = "elevator/v1/mockup/state";
constexpr char kEvents[] = "elevator/v1/mockup/events";
constexpr char kRequest[] = "elevator/v1/mockup/request";
constexpr char kAvailability[] = "elevator/v1/mockup/availability";
struct Message { char payload[768]; };
struct Previous { char id[65]; char result[16]; };
class TimedClient : public WiFiClient {
 public:
  int connect(IPAddress ip, uint16_t port) override { return WiFiClient::connect(ip, port, 1500); }
  int connect(const char* host, uint16_t port) override { return WiFiClient::connect(host, port, 1500); }
};
QueueHandle_t states, events, requests;
char boot[17];
Previous previous[32] = {};
size_t nextPrevious = 0;
volatile uint32_t lastWorkerAt = 0;
volatile uint8_t workerStage = 0;
volatile bool brokerConnected = false;

void callback(char*, byte* payload, unsigned int length) {
  JsonDocument doc;
  if (length > 384 || deserializeJson(doc, payload, length)) return;
  const char* id = doc["request_id"] | "";
  const char* bootId = doc["boot_id"] | "";
  const char* action = doc["action"] | "";
  const bool call = !strcmp(action, "verify") || !strcmp(action, "call");
  const bool select = !strcmp(action, "select");
  const bool enter = !strcmp(action, "enter");
  const bool here = !strcmp(action, "here");
  if (!call && !select && !enter && !here) return;

  char reqIdBuf[65];
  if (!*id) {
    snprintf(reqIdBuf, sizeof(reqIdBuf), "req-%lu", millis());
    id = reqIdBuf;
  }
  if (strlen(id) > 64) return;
  if ((select || here) && (!doc["floor"].is<int>() || doc["floor"].as<int>() < Config::kMinFloor ||
                 doc["floor"].as<int>() > Config::kMaxFloor)) return;
  Request request{};
  request.action = select ? Request::Action::Select
                 : enter ? Request::Action::Enter
                 : here ? Request::Action::Here : Request::Action::Call;
  request.floor = doc["floor"].is<int>() ? doc["floor"].as<int>() : 0;
  if (call && request.floor != 0 &&
      (request.floor < Config::kMinFloor || request.floor > Config::kMaxFloor)) return;
  strlcpy(request.id, id, sizeof(request.id));
  strlcpy(request.boot, strlen(bootId) == 16 ? bootId : boot, sizeof(request.boot));
  request.issuedAt = doc["issued_uptime_ms"].is<uint32_t>() ? doc["issued_uptime_ms"].as<uint32_t>() : millis();
  xQueueSend(requests, &request, 0);
}

void worker(void*) {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.begin(Secrets::wifiSsid, Secrets::wifiPassword);
  TimedClient socket;
  socket.setTimeout(2);
  PubSubClient mqtt(socket);
  mqtt.setServer(Secrets::mqttHost, 1883);
  mqtt.setBufferSize(1024);
  mqtt.setSocketTimeout(2);
  mqtt.setKeepAlive(10);
  mqtt.setCallback(callback);
  uint32_t lastWifi = millis(), lastMqtt = millis() - 5000;
  bool hadWifi = false;
  Message latest{};
  bool haveState = false, dirty = false;
  char clientId[40];
  snprintf(clientId, sizeof(clientId), "elevator-%s", boot);
  for (;;) {
    lastWorkerAt = millis();
    workerStage = 1;
    if (xQueueReceive(states, &latest, 0) == pdTRUE) {
      haveState = dirty = true;
    }
    const uint32_t now = millis();
    const bool wifi = WiFi.status() == WL_CONNECTED;
    if (wifi && !hadWifi) {
      Serial.printf("Wi-Fi connected: %s; ESP32 IP %s; broker %s:1883\n",
                    Secrets::wifiSsid, WiFi.localIP().toString().c_str(), Secrets::mqttHost);
    }
    hadWifi = wifi;
    if (!wifi) {
      if (mqtt.connected()) socket.stop();
      if (static_cast<uint32_t>(now-lastWifi) >= 15000) {
        Serial.printf("Wi-Fi waiting for %s (status %d). Retrying.\n", Secrets::wifiSsid, WiFi.status());
        WiFi.disconnect();
        WiFi.begin(Secrets::wifiSsid, Secrets::wifiPassword);
        lastWifi = now;
      }
    } else if (!mqtt.connected() && static_cast<uint32_t>(now-lastMqtt) >= 5000) {
      lastMqtt = now;
      if (mqtt.connect(clientId, Secrets::mqttUser, Secrets::mqttPassword,
                       kAvailability, 1, true, "offline")) {
        mqtt.subscribe(kRequest, 1);
        mqtt.publish(kAvailability, "online", true);
        dirty = haveState;
        Serial.println("MQTT connected. Dashboard is receiving ESP32 data.");
      } else {
        Serial.printf("MQTT connection failed (%d). Local simulation continues.\n", mqtt.state());
      }
    }
    if (mqtt.connected()) {
      brokerConnected = true;
      workerStage = 2;
      mqtt.loop();
      workerStage = 3;
      if (dirty && mqtt.publish(kState, latest.payload, true)) dirty = false;
      Message message;
      if (xQueueReceive(events, &message, 0) == pdTRUE) {
        workerStage = 4;
        mqtt.publish(kEvents, message.payload, false);
      }
    } else {
      brokerConnected = false;
      // Events are transient; never replay old commands or events on reconnect.
      xQueueReset(events);
    }
    workerStage = 5;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
}

const char* bootId() { return boot; }
void diagnostics() {
  Serial.printf("Network: worker age=%lu ms, stage=%u, MQTT=%s, heap=%lu, WiFi=%d\n",
                static_cast<unsigned long>(millis()-lastWorkerAt),workerStage,
                brokerConnected ? "connected" : "disconnected", static_cast<unsigned long>(ESP.getFreeHeap()), WiFi.status());
}
void begin() {
  snprintf(boot, sizeof(boot), "%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random());
  states = xQueueCreate(1, sizeof(Message));
  events = xQueueCreate(12, sizeof(Message));
  requests = xQueueCreate(8, sizeof(Request));
  if (!states || !events || !requests) {
    Serial.println("Network queue allocation failed.");
    abort();
  }
  xTaskCreatePinnedToCore(worker, "mqtt", 8192, nullptr, 1, nullptr, 0);
}

void publishState(const ElevatorSimulation& sim) {
  const char* phases[] = {"idle", "coming", "boarding", "riding", "arrived"};
  JsonDocument doc;
  doc["boot_id"] = boot;
  doc["uptime_ms"] = millis();
  doc["phase"] = phases[static_cast<int>(sim.phase())];
  char assigned[2] = {sim.selectedCar() ? sim.selectedCar()->id : '-', 0};
  doc["assigned"] = assigned;
  doc["hall_floor"] = sim.hallFloor();
  doc["destination"] = sim.destination();  // 0 = not chosen yet.
  doc["boarded"] = sim.boarded();          // Passenger is inside the car.
  doc["entry"] = sim.entry();              // Keypad digits typed so far.
  for (size_t i=0; i<4; ++i) {
    char id[2] = {sim.car(i).id, 0};
    doc["floors"][id] = sim.car(i).floor;
  }
  for (size_t i=0; i<4; ++i) {
    char id[2] = {sim.car(i).id, 0};
    // Where the car is heading for OTHER passengers (0 = free).
    doc["targets"][id] = sim.car(i).mode == ElevatorSimulation::Mode::Free ? 0 : sim.car(i).target;
    if (sim.phase() != ElevatorSimulation::Phase::Idle) doc["eta_s"][id] = sim.etaSeconds(i);
  }
  char top[17], bottom[17];
  sim.screen(top, bottom);
  doc["lcd"][0] = top;
  doc["lcd"][1] = bottom;
  Message message{};
  serializeJson(doc, message.payload, sizeof(message.payload));
  xQueueOverwrite(states, &message);
}

void event(const char* type, const char* source, const char* id) {
  JsonDocument doc;
  doc["type"] = type;
  doc["source"] = source;
  doc["request_id"] = id;
  doc["boot_id"] = boot;
  doc["uptime_ms"] = millis();
  Message message{};
  serializeJson(doc, message.payload, sizeof(message.payload));
  xQueueSend(events, &message, 0);
}
bool receive(Request& request) { return xQueueReceive(requests, &request, 0) == pdTRUE; }
const char* validate(const Request& request, uint32_t now) {
  if (strcmp(request.boot, boot)) return "expired";
  for (const auto& item : previous) {
    if (!strcmp(item.id, request.id)) return item.result;
  }
  if (static_cast<uint32_t>(now-request.issuedAt) > 5000) return "expired";
  return nullptr;
}
void reply(const Request& request, const char* result) {
  strlcpy(previous[nextPrevious].id, request.id, sizeof(previous[0].id));
  strlcpy(previous[nextPrevious].result, result, sizeof(previous[0].result));
  nextPrevious = (nextPrevious+1)%32;
  event(result, "dashboard", request.id);
}
}
