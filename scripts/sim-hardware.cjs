// Presses the (simulated) physical parts of the ESP32 when you run the fake ESP32 on this PC.
//   node scripts/sim-hardware.cjs button        # the push button (GPIO27): call lift / walk into the lift
//   node scripts/sim-hardware.cjs keys 16#      # the keypad: digits, * = clear, # = confirm
// With the real board you press the real button and keypad instead; this script is only for the emulator.
const fs = require('fs'), path = require('path');
const mqtt = require(path.resolve(__dirname, '../windows/node-red/node_modules/mqtt'));
const secrets = JSON.parse(fs.readFileSync(path.resolve(__dirname, '../windows/local-secrets.json')));
const [kind, value] = process.argv.slice(2);
if (kind !== 'button' && !(kind === 'keys' && value)) { console.error('usage: sim-hardware.cjs button | keys <digits#>'); process.exit(2); }
const client = mqtt.connect('mqtt://' + (process.env.MQTT_HOST || '127.0.0.1') + ':1883', { username: secrets.mqttUser, password: secrets.mqttPassword });
client.on('connect', () => {
  client.publish('elevator/v1/mockup/hw', JSON.stringify(kind === 'button' ? { button: true } : { keys: value }), { qos: 1 }, () => { console.log('sent', kind, value || ''); client.end(); });
});
client.on('error', (e) => { console.error(e.message); process.exit(1); });
