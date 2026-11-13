#pragma once
namespace geoputer::wifi {
// Flash-resident HTML, styles and translations. The browser owns editor contents.
inline constexpr char WebPage[]=R"RGP(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Geoputer</title>
<style>
:root{color-scheme:light dark}*{box-sizing:border-box}body{margin:0;font:16px/1.5 system-ui,sans-serif}main{max-width:1024px;margin:auto;padding:12px;padding-bottom:calc(16px + env(safe-area-inset-bottom))}h1{font-size:22px;margin:0}h2{font-size:19px;margin:12px 0}header,.toolbar,nav{display:flex;gap:8px;flex-wrap:wrap;align-items:center}header{justify-content:space-between}button,select,input{font:inherit;min-height:44px;max-width:100%}button{padding:8px 12px;cursor:pointer}button:disabled{cursor:default}button:focus-visible,select:focus-visible,textarea:focus-visible,a:focus-visible{outline:3px solid #528bcc;outline-offset:2px}nav{border-bottom:1px solid #999;padding:12px 0}nav [aria-current=page]{font-weight:bold;border-bottom:3px solid currentColor}.toolbar{margin:10px 0}.sub,small{opacity:.8}#message{padding:8px 0;overflow-wrap:anywhere}#message.error,#editorNotice.error{color:#c33}#path,#questionText,dd,dialog h2{overflow-wrap:anywhere}#questionText{white-space:pre-line}#entries{list-style:none;padding:0;margin:12px 0}.entry{display:grid;grid-template-columns:minmax(0,1fr);gap:8px;padding:12px 0;border-bottom:1px solid #999}.name{overflow-wrap:anywhere}.actions{display:flex;gap:8px;flex-wrap:wrap}.folder{text-align:left;overflow-wrap:anywhere;width:100%;background:none;border:0;padding:0;text-decoration:underline}.empty{padding:12px 0}progress{width:100%;height:18px}dialog{width:calc(100% - 24px);max-width:720px;max-height:92vh;max-height:92dvh;padding:12px;border:1px solid #999}dialog::backdrop{background:#0008}dialog h2{margin:0}#editor[open]{display:flex;flex-direction:column;height:90vh;height:min(90dvh,720px)}#editor .toolbar{flex-shrink:0}#editorNotice{margin:0;overflow-wrap:anywhere}textarea{width:100%;min-height:100px;flex:1;resize:none;padding:8px;font:16px/1.5 ui-monospace,monospace;tab-size:2}input[type=text]{width:100%;padding:8px}dl{display:grid;grid-template-columns:minmax(95px,1fr) minmax(0,2fr);gap:8px}dt,dd{margin:0}footer{border-top:1px solid #999;margin-top:20px;padding-top:12px}[hidden]{display:none!important}@media(min-width:680px){.entry{grid-template-columns:minmax(0,1fr) auto auto;align-items:center}.toolbar .spacer{flex:1}}
</style></head><body><main>
<header><h1>Geoputer</h1><label><span data-i18n="webLanguage">Language</span> <select id="language"><option value="ja">日本語</option><option value="en">English</option></select></label></header>
<nav aria-label="Pages" data-i18n-aria="pages"><button id="tabInfo" data-i18n="info" aria-current="page">Info</button><button id="tabFiles" data-i18n="files">Files</button><button id="tabDatasets" data-i18n="datasets">Datasets</button></nav>
<p class="sub" data-i18n="networkNote">This Wi-Fi may not provide internet access.</p>
<div id="message" role="status" aria-live="polite"></div>
<section id="infoPanel" aria-labelledby="tabInfo"><h2 data-i18n="info">Info</h2><button id="refreshInfo" data-i18n="refresh">Refresh</button><dl id="statusValues"></dl><p class="sub" data-i18n="settingsNote">Change device settings on the device.</p></section>
<section id="filesPanel" aria-labelledby="tabFiles" hidden><h2 data-i18n="fileManager">File manager</h2><div class="sub" data-i18n="currentFolder">Current folder</div><strong id="path">/</strong><div class="toolbar"><button id="parent" data-i18n="parent">Parent folder</button><button id="refresh" data-i18n="refresh">Refresh</button><span class="spacer"></span><button id="mkdir" data-i18n="mkdir">New folder</button><button id="upload" data-i18n="upload">Upload file</button><input type="file" id="file" hidden></div><p class="sub" data-i18n="fileNote">Open and edit UTF-8 text files. Upload one file at a time.</p>
<div id="transfer" hidden><progress id="progress" max="100" value="0"></progress><div id="transferLabel"></div><button id="cancelUpload" data-i18n="cancelUpload">Cancel upload</button></div>
<ul id="entries" aria-label="File list" data-i18n-aria="fileList"></ul><div class="toolbar"><button id="prev" data-i18n="prev">Previous</button><span id="page"></span><button id="next" data-i18n="next">Next</button></div></section>
<section id="datasetsPanel" aria-labelledby="tabDatasets" hidden><h2 data-i18n="datasetManager">Dataset manager</h2><p data-i18n="comingSoon">Coming soon.</p></section>
<footer><button id="exit" data-i18n="exit">Return to GPS logger</button></footer>
<dialog id="editor" aria-labelledby="editorTitle"><h2 id="editorTitle"></h2><div class="toolbar"><button id="save" data-i18n="save" disabled>Save changes</button><button id="closeEditor" data-i18n="close">Close</button><span id="dirty" role="status"></span></div><p id="editorNotice" role="status" aria-live="polite"></p><label for="text" class="sub" data-i18n="textNote">UTF-8 text (up to 64 KiB)</label><textarea id="text" spellcheck="false" autocapitalize="off" autocomplete="off" autocorrect="off"></textarea></dialog>
<dialog id="question" aria-labelledby="questionTitle"><h2 id="questionTitle"></h2><p id="questionText"></p><label for="questionInput" data-i18n="folderName" id="questionInputLabel" hidden>Folder name</label><input id="questionInput" type="text" maxlength="127" hidden><div class="toolbar"><button id="questionCancel" data-i18n="cancel">Cancel</button><button id="questionOk" data-i18n="confirm">Confirm</button></div></dialog>
<script>
'use strict';
const $=id=>document.getElementById(id);
const messages={
 ja:{webLanguage:'表示言語',pages:'画面選択',info:'情報',files:'ファイル',datasets:'データセット',fileManager:'ファイルマネージャ',datasetManager:'データセットマネージャ',networkNote:'このWi-Fiに接続中は、インターネットを利用できない場合があります。',settingsNote:'通常の設定は端末で変更してください。',refresh:'更新',currentFolder:'現在のフォルダー',parent:'上の階層へ',mkdir:'フォルダー作成',upload:'ファイルを追加',fileNote:'内容を開けるのはUTF-8のテキストのみです。ファイルは1つずつ転送します。',cancelUpload:'転送を中止',fileList:'ファイル一覧',prev:'前のページ',next:'次のページ',comingSoon:'準備中です。',exit:'GPSロガーモードへ戻る',save:'変更を保存',close:'閉じる',textNote:'UTF-8テキスト（最大64 KiB）',cancel:'取消',confirm:'確認',folderName:'フォルダー名',connecting:'接続しています…',ready:'SDカードを操作できます。',noSd:'SDカードが利用できません。端末を確認してください。',connectionFailed:'端末へ接続できません。Wi-Fi接続を確認してください。',badResponse:'端末からの応答を確認できません。再接続して更新してください。',operationFailed:'操作に失敗しました。',deviceError:'端末のエラー',unknownDate:'日時不明',folder:'フォルダー',edit:'開く・編集',remove:'削除',empty:'このフォルダーは空です。',page:'{n} ページ',deleteTitle:'削除しますか？',deleteFolder:'中のファイルとフォルダーもすべて削除します。',irreversible:'元には戻せません。',removing:'削除しています…',savingWait:'端末の保存完了を待っています',saveFailed:'保存に失敗しました。',saveUnknown:'保存結果を確認できません。再接続して一覧を更新してください。',disconnected:'接続が切れました。保存先・空き容量を確認し、再接続して一覧を更新してください。',uploadCancelled:'転送を中止しました。保存結果は一覧を更新して確認してください。',transferring:'転送しています…',savedNotice:'保存が完了しました。',openFailed:'ファイルを開けません。',invalidUtf8:'有効なUTF-8テキストではありません。',textTooLarge:'テキストは64 KiBまで保存できます。',saved:'保存しました',dirty:'未保存の変更',discardTitle:'変更を破棄しますか？',discardText:'未保存の内容を閉じます。',fileTooLarge:'ファイルは2 GiB未満にしてください。',folderExists:'同名のフォルダーがあります。',replaceTitle:'上書きしますか？',replaceText:'現在のファイルを置き換えます。',mkdirText:'現在のフォルダーの直下へ作成します。',nameRequired:'フォルダー名を入力してください。',exitTitle:'GPSロガーへ戻りますか？',exitText:'Wi-Fi接続を終了して端末を再起動します。',restarting:'再起動しています。端末の画面を確認してください。',version:'バージョン',mode:'モード',wifiMode:'Wi-Fi管理',total:'容量',free:'空き容量',notSelected:'未選択',deviceLanguage:'端末の表示言語',logInterval:'記録間隔',flushInterval:'SD保存間隔',screenTimeout:'消灯時間',seconds:'{n} 秒',never:'消灯しない',wordbookLanguage:'辞書の言語',datasetDefault:'データセットの既定値',brightness:'明るさ',volume:'音量',power:'省電力',powerOff:'しない',powerNormal:'通常',powerMax:'最大限',mounted:'利用可能',absent:'未挿入',sdError:'エラー',waiting:'確認中',safeToRemove:'取外し可能',fat32Required:'FAT32が必要'},
 en:{webLanguage:'Language',pages:'Pages',info:'Info',files:'Files',datasets:'Datasets',fileManager:'File manager',datasetManager:'Dataset manager',networkNote:'This Wi-Fi may not provide internet access.',settingsNote:'Change device settings on the device.',refresh:'Refresh',currentFolder:'Current folder',parent:'Parent folder',mkdir:'New folder',upload:'Upload file',fileNote:'Open and edit UTF-8 text files. Upload one file at a time.',cancelUpload:'Cancel upload',fileList:'File list',prev:'Previous',next:'Next',comingSoon:'Coming soon.',exit:'Return to GPS logger',save:'Save changes',close:'Close',textNote:'UTF-8 text (up to 64 KiB)',cancel:'Cancel',confirm:'Confirm',folderName:'Folder name',connecting:'Connecting…',ready:'SD card is ready.',noSd:'SD card unavailable. Check the device.',connectionFailed:'Cannot reach the device. Check your Wi-Fi connection.',badResponse:'Cannot read the device response. Reconnect and refresh.',operationFailed:'Operation failed.',deviceError:'Device error',unknownDate:'Unknown date',folder:'Folder',edit:'Open / edit',remove:'Delete',empty:'This folder is empty.',page:'Page {n}',deleteTitle:'Delete this item?',deleteFolder:'All files and folders inside will also be deleted.',irreversible:'This cannot be undone.',removing:'Deleting…',savingWait:'Waiting for the device to finish saving',saveFailed:'Save failed.',saveUnknown:'Cannot confirm the save result. Reconnect and refresh the file list.',disconnected:'Connection lost. Check the destination and free space, reconnect, then refresh.',uploadCancelled:'Upload cancelled. Refresh the file list to check the result.',transferring:'Transferring…',savedNotice:'File saved.',openFailed:'Cannot open the file.',invalidUtf8:'This is not valid UTF-8 text.',textTooLarge:'Text files must be 64 KiB or smaller.',saved:'Saved',dirty:'Unsaved changes',discardTitle:'Discard changes?',discardText:'Close without saving your changes.',fileTooLarge:'Files must be smaller than 2 GiB.',folderExists:'A folder with the same name already exists.',replaceTitle:'Replace this file?',replaceText:'The existing file will be replaced.',mkdirText:'Create a folder inside the current folder.',nameRequired:'Enter a folder name.',exitTitle:'Return to GPS logger?',exitText:'Disconnect Wi-Fi and restart the device.',restarting:'Restarting. Check the device screen.',version:'Version',mode:'Mode',wifiMode:'Wi-Fi management',total:'Capacity',free:'Free space',notSelected:'Not selected',deviceLanguage:'Device language',logInterval:'Log interval',flushInterval:'SD flush interval',screenTimeout:'Screen timeout',seconds:'{n} seconds',never:'Never',wordbookLanguage:'Wordbook language',datasetDefault:'Dataset default',brightness:'Brightness',volume:'Volume',power:'Power saving',powerOff:'Off',powerNormal:'Normal',powerMax:'Maximum',mounted:'Available',absent:'Not inserted',sdError:'Error',waiting:'Checking',safeToRemove:'Safe to remove',fat32Required:'FAT32 required'}
};
const languageKey='rgp.web.language';
let language=(navigator.language||'en').toLowerCase().startsWith('ja')?'ja':'en';
try{const saved=localStorage.getItem(languageKey);if(saved==='ja'||saved==='en')language=saved;}catch{}
const tr=(key,args={})=>messages[language][key].replace(/\{(\w+)\}/g,(_,name)=>String(args[name]??''));
let session='',directory='/',offset=0,more=false,busy=false,usable=false,connected=false,loaded=false,pageEntries=20,textMaxBytes=65536,uploadMaxBytes=2147483647,rows=[],xhr=null,original='',editPath='',editVersion='',statusData=null,savedText=false;
let currentNotice={key:'connecting',args:{},error:false,detail:''};
const bytes=n=>n>=1073741824?(n/1073741824).toFixed(2)+' GiB':n>=1048576?(n/1048576).toFixed(1)+' MiB':n>=1024?(n/1024).toFixed(1)+' KiB':n+' B';
function renderNotice(){const text=tr(currentNotice.key,currentNotice.args)+(currentNotice.detail?' '+currentNotice.detail:'');$('message').textContent=text;$('message').className=currentNotice.error?'error':'';if($('editor').open){$('editorNotice').textContent=text;$('editorNotice').className=currentNotice.error?'error':'';}}
function notice(key,error=false,detail=''){currentNotice={key,args:{},error,detail};renderNotice();}
function failure(e){notice(e.messageKey||'operationFailed',true,e.detail||(!e.messageKey?e.message:''));}
function error(key,detail=''){return Object.assign(new Error(tr(key)),{messageKey:key,detail});}
function updateControls(){for(const id of ['upload','mkdir','refresh','parent','prev','next','exit','save','closeEditor','refreshInfo','tabInfo','tabFiles','tabDatasets','language'])$(id).disabled=busy;for(const b of $('entries').querySelectorAll('button'))b.disabled=busy||!usable;if(!busy){$('parent').disabled=directory==='/'||!usable;$('prev').disabled=offset===0||!usable;$('next').disabled=!more||!usable;$('save').disabled=$('text').value===original;$('upload').disabled=$('mkdir').disabled=$('refresh').disabled=!usable;$('exit').disabled=!connected;}}
function lock(value){busy=value;updateControls();}
function join(name){return directory==='/'?'/'+name:directory+'/'+name;}
function dirtyLabel(){$('dirty').textContent=$('text').value!==original?tr('dirty'):savedText?tr('saved'):'';}
function stamp(date,time){if(!date)return tr('unknownDate');const p=n=>String(n).padStart(2,'0');return `${1980+(date>>9)}-${p((date>>5)&15)}-${p(date&31)} ${p(time>>11)}:${p((time>>5)&63)}`;}
function renderEntries(){
 $('entries').replaceChildren();
 for(const row of rows){
  const li=document.createElement('li');li.className='entry';
  const name=document.createElement(row.directory?'button':'strong');name.className=row.directory?'folder':'name';name.textContent=(row.directory?'▸ ':'')+row.name;if(row.directory)name.onclick=()=>list(join(row.name),0);li.append(name);
  const info=document.createElement('small');info.textContent=(row.directory?tr('folder'):bytes(row.size))+' · '+stamp(row.date,row.time);li.append(info);
  const actions=document.createElement('div');actions.className='actions';
  if(!row.directory&&/\.(ini|txt|csv|md|log|json)$/i.test(row.name)){const b=document.createElement('button');b.textContent=tr('edit');b.onclick=()=>openText(join(row.name));actions.append(b);}
  const del=document.createElement('button');del.textContent=tr('remove');del.onclick=()=>remove(join(row.name),row.directory);actions.append(del);li.append(actions);$('entries').append(li);
 }
 if(loaded&&!rows.length){const li=document.createElement('li');li.className='empty';li.textContent=tr('empty');$('entries').append(li);}
 $('page').textContent=loaded?tr('page',{n:Math.floor(offset/pageEntries)+1}):'';
}
function renderStatus(){
 $('statusValues').replaceChildren();if(!statusData)return;
 const s=statusData,sd=({'MOUNTED':'mounted','NO CARD':'absent','SD ERROR':'sdError','WAITING':'waiting','SAFE TO REMOVE':'safeToRemove','FAT32 REQUIRED':'fat32Required'})[s.sd];
 const values=[[tr('version'),s.version],[tr('mode'),tr('wifiMode')],['SSID',s.ssid],['SD',sd?tr(sd):s.sd],[tr('total'),bytes(s.total)],[tr('free'),bytes(s.free)],[tr('datasets'),s.dataset?String(s.dataset).padStart(2,'0'):tr('notSelected')],[tr('deviceLanguage'),s.language?'English':'日本語'],[tr('logInterval'),tr('seconds',{n:s.logInterval})],[tr('flushInterval'),tr('seconds',{n:s.flushInterval})],[tr('screenTimeout'),s.screenTimeout?tr('seconds',{n:s.screenTimeout}):tr('never')],[tr('wordbookLanguage'),s.wordbookLanguage||tr('datasetDefault')],[tr('brightness'),s.brightness+'%'],[tr('volume'),s.volume],[tr('power'),['powerOff','powerNormal','powerMax'][s.power-1]?tr(['powerOff','powerNormal','powerMax'][s.power-1]):s.power]];
 for(const [key,value]of values){const dt=document.createElement('dt'),dd=document.createElement('dd');dt.textContent=key;dd.textContent=value??'—';$('statusValues').append(dt,dd);}
}
function applyLanguage(){document.documentElement.lang=language;$('language').value=language;for(const el of document.querySelectorAll('[data-i18n]'))el.textContent=tr(el.dataset.i18n);for(const el of document.querySelectorAll('[data-i18n-aria]'))el.setAttribute('aria-label',tr(el.dataset.i18nAria));renderEntries();renderStatus();renderNotice();dirtyLabel();updateControls();}
$('language').onchange=()=>{language=$('language').value;try{localStorage.setItem(languageKey,language);}catch{}applyLanguage();};
async function ask(title,text,input=false){
 const d=$('question');$('questionTitle').textContent=tr(title);$('questionText').textContent=text;$('questionInput').hidden=$('questionInputLabel').hidden=!input;$('questionInput').value='';
 return new Promise(resolve=>{let done=false;const finish=value=>{if(done)return;done=true;d.close();resolve(value);};$('questionCancel').onclick=()=>finish(null);$('questionOk').onclick=()=>finish(input?$('questionInput').value:true);d.oncancel=e=>{e.preventDefault();finish(null);};d.showModal();(input?$('questionInput'):$('questionCancel')).focus();});
}
async function request(url,options={}){const r=await fetch(url,{cache:'no-store',...options});let body;try{body=await r.json();}catch{throw error('badResponse');}if(!r.ok||body.error)throw error(body.error?'deviceError':'operationFailed',body.error||'');return body;}
async function loadStatus(){
 if(busy)return;lock(true);
 try{const s=await request('/api/status');statusData=s;session=s.session;connected=true;usable=s.usable;pageEntries=s.pageEntries||20;textMaxBytes=s.textMaxBytes||65536;uploadMaxBytes=s.uploadMaxBytes||2147483647;renderStatus();notice(usable?'ready':'noSd',!usable,s.notice||'');}
 catch(e){connected=false;usable=false;notice('connectionFailed',true,e.detail||'');}
 finally{lock(false);}
}
async function selectPanel(panel){
 if(busy)return;
 for(const name of ['info','files','datasets']){const button=$('tab'+name[0].toUpperCase()+name.slice(1));$(name+'Panel').hidden=name!==panel;if(name===panel)button.setAttribute('aria-current','page');else button.removeAttribute('aria-current');}
 if(panel==='files'&&!loaded&&usable)await list('/',0);
}
for(const panel of ['info','files','datasets'])$('tab'+panel[0].toUpperCase()+panel.slice(1)).onclick=()=>selectPanel(panel);
$('refreshInfo').onclick=loadStatus;
async function list(path=directory,start=offset){
 if(busy||!usable)return;lock(true);
 try{const data=await request('/api/list?path='+encodeURIComponent(path)+'&offset='+start);if(data.error)throw error('deviceError',data.error);directory=path;offset=start;more=data.more;rows=data.entries;loaded=true;$('path').textContent=directory;renderEntries();notice('ready');}
 catch(e){failure(e);}finally{lock(false);}
}
async function mutate(uri,path){return request(uri,{method:'POST',headers:{'X-Session':session,'X-Path':encodeURIComponent(path)}});}
async function remove(path,folder){if(busy||!await ask('deleteTitle',path+'\n'+(folder?tr('deleteFolder')+'\n':'')+tr('irreversible')))return;lock(true);notice('removing');try{await mutate('/api/delete',path);}catch(e){failure(e);lock(false);return;}lock(false);await list(directory,0);}
function sendBlob(url,path,blob,replace,version){
 return new Promise((resolve,reject)=>{
  xhr=new XMLHttpRequest();xhr.open('POST',url);xhr.setRequestHeader('Content-Type','application/octet-stream');xhr.setRequestHeader('X-Session',session);xhr.setRequestHeader('X-Path',encodeURIComponent(path));xhr.setRequestHeader('X-Replace',replace?'1':'0');if(version)xhr.setRequestHeader('X-Version',version);
  xhr.upload.onprogress=e=>{if(e.lengthComputable){$('progress').value=e.total?100*e.loaded/e.total:100;$('transferLabel').textContent=bytes(e.loaded)+' / '+bytes(e.total)+(e.loaded===e.total?' — '+tr('savingWait'):'');}};
  xhr.onload=()=>{try{const data=JSON.parse(xhr.responseText);xhr.status>=200&&xhr.status<300&&!data.error?resolve(data):reject(error(data.error?'deviceError':'saveFailed',data.error||''));}catch{reject(error('saveUnknown'));}};
  xhr.onerror=()=>reject(error('disconnected'));xhr.onabort=()=>reject(error('uploadCancelled'));xhr.send(blob);
 });
}
async function transfer(url,path,blob,replace,version){lock(true);$('transfer').hidden=false;$('progress').value=0;$('transferLabel').textContent='';notice('transferring');try{await sendBlob(url,path,blob,replace,version);notice('savedNotice');return true;}catch(e){failure(e);return false;}finally{xhr=null;$('transfer').hidden=true;lock(false);}}
async function openText(path){
 if(busy)return;lock(true);
 try{const r=await fetch('/api/text?path='+encodeURIComponent(path),{cache:'no-store'});if(!r.ok){let detail='';try{detail=(await r.json()).error||'';}catch{}throw error('openFailed',detail);}const data=await r.arrayBuffer();try{original=new TextDecoder('utf-8',{fatal:true,ignoreBOM:true}).decode(data).replace(/\r\n?/g,'\n');}catch{throw error('invalidUtf8');}editVersion=r.headers.get('X-File-Version');editPath=path;savedText=false;$('editorNotice').textContent='';$('editorNotice').className='';$('editorTitle').textContent=path;$('text').value=original;dirtyLabel();$('editor').showModal();}
 catch(e){failure(e);}finally{lock(false);}
}
$('save').onclick=async()=>{
 const blob=new Blob([$('text').value],{type:'application/octet-stream'});if(blob.size>textMaxBytes){notice('textTooLarge',true);return;}
 $('text').disabled=true;const ok=await transfer('/api/save',editPath,blob,true,editVersion);$('text').disabled=false;
 if(ok){original=$('text').value;const data=new Uint8Array(await blob.arrayBuffer());let h=14695981039346656037n;for(const c of data)h=BigInt.asUintN(64,(h^BigInt(c))*1099511628211n);editVersion=h.toString(16).padStart(16,'0');savedText=true;dirtyLabel();updateControls();}
};
$('text').oninput=()=>{savedText=false;dirtyLabel();updateControls();};
async function closeEditor(){if(busy)return;if($('text').value!==original&&!await ask('discardTitle',tr('discardText')))return;$('editor').close();await list();}
$('closeEditor').onclick=closeEditor;$('editor').oncancel=e=>{e.preventDefault();closeEditor();};
$('upload').onclick=()=>$('file').click();
$('file').onchange=async()=>{
 const file=$('file').files[0];$('file').value='';if(!file||busy)return;if(file.size>uploadMaxBytes){notice('fileTooLarge',true);return;}
 const destination=join(file.name);let info;lock(true);try{info=await request('/api/info?path='+encodeURIComponent(destination));}catch(e){failure(e);return;}finally{lock(false);}
 if(info.directory){notice('folderExists',true);return;}let replace=false;if(info.exists){if(!await ask('replaceTitle',destination+'\n'+tr('replaceText')))return;replace=true;}
 if(await transfer('/api/upload',destination,file,replace))await list(directory,0);
};
$('mkdir').onclick=async()=>{const name=await ask('mkdir',tr('mkdirText'),true);if(name===null)return;if(!name.trim()||/[\/\\]/.test(name)){notice('nameRequired',true);return;}lock(true);try{await mutate('/api/mkdir',join(name));lock(false);await list(directory,0);}catch(e){failure(e);lock(false);}};
$('parent').onclick=()=>list(directory.slice(0,directory.lastIndexOf('/'))||'/',0);$('refresh').onclick=()=>list();$('prev').onclick=()=>list(directory,Math.max(0,offset-pageEntries));$('next').onclick=()=>list(directory,offset+pageEntries);$('cancelUpload').onclick=()=>{if(xhr)xhr.abort();};
$('exit').onclick=async()=>{if(busy||!await ask('exitTitle',tr('exitText')))return;lock(true);try{await request('/api/exit',{method:'POST',headers:{'X-Session':session}});notice('restarting');}catch(e){failure(e);lock(false);}};
window.addEventListener('beforeunload',e=>{if(busy||($('editor').open&&$('text').value!==original)){e.preventDefault();e.returnValue='';}});
applyLanguage();loadStatus();
</script></main></body></html>)RGP";
}
