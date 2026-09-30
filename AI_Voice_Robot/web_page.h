// 設定網頁（手機 / 電腦瀏覽器開啟機器人 IP 即可修改 Wi-Fi、API Key、雲端硬碟網址）
#pragma once

static const char WEB_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="zh-Hant"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>小柯設定</title>
<style>
:root{--bg:#0b1020;--card:#151c33;--line:#2a3558;--text:#e8ecff;--muted:#8d97bd;--accent:#39e6ff;--blue:#4a96ff;--ok:#3ddc84;--err:#ff5d73}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:16px/1.5 system-ui,-apple-system,"Noto Sans TC","Microsoft JhengHei",sans-serif}
main{max-width:520px;margin:0 auto;padding:20px 16px 40px}
header{display:flex;align-items:center;gap:12px;margin-bottom:16px}
.face{width:52px;height:44px;border-radius:18px;background:var(--blue);display:grid;place-items:center;flex:none}
.visor{width:40px;height:30px;border-radius:12px;background:#10163a;display:flex;align-items:center;justify-content:center;gap:9px}
.visor i{width:6px;height:9px;border-radius:3px;background:var(--accent)}
h1{font-size:20px;margin:0}
.sub{color:var(--muted);font-size:13px}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:16px;margin-bottom:14px}
label{display:block;font-size:14px;color:var(--muted);margin:12px 0 6px}
label:first-child{margin-top:0}
.row{display:flex;gap:8px}
select,input{width:100%;min-width:0;padding:11px 12px;border-radius:10px;border:1px solid var(--line);background:#0e1429;color:var(--text);font-size:16px}
select:focus,input:focus{outline:2px solid var(--blue);border-color:transparent}
button{padding:11px 14px;border-radius:10px;border:1px solid var(--line);background:#1d2749;color:var(--text);font-size:15px;cursor:pointer;white-space:nowrap}
button.primary{width:100%;background:var(--blue);border:none;font-weight:600;font-size:17px;padding:13px}
button:disabled{opacity:.6}
.hint{font-size:12px;color:var(--muted);margin-top:4px}
.status{display:grid;grid-template-columns:auto 1fr;gap:4px 12px;font-size:14px}
.status span:nth-child(odd){color:var(--muted)}
#msg{margin-top:12px;font-size:15px;min-height:1.5em}
.ok{color:var(--ok)}.err{color:var(--err)}
.hidden{display:none}
</style></head><body><main>
<header><div class="face"><div class="visor"><i></i><i></i></div></div>
<div><h1 id="title">小柯設定</h1><div class="sub">Wi-Fi 與 AI 金鑰設定</div></div></header>

<div class="card status" id="status"><span>狀態</span><span>讀取中…</span></div>

<form id="f" class="card" autocomplete="off">
  <label for="ssidSel">Wi-Fi 名稱（只支援 2.4GHz）</label>
  <div class="row">
    <select id="ssidSel"><option value="">掃描中…</option></select>
    <button type="button" id="scanBtn">重新掃描</button>
  </div>
  <input id="ssidManual" class="hidden" placeholder="輸入 Wi-Fi 名稱" style="margin-top:8px">

  <label for="pass">Wi-Fi 密碼</label>
  <div class="row"><input id="pass" type="password" placeholder="留空＝不變更">
    <button type="button" data-eye="pass">👁</button></div>
  <div class="hint" id="passHint"></div>

  <label for="apikey">OpenAI API Key</label>
  <div class="row"><input id="apikey" type="password" placeholder="留空＝不變更">
    <button type="button" data-eye="apikey">👁</button></div>
  <div class="hint">目前：<span id="keyNow">—</span>　到 platform.openai.com/api-keys 取得</div>

  <label for="gdrive">Google 雲端硬碟上傳網址（選填）</label>
  <input id="gdrive" placeholder="https://script.google.com/macros/s/…/exec">
  <div class="hint">部署 GoogleDrive_Upload.gs 後取得；留空＝拍照不上傳</div>

  <label for="playlist">YouTube 創作歌單（說「唱首歌」隨機選歌）</label>
  <div class="row"><input id="playlist" placeholder="https://www.youtube.com/playlist?list=…">
    <button type="button" id="refreshBtn">更新歌單</button></div>
  <div class="hint">目前歌單：<span id="songCount">—</span> 首</div>

  <div style="margin-top:18px"><button class="primary" id="saveBtn" type="submit">儲存並重新啟動</button></div>
  <div id="msg"></div>
</form>

<script>
const $=id=>document.getElementById(id);
let cur={};
function bars(r){return r>-55?'▂▄▆█':r>-67?'▂▄▆':r>-78?'▂▄':'▂'}
async function loadStatus(){
  try{
    cur=await (await fetch('/status')).json();
    $('title').textContent=cur.name+'設定';
    const rows=[['模式',cur.mode==='ap'?'設定模式（尚未連上 Wi-Fi）':'已連線'],['Wi-Fi',cur.ssid||'（未設定）'],
      ['IP',cur.ip],['訊號',cur.rssi?cur.rssi+' dBm':'—']];
    $('status').innerHTML=rows.map(r=>`<span>${r[0]}</span><span>${r[1]}</span>`).join('');
    $('keyNow').textContent=cur.apiKey||'（未設定）';
    $('gdrive').value=cur.gdrive||'';
    $('playlist').value=cur.playlist||'';$('songCount').textContent=cur.songs;
    $('passHint').textContent=cur.hasPass?'已儲存密碼；只在要更換時填寫':'';
  }catch(e){$('status').innerHTML='<span>狀態</span><span class="err">無法讀取</span>'}
}
async function scan(){
  const sel=$('ssidSel');$('scanBtn').disabled=true;
  sel.innerHTML='<option value="">掃描中…（約 3 秒）</option>';
  try{
    const list=await (await fetch('/scan')).json();
    sel.innerHTML='';
    list.forEach(n=>{const o=document.createElement('option');o.value=n.ssid;
      o.textContent=`${bars(n.rssi)}  ${n.ssid}${n.secure?' 🔒':''}`;sel.appendChild(o)});
    if(cur.ssid&&!list.some(n=>n.ssid===cur.ssid)){const o=document.createElement('option');
      o.value=cur.ssid;o.textContent=`${cur.ssid}（目前設定，未掃到）`;sel.prepend(o)}
    sel.insertAdjacentHTML('beforeend','<option value="__manual">✏️ 手動輸入…</option>');
    if(cur.ssid)sel.value=cur.ssid;
  }catch(e){sel.innerHTML='<option value="__manual">掃描失敗，請手動輸入</option>'}
  $('scanBtn').disabled=false;sel.onchange();
}
$('ssidSel').onchange=()=>{$('ssidManual').classList.toggle('hidden',$('ssidSel').value!=='__manual')};
$('scanBtn').onclick=scan;
document.querySelectorAll('[data-eye]').forEach(b=>b.onclick=()=>{const i=$(b.dataset.eye);i.type=i.type==='password'?'text':'password'});
$('f').onsubmit=async e=>{
  e.preventDefault();
  const ssid=$('ssidSel').value==='__manual'?$('ssidManual').value.trim():$('ssidSel').value;
  const msg=$('msg');
  if(!ssid){msg.className='err';msg.textContent='請選擇或輸入 Wi-Fi 名稱';return}
  const key=$('apikey').value.trim();
  if(key&&!key.startsWith('sk-')){msg.className='err';msg.textContent='API Key 應該以 sk- 開頭';return}
  $('saveBtn').disabled=true;msg.className='';msg.textContent='儲存中…';
  try{
    const body=new URLSearchParams({ssid,pass:$('pass').value,apikey:key,gdrive:$('gdrive').value.trim(),playlist:$('playlist').value.trim()});
    const r=await fetch('/save',{method:'POST',body});
    const t=await r.text();
    if(!r.ok)throw new Error(t);
    msg.className='ok';msg.textContent='✓ 已儲存！機器人重新啟動中，約 20 秒後可重新整理本頁（IP 可能改變，請看機器人螢幕）';
  }catch(err){msg.className='err';msg.textContent='儲存失敗：'+err.message;$('saveBtn').disabled=false}
};
$('refreshBtn').onclick=async()=>{
  const b=$('refreshBtn');b.disabled=true;b.textContent='更新中…';
  try{const r=await fetch('/refresh',{method:'POST'});const t=await r.text();
    $('songCount').textContent=r.ok?t:'失敗';}catch(e){$('songCount').textContent='失敗'}
  b.disabled=false;b.textContent='更新歌單';
};
loadStatus().then(scan);
</script></main></body></html>)HTML";
