// Creates windows/local-secrets.json with RANDOM test credentials for a localhost run.
// Skips if the file already exists. The file is private (gitignored).
const fs = require('fs'), path = require('path'), crypto = require('crypto');
const file = path.resolve(__dirname, '../windows/local-secrets.json');
if (fs.existsSync(file)) { console.log('local-secrets.json already exists - left unchanged.'); process.exit(0); }
fs.writeFileSync(file, JSON.stringify({
  mqttHost: '127.0.0.1',
  mqttUser: 'elevator',
  mqttPassword: crypto.randomBytes(12).toString('hex'),
  credentialSecret: crypto.randomBytes(24).toString('hex'),
  networkMode: 'existing-wifi',
}, null, 2));
console.log('Created windows/local-secrets.json with random credentials.');
