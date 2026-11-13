#pragma once
#include <string_view>
namespace geoputer::wifi {
inline bool canonicalHost(std::string_view host,std::string_view ip) {
    return host==ip || (host.size()==ip.size()+3 && host.substr(0,ip.size())==ip && host.substr(ip.size())==":80");
}
inline bool captiveProbe(std::string_view path) {
    for(auto probe:{"/generate_204","/gen_204","/hotspot-detect.html","/library/test/success.html",
                    "/connecttest.txt","/ncsi.txt","/redirect","/canonical.html","/success.txt"})
        if(path==probe)return true;
    return false;
}
inline constexpr char PortalPage[]=R"PORTAL(<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Geoputer</title>
<style>:root{color-scheme:light dark}*{box-sizing:border-box}body{font:16px/1.6 system-ui,sans-serif;margin:0;padding:16px}main{max-width:540px;margin:auto}header{display:flex;gap:12px;align-items:center;justify-content:space-between;flex-wrap:wrap}h1{font-size:22px;margin:0}h2{font-size:19px}a{display:block;min-height:44px;padding:10px 0;margin:12px 0}select{font:inherit;min-height:44px}code{display:block;overflow-wrap:anywhere;border:1px solid #999;padding:10px}a:focus-visible,select:focus-visible{outline:3px solid #528bcc}</style></head><body>
<main><header><h1>Geoputer</h1><label><span data-i18n="language">Language</span> <select id="language"><option value="ja">日本語</option><option value="en">English</option></select></label></header><h2 data-i18n="connected">Connected to Wi-Fi</h2><p data-i18n="intro">Manage files on the SD card. This Wi-Fi does not provide internet access.</p><a href="/" target="_blank" rel="noopener" data-i18n="open">Open manager</a><p data-i18n="help">If the manager or file picker does not open, use your usual browser at the URL below, or scan WEB QR on the device.</p><code id="url"></code><p data-i18n="stay">Keep your Wi-Fi connection to Geoputer when closing this guide.</p></main>
<script>
'use strict';
const messages={ja:{language:'表示言語',connected:'Wi-Fiに接続しました',intro:'SDカードのファイルを管理できます。このWi-Fiはインターネット接続用ではありません。',open:'管理画面を開く',help:'開けない場合やファイル選択が使えない場合は、普段のブラウザーで次のURLを開いてください。本体の「WEB QR」を読み取る方法でも開けます。',stay:'この案内画面を閉じる際も、GeoputerへのWi-Fi接続を維持してください。'},en:{language:'Language',connected:'Connected to Wi-Fi',intro:'Manage files on the SD card. This Wi-Fi does not provide internet access.',open:'Open manager',help:'If the manager or file picker does not open, use your usual browser at the URL below, or scan WEB QR on the device.',stay:'Keep your Wi-Fi connection to Geoputer when closing this guide.'}};
const key='rgp.web.language';let language=(navigator.language||'en').toLowerCase().startsWith('ja')?'ja':'en';try{const saved=localStorage.getItem(key);if(saved==='ja'||saved==='en')language=saved;}catch{}
function render(){document.documentElement.lang=language;document.getElementById('language').value=language;for(const el of document.querySelectorAll('[data-i18n]'))el.textContent=messages[language][el.dataset.i18n];}
document.getElementById('language').onchange=e=>{language=e.target.value;try{localStorage.setItem(key,language);}catch{}render();};document.getElementById('url').textContent=location.origin+'/';render();
</script></body></html>)PORTAL";
}
