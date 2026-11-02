const {chromium}=require('../windows/test-tools/node_modules/playwright');
(async()=>{
 const browser=await chromium.launch({executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',headless:true});
 const page=await browser.newPage({viewport:{width:1440,height:1050}});
 const errors=[];
 page.on('pageerror',e=>errors.push(e.message));
 await page.goto('http://127.0.0.1:1880/dashboard/elevators');
 await page.getByRole('heading',{name:'ระบบลิฟต์จำลอง',exact:true}).waitFor();
 await page.waitForTimeout(1500);
 console.log((await page.locator('.elevator-app').innerText()).slice(0,2200));
 await page.screenshot({path:'windows/logs/dashboard-desktop.png',fullPage:true});
 await page.setViewportSize({width:390,height:844});
 await page.screenshot({path:'windows/logs/dashboard-mobile.png',fullPage:true});
 const width=await page.evaluate(()=>({screen:innerWidth,content:document.documentElement.scrollWidth}));
 if(errors.length || width.content>width.screen) throw new Error(JSON.stringify({errors,width}));
 console.log('PASS: dashboard renders without browser errors; desktop and mobile layouts fit.');
 await browser.close();
})().catch(e=>{console.error(e);process.exit(1)});
