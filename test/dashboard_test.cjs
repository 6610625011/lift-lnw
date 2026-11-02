const assert=require('node:assert/strict'), fs=require('node:fs');
const values=new Map(); const flow={get:k=>structuredClone(values.get(k)),set:(k,v)=>values.set(k,structuredClone(v))};
const monitor=new Function('msg','flow',fs.readFileSync('windows/node-red/monitor-function.js','utf8'));
const request=new Function('msg','flow',fs.readFileSync('windows/node-red/request-function.js','utf8'));
const state={boot_id:'0123456789abcdef',uptime_ms:10000,phase:'idle',assigned:'-',hall_floor:1,destination:0,floors:{A:5,B:14,C:3,D:9},lcd:['F01 PRESS VERIFY','A05 B14 C03 D09']};
const send=s=>monitor({topic:'elevator/v1/mockup/state',payload:s},flow);
monitor({topic:'broker-status',status:{text:'connected'}},flow);
assert.equal(monitor({topic:'elevator/v1/mockup/state',retain:true,payload:JSON.stringify(state)},flow).payload.online,false,'retained data must not prove liveness');
assert.equal(request({topic:'verify'},flow),null,'offline request blocked');
assert.equal(monitor({topic:'elevator/v1/mockup/state',retain:false,payload:JSON.stringify(state)},flow).payload.online,true);

// Hall call.
const command=JSON.parse(request({topic:'verify',payload:7},flow).payload);
assert.equal(command.floor,7,'hall call carries the chosen standing floor');
assert.equal(command.action,'verify'); assert.equal(command.boot_id,state.boot_id);assert.equal(command.issued_uptime_ms,10000);
assert.equal(request({topic:'verify'},flow),null,'pending command blocks double clicks');
let result=monitor({topic:'elevator/v1/mockup/events',payload:JSON.stringify({boot_id:state.boot_id,type:'accepted',source:'dashboard',request_id:command.request_id})},flow).payload;
assert.equal(result.pending,null); assert.match(result.feedback,/รับคำสั่ง/);

// Car coming: neither a second call nor a floor choice is allowed yet.
state.phase='coming';state.assigned='C';send(state);
assert.equal(request({topic:'verify'},flow),null,'active journey blocks calls');
assert.equal(request({topic:'select',payload:16},flow),null,'cannot choose floor before the car arrives');

// Car open at the hall: choose a floor.
state.phase='boarding';state.floors.C=1;send(state);
assert.equal(request({topic:'verify'},flow),null,'boarding blocks hall calls');
assert.equal(request({topic:'select',payload:0},flow),null,'floor 0 rejected');
assert.equal(request({topic:'select',payload:21},flow),null,'floor 21 rejected');
assert.equal(request({topic:'select',payload:1.5},flow),null,'non-integer rejected');
assert.equal(request({topic:'select',payload:1},flow),null,'current floor rejected');
const pick=JSON.parse(request({topic:'select',payload:16},flow).payload);
assert.equal(pick.action,'select'); assert.equal(pick.floor,16); assert.equal(pick.boot_id,state.boot_id);
assert.equal(request({topic:'select',payload:9},flow),null,'pending select blocks a second tap');
result=monitor({topic:'elevator/v1/mockup/events',payload:JSON.stringify({boot_id:state.boot_id,type:'accepted',source:'dashboard',request_id:pick.request_id})},flow).payload;
assert.equal(result.pending,null);

// After the board confirms the choice, selecting again is blocked.
state.destination=16;send(state);
assert.equal(request({topic:'select',payload:9},flow),null,'one floor choice per journey');
state.phase='riding';send(state);
assert.equal(request({topic:'verify'},flow),null);

// Validation / offline detection.
const old=flow.get('model');old.liveAt=Date.now()-4000;flow.set('model',old);
assert.equal(monitor({topic:'tick'},flow).payload.online,false,'stale status detected');
assert.equal(monitor({topic:'elevator/v1/mockup/state',payload:{...state,floors:{A:25,B:1,C:2,D:3}}},flow),null,'invalid floors rejected');
assert.equal(monitor({topic:'elevator/v1/mockup/state',payload:{...state,destination:21}},flow),null,'invalid destination rejected');
send(state);
assert.equal(monitor({topic:'broker-status',status:{text:'disconnected'}},flow).payload.online,false,'broker disconnect detected');
console.log('PASS: retained/stale/offline handling, state validation, hall call + in-car floor selection guards, pending/busy request guards, acknowledgements.');
