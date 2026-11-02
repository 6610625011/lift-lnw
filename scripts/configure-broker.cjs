const fs=require('fs'), path=require('path'), {spawnSync}=require('child_process');
const root=path.resolve(__dirname,'..'), dir=path.join(root,'windows/broker');
const secrets=JSON.parse(fs.readFileSync(path.join(root,'windows/local-secrets.json')));
const executable=path.join(root,'windows/mosquitto/mosquitto_passwd.exe');
const passwords=path.join(dir,'passwords');
if(!fs.existsSync(passwords)) {
 const result=spawnSync(executable,['-b','-c',passwords,secrets.mqttUser,secrets.mqttPassword],{encoding:'utf8',windowsHide:true});
 if(result.status!==0) throw new Error('Mosquitto password creation failed: '+result.stderr);
}
fs.writeFileSync(path.join(dir,'acl'),`user ${secrets.mqttUser}\ntopic readwrite elevator/v1/mockup/#\n`);
const normalized=dir.replaceAll('\\','/');
fs.writeFileSync(path.join(dir,'mosquitto.conf'),`# Local authenticated elevator mockup; Windows firewall limits Wi-Fi access.\nlistener 1883 0.0.0.0\nallow_anonymous false\npassword_file ${normalized}/passwords\nacl_file ${normalized}/acl\npersistence true\npersistence_location ${normalized}/\nautosave_interval 60\nlog_dest stdout\nlog_type error\nlog_type warning\nlog_type notice\nconnection_messages true\n`);
console.log('Mosquitto credentials and topic ACL configured.');
