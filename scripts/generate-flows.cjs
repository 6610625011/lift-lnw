const fs=require('fs');
const root=require('path').resolve(__dirname,'..');
const dir=root+'/windows/node-red/';
const func=name=>fs.readFileSync(dir+name,'utf8');
const flows=[
{id:'mockup',type:'tab',label:'ESP32 Elevator A–D',disabled:false,info:'ESP32 authoritative four-car mockup. Windows Mosquitto + Node-RED.'},
{id:'mqtt-local',type:'mqtt-broker',name:'Windows Mosquitto',broker:'127.0.0.1',port:'1883',clientid:'node-red-elevator',autoConnect:true,usetls:false,protocolVersion:'4',keepalive:'15',cleansession:true,autoUnsubscribe:true},
{id:'state-in',type:'mqtt in',z:'mockup',name:'ESP32 state + events',topic:'elevator/v1/mockup/+',qos:'1',datatype:'utf8',broker:'mqtt-local',nl:false,rap:true,rh:0,x:160,y:100,wires:[['monitor','wirelog']]},
{id:'broker-status',type:'status',z:'mockup',name:'Broker connection',scope:['state-in'],x:160,y:160,wires:[['broker-topic']]},
{id:'broker-topic',type:'change',z:'mockup',name:'Broker status topic',rules:[{t:'set',p:'topic',pt:'msg',to:'broker-status',tot:'str'}],x:390,y:160,wires:[['monitor']]},
{id:'heartbeat',type:'inject',z:'mockup',name:'Check stale data each second',props:[{p:'topic',vt:'str'},{p:'payload'}],topic:'tick',payload:'',payloadType:'date',repeat:'1',once:true,onceDelay:0.5,x:200,y:220,wires:[['monitor','wirelog']]},
{id:'monitor',type:'function',z:'mockup',name:'Validate state / detect offline / events',func:func('monitor-function.js'),outputs:1,noerr:0,initialize:'',finalize:'',libs:[],x:650,y:100,wires:[['dashboard']]},
{id:'dashboard',type:'ui-template',z:'mockup',group:'lift-group',page:'',ui:'',name:'Four elevators',order:1,width:12,height:0,format:func('dashboard.html'),storeOutMessages:true,passthru:false,resendOnRefresh:true,templateScope:'local',className:'',x:950,y:100,wires:[['request']]},
{id:'wirelog',type:'function',z:'mockup',name:'Record every MQTT message for data-flow page',func:func('wire-function.js'),outputs:1,noerr:0,initialize:'',finalize:'',libs:[],x:650,y:300,wires:[['dataflow-ui']]},
{id:'dataflow-ui',type:'ui-template',z:'mockup',group:'flow-group',page:'',ui:'',name:'Data flow page',order:1,width:12,height:0,format:func('dataflow.html'),storeOutMessages:true,passthru:false,resendOnRefresh:true,templateScope:'local',className:'',x:950,y:300,wires:[['request']]},
{id:'request',type:'function',z:'mockup',name:'Fresh request + unique ID',func:func('request-function.js'),outputs:1,noerr:0,initialize:'',finalize:'',libs:[],x:960,y:220,wires:[['mqtt-out']]},
{id:'mqtt-out',type:'mqtt out',z:'mockup',name:'Request to ESP32',topic:'elevator/v1/mockup/request',qos:'1',retain:'false',broker:'mqtt-local',x:1200,y:220,wires:[]},
{id:'status-api',type:'http in',z:'mockup',name:'Read-only test status',url:'/mockup/status',method:'get',upload:false,x:190,y:340,wires:[['status-fn']]},
{id:'status-fn',type:'function',z:'mockup',name:'Last received ESP32 state',func:"msg.payload=flow.get('model')||{online:false,state:null};msg.headers={'Content-Type':'application/json','Cache-Control':'no-store'};return msg;",outputs:1,x:480,y:340,wires:[['status-response']]},
{id:'status-response',type:'http response',z:'mockup',name:'Local status',statusCode:'',headers:{},x:750,y:340,wires:[]},
{id:'lift-ui',type:'ui-base',name:'Elevator Mockup',path:'/dashboard',appIcon:'mdi-elevator',includeClientData:true,acceptsClientConfig:['ui-control','ui-notification'],showPathInSidebar:false,showPageTitle:false,navigationStyle:'none',titleBarStyle:'hidden',showReconnectNotification:true,showDisconnectNotification:true,notificationDisplayTime:3,allowInstall:false},
{id:'lift-theme',type:'ui-theme',name:'Elevator',colors:{surface:'#ffffff',primary:'#126c82',bgPage:'#f3f6f8',groupBg:'#f3f6f8',groupOutline:'#f3f6f8'},sizes:{density:'default',pagePadding:'18px',groupGap:'12px',groupBorderRadius:'12px',widgetGap:'12px'}},
{id:'lift-page',type:'ui-page',name:'Elevator A–D',ui:'lift-ui',path:'/elevators',icon:'mdi-elevator',layout:'grid',theme:'lift-theme',order:1,visible:true,disabled:false,breakpoints:[{name:'Default',px:0,cols:12}]},
{id:'flow-page',type:'ui-page',name:'Data flow',ui:'lift-ui',path:'/dataflow',icon:'mdi-lan',layout:'grid',theme:'lift-theme',order:2,visible:true,disabled:false,breakpoints:[{name:'Default',px:0,cols:12}]},
{id:'flow-group',type:'ui-group',name:'Data flow',page:'flow-page',width:12,height:1,order:1,showTitle:false,className:'',visible:true,disabled:false,groupType:'default'},
{id:'lift-group',type:'ui-group',name:'Elevators',page:'lift-page',width:12,height:1,order:1,showTitle:false,className:'',visible:true,disabled:false,groupType:'default'}
];
fs.writeFileSync(dir+'flows.json',JSON.stringify(flows,null,2));
// Node-RED encrypts these credentials on first deploy. Kept outside source control.
const secrets=JSON.parse(fs.readFileSync(root+'/windows/local-secrets.json'));
fs.writeFileSync(dir+'flows_cred.json',JSON.stringify({'mqtt-local':{user:secrets.mqttUser,password:secrets.mqttPassword}}));
console.log('Generated Node-RED flows.');
