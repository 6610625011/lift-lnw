// End-to-end check against the REAL ESP32 over MQTT (no browser, no simulation):
// hall call -> board assigns a car -> choose a floor -> board rides -> arrived.
// Usage: MQTT_HOST=127.0.0.1 node scripts/test-live-select.cjs [floor]
// Run it on the Raspberry Pi (broker on localhost) or on Windows. Board must be
// online and idle/arrived. Takes up to ~1-2 minutes depending on car position.
const fs = require('fs'), path = require('path');
const mqtt = require(path.resolve(__dirname, '../windows/node-red/node_modules/mqtt'));
const secrets = JSON.parse(fs.readFileSync(path.resolve(__dirname, '../windows/local-secrets.json')));
const floor = Number(process.argv[2] || 16);
const T = 'elevator/v1/mockup/';
const client = mqtt.connect('mqtt://' + (process.env.MQTT_HOST || '127.0.0.1') + ':1883', { username: secrets.mqttUser, password: secrets.mqttPassword });
let state = null, liveAt = 0, step = 'wait-live', answers = [];
const fail = (m) => { console.error('FAIL: ' + m); client.end(true); process.exit(1); };
const send = (body) => client.publish(T + 'request', JSON.stringify({ request_id: Date.now().toString(36) + Math.random().toString(36).slice(2, 8), boot_id: state.boot_id, issued_uptime_ms: state.uptime_ms, ...body }), { qos: 1 });
const log = (m) => console.log(new Date().toLocaleTimeString() + '  ' + m);
client.on('connect', () => client.subscribe(T + '+', { qos: 1 }));
client.on('error', (e) => fail('MQTT error ' + e.message));
client.on('message', (topic, buf, packet) => {
  if (topic !== T + 'state' && topic !== T + 'events') return;
  let data; try { data = JSON.parse(buf.toString()); } catch { return; }
  if (topic === T + 'events' && !packet.retain) { answers.push(data.type); log('event: ' + data.type); return; }
  if (topic !== T + 'state' || packet.retain) return;
  state = data; liveAt = Date.now();
  const car = state.assigned !== '-' ? state.assigned : null;
  if (step === 'wait-live' && ['idle', 'arrived'].includes(state.phase)) {
    log('board live, floors ' + JSON.stringify(state.floors) + '. Hall call at floor ' + state.hall_floor);
    send({ action: 'verify' }); step = 'wait-boarding';
  } else if (step === 'wait-boarding' && state.phase === 'boarding' && state.destination === 0) {
    log('car ' + car + ' open at floor ' + state.hall_floor + '. LCD: ' + state.lcd.join(' / ') + '. Choosing floor ' + floor);
    send({ action: 'select', floor }); step = 'wait-riding';
  } else if (step === 'wait-riding' && state.phase === 'riding') {
    log('riding to ' + state.destination + ' (car ' + car + ' at ' + state.floors[car] + ')'); step = 'wait-arrived';
  } else if (step === 'wait-arrived' && state.phase === 'arrived') {
    if (state.floors[car] !== floor) fail('arrived at ' + state.floors[car] + ', expected ' + floor);
    log('PASS: car ' + car + ' delivered passenger to floor ' + floor + '. LCD: ' + state.lcd.join(' / '));
    client.end(); process.exit(0);
  }
});
setTimeout(() => fail('timeout in step ' + step + (liveAt ? '' : ' (no live state from board: check Wi-Fi/broker/credentials)')), 180000);
