const {chromium}=require(process.env.RGP_PLAYWRIGHT_MODULE||'playwright');
const fs=require('fs'),assert=require('assert'),path=require('path'),os=require('os');
const screenshots=process.env.RGP_WIFI_SCREENSHOTS||fs.mkdtempSync(path.join(os.tmpdir(),'rgp-wifi-browser-'));
const root=process.argv[2]||path.resolve(__dirname,'..');
const extract=(file,delimiter)=>fs.readFileSync(path.join(root,'application/wifi',file),'utf8').split('R"'+delimiter+'(')[1].split(')'+delimiter+'"')[0];
const html=extract('WifiWeb.h','RGP'),portal=extract('WifiPortal.h','PORTAL');
const labels={
 ja:{info:'情報',files:'ファイル',datasets:'データセット',ready:'SDカードを操作できます。',soon:'準備中です。',edit:'開く・編集',remove:'削除',saved:'保存しました',replace:'上書きしますか？',page:n=>`${n} ページ`,mode:'Wi-Fi管理',restarting:'再起動しています。端末の画面を確認してください。',connected:'Wi-Fiに接続しました',open:'管理画面を開く',noSd:'SDカードが利用できません。端末を確認してください。'},
 en:{info:'Info',files:'Files',datasets:'Datasets',ready:'SD card is ready.',soon:'Coming soon.',edit:'Open / edit',remove:'Delete',saved:'Saved',replace:'Replace this file?',page:n=>`Page ${n}`,mode:'Wi-Fi management',restarting:'Restarting. Check the device screen.',connected:'Connected to Wi-Fi',open:'Open manager',noSd:'SD card unavailable. Check the device.'}
};
const status={session:'token',usable:true,version:'host-test',sd:'MOUNTED',ssid:'ReverseGeoputer-1234',total:1e9,free:5e8,dataset:1,language:0,logInterval:10,flushInterval:10,screenTimeout:60,wordbookLanguage:'ja',brightness:40,volume:1,power:2};
const json=(route,code,body)=>route.fulfill({status:code,contentType:'application/json',body:JSON.stringify(body)});
const noOverflow=async(page,label)=>assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),label);
const idle=page=>page.waitForFunction(()=>!document.getElementById('refreshInfo').disabled);
(async()=>{
 const browser=await chromium.launch({headless:true,args:['--no-sandbox']});
 for(const language of ['ja','en'])for(const width of [320,390,768,1280]){
  const l=labels[language];
  const context=await browser.newContext({locale:language==='ja'?'ja-JP':'en-US',viewport:{width,height:844},isMobile:width<768,hasTouch:width<768});
  const page=await context.newPage(),errors=[];page.on('pageerror',e=>errors.push(e.message));
  const files=new Map([['/dataset',{directory:true}],['/dataset/01',{directory:true}],['/dataset/01/dataset.ini',{body:'[dataset]\nversion=1\n'}],['/notes.txt',{body:'sample\n'}],['/binary.glc',{body:'BINARY'}],['/とても長い名前の日本語テキストファイルの表示状態を確認.txt',{body:'text'}]]);
  for(let i=0;i<21;++i)files.set('/extra-'+i+'.txt',{body:'page'});
  const calls=[];let writes=0,exited=false,releaseUpload=null,holdUpload=false,sdAvailable=true;
  await page.route('http://192.168.4.1/**',async route=>{
   const req=route.request(),url=new URL(req.url()),target=url.searchParams.get('path');calls.push(url.pathname);
   if(url.pathname==='/')return route.fulfill({contentType:'text/html',body:html});
   if(url.pathname==='/favicon.ico')return route.fulfill({status:204,body:''});
   if(url.pathname==='/api/status')return json(route,200,{...status,usable:sdAvailable,sd:sdAvailable?'MOUNTED':'NO CARD'});
   if(url.pathname==='/api/list'){
    const prefix=target==='/'?'/':target+'/',entries=[];
    for(const [name,entry]of files)if(name.startsWith(prefix)&&!name.slice(prefix.length).includes('/'))entries.push({name:name.slice(prefix.length),directory:!!entry.directory,size:entry.body?.length||0,date:23902,time:123});
    const offset=Number(url.searchParams.get('offset')||0);return json(route,200,{entries:entries.slice(offset,offset+20),more:entries.length>offset+20});
   }
   if(url.pathname==='/api/text')return route.fulfill({contentType:'text/plain; charset=utf-8',headers:{'X-File-Version':'123456789abcdef0'},body:files.get(target).body});
   if(url.pathname==='/api/info')return json(route,200,{exists:files.has(target),directory:!!files.get(target)?.directory});
   assert.equal(req.headers()['x-session'],'token');const dest=decodeURIComponent(req.headers()['x-path']||'');
   if(url.pathname==='/api/mkdir')files.set(dest,{directory:true});
   else if(url.pathname==='/api/upload'||url.pathname==='/api/save'){
    assert.equal(req.headers()['content-type'],'application/octet-stream');
    if(url.pathname==='/api/save')assert.equal(req.headers()['x-version'],'123456789abcdef0');
    if(holdUpload)await new Promise(resolve=>{releaseUpload=resolve;});
    writes++;files.set(dest,{body:req.postDataBuffer().toString('utf8')});
   }else if(url.pathname==='/api/delete'){for(const key of files.keys())if(key===dest||key.startsWith(dest+'/'))files.delete(key);}
   else if(url.pathname==='/api/exit')exited=true;
   else return json(route,404,{error:'Unknown operation'});
   return json(route,200,{ok:true});
  });
  await page.goto('http://192.168.4.1/');await page.getByText(l.ready,{exact:true}).waitFor();await idle(page);
  assert.equal(await page.locator('html').getAttribute('lang'),language);
  assert(await page.locator('#infoPanel').isVisible());assert(await page.locator('#filesPanel').isHidden());
  assert((await page.locator('#statusValues').innerText()).includes(l.mode));
  assert.equal(calls.filter(x=>x==='/api/list').length,0,'info must not load directory entries');
  assert.deepEqual(await page.evaluate(()=>Object.keys(messages.ja).sort()),await page.evaluate(()=>Object.keys(messages.en).sort()),'translation key coverage');
  const beforePlaceholder=calls.length;await page.locator('#tabDatasets').click();
  await page.getByText(l.soon,{exact:true}).waitFor();assert.equal(calls.length,beforePlaceholder,'placeholder makes no requests');assert.equal(await page.locator('#datasetsPanel button').count(),0);
  await noOverflow(page,`${language} ${width} info/placeholder overflow`);
  await page.locator('#tabFiles').click();await page.locator('#entries .entry').first().waitFor();await idle(page);
  assert.equal(await page.locator('#entries li').filter({hasText:'binary.glc'}).getByRole('button',{name:l.edit,exact:true}).count(),0,'binary has no preview');
  await noOverflow(page,`${language} ${width} file list overflow`);
  await page.screenshot({path:path.join(screenshots,`files-${language}-${width}.png`),fullPage:true});
  await page.locator('#next').click();await page.waitForFunction(value=>document.getElementById('page').textContent===value,l.page(2));
  await page.locator('#prev').click();await page.waitForFunction(value=>document.getElementById('page').textContent===value,l.page(1));
  await page.locator('#entries li').filter({hasText:'notes.txt'}).getByRole('button',{name:l.edit,exact:true}).click();await page.locator('#editor[open]').waitFor();await idle(page);
  assert(await page.locator('#save').isDisabled());
  const saveBox=await page.locator('#save').boundingBox(),textBox=await page.locator('#text').boundingBox();assert(saveBox.y<textBox.y&&saveBox.y+saveBox.height<844,'editor actions visible above text');
  await page.locator('#text').fill('edited 日本語\n');await page.locator('#save').click();await page.locator('#dirty').filter({hasText:l.saved}).waitFor();assert.equal(files.get('/notes.txt').body,'edited 日本語\n');assert(await page.locator('#save').isDisabled());
  await page.locator('#text').fill('unsaved');await page.locator('#closeEditor').click();await page.locator('#question[open]').waitFor();await page.locator('#questionCancel').click();assert(await page.locator('#editor').isVisible());assert.equal(await page.locator('#text').inputValue(),'unsaved');
  await page.locator('#text').fill('edited 日本語\n');await page.locator('#closeEditor').click();await page.locator('#editor').waitFor({state:'hidden'});await idle(page);
  await page.locator('#mkdir').click();await page.locator('#questionInput').fill('追加フォルダー');await page.locator('#questionOk').click();await idle(page);assert(files.has('/追加フォルダー'));
  await page.getByRole('button',{name:'▸ dataset',exact:true}).click();await page.getByRole('button',{name:'▸ 01',exact:true}).click();await page.waitForFunction(()=>document.getElementById('path').textContent==='/dataset/01');await idle(page);
  const beforeSwitch=calls.length;await page.locator('#tabInfo').click();await page.locator('#tabDatasets').click();await page.locator('#tabFiles').click();assert.equal(calls.length,beforeSwitch,'switching cached panels does not re-read SD');assert.equal(await page.locator('#path').innerText(),'/dataset/01');
  await page.locator('#file').setInputFiles({name:'upload.txt',mimeType:'text/plain',buffer:Buffer.from('file upload\n')});await page.getByText('upload.txt',{exact:true}).waitFor();await idle(page);assert.equal(files.get('/dataset/01/upload.txt').body,'file upload\n');
  await page.locator('#file').setInputFiles({name:'upload.txt',mimeType:'text/plain',buffer:Buffer.from('replaced\n')});await page.getByText(l.replace,{exact:true}).waitFor();await page.locator('#questionOk').click();await idle(page);await page.getByText(l.ready,{exact:true}).waitFor();assert.equal(files.get('/dataset/01/upload.txt').body,'replaced\n');
  await page.locator('#entries li').filter({hasText:'upload.txt'}).getByRole('button',{name:l.remove,exact:true}).click();await page.locator('#questionCancel').click();assert(files.has('/dataset/01/upload.txt'));
  holdUpload=true;await page.locator('#file').setInputFiles({name:'held.txt',mimeType:'text/plain',buffer:Buffer.from('held\n')});await page.waitForFunction(()=>!document.getElementById('transfer').hidden);
  assert(await page.locator('#tabInfo').isDisabled());assert(await page.locator('#tabDatasets').isDisabled());assert(await page.locator('#language').isDisabled());
  while(!releaseUpload)await new Promise(resolve=>setTimeout(resolve,10));releaseUpload();await page.getByText('held.txt',{exact:true}).waitFor();await idle(page);holdUpload=false;
  await page.locator('#entries li').filter({hasText:'held.txt'}).getByRole('button',{name:l.remove,exact:true}).click();await page.locator('#questionOk').click();await idle(page);assert(!files.has('/dataset/01/held.txt'));
  await page.locator('#tabInfo').click();await page.locator('#refreshInfo').click();await idle(page);assert((await page.locator('#statusValues').innerText()).includes(l.mode));
  sdAvailable=false;await page.locator('#refreshInfo').click();await idle(page);await page.locator('#tabFiles').click();assert(await page.locator('#upload').isDisabled());for(const button of await page.locator('#entries button').all())assert(await button.isDisabled(),'cached file actions must be disabled when SD is unavailable');await page.locator('#tabInfo').click();sdAvailable=true;await page.locator('#refreshInfo').click();await idle(page);
  await page.locator('#exit').click();await page.locator('#questionOk').click();await page.getByText(l.restarting,{exact:true}).waitFor();assert(exited);assert.equal(writes,4);assert.deepEqual(errors,[]);
  console.log(`PASS ${language} ${width}px: info, placeholder, lazy list, text save/dirty cancel, pagination, folders, upload/replace, delete, busy navigation, refresh and exit`);await context.close();
 }
 // Language changes are local to the browser, survive reload, and retain the file view.
 for(const stored of [null,'broken','en']){
  const context=await browser.newContext({locale:'ja-JP',viewport:{width:390,height:844}});await context.addInitScript(value=>{if(!sessionStorage.getItem('seeded')){if(value!==null)localStorage.setItem('rgp.web.language',value);sessionStorage.setItem('seeded','1');}},stored);
  const page=await context.newPage();let requests=0;
  await page.route('http://192.168.4.1/**',route=>{const url=new URL(route.request().url());if(url.pathname==='/')return route.fulfill({contentType:'text/html',body:html});if(url.pathname==='/api/status'){requests++;return json(route,200,status);}if(url.pathname==='/api/list'){requests++;return json(route,200,{entries:[],more:false});}return route.fulfill({status:204,body:''});});
  await page.goto('http://192.168.4.1/');await idle(page);const expected=stored==='en'?'en':'ja';assert.equal(await page.locator('html').getAttribute('lang'),expected);
  await page.locator('#tabFiles').click();await idle(page);const before=requests;await page.locator('#language').selectOption(expected==='ja'?'en':'ja');assert.equal(requests,before,'translation makes no API requests');assert(await page.locator('#filesPanel').isVisible());
  const selected=expected==='ja'?'en':'ja';await page.reload();await idle(page);assert.equal(await page.locator('html').getAttribute('lang'),selected);console.log(`PASS browser language preference ${stored}: valid fallback, local switching and persistence`);await context.close();
 }
 for(const locale of ['en-US','fr-FR','ja-JP']){
  const language=locale==='ja-JP'?'ja':'en',l=labels[language];const context=await browser.newContext({locale,viewport:{width:320,height:844}});
  await context.addInitScript(()=>{Storage.prototype.getItem=()=>{throw Error('storage blocked');};Storage.prototype.setItem=()=>{throw Error('storage blocked');};});const page=await context.newPage(),errors=[];page.on('pageerror',e=>errors.push(e.message));
  await page.route('http://192.168.4.1/**',route=>new URL(route.request().url()).pathname==='/'?route.fulfill({contentType:'text/html',body:html}):json(route,200,{...status,usable:false,sd:'NO CARD',notice:''}));
  await page.goto('http://192.168.4.1/');await page.getByText(l.noSd,{exact:true}).waitFor();await idle(page);assert.equal(await page.locator('html').getAttribute('lang'),language);await page.locator('#tabFiles').click();assert(await page.locator('#upload').isDisabled());await page.locator('#tabDatasets').click();await page.getByText(l.soon,{exact:true}).waitFor();
  await page.locator('#language').selectOption(language==='ja'?'en':'ja');await noOverflow(page,'blocked storage / absent SD');assert.deepEqual(errors,[]);console.log(`PASS ${locale}: absent SD, unsupported-locale fallback and blocked browser storage`);await context.close();
 }
 for(const language of ['ja','en'])for(const width of [320,390]){
  const l=labels[language],context=await browser.newContext({locale:language==='ja'?'ja-JP':'en-US',viewport:{width,height:844},isMobile:true,hasTouch:true}),page=await context.newPage();
  await context.route('http://192.168.4.1/**',route=>{const url=new URL(route.request().url());if(url.pathname==='/connect')return route.fulfill({contentType:'text/html',body:portal});if(url.pathname==='/api/status')return json(route,200,status);return route.fulfill({contentType:'text/html',body:html});});
  await page.goto('http://192.168.4.1/connect');await page.getByText(l.connected,{exact:true}).waitFor();assert.equal(await page.locator('#url').innerText(),'http://192.168.4.1/');await noOverflow(page,'portal '+language+' '+width);
  const other=language==='ja'?'en':'ja';await page.locator('#language').selectOption(other);const opened=page.waitForEvent('popup');await page.getByRole('link',{name:labels[other].open,exact:true}).click();const manager=await opened;await manager.waitForLoadState();await manager.getByText(labels[other].ready,{exact:true}).waitFor();assert.equal(await manager.locator('html').getAttribute('lang'),other,'portal language preference reaches manager');
  await page.screenshot({path:path.join(screenshots,`portal-${language}-${width}.png`),fullPage:true});console.log(`PASS portal ${language} ${width}px: URL, language switch, manager link and shared preference`);await context.close();
 }
 await browser.close();console.log('Screenshots: '+screenshots);
})().catch(e=>{console.error(e);process.exit(1)});
