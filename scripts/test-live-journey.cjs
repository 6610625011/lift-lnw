// Requires the actual ESP32, working Wi-Fi and idle/arrived state. Does not publish simulated floors.
const assert=require('node:assert/strict');
const mqtt=require('../windows/node-red/node_modules/mqtt');
const {chromium}=require('../windows/test-tools/node_modules/playwright');
const secrets=require('../windows/local-secrets.json');
const base='elevator/v1/mockup/';
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
async function until(check,timeout=8000){const end=Date.now()+timeout;while(Date.now()<end){const value=await check();if(value)return value;await sleep(100);}throw Error('Timed out waiting for actual ESP32 response. Check Wi-Fi/firewall.');}
(async()=>{
 let client,browser;
 try {
  let state,liveAt=0,maxGap=0,stateCount=0;const events=[],phases=new Set();
  client=mqtt.connect('mqtt://127.0.0.1:1883',{username:secrets.mqttUser,password:secrets.mqttPassword,reconnectPeriod:0,connectTimeout:3000});
  client.on('error',e=>console.error(e.message));
  client.on('connect',()=>client.subscribe(base+'+',{qos:1}));
  client.on('message',(topic,buffer,packet)=>{
   if(![base+'state',base+'events'].includes(topic))return;
   try {const data=JSON.parse(buffer.toString());
    if(topic===base+'state'&&!packet.retain){state=data;const now=Date.now();if(liveAt)maxGap=Math.max(maxGap,now-liveAt);liveAt=now;++stateCount;if(!phases.has(data.phase))console.log('Observed phase:',data.phase,'uptime',data.uptime_ms);phases.add(data.phase);}
    if(topic===base+'events'&&!packet.retain)events.push(data);
   } catch { console.error('Invalid JSON from topic',topic,buffer.toString('hex').slice(0,200)); }
  });
  await until(()=>state&&Date.now()-liveAt<2000,30000);
  assert.ok(['idle','arrived'].includes(state.phase),'Run this test when the current journey has finished.');
  const initial={...state.floors};
  browser=await chromium.launch({executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',headless:true});
  const page=await browser.newPage({viewport:{width:1440,height:1050}});
  await page.goto('http://127.0.0.1:1880/dashboard/elevators');
  const button=page.getByRole('button',{name:'ยืนยัน / เรียกลิฟต์',exact:true});
  await button.waitFor();await until(()=>button.isEnabled());
  await button.click();
  const accepted=await until(()=>events.find(e=>e.type==='accepted'&&e.source==='dashboard'));
  const car=await until(()=>state.assigned!=='-'&&state.assigned);
  console.log('Actual ESP32 accepted dashboard click; selected elevator '+car+'.');
  const send=(id,age=0)=>client.publish(base+'request',JSON.stringify({action:'verify',request_id:id,boot_id:state.boot_id,issued_uptime_ms:(state.uptime_ms-age)>>>0}),{qos:1,retain:false});
  send(accepted.request_id);
  const busyId='live-busy-'+Date.now();send(busyId);
  await until(()=>events.find(e=>e.request_id===busyId&&e.type==='busy'));
  const expiredId='live-expired-'+Date.now();send(expiredId,10000);
  await until(()=>events.find(e=>e.request_id===expiredId&&e.type==='expired'));
  await until(()=>state.phase==='arrived'&&state.floors[car]===16,70000);
  for(const id of ['A','B','C','D'])if(id!==car)assert.equal(state.floors[id],initial[id]);
  assert.ok(phases.has('boarding')&&phases.has('riding'));
  send(accepted.request_id);await sleep(1800);
  assert.equal(state.phase,'arrived','Duplicate request after arrival must not start a new trip.');
  const model=await (await fetch('http://127.0.0.1:1880/mockup/status')).json();
  assert.equal(model.online,true);assert.equal(model.state.floors[car],16);
  console.log('Telemetry:',stateCount,'snapshots; maximum interval',maxGap,'ms.');
  assert.ok(maxGap<3500,'Telemetry gap exceeded the dashboard stale threshold.');
  await page.screenshot({path:'windows/logs/dashboard-live.png',fullPage:true});
  console.log('PASS: real Wi-Fi MQTT telemetry; dashboard command; busy/expired/duplicate handling; pickup, boarding, floor movement and arrival; other three cars unchanged.');
 } finally {if(browser)await browser.close();if(client)client.end(true);}
})().catch(e=>{console.error(e.message);process.exitCode=1;});
