// JavaScript mirror of include/elevator_sim.h (same rules, same constants).
// Used by scripts/fake-esp32.cjs and the browser preview. Time is passed in (ms), like the C++ class.
// Keep in sync with config.h / elevator_sim.h.
function createSim(options = {}) {
  const C = {
    initial: [5, 14, 3, 9], home: 1, minFloor: 1, maxFloor: 20,
    stepMs: 2000, doorOpenMs: 7000, doorClosingMs: 3000, selectTimeoutMs: 30000, arrivalMs: 10000,
    ambientCheckMs: 3000, ambientPercent: 55, ambientMaxBusy: 2, ambientDwellMs: 10000,
    ...options.config,
  };
  const rand = options.random || Math.random;
  const dist = (a, b) => Math.abs(a - b);
  const pad = (n) => String(n).padStart(2, '0');
  const sim = {
    ambient: options.ambient !== false,
    cars: C.initial.map((floor, i) => ({ id: 'ABCD'[i], floor, assignments: 0, mode: 'free', target: 0, stepAt: 0, dwellAt: 0 })),
    eta: [0, 0, 0, 0], phase: 'idle', sel: -1, dest: 0, hall: C.home, boarded: false, entry: 0, doorClosing: false, doorClosingAt: 0,
    selectedAt: 0, phaseAt: 0, lastStepAt: 0, lastSpawn: 0,
  };
  const go = (p, now) => { sim.phase = p; if (p === 'idle') sim.dest = 0; if (p === 'idle' || p === 'coming' || p === 'boarding') { sim.boarded = false; sim.entry = 0; }
    sim.doorClosing = false; sim.doorClosingAt = 0; sim.phaseAt = now; sim.lastStepAt = now; };

  function etaMs(i, floor, now) {
    const c = sim.cars[i];
    if (c.mode === 'free') return dist(c.floor, floor) * C.stepMs;
    if (c.mode === 'moving') return dist(c.floor, c.target) * C.stepMs + C.ambientDwellMs + dist(c.target, floor) * C.stepMs;
    const remaining = Math.max(0, C.ambientDwellMs - (now - c.dwellAt));
    return remaining + dist(c.floor, floor) * C.stepMs;
  }
  function request(now, hallFloor = 0, forcedCar = -1) {
    if (sim.phase !== 'idle' && sim.phase !== 'arrived') return false;
    if (hallFloor) {
      if (!Number.isInteger(hallFloor) || hallFloor < C.minFloor || hallFloor > C.maxFloor) return false;
      sim.hall = hallFloor;
    }
    let best = 0, bestEta = 0;
    for (let i = 0; i < 4; i++) {
      const eta = etaMs(i, sim.hall, now);
      sim.eta[i] = Math.round(eta / 1000);
      if (i === 0 || eta < bestEta || (eta === bestEta && sim.cars[i].assignments < sim.cars[best].assignments)) { best = i; bestEta = eta; }
    }
    if (Number.isInteger(forcedCar) && forcedCar >= 0 && forcedCar < 4) best = forcedCar;  // central's choice wins
    sim.sel = best; sim.dest = 0; sim.cars[best].assignments++;
    const car = sim.cars[best];
    go(car.mode === 'free' && car.floor === sim.hall ? 'boarding' : 'coming', now);
    return true;
  }
  function setHall(floor) {
    if ((sim.phase !== 'idle' && sim.phase !== 'arrived') || !Number.isInteger(floor) || floor < C.minFloor || floor > C.maxFloor) return false;
    sim.hall = floor; return true;
  }
  function enter(now) {
    if (sim.phase !== 'boarding' || sim.boarded) return false;
    sim.boarded = true; sim.entry = 0; sim.phaseAt = now; return true;
  }
  function setEntry(value) {
    if (sim.phase !== 'boarding' || !sim.boarded || sim.dest || !Number.isInteger(value) || value < 0 || value > 99) return false;
    sim.entry = value; return true;
  }
  function selectFloor(now, floor) {
    if (sim.phase !== 'boarding' || !sim.boarded || sim.dest) return false;
    if (!Number.isInteger(floor) || floor < C.minFloor || floor > C.maxFloor || floor === sim.cars[sim.sel].floor) return false;
    sim.dest = floor; sim.selectedAt = now; return true;
  }
  function stepBackground(c, now) {
    if (c.mode === 'moving') {
      if (now - c.stepAt < C.stepMs) return false;
      c.floor += c.target > c.floor ? 1 : -1; c.stepAt = now;
      if (c.floor === c.target) { c.mode = 'dwelling'; c.dwellAt = now; }
      return true;
    }
    if (c.mode === 'dwelling' && now - c.dwellAt >= C.ambientDwellMs) { c.mode = 'free'; c.target = 0; return true; }
    return false;
  }
  function maybeStartBackgroundTrip(now) {
    if (now - sim.lastSpawn < C.ambientCheckMs) return false;
    sim.lastSpawn = now;
    let busy = 0; const free = [];
    sim.cars.forEach((c, i) => { if (i === sim.sel) return; if (c.mode === 'free') free.push(c); else busy++; });
    if (!free.length || busy >= C.ambientMaxBusy || rand() * 100 >= C.ambientPercent) return false;
    const c = free[Math.floor(rand() * free.length)];
    let target = c.floor;
    for (let a = 0; a < 8 && target === c.floor; a++) target = C.minFloor + Math.floor(rand() * (C.maxFloor - C.minFloor + 1));
    if (target === c.floor) return false;
    c.mode = 'moving'; c.target = target; c.stepAt = now; return true;
  }
  function tick(now) {
    let changed = false;
    sim.cars.forEach((c, i) => { if (!(i === sim.sel && sim.phase !== 'coming')) changed = stepBackground(c, now) || changed; });
    if (sim.ambient) changed = maybeStartBackgroundTrip(now) || changed;
    const car = sim.sel >= 0 ? sim.cars[sim.sel] : null;
    if (sim.phase === 'coming' && car.mode === 'free') {
      if (car.floor === sim.hall) { go('boarding', now); changed = true; }
      else if (now - sim.lastStepAt >= C.stepMs) {
        car.floor += sim.hall > car.floor ? 1 : -1; sim.lastStepAt = now; changed = true;
        if (car.floor === sim.hall) go('boarding', now);
      }
    } else if (sim.phase === 'riding' && now - sim.lastStepAt >= C.stepMs) {
      car.floor += sim.dest > car.floor ? 1 : -1; sim.lastStepAt = now; changed = true;
      if (car.floor === sim.dest) go('arrived', now);
    } else if (sim.phase === 'boarding') {
      if (sim.dest) {
        if (sim.doorClosing) { if (now - sim.doorClosingAt >= C.doorClosingMs) { go('riding', now); changed = true; } }
        else if (now - sim.selectedAt >= C.doorOpenMs) { sim.doorClosing = true; sim.doorClosingAt = now; changed = true; }
      }
      else if (now - sim.phaseAt >= C.selectTimeoutMs) { sim.sel = -1; go('idle', now); changed = true; }
    } else if (sim.phase === 'arrived' && now - sim.phaseAt >= C.arrivalMs) { sim.sel = -1; go('idle', now); changed = true; }
    return changed;
  }
  function lcd() {
    const f = sim.cars.map((c) => c.floor), car = sim.cars[sim.sel], c = car && car.id, n = car && car.floor;
    switch (sim.phase) {
      case 'idle': return [`F${pad(sim.hall)} PRESS VERIFY`, `A${pad(f[0])} B${pad(f[1])} C${pad(f[2])} D${pad(f[3])}`];
      case 'coming': return [`GO TO LIFT ${c}`, `NOW:${pad(n)} HALL:${pad(sim.hall)}`];
      case 'boarding':
        if (!sim.boarded) return [`LIFT ${c} OPEN`, 'PRESS TO ENTER'];
        if (sim.dest) return [`LIFT ${c} TO F${pad(sim.dest)}`, sim.doorClosing ? '>DOORS CLOSING<' : 'DOOR OPEN'];
        return [`INSIDE LIFT ${c}`, sim.entry > 0 ? `FLOOR:${sim.entry} #=OK` : 'KEY FLOOR + #'];
      case 'riding': return [`LIFT ${c} MOVING ${sim.dest > n ? 'UP' : 'DN'}`, `NOW:${pad(n)} TO:${pad(sim.dest)}`];
      default: return [`LIFT ${c} ARRIVED`, `NOW:${pad(n)} TO:${pad(sim.dest)}`];
    }
  }
  // Same fields as the firmware's MQTT "state" message (without boot_id/uptime_ms).
  function stateBody() {
    const floors = {}, targets = {}, eta_s = {};
    sim.cars.forEach((c, i) => { floors[c.id] = c.floor; targets[c.id] = c.mode === 'free' ? 0 : c.target; if (sim.phase !== 'idle') eta_s[c.id] = sim.eta[i]; });
    const body = { phase: sim.phase, assigned: sim.sel < 0 ? '-' : sim.cars[sim.sel].id, hall_floor: sim.hall, destination: sim.dest, boarded: sim.boarded, entry: sim.entry, floors, targets, lcd: lcd() };
    if (sim.phase !== 'idle') body.eta_s = eta_s;
    return body;
  }
  return { sim, request, setHall, enter, setEntry, selectFloor, tick, lcd, stateBody };
}
if (typeof module !== 'undefined') module.exports = { createSim };
