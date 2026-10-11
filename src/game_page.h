#pragma once
#include <Arduino.h>

static const char kGamePage[] PROGMEM = R"HTML(<!doctype html>
<html lang="ja"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Coin Trail · SNES</title><style>
:root{color-scheme:dark;font-family:system-ui,sans-serif;background:#101a28;color:#edf4ff}*{box-sizing:border-box}
body{margin:0;padding:20px 12px}main{max-width:960px;margin:auto}header,.toolbar{display:flex;align-items:center;justify-content:space-between;gap:12px;flex-wrap:wrap}
h1{margin:0;font-size:28px}a{color:#a8e7ff}p{line-height:1.7;color:#b8c9da}.screen{position:relative;margin:20px 0;border:2px solid #3b5166;border-radius:14px;overflow:hidden;background:#81d8f4}
canvas{display:block;width:100%;height:auto;image-rendering:pixelated}#overlay{position:absolute;inset:0;display:grid;place-items:center;background:#10203399;text-align:center;padding:20px;font-size:clamp(16px,3vw,25px);font-weight:700}
#overlay[hidden]{display:none}button{font:inherit;border:1px solid #52697d;border-radius:10px;padding:10px 16px;background:#23374a;color:white;cursor:pointer}#play{background:#2e8865;border-color:#50bb8a}
#score{font-size:20px;font-weight:700;color:#ffdc6c}#connection{font-size:14px;color:#a4b8cb}#connection.live{color:#74efb5}.help{font-size:14px}kbd{background:#263b4e;border-radius:4px;padding:2px 5px}
</style></head><body><main>
<header><h1>Coin Trail</h1><a href="/">コントローラーモニター</a></header>
<p>走って、跳んで、コインを集める横スクロールアクション。</p>
<div class="toolbar"><span id="score" role="status">COINS 0</span><span id="connection">コントローラー接続中…</span></div>
<div class="screen"><canvas id="game" width="960" height="480" aria-label="横スクロールアクションゲーム"></canvas><div id="overlay">「プレイ・音を有効にする」を押して開始</div></div>
<div class="toolbar"><div><button id="play">プレイ・音を有効にする</button> <button id="reset">最初から</button></div><button id="sound" aria-pressed="false">音：ON</button></div>
<p class="help">SNES：十字キー左右＝移動 / B・A＝ジャンプ / Y＝ダッシュ / START＝一時停止 / SELECT＝最初から<br>
キーボード：← →＝移動 / Space・Z＝ジャンプ / Shift＝ダッシュ / Enter＝一時停止<br>
穴に落ちると近くの足場から再開します。敵やゴール旗はありません。コインを集めながら何周でも遊べます。</p>
<p class="help">音はこの端末のスピーカーから鳴ります。各端末のゲーム進行は独立し、同じSNES入力を受け取ります。</p>
<script>
(() => {
'use strict';
const canvas=document.getElementById('game'),ctx=canvas.getContext('2d');
const overlay=document.getElementById('overlay'),scoreLabel=document.getElementById('score');
const playButton=document.getElementById('play'),connection=document.getElementById('connection');
const W=960,H=480,LENGTH=9600,GROUND=394,STEP=1/60;
const JUMP=(1<<0)|(1<<8),LEFT=1<<6,RIGHT=1<<7,RUN=1<<1,START=1<<3;
let player,platforms,coins,particles=[],score=0,lap=1,camera=0,checkpoint=60;
let started=false,paused=false,remote=0,keyboard=0,jumpBuffer=0,coyote=0;
let audio=null,muted=false,socket=null,retry=null,seq=null,received=0,connected=false;
let accumulator=0,lastFrame=0,worldTime=0;
function buildWorld(){
  platforms=[];coins=[];
  for(let base=0;base<LENGTH;base+=960){
    platforms.push({x:base,y:GROUND,w:700,h:H-GROUND},{x:base+780,y:GROUND,w:180,h:H-GROUND});
    platforms.push({x:base+300,y:310,w:150,h:24},{x:base+520,y:242,w:150,h:24},{x:base+704,y:325,w:65,h:20});
    for(let x=140;x<=260;x+=40)coins.push({x:base+x,y:GROUND-28,taken:false});
    for(let x=325;x<=425;x+=50)coins.push({x:base+x,y:282,taken:false});
    for(let x=545;x<=645;x+=50)coins.push({x:base+x,y:214,taken:false});
    coins.push({x:base+735,y:290,taken:false},{x:base+840,y:GROUND-28,taken:false});
  }
}
function respawn(){player={x:checkpoint,y:GROUND-34,w:24,h:34,vx:0,vy:0,grounded:true,face:1};jumpBuffer=0;coyote=0;}
function reset(){score=0;lap=1;checkpoint=60;camera=0;particles=[];buildWorld();respawn();scoreLabel.textContent='COINS 0';}
function control(mask,isRemote){
  const before=remote|keyboard;
  if(isRemote)remote=mask;else keyboard=mask;
  const after=remote|keyboard;
  if((after&JUMP)&&!(before&JUMP))jumpBuffer=.12;
  // USB Enter and Wi-Fi START share one rising edge while either is held.
  if(started&&(after&START)&&!(before&START))paused=!paused;
  if(started&&isRemote&&(mask&4)&&!(before&4))reset();
}
function unlockAudio(){
  try{const AC=window.AudioContext||window.webkitAudioContext;if(!AC)return;
    if(!audio)audio=new AC();audio.resume().catch(()=>{});
  }catch{document.getElementById('sound').textContent='音：利用できません';}
}
function coinSound(){
  if(muted||!audio||audio.state!=='running')return;
  const now=audio.currentTime,osc=audio.createOscillator(),gain=audio.createGain();
  osc.type='square';osc.frequency.setValueAtTime(880,now);osc.frequency.setValueAtTime(1320,now+.075);
  gain.gain.setValueAtTime(.0001,now);gain.gain.exponentialRampToValueAtTime(.06,now+.006);
  gain.gain.exponentialRampToValueAtTime(.0001,now+.19);
  osc.connect(gain);gain.connect(audio.destination);osc.start(now);osc.stop(now+.2);
  osc.onended=()=>{osc.disconnect();gain.disconnect();};
}
function overlap(a,b){return a.x<b.x+b.w&&a.x+a.w>b.x&&a.y<b.y+b.h&&a.y+a.h>b.y;}
function update(dt){
  worldTime+=dt;
  const input=remote|keyboard,dir=Boolean(input&RIGHT)-Boolean(input&LEFT),speed=input&RUN?310:205;
  player.vx=dir*speed;if(dir)player.face=dir;
  coyote=player.grounded?.10:Math.max(0,coyote-dt);jumpBuffer=Math.max(0,jumpBuffer-dt);
  if(jumpBuffer>0&&coyote>0){player.vy=-540;player.grounded=false;coyote=0;jumpBuffer=0;}
  if(!(input&JUMP)&&player.vy<-230)player.vy=-230;
  player.vy=Math.min(720,player.vy+1500*dt);
  player.x+=player.vx*dt;
  for(const tile of platforms)if(overlap(player,tile)){
    if(player.vx>0)player.x=tile.x-player.w;else if(player.vx<0)player.x=tile.x+tile.w;
  }
  player.x=Math.max(0,player.x);
  player.y+=player.vy*dt;player.grounded=false;
  for(const tile of platforms)if(overlap(player,tile)){
    if(player.vy>0){player.y=tile.y-player.h;player.grounded=true;player.vy=0;
      if(tile.y===GROUND)checkpoint=Math.max(tile.x+10,Math.min(player.x,tile.x+tile.w-player.w-10));
    }else if(player.vy<0){player.y=tile.y+tile.h;player.vy=0;}
  }
  if(player.y>H+100)respawn();
  if(player.x>LENGTH-player.w){lap++;checkpoint=60;buildWorld();respawn();camera=0;}
  for(const coin of coins)if(!coin.taken&&overlap(player,{x:coin.x-10,y:coin.y-13,w:20,h:26})){
    coin.taken=true;score++;scoreLabel.textContent='COINS '+score;coinSound();
    for(let i=0;i<8;i++)particles.push({x:coin.x,y:coin.y,vx:Math.cos(i*Math.PI/4)*75,vy:Math.sin(i*Math.PI/4)*75,life:.4});
  }
  for(const p of particles){p.x+=p.vx*dt;p.y+=p.vy*dt;p.life-=dt;}particles=particles.filter(p=>p.life>0);
  camera=Math.max(0,Math.min(LENGTH-W,player.x-W*.35));
}
function rect(x,y,w,h,color){ctx.fillStyle=color;ctx.fillRect(Math.round(x),Math.round(y),w,h);}
function draw(){
  rect(0,0,W,H,'#8ddcf3');rect(0,285,W,110,'#b4e2ca');
  for(let i=-1;i<8;i++){
    const x=i*240-(camera*.20)%240;ctx.fillStyle='#79c6ab';ctx.beginPath();ctx.moveTo(x,GROUND);ctx.lineTo(x+120,230);ctx.lineTo(x+240,GROUND);ctx.fill();
    const cloud=i*270-(camera*.12)%270;rect(cloud,86,88,20,'#eefcfb');rect(cloud+20,66,44,20,'#eefcfb');
  }
  ctx.save();ctx.translate(-Math.round(camera),0);
  for(const tile of platforms){if(tile.x+tile.w<camera||tile.x>camera+W)continue;
    rect(tile.x,tile.y,tile.w,tile.h,'#b77c4c');rect(tile.x,tile.y,tile.w,8,'#408d61');
    for(let x=tile.x+4;x<tile.x+tile.w;x+=28){rect(x,tile.y+13,16,4,'#d99d65');}
  }
  for(const coin of coins){if(coin.taken||coin.x<camera-16||coin.x>camera+W+16)continue;
    const bob=Math.sin(worldTime*5+coin.x)*2;ctx.fillStyle='#fff1a2';ctx.beginPath();ctx.ellipse(coin.x,coin.y+bob,9,12,0,0,Math.PI*2);ctx.fill();
    rect(coin.x-2,coin.y-7+bob,4,14,'#e3aa32');
  }
  for(const p of particles)rect(p.x,p.y,4,4,'#fff6a0');
  const x=player.x,y=player.y,walk=player.grounded&&player.vx?Math.sin(worldTime*18)*3:0;
  rect(x+3,y+1,18,12,'#214b66');rect(x+1,y+11,22,17,'#1a9b93');rect(x+5,y+5,14,8,'#ffdeb1');
  rect(x+(player.face>0?15:7),y+7,3,3,'#172f44');rect(x,y+13,24,4,'#ff8660');
  rect(x+3,y+27,7,7+Math.round(walk),'#214b66');rect(x+14,y+27,7,7-Math.round(walk),'#214b66');
  ctx.restore();ctx.fillStyle='#245273';ctx.font='bold 16px system-ui';ctx.fillText('COIN TRAIL  /  LAP '+lap,22,30);
  let message='';
  if(!started)message='「プレイ・音を有効にする」を押して開始';
  else if(!connected)message='コントローラー未接続 · キーボードでも遊べます';
  if(paused||document.hidden)message='一時停止 · START または「再開」';
  // Disconnection freezes remote motion; keyboard remains available.
  overlay.hidden=started&&!paused;overlay.textContent=message;
  if(started&&!paused&&!connected){ctx.fillStyle='#173347';ctx.font='14px system-ui';ctx.fillText('コントローラー未接続 · キーボード操作可',22,H-18);}
  playButton.textContent=started?(paused?'再開':'一時停止'):'プレイ・音を有効にする';
  canvas.dataset.x=player.x.toFixed(1);canvas.dataset.y=player.y.toFixed(1);canvas.dataset.coins=String(score);
}
function frame(time){
  const dt=lastFrame?Math.min(.05,(time-lastFrame)/1000):0;lastFrame=time;
  if(started&&!paused&&!document.hidden){accumulator+=dt;while(accumulator>=STEP){update(STEP);accumulator-=STEP;}}
  else accumulator=0;
  draw();requestAnimationFrame(frame);
}
playButton.onclick=()=>{unlockAudio();if(!started){started=true;paused=false;}else paused=!paused;playButton.blur();};
document.getElementById('reset').onclick=event=>{unlockAudio();reset();event.currentTarget.blur();};
document.getElementById('sound').onclick=event=>{unlockAudio();muted=!muted;event.currentTarget.textContent='音：'+(muted?'OFF':'ON');event.currentTarget.setAttribute('aria-pressed',String(muted));event.currentTarget.blur();};
const keyMap={ArrowLeft:LEFT,ArrowRight:RIGHT,Space:1,KeyZ:1,ShiftLeft:RUN,ShiftRight:RUN,Enter:START};
const held=new Set();
function syncKeys(){let mask=0;for(const key of held)mask|=keyMap[key]||0;control(mask,false);}
window.addEventListener('keydown',event=>{
  if(event.target.closest('button,a,input'))return;
  if(!(event.code in keyMap))return;
  event.preventDefault();
  const startGame=event.code==='Enter'&&!event.repeat&&!started;
  if(event.code==='Enter'&&!event.repeat)unlockAudio();
  held.add(event.code);syncKeys();
  // Sync first so the Enter that starts the game cannot also pause it.
  if(startGame){started=true;paused=false;}
});
window.addEventListener('keyup',event=>{if(event.code in keyMap){event.preventDefault();held.delete(event.code);syncKeys();}});
window.addEventListener('blur',()=>{held.clear();control(0,false);});
document.addEventListener('visibilitychange',()=>{if(document.hidden){held.clear();control(0,false);paused=started;jumpBuffer=0;}lastFrame=0;});
function offline(){connected=false;control(0,true);jumpBuffer=0;connection.classList.remove('live');connection.textContent='切断 · 自動再接続中';}
function connect(){
  clearTimeout(retry);seq=null;const current=new WebSocket('ws://'+location.hostname+':81/');socket=current;received=Date.now();
  current.onmessage=event=>{
    if(socket!==current)return;let data;try{data=JSON.parse(event.data);}catch{return;}
    if(!Number.isInteger(data.mask)||data.mask<0||data.mask>4095||!Number.isInteger(data.seq)||data.seq<0||data.seq>4294967295)return;
    received=Date.now();const initial=seq===null,delta=initial?1:(data.seq-seq)>>>0;
    if(delta>0&&delta<2147483648){seq=data.seq;if(initial){remote=data.mask;jumpBuffer=0;}else control(data.mask,true);}
    connected=true;connection.classList.add('live');connection.textContent='SNES 接続済み';
  };
  current.onerror=()=>current.close();current.onclose=()=>{if(socket===current){offline();retry=setTimeout(connect,1000);}};
}
setInterval(()=>{if(socket&&Date.now()-received>5000){offline();const old=socket;socket=null;old.close();connect();}},1000);
window.addEventListener('pagehide',()=>{clearTimeout(retry);const old=socket;socket=null;if(old)old.close();});
window.addEventListener('pageshow',event=>{if(event.persisted)connect();});
reset();connect();requestAnimationFrame(frame);
})();
</script></main></body></html>)HTML";
