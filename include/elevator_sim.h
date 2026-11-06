#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "config.h"

// Four simulated cars in a 20-floor building.
//
// Background traffic: while the user does nothing, cars randomly "carry other
// passengers" (go to a random floor, wait, become free again) so the building
// looks alive.
//
// The user's journey: hall call at hall_ -> the dispatcher picks the car with
// the SHORTEST ESTIMATED TIME to reach the hall (a busy car must first finish
// its current trip) -> that car arrives and opens its doors -> the passenger
// walks in (enter) -> keys a floor (setEntry, then selectFloor) -> doors close
// -> ride -> arrive.
class ElevatorSimulation {
 public:
  enum class Phase { Idle, Coming, Boarding, Riding, Arrived };
  // Background-traffic state of a car (independent of the user's journey).
  enum class Mode : uint8_t { Free, Moving, Dwelling };
  struct Car {
    char id;
    int floor;
    uint32_t assignments;
    Mode mode;
    int target;        // Where the current background trip ends (0 = none).
    uint32_t stepAt;   // Last floor step (Moving).
    uint32_t dwellAt;  // When the car stopped at its target (Dwelling).
  };

  explicit ElevatorSimulation(bool backgroundTraffic = Config::kAmbientEnabled)
      : ambient_(backgroundTraffic) {
    for (size_t i = 0; i < 4; ++i) {
      cars_[i] = {static_cast<char>('A' + i), Config::kInitialFloors[i], 0,
                  Mode::Free, 0, 0, 0};
      etaSeconds_[i] = 0;
    }
  }

  void seed(uint32_t value) { rng_ = value ? value : 0x9E3779B9u; }
  void setBackgroundTraffic(bool on) { ambient_ = on; }

  // Hall call from the floor where the passenger stands (0 = reuse the last
  // chosen hall floor, which starts at Config::kHomeFloor).
  bool request(uint32_t now, int hallFloor = 0) {
    if (phase_ != Phase::Idle && phase_ != Phase::Arrived) {
      return false;
    }
    if (hallFloor != 0) {
      if (hallFloor < Config::kMinFloor || hallFloor > Config::kMaxFloor) {
        return false;
      }
      hall_ = hallFloor;
    }

    size_t best = 0;
    uint32_t bestEta = 0;
    for (size_t i = 0; i < 4; ++i) {
      const uint32_t eta = etaMs(i, hall_, now);
      etaSeconds_[i] = static_cast<int>((eta + 500) / 1000);
      if (i == 0 || eta < bestEta ||
          (eta == bestEta && cars_[i].assignments < cars_[best].assignments)) {
        best = i;
        bestEta = eta;
      }
    }
    selected_ = static_cast<int>(best);
    destination_ = 0;
    ++cars_[best].assignments;
    go(cars_[best].mode == Mode::Free && cars_[best].floor == hall_
              ? Phase::Boarding
              : Phase::Coming,
          now);
    return true;
  }

  // Where the passenger stands, set before calling (web page or keypad).
  bool setHall(int floor) {
    if ((phase_ != Phase::Idle && phase_ != Phase::Arrived) ||
        floor < Config::kMinFloor || floor > Config::kMaxFloor) {
      return false;
    }
    hall_ = floor;
    return true;
  }

  // Passenger walks into the waiting car (doors are open).
  bool enter(uint32_t now) {
    if (phase_ != Phase::Boarding || boarded_) {
      return false;
    }
    boarded_ = true;
    entry_ = 0;
    phaseStartedAt_ = now;  // The floor-choice timeout starts now.
    return true;
  }

  // Digits typed so far on the keypad (0 = nothing). Not yet confirmed.
  bool setEntry(int value) {
    if (phase_ != Phase::Boarding || !boarded_ || destination_ != 0 ||
        value < 0 || value > 99) {
      return false;
    }
    entry_ = value;
    return true;
  }

  // Floor chosen inside the car (keypad '#' or web pad).
  bool selectFloor(uint32_t now, int floor) {
    if (phase_ != Phase::Boarding || !boarded_ || destination_ != 0) {
      return false;
    }
    if (floor < Config::kMinFloor || floor > Config::kMaxFloor ||
        floor == cars_[selected_].floor) {
      return false;
    }
    destination_ = floor;
    selectedAt_ = now;
    return true;
  }

  // Returns true when anything visible changed (a floor, a phase, a target).
  bool tick(uint32_t now) {
    bool changed = false;

    // Background trips advance for every car except the one serving the user.
    for (size_t i = 0; i < 4; ++i) {
      const bool servingUser =
          static_cast<int>(i) == selected_ && phase_ != Phase::Coming;
      if (!servingUser) {
        changed |= stepBackground(i, now);
      }
    }
    if (ambient_) {
      changed |= maybeStartBackgroundTrip(now);
    }

    switch (phase_) {
      case Phase::Coming: {
        Car& car = cars_[selected_];
        if (car.mode != Mode::Free) {
          break;  // Still finishing another passenger's trip.
        }
        if (car.floor == hall_) {
          go(Phase::Boarding, now);
          changed = true;
        } else if (static_cast<uint32_t>(now - lastStepAt_) >=
                   Config::kTravelPerFloorMs) {
          car.floor += hall_ > car.floor ? 1 : -1;
          lastStepAt_ = now;
          changed = true;
          if (car.floor == hall_) {
            go(Phase::Boarding, now);
          }
        }
        break;
      }
      case Phase::Riding: {
        if (static_cast<uint32_t>(now - lastStepAt_) <
            Config::kTravelPerFloorMs) {
          break;
        }
        Car& car = cars_[selected_];
        car.floor += destination_ > car.floor ? 1 : -1;
        lastStepAt_ = now;
        changed = true;
        if (car.floor == destination_) {
          go(Phase::Arrived, now);
        }
        break;
      }
      case Phase::Boarding:
        if (destination_ != 0) {
          if (static_cast<uint32_t>(now - selectedAt_) >=
              Config::kDoorCloseMs) {
            go(Phase::Riding, now);
            changed = true;
          }
        } else if (static_cast<uint32_t>(now - phaseStartedAt_) >=
                   Config::kSelectTimeoutMs) {
          selected_ = -1;  // Nobody chose a floor: cancel and free the system.
          go(Phase::Idle, now);
          changed = true;
        }
        break;
      case Phase::Arrived:
        if (static_cast<uint32_t>(now - phaseStartedAt_) >=
            Config::kArrivalDisplayMs) {
          selected_ = -1;
          go(Phase::Idle, now);
          changed = true;
        }
        break;
      case Phase::Idle:
        break;
    }
    return changed;
  }

  void screen(char (&top)[17], char (&bottom)[17]) const {
    if (phase_ == Phase::Idle) {
      snprintf(top, sizeof(top), "F%02d PRESS VERIFY", hall_);
      snprintf(bottom, sizeof(bottom), "A%02d B%02d C%02d D%02d",
               cars_[0].floor, cars_[1].floor, cars_[2].floor, cars_[3].floor);
      return;
    }

    const Car& car = cars_[selected_];
    switch (phase_) {
      case Phase::Coming:
        snprintf(top, sizeof(top), "GO TO LIFT %c", car.id);
        snprintf(bottom, sizeof(bottom), "NOW:%02d HALL:%02d", car.floor,
                 hall_);
        break;
      case Phase::Boarding:
        if (!boarded_) {
          snprintf(top, sizeof(top), "LIFT %c OPEN", car.id);
          snprintf(bottom, sizeof(bottom), "PRESS TO ENTER");
        } else if (destination_ != 0) {
          snprintf(top, sizeof(top), "LIFT %c TO F%02d", car.id, destination_);
          snprintf(bottom, sizeof(bottom), "DOORS CLOSING");
        } else {
          snprintf(top, sizeof(top), "INSIDE LIFT %c", car.id);
          if (entry_ > 0) {
            snprintf(bottom, sizeof(bottom), "FLOOR:%d #=OK", entry_);
          } else {
            snprintf(bottom, sizeof(bottom), "KEY FLOOR + #");
          }
        }
        break;
      case Phase::Riding:
        snprintf(top, sizeof(top), "LIFT %c MOVING %s", car.id,
                 destination_ > car.floor ? "UP" : "DN");
        snprintf(bottom, sizeof(bottom), "NOW:%02d TO:%02d", car.floor,
                 destination_);
        break;
      case Phase::Arrived:
        snprintf(top, sizeof(top), "LIFT %c ARRIVED", car.id);
        snprintf(bottom, sizeof(bottom), "NOW:%02d TO:%02d", car.floor,
                 destination_);
        break;
      case Phase::Idle:
        break;
    }
  }

  Phase phase() const { return phase_; }
  uint32_t phaseStartedAt() const { return phaseStartedAt_; }
  const Car* selectedCar() const {
    return selected_ < 0 ? nullptr : &cars_[selected_];
  }
  const Car& car(size_t index) const { return cars_[index]; }
  // 0 until the passenger has chosen a floor.
  int destination() const { return destination_; }
  int hallFloor() const { return hall_; }
  bool boarded() const { return boarded_; }
  int entry() const { return entry_; }
  // Estimated seconds each car needed to reach the hall at the moment of the
  // call; this is what the dispatcher compared.
  int etaSeconds(size_t index) const { return etaSeconds_[index]; }

 private:
  static int distance(int a, int b) { return a > b ? a - b : b - a; }

  void go(Phase phase, uint32_t now) {
    phase_ = phase;
    if (phase == Phase::Idle) {
      destination_ = 0;
    }
    if (phase == Phase::Idle || phase == Phase::Coming ||
        phase == Phase::Boarding) {
      boarded_ = false;
      entry_ = 0;
    }
    phaseStartedAt_ = now;
    lastStepAt_ = now;
  }

  // Time (ms) until car i could be standing at `floor`, ready to take the user.
  uint32_t etaMs(size_t i, int floor, uint32_t now) const {
    const Car& c = cars_[i];
    const uint32_t step = Config::kTravelPerFloorMs;
    switch (c.mode) {
      case Mode::Free:
        return distance(c.floor, floor) * step;
      case Mode::Moving:
        return distance(c.floor, c.target) * step + Config::kAmbientDwellMs +
               distance(c.target, floor) * step;
      case Mode::Dwelling: {
        const uint32_t elapsed = static_cast<uint32_t>(now - c.dwellAt);
        const uint32_t remaining = elapsed >= Config::kAmbientDwellMs
                                       ? 0
                                       : Config::kAmbientDwellMs - elapsed;
        return remaining + distance(c.floor, floor) * step;
      }
    }
    return 0;
  }

  bool stepBackground(size_t i, uint32_t now) {
    Car& c = cars_[i];
    if (c.mode == Mode::Moving) {
      if (static_cast<uint32_t>(now - c.stepAt) < Config::kTravelPerFloorMs) {
        return false;
      }
      c.floor += c.target > c.floor ? 1 : -1;
      c.stepAt = now;
      if (c.floor == c.target) {
        c.mode = Mode::Dwelling;
        c.dwellAt = now;
      }
      return true;
    }
    if (c.mode == Mode::Dwelling &&
        static_cast<uint32_t>(now - c.dwellAt) >= Config::kAmbientDwellMs) {
      c.mode = Mode::Free;
      c.target = 0;
      return true;
    }
    return false;
  }

  uint32_t random() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
  }

  bool maybeStartBackgroundTrip(uint32_t now) {
    if (static_cast<uint32_t>(now - lastSpawnCheck_) <
        Config::kAmbientCheckMs) {
      return false;
    }
    lastSpawnCheck_ = now;

    int busy = 0;
    size_t freeCars[4];
    size_t freeCount = 0;
    for (size_t i = 0; i < 4; ++i) {
      if (static_cast<int>(i) == selected_) {
        continue;  // The user's car takes no other passengers.
      }
      if (cars_[i].mode == Mode::Free) {
        freeCars[freeCount++] = i;
      } else {
        ++busy;
      }
    }
    if (freeCount == 0 || busy >= Config::kAmbientMaxBusy ||
        random() % 100 >= static_cast<uint32_t>(Config::kAmbientStartPercent)) {
      return false;
    }
    Car& c = cars_[freeCars[random() % freeCount]];
    int target = c.floor;
    for (int attempts = 0; attempts < 8 && target == c.floor; ++attempts) {
      target = Config::kMinFloor +
               static_cast<int>(random() % static_cast<uint32_t>(
                                               Config::kMaxFloor -
                                               Config::kMinFloor + 1));
    }
    if (target == c.floor) {
      return false;
    }
    c.mode = Mode::Moving;
    c.target = target;
    c.stepAt = now;
    return true;
  }

  Car cars_[4];
  int etaSeconds_[4];
  Phase phase_ = Phase::Idle;
  int selected_ = -1;
  int destination_ = 0;
  int hall_ = Config::kHomeFloor;
  bool boarded_ = false;
  int entry_ = 0;
  uint32_t selectedAt_ = 0;
  uint32_t phaseStartedAt_ = 0;
  uint32_t lastStepAt_ = 0;
  uint32_t lastSpawnCheck_ = 0;
  uint32_t rng_ = 0x2545F491u;
  bool ambient_;
};
