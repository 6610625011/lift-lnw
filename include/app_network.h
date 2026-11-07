#pragma once
#include "elevator_sim.h"

// All socket operations run in a separate FreeRTOS task. The Arduino loop owns
// the simulation and exchanges bounded copies through queues.
namespace AppNetwork {
struct Request {
  enum class Action : uint8_t { Call, Select, Enter, Here };
  Action action;
  int floor;  // Select only.
  char id[65];
  char boot[17];
  uint32_t issuedAt;
};
void begin();
void publishState(const ElevatorSimulation& sim);
void event(const char* type, const char* source, const char* id = "");
bool receive(Request& request);
const char* validate(const Request& request, uint32_t now);
void reply(const Request& request, const char* result);
const char* bootId();
void diagnostics();
}
