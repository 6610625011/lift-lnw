#include <Arduino.h>
#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>
#include <string.h>

#include "config.h"
#include "elevator_sim.h"
#include "network.h"

namespace {
// Automatically identify the expander-to-LCD pin mapping at the scanned address.
hd44780_I2Cexp display(Config::kDisplayAddress);

ElevatorSimulation simulation;
bool displayReady = false;
bool displayDirty = true;
bool i2cReady = false;
char lastDisplayRows[2][17] = {};

bool lastRawButton = HIGH;
bool stableButton = HIGH;
uint32_t rawButtonChangedAt = 0;

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
    Network::diagnostics();
  }
}

void logSimulation() {
  char top[17], bottom[17];
  simulation.screen(top, bottom);
  Serial.printf("LCD: [%s] [%s]\n", top, bottom);
}

bool onVerificationPressed(uint32_t now, const char* source = "button", int hallFloor = 0) {
  if (!simulation.request(now, hallFloor)) {
    Serial.println("Trip already active; request ignored.");
    return false;
  }

  displayDirty = true;
  logSimulation();
  Network::publishState(simulation);
  Network::event("assigned", source);
  return true;
}

bool onFloorSelected(uint32_t now, int floor, const char* source = "dashboard") {
  if (!simulation.selectFloor(now, floor)) {
    Serial.printf("Floor %d rejected.\n", floor);
    return false;
  }
  displayDirty = true;
  logSimulation();
  Network::publishState(simulation);
  Network::event("selected", source);
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
  Network::publishState(simulation);
  Network::event("boarded", source);
  return true;
}

// Passenger tells the system which floor they stand on (idle only).
bool onHereSet(int floor) {
  if (!simulation.setHall(floor)) {
    return false;
  }
  displayDirty = true;
  logSimulation();
  Network::publishState(simulation);
  return true;
}

// The one physical button means different things as the journey progresses:
// idle/arrived = call a lift from the chosen floor, doors open = walk in.
void onButtonPressed(uint32_t now) {
  const auto phase = simulation.phase();
  if (phase == ElevatorSimulation::Phase::Boarding && !simulation.boarded()) {
    onEnterCar(now, "button");
  } else {
    onVerificationPressed(now);
  }
}

// ---- 4x4 keypad (floor entry inside the lift) -------------------------------
// Digits build a floor number, '*' clears it, '#' confirms.
const char kKeymap[4][4] = {{'1', '2', '3', 'A'}, {'4', '5', '6', 'B'},
                            {'7', '8', '9', 'C'}, {'*', '0', '#', 'D'}};
char lastRawKey = 0;
char stableKey = 0;
uint32_t rawKeyChangedAt = 0;
int keypadEntry = 0;

char scanKeypad() {
  char found = 0;
  for (uint8_t r = 0; r < 4; ++r) {
    for (uint8_t i = 0; i < 4; ++i) {
      digitalWrite(Config::kKeypadRows[i], i == r ? LOW : HIGH);
    }
    delayMicroseconds(10);
    for (uint8_t c = 0; c < 4; ++c) {
      if (digitalRead(Config::kKeypadCols[c]) == LOW) {
        found = kKeymap[r][c];
      }
    }
  }
  for (uint8_t i = 0; i < 4; ++i) {
    digitalWrite(Config::kKeypadRows[i], HIGH);
  }
  return found;
}

void onKeyPressed(uint32_t now, char key) {
  const bool typing = simulation.phase() == ElevatorSimulation::Phase::Boarding &&
                      simulation.boarded() && simulation.destination() == 0;
  if (!typing) {
    Serial.printf("Key '%c' ignored (walk in first, then key a floor).\n", key);
    return;
  }
  keypadEntry = simulation.entry();  // Stay in sync (the sim clears it per journey).
  if (key >= '0' && key <= '9') {
    const int next = keypadEntry * 10 + (key - '0');
    keypadEntry = next > Config::kMaxFloor ? (key - '0') : next;  // Restart on overflow.
  } else if (key == '*') {
    keypadEntry = 0;
  } else if (key == '#') {
    const int floor = keypadEntry;
    keypadEntry = 0;
    simulation.setEntry(0);
    if (!onFloorSelected(now, floor, "keypad")) {
      displayDirty = true;  // Rejected: show the prompt again.
    }
    return;
  } else {
    return;
  }
  simulation.setEntry(keypadEntry);
  displayDirty = true;
  logSimulation();
  Network::publishState(simulation);
}

void updateKeypad(uint32_t now) {
  const char raw = scanKeypad();
  if (raw != lastRawKey) {
    lastRawKey = raw;
    rawKeyChangedAt = now;
  }
  if (raw != stableKey &&
      static_cast<uint32_t>(now - rawKeyChangedAt) >= Config::kDebounceMs) {
    stableKey = raw;
    if (stableKey != 0) {
      onKeyPressed(now, stableKey);
    }
  }
}

void updateButton(uint32_t now) {
  const bool rawButton = digitalRead(Config::kVerifyButton);
  if (rawButton != lastRawButton) {
    lastRawButton = rawButton;
    rawButtonChangedAt = now;
  }

  // Unsigned subtraction also works across millis() wraparound.
  if (rawButton != stableButton &&
      static_cast<uint32_t>(now - rawButtonChangedAt) >= Config::kDebounceMs) {
    stableButton = rawButton;
    if (stableButton == LOW) {
      onButtonPressed(now);
    }
  }
}

void updateTrip(uint32_t now) {
  const auto previous = simulation.phase();
  if (simulation.tick(now)) {
    displayDirty = true;
    logSimulation();
    Network::publishState(simulation);
    if (simulation.phase() != previous) {
      const char* names[] = {"ready", "coming", "pickup", "riding", "arrival"};
      Network::event(names[static_cast<int>(simulation.phase())], "simulation");
    }
  }
}

void updateStatusLed(uint32_t now) {
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
  pinMode(Config::kVerifyButton, INPUT_PULLUP);
  for (uint8_t i = 0; i < 4; ++i) {
    pinMode(Config::kKeypadRows[i], OUTPUT);
    digitalWrite(Config::kKeypadRows[i], HIGH);
    pinMode(Config::kKeypadCols[i], INPUT_PULLUP);
  }

  delay(250);  // Allow the LCD supply to settle once at startup.
  initializeDisplay();
  if (!displayReady) {
    scanI2c();
  }
  Serial.println("Diagnostics: 'i' I2C scan; 'o' retry LCD; 't' LCD memory test; 'n' network.");

  // A button held during boot must be released before it can start a trip.
  lastRawButton = digitalRead(Config::kVerifyButton);
  stableButton = lastRawButton;
  rawButtonChangedAt = millis();
  Serial.println("Four elevators ready. Set your floor on the dashboard; button = call lift, then button = walk in, keypad = floor + #.");
  simulation.seed(esp_random());  // Different background traffic every boot.
  Network::begin();
  Network::publishState(simulation);
  logSimulation();
  renderDisplay();
}

void loop() {
  const uint32_t now = millis();
  updateButton(now);
  updateKeypad(now);
  Network::Request request;
  if (Network::receive(request)) {
    const char* result = Network::validate(request, now);
    if (!result) {
      if (request.action == Network::Request::Action::Select) {
        result = onFloorSelected(now, request.floor) ? "accepted" : "busy";
      } else if (request.action == Network::Request::Action::Enter) {
        result = onEnterCar(now, "dashboard") ? "accepted" : "busy";
      } else if (request.action == Network::Request::Action::Here) {
        result = onHereSet(request.floor) ? "accepted" : "busy";
      } else {
        result = onVerificationPressed(now, "dashboard", request.floor) ? "accepted" : "busy";
      }
    }
    Network::reply(request, result);
  }
  updateTrip(now);
  updateStatusLed(now);
  renderDisplay();
  updateSerialDiagnostics();
  static uint32_t lastPublished = 0;
  if (static_cast<uint32_t>(now-lastPublished) >= 1000) {
    Network::publishState(simulation);
    lastPublished = now;
  }
  delay(1);  // Yield to ESP32 background tasks, not a trip/debounce timer.
}
