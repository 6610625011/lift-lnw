// msg.topic selects the request sent to the ESP32 (msg.payload = floor where needed):
//   'here'   tell the board which floor the passenger stands on (idle/arrived only, fire-and-forget)
//   'verify' hall call; payload = floor where the passenger stands (optional)
//   'enter'  passenger walks into the car that is waiting with doors open
//   'select' floor chosen inside the car (after walking in)
const model=flow.get('model');
const now=Date.now();
if (!model || !model.online || now-model.liveAt>3000) return null;
const st=model.state, phase=st.phase;
const body={request_id:null,boot_id:st.boot_id,issued_uptime_ms:st.uptime_ms};
const id=now.toString(36)+'-'+Math.random().toString(36).slice(2,14);
body.request_id=id;
const out=()=>({topic:'elevator/v1/mockup/request',retain:false,qos:'1',payload:JSON.stringify(body)});
if (msg.topic==='here') {
    const floor=Number(msg.payload);
    if (!['idle','arrived'].includes(phase) || !Number.isInteger(floor) || floor<1 || floor>20) return null;
    body.action='here'; body.floor=floor;
    return out();   // Not tracked as pending: the next click must not be blocked by it.
}
if (model.pending) return null;
if (msg.topic==='select') {
    const floor=Number(msg.payload);
    if (phase!=='boarding' || !st.boarded || st.destination!==0 || !Number.isInteger(floor) || floor<1 || floor>20 || floor===st.floors[st.assigned]) return null;
    body.action='select'; body.floor=floor;
    model.feedback='กำลังส่งชั้น '+floor+' ไปที่ ESP32…';
} else if (msg.topic==='enter') {
    if (phase!=='boarding' || st.boarded) return null;
    body.action='enter';
    model.feedback='กำลังส่งคำสั่งเดินเข้าลิฟต์ไปที่ ESP32…';
} else {
    if (!['idle','arrived'].includes(phase)) return null;
    body.action='verify';
    const here=Number(msg.payload);
    if (Number.isInteger(here) && here>=1 && here<=20) body.floor=here;
    model.feedback='กำลังส่งคำขอเรียกลิฟต์จากชั้น '+(body.floor||st.hall_floor)+' ไปที่ ESP32…';
}
model.pending={id,time:now};
flow.set('model',model);
return out();
