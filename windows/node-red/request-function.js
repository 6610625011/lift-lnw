// msg.topic 'verify' = hall call (msg.payload = floor where the passenger stands, optional); 'select' = floor chosen inside the car (msg.payload = floor).
const model=flow.get('model');
const now=Date.now();
if (!model || !model.online || now-model.liveAt>3000 || model.pending) return null;
const phase=model.state.phase;
const body={request_id:null,boot_id:model.state.boot_id,issued_uptime_ms:model.state.uptime_ms};
if (msg.topic==='select') {
    const floor=Number(msg.payload);
    if (phase!=='boarding' || model.state.destination!==0 || !Number.isInteger(floor) || floor<1 || floor>20 || floor===model.state.floors[model.state.assigned]) return null;
    body.action='select'; body.floor=floor;
    model.feedback='กำลังส่งชั้น '+floor+' ไปที่ ESP32…';
} else {
    if (!['idle','arrived'].includes(phase)) return null;
    body.action='verify';
    const here=Number(msg.payload);
    if (Number.isInteger(here) && here>=1 && here<=20) body.floor=here;
    model.feedback='กำลังส่งคำขอเรียกลิฟต์จากชั้น '+(body.floor||model.state.hall_floor)+' ไปที่ ESP32…';
}
const id=now.toString(36)+'-'+Math.random().toString(36).slice(2,14);
body.request_id=id;
model.pending={id,time:now};
flow.set('model',model);
return {topic:'elevator/v1/mockup/request',retain:false,qos:'1',payload:JSON.stringify(body)};
