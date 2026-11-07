#pragma once

#include <stdint.h>

namespace Config {
constexpr uint8_t kDisplaySda = 21;
constexpr uint8_t kDisplayScl = 22;
constexpr uint8_t kStatusLed = 25;

// Car Unit (ESP32) Physical Buttons:
constexpr uint8_t kEmergencyButton = 27;  // Emergency Stop (GPIO 27 -> GND)
constexpr uint8_t kDoorOpenButton  = 32;  // Door Open (GPIO 32 -> GND)
constexpr uint8_t kDoorCloseButton = 33;  // Door Close (GPIO 33 -> GND)
constexpr uint8_t kVerifyButton = 27;     // Legacy alias

constexpr uint8_t kDisplayColumns = 16;
constexpr uint8_t kDisplayRows = 2;
// Confirmed by the I2C scan on the connected character LCD backpack.
constexpr uint8_t kDisplayAddress = 0x27;

// The single button is a hall call from the ground floor. The passenger then
// picks the destination floor inside the car (dashboard floor pad / MQTT).
constexpr int kHomeFloor = 1;
constexpr int kMinFloor = 1;
constexpr int kMaxFloor = 20;
constexpr int kInitialFloors[4] = {5, 14, 3, 9};  // Elevators A, B, C, D.
constexpr uint32_t kDebounceMs = 35;
constexpr uint32_t kTravelPerFloorMs = 2000;
constexpr uint32_t kSelectTimeoutMs = 30000;  // Wait for a floor choice.
constexpr uint32_t kDoorOpenDwellMs = 7000;     // Doors stay open for 7 seconds.
constexpr uint32_t kDoorClosingMs   = 3000;     // Doors closing duration (3s, interruptible by Door Open).
constexpr uint32_t kDoorCloseMs     = 10000;    // Total stop time (7s open + 3s close = 10s per exam).
// Background traffic: other (imaginary) passengers keep the cars busy.
constexpr bool kAmbientEnabled = true;
constexpr uint32_t kAmbientCheckMs = 3000;     // How often a new trip may start.
constexpr int kAmbientStartPercent = 55;       // Chance per check.
constexpr int kAmbientMaxBusy = 2;             // Cars busy with others at once.
constexpr uint32_t kAmbientDwellMs = 4000;     // Stop time at each trip's end.
constexpr uint32_t kArrivalDisplayMs = 10000;
constexpr uint32_t kLedBlinkMs = 250;
// Conservative timings for the connected character LCD.
constexpr uint32_t kI2cFrequencyHz = 50000;
constexpr uint32_t kLcdClearHomeUs = 5000;
constexpr uint32_t kLcdInstructionUs = 150;

static_assert(kHomeFloor >= 1 && kHomeFloor <= 20, "Home floor must be 1-20");
static_assert(kLedBlinkMs > 0 && kTravelPerFloorMs > 0,
              "Timing intervals must be positive");
static_assert(kInitialFloors[0] >= 1 && kInitialFloors[0] <= 20 &&
                  kInitialFloors[1] >= 1 && kInitialFloors[1] <= 20 &&
                  kInitialFloors[2] >= 1 && kInitialFloors[2] <= 20 &&
                  kInitialFloors[3] >= 1 && kInitialFloors[3] <= 20,
              "Initial elevator floors must be 1-20");
}  // namespace Config
