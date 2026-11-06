// Executed inside a Node-RED function node. Feeds the "data flow" page: records every real MQTT message
// seen by Node-RED (both directions) so people can SEE that the parts talk to each other.
const base = 'elevator/v1/mockup/';
let wire = flow.get('wire') || {counts: {}, last: {}, lastRaw: {}, log: [], since: Date.now()};
const now = Date.now();
const phaseTh = {idle: 'ว่าง', coming: 'ลิฟต์กำลังมารับ', boarding: 'ประตูเปิด / อยู่ในลิฟต์', riding: 'กำลังเดินทาง', arrived: 'ถึงปลายทาง'};
const eventTh = {assigned: 'มอบหมายลิฟต์แล้ว', pickup: 'ลิฟต์ถึงจุดรับ (รอเดินเข้า)', boarded: 'ผู้โดยสารเดินเข้าลิฟต์แล้ว', selected: 'เลือกชั้นแล้ว', riding: 'เริ่มเดินทาง', arrival: 'ถึงปลายทาง', ready: 'พร้อมรับงานใหม่', coming: 'ลิฟต์กำลังมา', accepted: 'บอร์ดตอบรับคำสั่ง', busy: 'บอร์ดปฏิเสธ (ไม่ว่าง)', expired: 'คำสั่งหมดอายุ'};

if (typeof msg.topic === 'string' && msg.topic.startsWith(base) && !(msg.retain && msg.topic !== base + 'availability')) {
    const name = msg.topic.slice(base.length);
    let obj = null;
    const raw = typeof msg.payload === 'string' ? msg.payload : JSON.stringify(msg.payload);
    try { obj = typeof msg.payload === 'string' ? JSON.parse(msg.payload) : msg.payload; } catch { obj = null; }
    let text = raw, dir = 'up';
    if (name === 'state' && obj && obj.floors) {
        const f = obj.floors;
        text = 'สถานะลิฟต์ A' + f.A + ' B' + f.B + ' C' + f.C + ' D' + f.D + ' · ' + (phaseTh[obj.phase] || obj.phase) + (obj.assigned && obj.assigned !== '-' ? ' (ลิฟต์ ' + obj.assigned + ')' : '');
    } else if (name === 'events' && obj) {
        text = 'เหตุการณ์: ' + (eventTh[obj.type] || obj.type);
    } else if (name === 'availability') {
        text = 'ESP32 ' + raw;
    } else if (name === 'hw') {
        dir = 'down';
        text = 'ฮาร์ดแวร์จำลอง (ตัวจำลองเท่านั้น): ' + raw;
    } else if (name === 'request' && obj) {
        dir = 'down';
        text = obj.action === 'select' ? 'คำสั่งจากแดชบอร์ด: เลือกชั้น ' + obj.floor : obj.action === 'enter' ? 'คำสั่งจากแดชบอร์ด: เดินเข้าลิฟต์' : obj.action === 'here' ? 'คำสั่งจากแดชบอร์ด: ฉันอยู่ชั้น ' + obj.floor : 'คำสั่งจากแดชบอร์ด: เรียกลิฟต์' + (obj.floor ? ' จากชั้น ' + obj.floor : '');
    }
    wire.counts[name] = (wire.counts[name] || 0) + 1;
    wire.last[name] = now;
    wire.lastRaw[name] = raw.slice(0, 600);
    wire.log.unshift({t: now, name, dir, text, raw: raw.slice(0, 600)});
    wire.log = wire.log.slice(0, 80);
}
flow.set('wire', wire);
const model = flow.get('model') || {};
return {payload: {
    now,
    since: wire.since,
    counts: wire.counts,
    last: wire.last,
    lastRaw: wire.lastRaw,
    log: wire.log,
    hops: {
        broker: !!model.broker,
        esp: model.availability === 'online' && !!model.online,
        fresh: !!model.online,
        phase: model.state ? model.state.phase : null,
        emulator: !!(model.state && model.state.emulator),
        uptime: model.state ? model.state.uptime_ms : null,
        boot: model.state ? model.state.boot_id : null
    }
}};
