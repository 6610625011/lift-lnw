const mqtt=require('../windows/node-red/node_modules/mqtt');
const secrets=require('../windows/local-secrets.json');
async function connect(options,expectAccepted){
 return new Promise((resolve,reject)=>{
  const client=mqtt.connect('mqtt://127.0.0.1:1883',{connectTimeout:3000,reconnectPeriod:0,...options});
  const timer=setTimeout(()=>{client.end(true);reject(new Error('MQTT authentication test timeout'));},4000);
  client.on('connect',()=>{clearTimeout(timer);client.end(true);expectAccepted?resolve():reject(new Error('Unauthenticated client was accepted'));});
  client.on('error',error=>{clearTimeout(timer);client.end(true);!expectAccepted&&error.code===5?resolve():reject(error);});
 });
}
(async()=>{
 await connect({},false);
 await connect({username:secrets.mqttUser,password:secrets.mqttPassword},true);
 console.log('PASS: unauthenticated MQTT rejected; configured client accepted.');
})().catch(error=>{console.error(error.message);process.exitCode=1;});
