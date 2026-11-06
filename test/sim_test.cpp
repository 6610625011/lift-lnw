#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "elevator_sim.h"

using Phase = ElevatorSimulation::Phase;

void expectScreen(const ElevatorSimulation& sim, const char* top,
                  const char* bottom) {
  char actualTop[17], actualBottom[17];
  sim.screen(actualTop, actualBottom);
  if (strcmp(actualTop, top) != 0 || strcmp(actualBottom, bottom) != 0) {
    fprintf(stderr, "Expected [%s] [%s], got [%s] [%s]\n", top, bottom,
            actualTop, actualBottom);
  }
  assert(strcmp(actualTop, top) == 0);
  assert(strcmp(actualBottom, bottom) == 0);
  assert(strlen(actualTop) <= 16 && strlen(actualBottom) <= 16);
}

// Walk in (if not yet inside) and key a floor, like the keypad's '#'.
bool pick(ElevatorSimulation& sim, uint32_t now, int floor) {
  sim.enter(now);
  return sim.selectFloor(now, floor);
}

void runUntil(ElevatorSimulation& sim, uint32_t& now, Phase phase) {
  unsigned int steps = 0;
  while (sim.phase() != phase && steps++ < 4000) {
    now += 100;
    sim.tick(now);
  }
  assert(sim.phase() == phase);
}

// Full journey: hall call, wait for the car, choose a floor, ride, arrive.
void fullTrip(ElevatorSimulation& sim, uint32_t& now, int floor) {
  assert(sim.request(now));
  runUntil(sim, now, Phase::Boarding);
  assert(sim.selectedCar()->floor == Config::kHomeFloor);
  assert(pick(sim, now, floor));
  runUntil(sim, now, Phase::Arrived);
  assert(sim.selectedCar()->floor == floor);
}

int main() {
  {
    ElevatorSimulation sim(false);
    expectScreen(sim, "F01 PRESS VERIFY", "A05 B14 C03 D09");
    assert(sim.selectedCar() == nullptr);
    assert(!pick(sim, 0, 10));  // No car waiting yet.
    assert(sim.request(0));
    assert(sim.selectedCar()->id == 'C');
    expectScreen(sim, "GO TO LIFT C", "NOW:03 HALL:01");
    assert(!sim.request(1));
    assert(!pick(sim, 1, 10));  // Car not at the hall yet.
    assert(!sim.tick(Config::kTravelPerFloorMs - 1));
    assert(sim.selectedCar()->floor == 3);
    assert(sim.tick(Config::kTravelPerFloorMs));
    assert(sim.selectedCar()->floor == 2);
    assert(sim.tick(2 * Config::kTravelPerFloorMs));
    assert(sim.phase() == Phase::Boarding);
    expectScreen(sim, "LIFT C OPEN", "PRESS TO ENTER");
    uint32_t now = 2 * Config::kTravelPerFloorMs;

    // Invalid choices are rejected and keep waiting.
    assert(!pick(sim, now, 0));
    assert(!pick(sim, now, 21));
    assert(!pick(sim, now, Config::kHomeFloor));
    assert(sim.phase() == Phase::Boarding && sim.destination() == 0);

    assert(pick(sim, now, 16));
    assert(!pick(sim, now, 9));  // One choice per journey.
    assert(sim.destination() == 16);
    expectScreen(sim, "LIFT C TO F16", "DOORS CLOSING");
    assert(!sim.tick(now + Config::kDoorCloseMs - 1));
    assert(sim.tick(now + Config::kDoorCloseMs));
    now += Config::kDoorCloseMs;
    assert(sim.phase() == Phase::Riding);
    expectScreen(sim, "LIFT C MOVING UP", "NOW:01 TO:16");
    runUntil(sim, now, Phase::Arrived);
    expectScreen(sim, "LIFT C ARRIVED", "NOW:16 TO:16");
    assert(sim.car(0).floor == 5 && sim.car(1).floor == 14 &&
           sim.car(3).floor == 9);
    assert(!sim.tick(now + Config::kArrivalDisplayMs - 1));
    assert(sim.tick(now + Config::kArrivalDisplayMs));
    expectScreen(sim, "F01 PRESS VERIFY", "A05 B14 C16 D09");
    assert(sim.selectedCar() == nullptr && sim.destination() == 0);
  }

  {
    // Going down: destination below the hall is impossible from floor 1, but a
    // second trip starting from a high floor must ride down after pickup.
    ElevatorSimulation sim(false);
    uint32_t now = 0;
    fullTrip(sim, now, 12);  // C -> 12.
    assert(sim.request(now));
    // A(5) and D(9) vs C(12): nearest to floor 1 is A.
    assert(sim.selectedCar()->id == 'A');
    runUntil(sim, now, Phase::Boarding);
    assert(pick(sim, now, 3));
    runUntil(sim, now, Phase::Riding);
    expectScreen(sim, "LIFT A MOVING UP", "NOW:01 TO:03");
  }

  {
    // Nobody chooses a floor: journey is cancelled and the system frees up.
    ElevatorSimulation sim(false);
    uint32_t now = 0;
    assert(sim.request(now));
    runUntil(sim, now, Phase::Boarding);
    const uint32_t boardedAt = now;
    assert(!sim.tick(boardedAt + Config::kSelectTimeoutMs - 1));
    assert(sim.tick(boardedAt + Config::kSelectTimeoutMs));
    assert(sim.phase() == Phase::Idle && sim.selectedCar() == nullptr);
    assert(sim.request(boardedAt + Config::kSelectTimeoutMs));
  }

  {
    // Repeated trips use actual positions; every car eventually works.
    ElevatorSimulation sim(false);
    uint32_t now = 0;
    const char expectedCars[] = {'C', 'A', 'D', 'B'};
    const int floors[] = {20, 20, 20, 8};
    for (size_t i = 0; i < 4; ++i) {
      assert(sim.request(now));
      assert(sim.selectedCar()->id == expectedCars[i]);
      runUntil(sim, now, Phase::Boarding);
      assert(pick(sim, now, floors[i]));
      runUntil(sim, now, Phase::Arrived);
    }
  }

  {
    // A car parked just above the hall serves the next call again.
    ElevatorSimulation sim(false);
    uint32_t now = 0;
    fullTrip(sim, now, 2);  // C ends at floor 2, still nearest to floor 1.
    assert(sim.request(now));
    assert(sim.selectedCar()->id == 'C');
    runUntil(sim, now, Phase::Boarding);
    assert(pick(sim, now, 7));
    runUntil(sim, now, Phase::Arrived);
    assert(sim.car(2).floor == 7);
  }

  {
    // Passenger stands on another floor: car comes there, can ride down too.
    ElevatorSimulation sim(false);
    uint32_t now = 0;
    assert(!sim.request(now, 0 + 21));  // Out of range: rejected, unchanged.
    assert(sim.phase() == Phase::Idle && sim.hallFloor() == 1);
    assert(sim.request(now, 10));
    assert(sim.hallFloor() == 10 && sim.selectedCar()->id == 'D');
    expectScreen(sim, "GO TO LIFT D", "NOW:09 HALL:10");
    runUntil(sim, now, Phase::Boarding);
    assert(sim.selectedCar()->floor == 10);
    assert(!pick(sim, now, 10));  // Not the floor we stand on.
    assert(pick(sim, now, 2));
    runUntil(sim, now, Phase::Riding);
    expectScreen(sim, "LIFT D MOVING DN", "NOW:10 TO:02");
    runUntil(sim, now, Phase::Arrived);
    assert(sim.selectedCar()->floor == 2);
    now += Config::kArrivalDisplayMs;
    sim.tick(now);
    expectScreen(sim, "F10 PRESS VERIFY", "A05 B14 C03 D02");
    assert(sim.request(now));  // Button press reuses the last hall floor.
    assert(sim.hallFloor() == 10);
  }

  {
    // A car already standing on the hall floor opens immediately.
    ElevatorSimulation sim(false);
    assert(sim.request(0, 3));  // C is parked at floor 3.
    assert(sim.selectedCar()->id == 'C' && sim.phase() == Phase::Boarding);
    expectScreen(sim, "LIFT C OPEN", "PRESS TO ENTER");
  }

  {
    // Walk-in step, keypad entry and "I am on floor N" before calling.
    ElevatorSimulation sim(false);
    uint32_t now = 0;
    assert(!sim.enter(now));            // Nothing to enter yet.
    assert(sim.setHall(7) && sim.hallFloor() == 7);
    assert(!sim.setHall(0) && !sim.setHall(21));
    expectScreen(sim, "F07 PRESS VERIFY", "A05 B14 C03 D09");
    assert(sim.request(now));           // Uses the floor set above.
    assert(!sim.setHall(9));            // Not while a journey is active.
    runUntil(sim, now, Phase::Boarding);
    expectScreen(sim, "LIFT A OPEN", "PRESS TO ENTER");
    assert(!sim.boarded());
    assert(!sim.selectFloor(now, 12));  // Must walk in first.
    assert(!sim.setEntry(1));           // Keypad not usable before walking in.
    now += 5000;
    assert(sim.enter(now) && sim.boarded() && !sim.enter(now));
    expectScreen(sim, "INSIDE LIFT A", "KEY FLOOR + #");
    assert(sim.setEntry(1));
    expectScreen(sim, "INSIDE LIFT A", "FLOOR:1 #=OK");
    assert(sim.setEntry(12));
    expectScreen(sim, "INSIDE LIFT A", "FLOOR:12 #=OK");
    assert(!sim.setEntry(100));
    // The floor-choice timeout counts from the moment the passenger walked in.
    assert(!sim.tick(now + Config::kSelectTimeoutMs - 1));
    assert(sim.selectFloor(now + 1000, 12));
    expectScreen(sim, "LIFT A TO F12", "DOORS CLOSING");
    assert(!sim.setEntry(3));           // Locked after choosing.
    runUntil(sim, now, Phase::Arrived);
    assert(sim.selectedCar()->floor == 12);
  }

  {
    // Passenger never walks in: journey is cancelled after the timeout.
    ElevatorSimulation sim(false);
    uint32_t now = 0;
    assert(sim.request(now));
    runUntil(sim, now, Phase::Boarding);
    const uint32_t openedAt = now;
    assert(sim.tick(openedAt + Config::kSelectTimeoutMs));
    assert(sim.phase() == Phase::Idle && !sim.boarded());
  }

  {
    // Background traffic: cars move on their own, never more than the limit
    // at once, and always stay inside the building.
    ElevatorSimulation sim(true);
    sim.seed(12345);
    uint32_t now = 0;
    bool moved = false;
    for (int i = 0; i < 1200; ++i) {  // 2 minutes
      now += 100;
      sim.tick(now);
      int busy = 0;
      for (size_t c = 0; c < 4; ++c) {
        assert(sim.car(c).floor >= 1 && sim.car(c).floor <= 20);
        if (sim.car(c).mode != ElevatorSimulation::Mode::Free) ++busy;
        if (sim.car(c).floor != Config::kInitialFloors[c]) moved = true;
      }
      assert(busy <= Config::kAmbientMaxBusy);
    }
    assert(moved);
  }

  {
    // Dispatcher: chosen car has the smallest estimated time to the hall, and a
    // car busy with another passenger finishes that trip before coming.
    unsigned int busyChoices = 0;
    for (uint32_t seed = 1; seed <= 40; ++seed) {
      ElevatorSimulation sim(true);
      sim.seed(seed * 7919u);
      uint32_t now = 0;
      for (int i = 0; i < 200 + static_cast<int>(seed) * 13; ++i) {
        now += 100;
        sim.tick(now);
      }
      const int hall = 1 + static_cast<int>(seed % 20);
      assert(sim.request(now, hall));
      const auto* chosen = sim.selectedCar();
      int minEta = 1 << 30;
      for (size_t c = 0; c < 4; ++c) {
        if (sim.etaSeconds(c) < minEta) minEta = sim.etaSeconds(c);
      }
      assert(sim.etaSeconds(chosen->id - 'A') == minEta);
      const bool wasBusy = chosen->mode != ElevatorSimulation::Mode::Free;
      if (wasBusy) {
        ++busyChoices;
        assert(sim.phase() == Phase::Coming);
        // Does not reach the passenger while still on the other trip.
        while (sim.phase() == Phase::Coming) {
          now += 100;
          sim.tick(now);
        }
      } else {
        runUntil(sim, now, Phase::Boarding);
      }
      assert(sim.phase() == Phase::Boarding);
      assert(chosen->floor == hall && chosen->mode == ElevatorSimulation::Mode::Free);
      assert(pick(sim, now, hall == 20 ? 1 : 20));
    }
    assert(busyChoices > 0);  // The busy-car path was exercised.
  }

  {
    ElevatorSimulation sim(false);
    uint32_t now = UINT32_MAX - 3000;  // millis() wraparound mid-journey.
    fullTrip(sim, now, 16);
    assert(sim.selectedCar()->floor == 16);
  }

  puts("PASS: ETA dispatch, background traffic, hall call at any floor, floor selection "
       "(valid/invalid/once), select timeout, door delay, ride, LCD text, "
       "and timer wraparound.");
}
