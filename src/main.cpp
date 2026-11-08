#include <Arduino.h>
#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>
#include <string.h>

#include "config.h"
#include "elevator_sim.h"
#include "app_network.h"

namespace {
// Automatically identify the expander-to-LCD pin mapping at the scanned address.
hd44780_I2Cexp display(Config::kDisplayAddress);

ElevatorSimulation simulation;
bool displayReady = false;
bool displayDirty = true;
bool i2cReady = false;
char lastDisplayRows[2][17] = {};


bool addressResponds(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

void scanI2c() {
  if (!i2cReady) {
    Serial.println("I2C bus is not initialized.");
    return;
  }
  Serial.printf("I2C scan: SDA=GPIO%d, SCL=GPIO%d, %lu Hz.\n",
                Config::kDisplaySda, Config::kDisplayScl,
                static_cast<unsigned long>(Config::kI2cFrequencyHz));
  Serial.printf("Idle levels: SDA=%s, SCL=%s (normally both HIGH).\n",
                digitalRead(Config::kDisplaySda) ? "HIGH" : "LOW",
                digitalRead(Config::kDisplayScl) ? "HIGH" : "LOW");
  unsigned int found = 0;
  unsigned int timeouts = 0;
  for (uint8_t address = 0x08; address <= 0x77; ++address) {
    Wire.beginTransmission(address);
    const uint8_t result = Wire.endTransmission();
    if (result == 0) {
      Serial.printf("  Device found at 0x%02X\n", address);
      ++found;
    } else if (result == 5) {
      ++timeouts;
    }
  }
  Serial.printf("Scan complete: %u device(s), %u timeout(s).\n", found,
                timeouts);
  if (found == 0) {
    Serial.println("No device acknowledged. Check LCD power, ground, wires, and pin labels.");
  }
}

void initializeDisplay() {
  if (!i2cReady) {
    i2cReady = Wire.begin(Config::kDisplaySda, Config::kDisplayScl,
                         Config::kI2cFrequencyHz);
  }
  if (!i2cReady) {
    Serial.println("I2C initialization failed. Check SDA21 / SCL22.");
    return;
  }
  Wire.setTimeOut(20);
  Wire.setClock(Config::kI2cFrequencyHz);

  if (!addressResponds(Config::kDisplayAddress)) {
    displayReady = false;
    Serial.printf("LCD backpack not found at 0x%02X. Check power and SDA21/SCL22.\n",
                  Config::kDisplayAddress);
    Serial.println("Button/LED simulation will still run. Reset after rewiring.");
    return;
  }

  // First argument is clear/home time, second is normal instruction time.
  display.setExecTimes(Config::kLcdClearHomeUs, Config::kLcdInstructionUs);
  const int status = display.begin(Config::kDisplayColumns, Config::kDisplayRows);
  displayReady = status == 0;
  if (!displayReady) {
    Serial.printf("LCD initialization failed (status %d); using Serial + LED.\n",
                  status);
    Serial.println("Check LCD contrast and the module's rated supply voltage.");
    return;
  }

  display.backlight();
  display.display();
  display.noCursor();
  display.noBlink();
  display.clear();
  memset(lastDisplayRows, 0, sizeof(lastDisplayRows));
  Serial.printf("HD44780 16x2 LCD initialized at 0x%02X.\n",
                Config::kDisplayAddress);
  Serial.println("If text is invisible, slowly adjust the backpack contrast screw.");
}

void testLcdMemory();

void updateSerialDiagnostics() {
  if (!Serial.available()) {
    return;
  }
  const char command = static_cast<char>(Serial.read());
  if (command == 'i' || command == 'I') {
    scanI2c();
  } else if (command == 'o' || command == 'O') {
    initializeDisplay();
    displayDirty = true;
  } else if (command == 't' || command == 'T') {
    testLcdMemory();
  } else if (command == 'n' || command == 'N') {
    AppNetwork::diagnostics();
  }
}

void logSimulation() {
  char top[17], bottom[17];
  simulation.screen(top, bottom);
  Serial.printf("LCD: [%s] [%s]\n", top, bottom);
}

bool onVerificationPressed(uint32_t now, const char* source = "button", int hallFloor = 0,
                           int car = -1) {
  if (!simulation.request(now, hallFloor, car)) {
    Serial.println("Trip already active; request ignored.");
    return false;
  }

  displayDirty = true;
  logSimulation();
  AppNetwork::publishState(simulation);
  AppNetwork::event("assigned", source);
  return true;
}

bool onFloorSelected(uint32_t now, int floor, const char* source = "dashboard") {
  if (!simulation.selectFloor(now, floor)) {
    Serial.printf("Floor %d rejected.\n", floor);
    return false;
  }
  displayDirty = true;
  logSimulation();
  AppNetwork::publishState(simulation);
  AppNetwork::event("selected", source);
  return true;
}

// Passenger walks into the car that is waiting with doors open.
bool onEnterCar(uint32_t now, const char* source) {
  if (!simulation.enter(now)) {
    Serial.println("No open car to enter.");
    return false;
  }
  displayDirty = true;
  logSimulation();
  AppNetwork::publishState(simulation);
  AppNetwork::event("boarded", source);
  return true;
}

// Passenger tells the system which floor they stand on (idle only).
bool onHereSet(int floor) {
  if (!simulation.setHall(floor)) {
    return false;
  }
  displayDirty = true;
  logSimulation();
  AppNetwork::publishState(simulation);
  return true;
}

void updateKeypad(uint32_t now) {
  (void)now;
}

bool lastRawEmergency = HIGH, stableEmergency = HIGH;
uint32_t rawEmergencyChangedAt = 0;

bool lastRawDoorOpen = HIGH, stableDoorOpen = HIGH;
uint32_t rawDoorOpenChangedAt = 0;

bool lastRawDoorClose = HIGH, stableDoorClose = HIGH;
uint32_t rawDoorCloseChangedAt = 0;

void onEmergencyPressed(uint32_t now) {
  const bool active = simulation.toggleEmergency(now);
  displayDirty = true;
  Serial.printf("[Car Unit] Emergency Stop button pressed! Emergency: %s\n",
                active ? "ACTIVE" : "CLEARED");
  AppNetwork::event(active ? "emergency_active" : "emergency_cleared", "button");
  AppNetwork::publishState(simulation);
}

void onDoorOpenPressed(uint32_t now) {
  Serial.println("[Car Unit] >>> Button D32 (DOOR OPEN) Pressed! <<<");
  const bool wasClosing = simulation.isDoorClosing();
  if (simulation.doorOpen(now)) {
    displayDirty = true;
    if (wasClosing) {
      Serial.println("  -> Action: Door closing INTERRUPTED! Re-opening doors.");
      AppNetwork::event("door_reopen", "button");
    } else {
      Serial.println("  -> Action: Doors extended / kept open.");
      AppNetwork::event("door_open", "button");
    }
    AppNetwork::publishState(simulation);
  } else {
    Serial.printf("  -> Note: Lift is currently idle/moving (Buttons active when doors are open).\n");
  }
}

void onDoorClosePressed(uint32_t now) {
  Serial.println("[Car Unit] >>> Button D33 (DOOR CLOSE) Pressed! <<<");
  if (simulation.doorClose(now)) {
    displayDirty = true;
    Serial.println("  -> Action: Door closing initiated (3 seconds)... Press Door Open (D32) to interrupt.");
    AppNetwork::event("door_close", "button");
    AppNetwork::publishState(simulation);
  } else {
    Serial.printf("  -> Note: Lift is currently idle/moving (Buttons active when doors are open).\n");
  }
}

void updateButtons(uint32_t now) {
  // 1. Emergency Button (GPIO 27)
  const bool rawEmer = digitalRead(Config::kEmergencyButton);
  if (rawEmer != lastRawEmergency) {
    lastRawEmergency = rawEmer;
    rawEmergencyChangedAt = now;
  }
  if (rawEmer != stableEmergency &&
      static_cast<uint32_t>(now - rawEmergencyChangedAt) >= Config::kDebounceMs) {
    stableEmergency = rawEmer;
    if (stableEmergency == LOW) {
      onEmergencyPressed(now);
    }
  }

  // 2. Door Open Button (GPIO 32)
  const bool rawOpen = digitalRead(Config::kDoorOpenButton);
  if (rawOpen != lastRawDoorOpen) {
    lastRawDoorOpen = rawOpen;
    rawDoorOpenChangedAt = now;
  }
  if (rawOpen != stableDoorOpen &&
      static_cast<uint32_t>(now - rawDoorOpenChangedAt) >= Config::kDebounceMs) {
    stableDoorOpen = rawOpen;
    if (stableDoorOpen == LOW) {
      onDoorOpenPressed(now);
    }
  }

  // 3. Door Close Button (GPIO 33)
  const bool rawClose = digitalRead(Config::kDoorCloseButton);
  if (rawClose != lastRawDoorClose) {
    lastRawDoorClose = rawClose;
    rawDoorCloseChangedAt = now;
  }
  if (rawClose != stableDoorClose &&
      static_cast<uint32_t>(now - rawDoorCloseChangedAt) >= Config::kDebounceMs) {
    stableDoorClose = rawClose;
    if (stableDoorClose == LOW) {
      onDoorClosePressed(now);
    }
  }
}

void updateButton(uint32_t now) {
  updateButtons(now);
}

void updateTrip(uint32_t now) {
  const auto previous = simulation.phase();
  if (simulation.tick(now)) {
    displayDirty = true;
    logSimulation();
    AppNetwork::publishState(simulation);
    if (simulation.phase() != previous) {
      const char* names[] = {"ready", "coming", "pickup", "riding", "arrival"};
      AppNetwork::event(names[static_cast<int>(simulation.phase())], "simulation");
    }
  }
}

void updateStatusLed(uint32_t now) {
  if (simulation.emergency()) {
    // Rapid strobe during emergency
    digitalWrite(Config::kStatusLed, ((now / 100) % 2) == 0 ? HIGH : LOW);
    return;
  }
  const auto phase = simulation.phase();
  bool ledOn = phase == ElevatorSimulation::Phase::Arrived ||
               phase == ElevatorSimulation::Phase::Boarding;
  if (phase == ElevatorSimulation::Phase::Coming ||
      phase == ElevatorSimulation::Phase::Riding) {
    ledOn = ((static_cast<uint32_t>(now - simulation.phaseStartedAt()) /
              Config::kLedBlinkMs) % 2) == 0;
  }
  digitalWrite(Config::kStatusLed, ledOn ? HIGH : LOW);
}

void writeDisplayLine(uint8_t row, const char* text) {
  // Pad each row so shorter text removes characters left by the previous state.
  char padded[Config::kDisplayColumns + 1];
  snprintf(padded, sizeof(padded), "%-16.16s", text);
  if (strcmp(lastDisplayRows[row], padded) == 0) {
    return;
  }
  display.setCursor(0, row);
  display.print(padded);
  memcpy(lastDisplayRows[row], padded, sizeof(padded));
}

void testLcdMemory() {
  if (!displayReady) {
    Serial.println("LCD memory test requires successful initialization.");
    return;
  }
  const char* patterns[] = {"LCD TEST 1234567", "ABCDEFGHIJKLMNOP"};
  unsigned int mismatches = 0;
  for (uint8_t row = 0; row < 2; ++row) {
    writeDisplayLine(row, patterns[row]);
    Serial.printf("LCD row %u read-back: ", row);
    for (uint8_t col = 0; col < 16; ++col) {
      display.setCursor(col, row);
      const int value = display.read();
      if (value != patterns[row][col]) {
        ++mismatches;
      }
      Serial.write(value >= 32 && value <= 126 ? value : '.');
    }
    Serial.println();
  }
  Serial.printf("LCD memory test: %s (%u mismatched characters).\n",
                mismatches == 0 ? "PASS" : "FAIL", mismatches);
  displayDirty = true;  // Restore the current elevator screen on the next loop.
}

void renderDisplay() {
  if (!displayReady || !displayDirty) {
    return;
  }
  displayDirty = false;
  char topLine[17];
  char bottomLine[17];
  simulation.screen(topLine, bottomLine);
  writeDisplayLine(0, topLine);
  writeDisplayLine(1, bottomLine);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  pinMode(Config::kStatusLed, OUTPUT);
  digitalWrite(Config::kStatusLed, LOW);
  pinMode(Config::kEmergencyButton, INPUT_PULLUP);
  pinMode(Config::kDoorOpenButton, INPUT_PULLUP);
  pinMode(Config::kDoorCloseButton, INPUT_PULLUP);

  lastRawEmergency = digitalRead(Config::kEmergencyButton);
  stableEmergency = lastRawEmergency;
  rawEmergencyChangedAt = millis();

  lastRawDoorOpen = digitalRead(Config::kDoorOpenButton);
  stableDoorOpen = lastRawDoorOpen;
  rawDoorOpenChangedAt = millis();

  lastRawDoorClose = digitalRead(Config::kDoorCloseButton);
  stableDoorClose = lastRawDoorClose;
  rawDoorCloseChangedAt = millis();

  delay(250);  // Allow the LCD supply to settle once at startup.
  initializeDisplay();
  if (!displayReady) {
    scanI2c();
  }
  Serial.println("Diagnostics: 'i' I2C scan; 'o' retry LCD; 't' LCD memory test; 'n' network.");
  Serial.println("Car Unit ready. Buttons: GPIO27 = Emergency, GPIO32 = Door Open, GPIO33 = Door Close.");
  simulation.seed(esp_random());  // Different background traffic every boot.
  AppNetwork::begin();
  AppNetwork::publishState(simulation);
  logSimulation();
  renderDisplay();
}

void loop() {
  const uint32_t now = millis();
  updateButton(now);
  updateKeypad(now);
  AppNetwork::Request request;
  if (AppNetwork::receive(request)) {
    const char* result = AppNetwork::validate(request, now);
    if (!result) {
      if (request.action == AppNetwork::Request::Action::Select) {
        result = onFloorSelected(now, request.floor) ? "accepted" : "busy";
      } else if (request.action == AppNetwork::Request::Action::Enter) {
        result = onEnterCar(now, "dashboard") ? "accepted" : "busy";
      } else if (request.action == AppNetwork::Request::Action::Here) {
        result = onHereSet(request.floor) ? "accepted" : "busy";
      } else {
        result = onVerificationPressed(now, "dashboard", request.floor, request.car) ? "accepted" : "busy";
      }
    }
    AppNetwork::reply(request, result);
  }
  updateTrip(now);
  updateStatusLed(now);
  renderDisplay();
  updateSerialDiagnostics();
  static uint32_t lastPublished = 0;
  if (static_cast<uint32_t>(now-lastPublished) >= 1000) {
    AppNetwork::publishState(simulation);
    lastPublished = now;
  }
  delay(1);  // Yield to ESP32 background tasks, not a trip/debounce timer.
}
