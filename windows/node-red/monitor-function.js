// Executed inside a Node-RED function node. Only fresh ESP32 state proves liveness.
let model = flow.get('model') || {state:null, receivedAt:0, liveAt:0, availability:'offline', broker:false, events:[], feedback:'', pending:null};
const now = Date.now();
if (msg.topic === 'broker-status') {
    model.broker = msg.status?.text === 'node-red:common.status.connected' || msg.status?.text === 'connected';
}
if (msg.topic === 'elevator/v1/mockup/state') {
    let state;
    try { state = typeof msg.payload === 'string' ? JSON.parse(msg.payload) : msg.payload; } catch { return null; }
    const phases = ['idle','coming','boarding','riding','arrived'];
    if (!state || !phases.includes(state.phase) || !/^[0-9a-f]{16}$/.test(state.boot_id) || !Number.isInteger(state.uptime_ms) || state.uptime_ms < 0 || state.uptime_ms > 4294967295 || !['-','A','B','C','D'].includes(state.assigned) || !state.floors || !['A','B','C','D'].every(id => Number.isInteger(state.floors[id]) && state.floors[id]>=1 && state.floors[id]<=20) || !Number.isInteger(state.hall_floor) || state.hall_floor<1 || state.hall_floor>20 || !Number.isInteger(state.destination) || state.destination<0 || state.destination>20 || (state.targets!==undefined && !(state.targets && ['A','B','C','D'].every(id=>Number.isInteger(state.targets[id]) && state.targets[id]>=0 && state.targets[id]<=20))) || !Array.isArray(state.lcd) || state.lcd.length!==2 || !state.lcd.every(row=>typeof row==='string' && row.length<=16)) return null;
    if (model.state && model.state.boot_id !== state.boot_id) model.pending=null;
    model.state=state; model.receivedAt=now;
    if (!msg.retain) { model.liveAt=now; model.availability='online'; model.broker=true; }
}
if (msg.topic === 'elevator/v1/mockup/availability') {
    if (['online','offline'].includes(msg.payload)) model.availability=msg.payload;
    if (msg.payload==='offline') model.liveAt=0;
}
if (msg.topic === 'elevator/v1/mockup/events' && !msg.retain) {
    let event;
    try { event=typeof msg.payload==='string'?JSON.parse(msg.payload):msg.payload; } catch {return null;}
    const hall=model.state?.hall_floor||1, dest=model.state?.destination||0;
    const labels={assigned:'มอบหมายลิฟต์แล้ว · ให้ไปที่ลิฟต์ที่ได้รับมอบหมาย',pickup:'ลิฟต์ถึงชั้น '+hall+' แล้ว รอเลือกชั้น',selected:'เลือกชั้นปลายทางแล้ว',riding:'กำลังไปชั้น '+(dest||'ปลายทาง'),arrival:'ถึงชั้น '+(dest||'ปลายทาง')+' แล้ว',ready:'พร้อมรับคำขอใหม่',accepted:'บอร์ดรับคำสั่งจากแดชบอร์ดแล้ว',busy:'บอร์ดปฏิเสธคำสั่ง (ลิฟต์ไม่อยู่ในสถานะที่รับได้)',expired:'คำขอหมดอายุ กรุณากดใหม่'};
    if (!event || event.boot_id!==model.state?.boot_id || !labels[event.type]) return null;
    model.events.unshift({time:now,text:labels[event.type]+(event.source==='button'?' (ปุ่มที่บอร์ด)':'')});
    model.events=model.events.slice(0,30);
    if (model.pending?.id===event.request_id && ['accepted','busy','expired'].includes(event.type)) {model.feedback=labels[event.type]; model.pending=null;}
}
model.online=Boolean(model.state && model.broker && model.availability==='online' && now-model.liveAt<3500);
if (model.pending && now-model.pending.time>6000) {model.pending=null;model.feedback='ยังไม่ได้รับคำตอบจากบอร์ด กรุณาตรวจสอบการเชื่อมต่อ';}
flow.set('model',model);
return {payload:model};
