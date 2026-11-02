// Builds windows/node-red/preview.html: the real dashboard.html template driven by an
// in-browser simulator (scripts/lib/sim-core.cjs, same rules as the firmware).
// Opens without Node-RED, MQTT or a board. Vue is loaded from a CDN, so it needs internet.
const fs = require('fs');
const path = require('path');
const dir = path.resolve(__dirname, '..', 'windows', 'node-red');
const src = fs.readFileSync(path.join(dir, 'dashboard.html'), 'utf8');
const core = fs.readFileSync(path.join(__dirname, 'lib', 'sim-core.cjs'), 'utf8');
const pick = (tag) => src.match(new RegExp('<' + tag + '>([\\s\\S]*?)</' + tag + '>'))[1];
const template = pick('template');
const script = pick('script').replace('export default', 'return');
const style = pick('style');

const html = `<!doctype html>
<html lang="th"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Elevator dashboard preview (simulated)</title>
<style>body{margin:0;background:#f3f6f8}.sim-bar{position:sticky;top:0;z-index:10;background:#172a3a;color:#fff;font:13px Tahoma,sans-serif;padding:8px 14px;display:flex;gap:12px;align-items:center;flex-wrap:wrap}.sim-bar button,.sim-bar select{font:inherit;padding:4px 10px;border-radius:6px;border:0;cursor:pointer}.journey button{border:0}${style}</style>
</head><body>
<div class="sim-bar"><b>PREVIEW — ข้อมูลจำลองในเบราว์เซอร์ ไม่ใช่ข้อมูลจาก ESP32</b>
<label>ความเร็ว <select id="speed"><option value="1" selected>1x (2 วิ/ชั้น)</option><option value="2">2x</option><option value="4">4x</option></select></label>
<button id="reset">รีเซ็ต</button></div>
<div id="app"></div>
<script src="https://cdnjs.cloudflare.com/ajax/libs/vue/3.4.21/vue.global.prod.min.js"></script>
<script>
const SimCore = (function(){ const module = { exports: {} }; ${core}
return module.exports; })();
const component = (function(){ ${script} })();
let S = SimCore.createSim();
let t = 0;                       // simulated clock in ms
const events = [];
const labels = { coming:'มอบหมายลิฟต์แล้ว · ให้ไปที่ลิฟต์ที่ได้รับมอบหมาย', boarding:'ลิฟต์ถึงแล้ว รอเลือกชั้น', riding:'เลือกชั้นแล้ว · กำลังเดินทาง', arrived:'ถึงปลายทางแล้ว', idle:'พร้อมรับคำขอใหม่' };
let lastPhase = 'idle';
const app = Vue.createApp({
  ...component,
  template: ${JSON.stringify(template)},
  data() { return { ...component.data(), msg: { payload: {} } }; },
  methods: { ...component.methods,
    send(m) {
      if (m && m.topic === 'select') S.selectFloor(t, m.payload); else S.request(t, Number(m.payload) || 0);
      this.sync();
    },
    sync() {
      if (S.sim.phase !== lastPhase) { events.unshift({ time: Date.now(), text: labels[S.sim.phase] }); events.length = Math.min(events.length, 30); lastPhase = S.sim.phase; }
      this.msg = { payload: { online: true, pending: null, feedback: '', receivedAt: Date.now(), events: events.slice(), state: { ...S.stateBody(), emulator: true } } };
    } },
  mounted() {
    let last = performance.now();
    this.sync();
    setInterval(() => {
      const now = performance.now(), speed = +document.getElementById('speed').value;
      t += (now - last) * speed; last = now;
      S.tick(t); this.sync();
    }, 100);
    document.getElementById('speed').onchange = (e) => document.documentElement.style.setProperty('--step', (2 / +e.target.value) + 's');
    document.getElementById('reset').onclick = () => { S = SimCore.createSim(); t = 0; events.length = 0; lastPhase = 'idle'; this.sync(); };
  }
});
app.mount('#app');
</script></body></html>`;
fs.writeFileSync(path.join(dir, 'preview.html'), html);
console.log('Wrote windows/node-red/preview.html');
