#pragma once
#include <Arduino.h>

// Self-contained page: no internet, CDN, or filesystem upload required.
static const char kWebPage[] PROGMEM = R"HTML(<!doctype html>
<html lang="ja">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>SNES Controller Monitor</title>
<style>
:root{color-scheme:dark;font-family:system-ui,sans-serif;background:#10151e;color:#edf2f8}
*{box-sizing:border-box}body{margin:0;padding:24px 16px;min-height:100vh;display:grid;place-items:center}
main{width:min(100%,820px)}header{display:flex;align-items:center;justify-content:space-between;gap:12px;flex-wrap:wrap}
h1{font-size:clamp(20px,4vw,30px);margin:0}p{color:#aeb9c9;line-height:1.7}
#connection{border:1px solid #677083;border-radius:24px;padding:7px 14px;font-size:14px}
#connection.live{color:#74f5ad;border-color:#3f9e6b}
.panel{background:#19212e;border:1px solid #303c4e;border-radius:22px;margin:22px 0;padding:16px 8px}
svg{display:block;width:100%;height:auto}.shell{fill:#c2c8d3}.recess{fill:#a6aebb}
.key{fill:#424c5f;stroke:#313b4a;stroke-width:1.2}.key.pressed{fill:#4fe58d;stroke:#14773d}
.letter{font:700 8px system-ui;fill:white;text-anchor:middle;dominant-baseline:middle;pointer-events:none}
.label{font:5px system-ui;fill:#263041;text-anchor:middle}.arrow{fill:#eaf0f8;pointer-events:none}
.pressed .arrow,.pressed .letter{fill:#10271a}.pad-center{fill:#424c5f}
.disconnected svg{opacity:.5}#buttons{color:#74f5ad;min-height:28px;font-weight:600;overflow-wrap:anywhere}
footer{font-size:13px;color:#aeb9c9;line-height:1.8}code{color:#dbe8ff}
</style>
</head>
<body><main>
<header><h1>SNES Controller Monitor</h1><span id="connection" role="status">接続中…</span></header>
<p>M5StickS3 · 押しているボタンを緑色で表示します。 <a href="/game" style="color:#74f5ad">横スクロールゲームで遊ぶ →</a></p>
<div class="panel disconnected" id="panel">
<svg viewBox="0 15 240 105" role="img" aria-label="SNESコントローラーのボタン配置">
<g data-bit="10" aria-label="L"><rect class="key" x="20" y="21" width="58" height="16" rx="5"/><text class="letter" x="49" y="29">L</text></g>
<g data-bit="11" aria-label="R"><rect class="key" x="162" y="21" width="58" height="16" rx="5"/><text class="letter" x="191" y="29">R</text></g>
<rect class="shell" x="8" y="36" width="224" height="76" rx="32"/>
<circle class="recess" cx="45" cy="74" r="30"/><circle class="recess" cx="195" cy="74" r="33"/>
<rect class="pad-center" x="39" y="68" width="12" height="12"/>
<g data-bit="4" aria-label="UP"><rect class="key" x="39" y="51" width="12" height="17"/><path class="arrow" d="M45 54 L41 60 H49 Z"/></g>
<g data-bit="5" aria-label="DOWN"><rect class="key" x="39" y="80" width="12" height="17"/><path class="arrow" d="M45 94 L41 88 H49 Z"/></g>
<g data-bit="6" aria-label="LEFT"><rect class="key" x="22" y="68" width="17" height="12"/><path class="arrow" d="M25 74 L31 70 V78 Z"/></g>
<g data-bit="7" aria-label="RIGHT"><rect class="key" x="51" y="68" width="17" height="12"/><path class="arrow" d="M65 74 L59 70 V78 Z"/></g>
<text class="label" x="94" y="70">SELECT</text><text class="label" x="136" y="70">START</text>
<g data-bit="2" aria-label="SELECT"><rect class="key" x="81" y="78" width="26" height="10" rx="5"/></g>
<g data-bit="3" aria-label="START"><rect class="key" x="123" y="78" width="26" height="10" rx="5"/></g>
<g data-bit="9" aria-label="X"><circle class="key" cx="195" cy="53" r="10"/><text class="letter" x="195" y="53">X</text></g>
<g data-bit="1" aria-label="Y"><circle class="key" cx="174" cy="74" r="10"/><text class="letter" x="174" y="74">Y</text></g>
<g data-bit="8" aria-label="A"><circle class="key" cx="216" cy="74" r="10"/><text class="letter" x="216" y="74">A</text></g>
<g data-bit="0" aria-label="B"><circle class="key" cx="195" cy="95" r="10"/><text class="letter" x="195" y="95">B</text></g>
</svg></div>
<p id="buttons">接続を待っています</p>
<footer>Wi-Fi：<code>SNES-Monitor</code> · 最大2台（各端末で1画面）<br>
接続が切れた場合は自動的に再接続します。画面のボタンは表示専用です。</footer>
<script>
(() => {
  const names=['B','Y','SELECT','START','UP','DOWN','LEFT','RIGHT','A','X','L','R'];
  const keys=[...document.querySelectorAll('[data-bit]')];
  const status=document.getElementById('connection');
  const panel=document.getElementById('panel');
  const buttons=document.getElementById('buttons');
  let socket=null, retry=null, sequence=null, lastReceived=0;
  function render(mask){
    keys.forEach(key=>{
      const down=Boolean(mask & (1 << Number(key.dataset.bit)));
      key.classList.toggle('pressed',down);
      key.querySelector('.key').classList.toggle('pressed',down);
      key.setAttribute('aria-label',names[Number(key.dataset.bit)]+(down?' 押下':' 解放'));
    });
    const pressed=names.filter((_,i)=>mask & (1 << i));
    buttons.textContent=pressed.length?'押下中：'+pressed.join(' · '):'ボタンは押されていません';
  }
  function offline(text){
    render(0); panel.classList.add('disconnected');
    status.classList.remove('live');status.textContent=text;
    buttons.textContent='状態を確認できません';
  }
  function connect(){
    clearTimeout(retry);sequence=null;status.textContent='接続中…';
    const current=new WebSocket('ws://'+location.hostname+':81/');
    socket=current;lastReceived=Date.now();
    current.onmessage=event=>{
      if(socket!==current)return;
      let state;try{state=JSON.parse(event.data);}catch{return;}
      if(!Number.isInteger(state.mask)||state.mask<0||state.mask>4095||
         !Number.isInteger(state.seq)||state.seq<0||state.seq>4294967295)return;
      lastReceived=Date.now();
      // Initial snapshot may arrive before older queued events. Ignore those.
      const delta=sequence===null?1:(state.seq-sequence)>>>0;
      if(delta>0 && delta<2147483648){sequence=state.seq;render(state.mask);}
      panel.classList.remove('disconnected');status.classList.add('live');status.textContent='接続済み';
    };
    current.onerror=()=>current.close();
    current.onclose=()=>{
      if(socket!==current)return;
      offline('切断 · 再接続中');retry=setTimeout(connect,1000);
    };
  }
  setInterval(()=>{
    if(socket && Date.now()-lastReceived>5000){
      offline('通信が途切れています');
      const old=socket;socket=null;old.close();connect();
    }
  },1000);
  window.addEventListener('pagehide',()=>{clearTimeout(retry);const old=socket;socket=null;if(old)old.close();});
  window.addEventListener('pageshow',event=>{if(event.persisted)connect();});
  connect();
})();
</script></main></body></html>)HTML";
