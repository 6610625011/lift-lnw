// Localhost stand-in for Mosquitto: authenticated MQTT broker on 127.0.0.1:1883 (aedes).
// Use only for development when Mosquitto (Raspberry Pi) is not available.
// Install once:  npm install --prefix windows/local-sim
const fs = require('fs'), path = require('path'), net = require('net');
const Aedes = require(path.resolve(__dirname, '../windows/local-sim/node_modules/aedes'));
const secrets = JSON.parse(fs.readFileSync(path.resolve(__dirname, '../windows/local-secrets.json')));
const broker = new Aedes();
broker.authenticate = (client, user, pass, cb) => {
  const ok = user === secrets.mqttUser && pass && pass.toString() === secrets.mqttPassword;
  cb(ok ? null : Object.assign(new Error('bad credentials'), { returnCode: 4 }), ok);
};
const allowed = (topic) => topic.startsWith('elevator/v1/');
broker.authorizePublish = (client, packet, cb) => cb(allowed(packet.topic) ? null : new Error('topic not allowed'));
broker.authorizeSubscribe = (client, sub, cb) => cb(null, allowed(sub.topic) ? sub : null);
broker.on('client', (c) => console.log('client connected:', c.id));
broker.on('clientDisconnect', (c) => console.log('client disconnected:', c.id));
net.createServer(broker.handle).listen(1883, process.env.MQTT_BIND || '0.0.0.0', () => console.log('dev MQTT broker on 1883 (0.0.0.0)'));
