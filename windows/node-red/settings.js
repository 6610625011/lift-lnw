const secrets = require('../local-secrets.json');
// Default (Windows): editor + dashboard on 127.0.0.1 only.
// Raspberry Pi: NR_HOST=0.0.0.0 serves the dashboard to phones/laptops on the LAN.
// The unauthenticated flow editor is switched off in that mode.
const host = process.env.NR_HOST || '127.0.0.1';
const lan = host !== '127.0.0.1' && host !== 'localhost';
module.exports={uiPort:1880,uiHost:host,httpAdminRoot:lan?false:'/',flowFile:'flows.json',credentialSecret:secrets.credentialSecret,editorTheme:{projects:{enabled:false}},functionGlobalContext:{},logging:{console:{level:'info',metrics:false,audit:false}}};
