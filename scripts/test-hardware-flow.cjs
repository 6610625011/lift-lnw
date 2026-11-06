// End-to-end check of the flow you described, over real MQTT through Node-RED:
//   web says "I am on floor N"  ->  physical button calls a lift (state goes 'coming', web shows it)
//   -> car opens at floor N -> physical button = walk into the lift -> keypad "<floor>#" chooses the floor
//   -> board publishes the choice (web sees destination) -> lift carries the passenger there.
// Uses the dashboard's own request path for "here" (Node-RED request-function semantics, sent as the page does)
// and scripts/sim-hardware.cjs-style messages for the button/keypad, so it needs the FAKE ESP32 running.
// Usage: node scripts/test-hardware-flow.cjs [myFloor=9] [destination=16]
const fs = require('fs'), path = require('path');
const mqtt = require(path.resolve(__dirname, '../windows/node-red/node_modules/mqtt'));
const secrets = JSON.parse(fs.readFileSync(path.resolve(__dirname, '../windows/local-secrets.json')));
const mine = Number(process.argv[2] || 9), dest = Number(process.argv[3] || 16);
const T = 'elevator/v1/mockup/';
const client = mqtt.connect('mqtt://' + (process.env.MQTT_HOST || '127.0.0.1') + ':1883', { username: secrets.mqttUser, password: secrets.mqttPassword });
const log = (m) => console.log(new Date().toLocaleTimeString() + '  ' + m);
const fail = (m) => { console.error('FAIL: ' + m); client.end(true); process.exit(1); };
let state = null, step = 'here';
const sendRequest = (body) => client.publish(T + 'request', JSON.stringify({ request_id: Date.now().toString(36) + Math.random().toString(36).slice(2, 8), boot_id: state.boot_id, issued_uptime_ms: state.uptime_ms, ...body }), { qos: 1 });
const hw = (body) => client.publish(T + 'hw', JSON.stringify(body), { qos: 1 });
const expect = (cond, msg) => { if (!cond) fail(msg + ' | state ' + JSON.stringify(state)); };

client.on('connect', () => client.subscribe([T + 'state', T + 'events'], { qos: 1 }));
client.on('error', (e) => fail('MQTT error ' + e.message));
client.on('message', (topic, buf, packet) => {
  if (packet.retain || topic !== T + 'state') return;
  state = JSON.parse(buf.toString());
  if (!state.emulator) fail('this test drives the fake ESP32 (needs the hw topic), not a real board');
  const lcd = state.lcd.join(' / ');
  if (step === 'here' && ['idle', 'arrived'].includes(state.phase)) {
    log(`web: "I am on floor ${mine}" -> ESP32`); sendRequest({ action: 'here', floor: mine }); step = 'here-ack';
  } else if (step === 'here-ack' && state.hall_floor === mine && state.phase === 'idle') {
    log(`ESP32 now knows I stand on floor ${state.hall_floor}. LCD: ${lcd}`);
    log('press the physical button (call lift)'); hw({ button: true }); step = 'coming';
  } else if (step === 'coming' && (state.phase === 'coming' || state.phase === 'boarding')) {
    expect(state.hall_floor === mine, 'hall floor must be the floor I told the web page');
    log(`lift ${state.assigned} assigned, phase ${state.phase}, ETA ${JSON.stringify(state.eta_s)}. LCD: ${lcd}`);
    step = 'boarding';
    if (state.phase === 'boarding') step = 'opened';
  } else if ((step === 'boarding' || step === 'opened') && state.phase === 'boarding') {
    if (step === 'boarding') log(`lift ${state.assigned} arrived and opened. LCD: ${lcd}`);
    expect(!state.boarded, 'must wait outside until I walk in');
    log('keypad before walking in must be ignored'); hw({ keys: '5#' });
    log('press the physical button again (walk into the lift)'); hw({ button: true }); step = 'inside';
  } else if (step === 'inside' && state.phase === 'boarding' && state.boarded) {
    expect(state.destination === 0, 'no floor may be chosen yet');
    log(`inside lift ${state.assigned}. LCD: ${lcd}. Keypad: ${dest}#`);
    hw({ keys: String(dest) + '#' }); step = 'riding';
  } else if (step === 'riding' && state.phase === 'riding') {
    expect(state.destination === dest, 'board must report the floor I keyed');
    log(`ESP32 reports destination ${state.destination}; lift ${state.assigned} is at ${state.floors[state.assigned]}. LCD: ${lcd}`); step = 'arrived';
  } else if (step === 'arrived' && state.phase === 'arrived') {
    expect(state.floors[state.assigned] === dest, 'arrived at the wrong floor');
    log(`PASS: lift ${state.assigned} took me from floor ${mine} to floor ${dest}. LCD: ${lcd}`);
    client.end(); process.exit(0);
  }
});
setTimeout(() => fail('timeout in step ' + step), 240000);
