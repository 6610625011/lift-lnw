// Software stand-in for the ESP32 firmware (src/main.cpp + include/elevator_sim.h + src/network.cpp).
// Speaks the SAME MQTT protocol: retained state each second + on change, events, availability LWT,
// request validation (boot_id, 5 s expiry, 32-entry dedupe), actions verify/call(+floor)/select(+floor).
// For testing Node-RED + broker + dashboard on a PC without the real board.
// Env: MQTT_HOST (127.0.0.1)   SPEED (1 = real 2 s/floor, 4 = four times faster)
const fs = require('fs'), path = require('path'), crypto = require('crypto');
const mqtt = require(path.resolve(__dirname, '../windows/node-red/node_modules/mqtt'));
const secrets = JSON.parse(fs.readFileSync(path.resolve(__dirname, '../windows/local-secrets.json')));
const SPEED = Number(process.env.SPEED || 1);
const T = 'elevator/v1/mockup/';
const { createSim } = require('./lib/sim-core.cjs');
const boot = crypto.randomBytes(8).toString('hex');
const t0 = Date.now(), uptime = () => Date.now() - t0;
const S = createSim({ config: {
  stepMs: 2000 / SPEED, doorMs: 10000 / SPEED, selectTimeoutMs: 30000 / SPEED, arrivalMs: 10000 / SPEED,
  ambientCheckMs: 3000 / SPEED, ambientDwellMs: 4000 / SPEED,
} });
const sim = S.sim, lcd = S.lcd;
const request = (hall) => S.request(Date.now(), hall);
const selectFloor = (f) => S.selectFloor(Date.now(), f);
const tick = (() => {
  let prev = '';
  return () => {
    S.tick(Date.now());
    const sig = JSON.stringify(S.stateBody());
    const changed = sig !== prev; prev = sig; return changed;
  };
})();

const client = mqtt.connect(`mqtt://${process.env.MQTT_HOST || '127.0.0.1'}:1883`, {
  clientId: 'elevator-' + boot, username: secrets.mqttUser, password: secrets.mqttPassword, keepalive: 10, reconnectPeriod: 5000,
  will: { topic: T + 'availability', payload: 'offline', qos: 1, retain: true },
});
const publishState = () => client.publish(T + 'state', JSON.stringify({
  boot_id: boot, uptime_ms: uptime(), ...S.stateBody(), emulator: true,
}), { retain: true });
const event = (type, source, id = '') => client.publish(T + 'events', JSON.stringify({ type, source, request_id: id, boot_id: boot, uptime_ms: uptime() }));
const names = { coming: 'coming', boarding: 'pickup', riding: 'riding', arrived: 'arrival', idle: 'ready' };
const previous = []; // bounded 32-entry dedupe, like the firmware

client.on('connect', () => {
  console.log(`fake ESP32 online, boot_id ${boot}, speed x${SPEED}`);
  client.subscribe(T + 'request', { qos: 1 });
  client.subscribe(T + 'hw', { qos: 0 });
  client.publish(T + 'availability', 'online', { retain: true });
  publishState();
});
client.on('error', (e) => console.error('MQTT error:', e.message));
// What the physical button does depends on the journey phase (same as src/main.cpp).
function pressButton() {
  const label = sim.phase === 'boarding' && !sim.boarded ? 'enter' : 'call';
  const ok = label === 'enter' ? S.enter(Date.now()) : S.request(Date.now(), 0);
  if (ok) { publishState(); event(label === 'enter' ? 'boarded' : 'assigned', 'button'); }
  return ok;
}
// Keypad: digits build a floor, '*' clears, '#' confirms (same as src/main.cpp).
function pressKey(key) {
  const typing = sim.phase === 'boarding' && sim.boarded && !sim.dest;
  if (!typing) return false;
  if (/^[0-9]$/.test(key)) { const next = sim.entry * 10 + Number(key); S.setEntry(next > 20 ? Number(key) : next); }
  else if (key === '*') S.setEntry(0);
  else if (key === '#') {
    const floor = sim.entry; S.setEntry(0);
    if (S.selectFloor(Date.now(), floor)) { publishState(); event('selected', 'keypad'); return true; }
    return false;
  } else return false;
  publishState(); return true;
}

client.on('message', (topic, buf) => {
  let m; try { m = JSON.parse(buf.toString()); } catch { return; }
  if (topic === T + 'hw') {
    // Simulated hardware input (scripts/sim-hardware.cjs): {"button":true} or {"keys":"16#"}.
    if (m.button) console.log(`${new Date().toLocaleTimeString()} [hw] button -> ${pressButton() ? 'ok' : 'ignored'}   LCD: ${lcd().join(' / ')}`);
    if (typeof m.keys === 'string') for (const k of m.keys) console.log(`${new Date().toLocaleTimeString()} [hw] key ${k} -> ${pressKey(k) ? 'ok' : 'ignored'}   LCD: ${lcd().join(' / ')}`);
    return;
  }
  const act = m.action;
  const call = act === 'verify' || act === 'call', select = act === 'select', enter = act === 'enter', here = act === 'here';
  if (!(call || select || enter || here)) return;
  const req_id = m.request_id || `req-${Date.now()}`;
  const req_boot = (typeof m.boot_id === 'string' && m.boot_id.length === 16) ? m.boot_id : boot;
  const req_uptime = Number.isInteger(m.issued_uptime_ms) ? m.issued_uptime_ms : uptime();
  if ((select || here) && !(Number.isInteger(m.floor) && m.floor >= 1 && m.floor <= 20)) return;
  let result;
  const dup = previous.find((p) => p.id === req_id);
  if (req_boot !== boot) result = 'expired';
  else if (dup) result = dup.result;
  else if (uptime() - req_uptime > 10000) result = 'expired';
  else if (select) { result = selectFloor(m.floor) ? 'accepted' : 'busy'; if (result === 'accepted') { publishState(); event('selected', 'dashboard'); } }
  else if (enter) { result = S.enter(Date.now()) ? 'accepted' : 'busy'; if (result === 'accepted') { publishState(); event('boarded', 'dashboard'); } }
  else if (here) { result = S.setHall(m.floor) ? 'accepted' : 'busy'; if (result === 'accepted') publishState(); }
  else { const from = Number.isInteger(m.floor) ? m.floor : 0; result = request(from) ? 'accepted' : 'busy'; if (result === 'accepted') { publishState(); event('assigned', 'dashboard'); } }
  if (!dup) { previous.push({ id: m.request_id, result }); if (previous.length > 32) previous.shift(); }
  event(result, 'dashboard', m.request_id);
  console.log(`${new Date().toLocaleTimeString()} ${act}${m.floor ? ' ' + m.floor : ''} -> ${result}   LCD: ${lcd().join(' / ')}`);
});
let lastPub = 0, lastPhase = sim.phase;
setInterval(() => {
  const changed = tick();
  if (changed) {
    console.log(`${new Date().toLocaleTimeString()} ${sim.phase}  floors ${sim.cars.map((c) => c.id + c.floor + (c.mode === 'free' ? '' : '>' + c.target)).join(' ')}  LCD: ${lcd().join(' / ')}`);
    if (sim.phase !== lastPhase) event(names[sim.phase], 'simulation');
    lastPhase = sim.phase;
  }
  if (changed || Date.now() - lastPub >= 1000) { if (client.connected) publishState(); lastPub = Date.now(); }
}, 100);
process.on('SIGINT', () => client.end(false, () => process.exit(0)));
