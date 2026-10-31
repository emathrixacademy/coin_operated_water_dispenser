// The phone / laptop screen, served by the ESP32 at http://192.168.4.1
//
// Mirrors the LCD screen for screen and button for button (ui_lcd.cpp): it
// draws only what the Mega reports (PG / VL / FT / LV / SD..HE) and sends
// touches back as "EV <event> [payload]". The numbers below are the enum
// values in the main project -- state_t (types.h), hmi_event_t (hmi.h),
// fault_t (types.h). If those enums change, change this page and the LCD too.

#pragma once
#include <pgmspace.h>

static const char PAGE_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<title>Water Vendo</title>
<style>
:root{--bg:#0c1824;--card:#162638;--line:#304a5c;--ink:#ecf3f8;--mute:#8ca3b5;--acc:#1e88e5;
--water:#26c6da;--ok:#2ecc71;--warn:#f5a623;--bad:#e5533d;--dim:#2a3b4d}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html,body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.35 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
.app{max-width:560px;margin:0 auto;min-height:100vh;display:flex;flex-direction:column}
header{display:flex;align-items:center;gap:10px;padding:12px 16px;background:var(--card);border-bottom:1px solid var(--line)}
.brand{display:flex;align-items:center;gap:8px;font-weight:800;letter-spacing:.04em;font-size:17px}
.drop{width:14px;height:18px;background:var(--water);border-radius:50% 50% 50% 50%/60% 60% 40% 40%;transform:rotate(180deg)}
.chip{font-size:12px;font-weight:700;color:var(--bg);background:var(--warn);border-radius:99px;padding:3px 10px;display:none;white-space:nowrap}
.chip.show{display:inline-block}
.tank{margin-left:auto;display:flex;align-items:center;gap:8px;font-size:13px;color:var(--mute)}
.bar{width:76px;height:12px;border:1px solid var(--line);border-radius:99px;overflow:hidden}
.bar>i{display:block;height:100%;width:0;background:var(--water);transition:width .4s}
.bar.low>i{background:var(--warn)}
.pct{color:var(--ink);font-weight:700;min-width:34px;text-align:right}
main{flex:1;padding:18px 16px 22px;display:flex;flex-direction:column}
.center{flex:1;display:flex;flex-direction:column;align-items:center;justify-content:center;text-align:center;gap:10px}
.t1{font-size:26px;font-weight:800;margin:0}
.t2{font-size:15px;color:var(--mute);margin:0}
.big{font-size:46px;font-weight:800;margin:0;font-variant-numeric:tabular-nums}
.btns{display:flex;gap:10px;margin-top:18px}
button{font:inherit;font-weight:800;border:0;border-radius:14px;min-height:60px;padding:0 16px;cursor:pointer;color:var(--ink);background:var(--acc)}
button.alt{background:var(--card);border:1px solid var(--line)}
button:disabled{background:var(--dim);color:var(--mute);border-color:transparent;cursor:default}
button:active:not(:disabled){filter:brightness(1.15)}
.btns .grow{flex:1.7}.btns button{flex:1}
.icon{font-size:56px;line-height:1}
.ok{color:var(--ok)}.warn{color:var(--warn)}.bad{color:var(--bad)}.water{color:var(--water)}
.row{display:flex;justify-content:space-between;align-items:baseline;margin-bottom:12px}
.grid{display:grid;grid-template-columns:repeat(5,1fr);gap:8px}
.tile{min-height:62px;border-radius:12px;padding:6px 2px;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:2px;
background:var(--card);border:1px solid var(--line);font-weight:800;font-size:15px}
.tile small{font-size:12px;font-weight:600;color:var(--water)}
.tile.sel{background:var(--acc);border-color:var(--acc)}.tile.sel small{color:var(--ink)}
.tile:disabled small{color:var(--mute)}
.prog{width:100%;height:24px;border-radius:99px;background:var(--dim);overflow:hidden}
.prog>i{display:block;height:100%;width:0;background:var(--water);transition:width .25s}
.cards{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin:14px 0}
.card{background:var(--card);border-radius:12px;padding:10px 12px}
.card span{display:block;font-size:12px;color:var(--mute)}
.card b{display:block;font-size:18px;margin-top:4px}
.list{border-top:1px solid var(--line)}
.sale{display:grid;grid-template-columns:1.2fr 1fr 1fr;gap:6px;padding:9px 2px;border-bottom:1px solid var(--line);font-size:14px}
.sale .d{color:var(--mute)}.sale .v{color:var(--water);text-align:right}
.sale.owed .v,.sale.owed .a{color:var(--warn)}
.foot{display:flex;justify-content:flex-end;margin-top:auto}
.small-btn{min-height:44px;font-size:15px;padding:0 18px}
.foot{gap:10px}
.dots{display:flex;gap:16px;justify-content:center;margin:14px 0}
.dots i{width:18px;height:18px;border-radius:50%;border:2px solid var(--mute)}.dots i.on{background:var(--ink);border-color:var(--ink)}
.keys{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;max-width:330px;margin:0 auto;width:100%}
.keys button{font-size:22px}
.menu{display:grid;grid-template-columns:repeat(2,1fr);gap:10px;margin-top:14px}
.kv{display:flex;justify-content:space-between;padding:12px 2px;border-bottom:1px solid var(--line)}
.kv span{color:var(--mute)}.kv b{font-variant-numeric:tabular-nums}
.load{display:flex;align-items:center;gap:8px;margin:12px 0}.load b{flex:1;font-size:20px}.load button{min-height:48px;padding:0 12px}
</style>
</head>
<body>
<div class="app">
<header>
  <div class="brand"><div class="drop"></div>WATER VENDO</div>
  <div id="gal" class="chip"></div>
  <div class="tank"><span>Tank</span><div id="bar" class="bar"><i></i></div><span id="pct" class="pct">--</span></div>
</header>
<main id="scr"></main>
</div>
<script>
const ST={BOOT:0,STANDBY:1,ACCEPTING:2,SELECTING:3,AWAITING:4,DISPENSING:5,PAUSED:6,SETTLING:7,COMPLETE:8,PAYING:9,THANKYOU:10,FAULT:11,ADMIN:12};
const EV={SELECT:1,AGAIN:2,FINISH:3,BACK:4,STATS:6,SET_P1:10,SET_P5:11,CONFIRM:15,CLEAR:16};
const FAULTS={1:"Out of water",2:"Not enough change in the machine",3:"Coin box is full",4:"Change dispenser jammed",5:"Water flow problem",6:"Refill pump problem",7:"Coin slot problem"};
const MAX_ML=2000;
const v={state:0,credit:0,inserted:0,maxml:0,target:0,poured:0,total:0,unpaid:0,p1:0,p5:0,b10:0,b20:0,fill:0},m={fault:0,tank:0,tankok:0,ga:0,gb:0,seen:0};
const S={income:0,volume:0,count:0,y:0,mo:0,d:0,rows:[],ready:false};
let ws,drawn="",sel=100,lastState=-1,stats=false,statsAt=0;
const $=id=>document.getElementById(id);
const peso=c=>"₱"+Math.floor(c/100)+(c%100?"."+String(c%100).padStart(2,"0"):"");
const vol=ml=>ml<1000?ml+" mL":(ml%1000===0?(ml/1000)+" L":(ml/1000).toFixed(1)+" L");
function send(ev,p){if(ws&&ws.readyState===1)ws.send(p===undefined?`EV ${ev}`:`EV ${ev} ${p}`)}
const lost=()=>Date.now()-m.seen>5000;

function header(){
  const g=$("gal"),n=lost()?0:m.ga+m.gb;
  g.textContent=n===2?"Both gallons low":m.ga?"Gallon A low":"Gallon B low";g.classList.toggle("show",n>0);
  const ok=!lost()&&m.tankok;$("bar").firstElementChild.style.width=(ok?m.tank:0)+"%";
  $("bar").classList.toggle("low",ok&&m.tank<25);$("pct").textContent=ok?m.tank+"%":"--";
}

function picker(){
  if(sel>v.maxml)sel=Math.max(100,Math.min(v.maxml,MAX_ML));if(sel<100)sel=100;
  let t="";for(let ml=100;ml<=MAX_ML;ml+=100){const on=ml<=v.maxml;
    t+=`<button class="tile${on&&ml===sel?" sel":""}" ${on?"":"disabled"} onclick="sel=${ml};draw(true)">${vol(ml)}<small>₱${ml/100}</small></button>`;}
  const can=v.maxml>=100;
  return `<div class="row"><b style="font-size:19px">Credit ${peso(v.credit)}</b><b class="water" style="font-size:19px">${can?vol(sel):""}</b></div>
  <div class="grid">${t}</div>
  <div class="btns">${v.total>0?`<button class="alt" onclick="send(${EV.FINISH})">Finish</button>`:""}
  <button class="grow" ${can?"":"disabled"} onclick="send(${EV.SELECT},${sel})">DISPENSE</button></div>`;
}

function salesPage(){
  const M=["Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"];
  const title=S.y&&S.mo?`Sales today — ${S.d} ${M[S.mo-1]} ${S.y}`:"Sales (no clock fitted)";
  let h=`<div class="row"><b style="font-size:19px">${title}</b><button class="alt small-btn" onclick="stats=false;draw(true)">Back</button></div>`;
  if(!S.ready)return h+`<div class="center"><p class="t2">Loading…</p></div>`;
  h+=`<div class="cards"><div class="card"><span>Total income</span><b class="ok">${peso(S.income)}</b></div>
  <div class="card"><span>Water dispensed</span><b class="water">${vol(S.volume)}</b></div>
  <div class="card"><span>Purchases</span><b>${S.count}</b></div></div>
  <p class="t2" style="margin-bottom:6px">Last 10 purchases</p><div class="list">`;
  if(!S.rows.length)h+=`<p class="t2" style="padding:14px 0">No purchases yet</p>`;
  S.rows.forEach(r=>{if(!r)return;let d="--/-- --:--";if(r.ts){const x=new Date(r.ts*1000);
    d=`${String(x.getUTCDate()).padStart(2,"0")}/${String(x.getUTCMonth()+1).padStart(2,"0")} ${String(x.getUTCHours()).padStart(2,"0")}:${String(x.getUTCMinutes()).padStart(2,"0")}`;}
    h+=r.owed>0?`<div class="sale owed"><span class="d">${d}</span><span class="a">owed ${peso(r.owed)}</span><span class="v">${vol(r.ml)}</span></div>`
               :`<div class="sale"><span class="d">${d}</span><span class="a">${peso(r.in)}</span><span class="v">${vol(r.ml)}</span></div>`;});
  return h+`</div>`;
}

// --- Admin. The PIN is checked by the ESP32 ("AD <pin>" -> "AA ..."), never
// here; admin events from a phone that has not logged in are dropped there.
let adm="",pin="",amsg="",admAt=0,lockUntil=0;const ld={p1:0,p5:0};
const idle=()=>[ST.STANDBY,ST.BOOT,ST.FAULT].includes(v.state);
function raw(t){if(ws&&ws.readyState===1)ws.send(t)}
function admOpen(){adm="pin";pin="";amsg="";admAt=Date.now();draw(true)}
function admClose(){if(adm)raw("AX");adm="";draw(true)}
function key(k){admAt=Date.now();
  if(k==="C")pin="";
  else if(k==="OK"){if(pin.length===4){raw((adm==="chpin"?"AP ":"AD ")+pin);pin="";}}
  else if(pin.length<4)pin+=k;
  draw(true)}
function go(p){admAt=Date.now();adm=p;amsg="";
  if(p==="menu"||p==="profit"||p==="hist"){S.ready=false;send(EV.STATS)}
  if(p==="load"){ld.p1=v.p1;ld.p5=v.p5}
  draw(true)}
function adj(k,d){admAt=Date.now();ld[k]=Math.max(0,Math.min(2000,ld[k]+d));draw(true)}
function saveLoad(){admAt=Date.now();send(EV.SET_P1,ld.p1);send(EV.SET_P5,ld.p5);go("coins")}
function clearFault(){admAt=Date.now();send(EV.CLEAR);amsg="Clear fault sent";draw(true)}
const kv=(k,val,cl)=>`<div class="kv"><span>${k}</span><b class="${cl||""}">${val}</b></div>`;
const atitle=(t,back)=>`<div class="row"><b style="font-size:19px">${t}</b><button class="alt small-btn" onclick="${back?"go('menu')":"admClose()"}">${back?"Back":"Exit"}</button></div>`;

function admPage(){
  if(adm==="pin"||adm==="chpin"){
    let h=`<div class="row"><b style="font-size:19px">${adm==="chpin"?"Change PIN":"Admin PIN"}</b><button class="alt small-btn" onclick="${adm==="chpin"?"go('menu')":"admClose()"}">Cancel</button></div>`;
    if(adm==="pin"&&Date.now()<lockUntil)return h+`<div class="center"><div class="icon bad">⚠</div><p class="t1">Admin locked</p><p class="t2">Try again in ${Math.ceil((lockUntil-Date.now())/1000)}s</p></div>`;
    let d="";for(let i=0;i<4;i++)d+=`<i class="${i<pin.length?"on":""}"></i>`;
    h+=`<p class="t2" style="text-align:center">${adm==="chpin"?"Enter a NEW 4-digit PIN":"Enter the 4-digit PIN"}</p><div class="dots">${d}</div>
    <p class="bad" style="text-align:center;min-height:20px;margin:0 0 10px">${amsg}</p><div class="keys">`;
    ["1","2","3","4","5","6","7","8","9","C","0","OK"].forEach(k=>{h+=`<button class="${k==="OK"?"":"alt"}" onclick="key('${k}')">${k==="C"?"Clear":k}</button>`});
    return h+`</div>`;}
  if(adm==="menu")return atitle("Service mode",false)+
    `<p class="${m.fault?"bad":"ok"}" style="font-weight:800;margin:4px 0">${m.fault?FAULTS[m.fault]||"Fault":"No fault"}</p>
    <p class="t2">${S.ready?`Today: ${peso(S.income)} · ${S.count} sales`:"Today: loading…"}</p>
    <p class="ok" style="min-height:20px;margin:4px 0">${amsg}</p>
    <div class="menu"><button onclick="go('profit')">Profit</button><button onclick="go('coins')">Coins</button>
    <button onclick="go('water')">Water</button><button class="alt" onclick="go('load')">Load change</button>
    <button class="alt" ${m.fault?"":"disabled"} onclick="clearFault()">Clear fault</button><button class="alt" onclick="go('hist')">History</button>
    <button class="alt" onclick="go('chpin');pin=''">Change PIN</button><button class="alt" onclick="admClose()">Exit</button></div>`;
  if(adm==="profit"){if(!S.ready)return atitle("Profit (today)",true)+`<p class="t2">Loading…</p>`;
    const owed=S.rows.reduce((a,r)=>a+(r?r.owed:0),0);
    return atitle("Profit (today)",true)+kv("Income today",peso(S.income),"ok")+kv("Sales",S.count)+kv("Water sold",vol(S.volume),"water")+kv("Change owed (recent)",peso(owed),owed?"warn":"");}
  if(adm==="coins")return atitle("Coins",true)+kv("₱1 hopper (change)","× "+v.p1,"water")+kv("₱5 hopper (change)","× "+v.p5,"water")+
    kv("₱10 in coin box","× "+v.b10)+kv("₱20 in coin box","× "+v.b20)+
    `<p class="t2" style="margin-top:12px">Coin box ${peso((v.b10*10+v.b20*20)*100)} · change float ${peso((v.p1+v.p5*5)*100)}</p>`;
  if(adm==="water")return atitle("Water",true)+kv("Cold tank",m.tankok?m.tank+"%":"no reading",m.tankok?"water":"warn")+
    kv("Gallon A",m.ga?"LOW":"OK",m.ga?"warn":"ok")+kv("Gallon B",m.gb?"LOW":"OK",m.gb?"warn":"ok")+
    kv("Refill pump",v.fill?"running":"off",v.fill?"water":"")+kv("Water sold today",vol(S.volume));
  if(adm==="hist"){let h=atitle("History (last 10 sales)",true);if(!S.ready)return h+`<p class="t2">Loading…</p>`;
    const sp=salesPage();return h+sp.slice(sp.indexOf('<p class="t2" style="margin-bottom:6px">'));}
  if(adm==="load"){const row=(k,l)=>`<div class="load"><b>${l} × ${ld[k]}</b><button class="alt" onclick="adj('${k}',-10)">-10</button><button class="alt" onclick="adj('${k}',-1)">-1</button><button class="alt" onclick="adj('${k}',1)">+1</button><button class="alt" onclick="adj('${k}',10)">+10</button></div>`;
    return atitle("Load change",true)+`<p class="t2">Count the coins in each hopper, set them here, then Save.</p>`+row("p1","₱1")+row("p5","₱5")+
    `<div class="btns"><button onclick="saveLoad()">SAVE</button></div>`;}
  return "";
}

function body(){
  const s=v.state,c=peso(v.credit);
  if(lost())return `<div class="center"><div class="icon water">💧</div><p class="t1">Connecting to the machine…</p><p class="t2">Waiting for the controller</p></div>`;
  if(adm)return admPage();
  if(stats)return salesPage();
  if(s===ST.ACCEPTING)return `<div class="center"><p class="t2">Credit</p><p class="big">${c}</p><p class="t2">Insert more coins, or tap Continue</p></div>
    <div class="btns"><button class="grow" onclick="send(${EV.CONFIRM})">CONTINUE</button></div>`;
  if(s===ST.SELECTING)return picker();
  if(s===ST.AWAITING)return `<div class="center"><div class="icon">🧴</div><p class="t1">Place your bottle under the nozzle</p><p class="t2">${vol(v.target)} will be dispensed</p></div>
    <div class="btns"><button class="alt" onclick="send(${EV.BACK})">Change amount</button></div>`;
  if(s===ST.DISPENSING||s===ST.SETTLING){const pc=v.target?Math.min(100,Math.round(v.poured*100/v.target)):100;
    return `<div class="center"><p class="t2">Dispensing</p><p class="big">${pc}%</p><div class="prog"><i style="width:${pc}%"></i></div><p class="t2">${vol(v.poured)} of ${vol(v.target)}</p></div>`;}
  if(s===ST.PAUSED)return `<div class="center"><div class="icon warn">⚠</div><p class="t1">Bottle removed</p><p class="t2">Put it back within 10 seconds to continue</p></div>`;
  if(s===ST.COMPLETE)return `<div class="center"><div class="icon ok">✔</div><p class="t1">${vol(v.total)} dispensed</p><p class="t2">Credit left</p><p class="big">${c}</p></div>
    <div class="btns"><button onclick="send(${EV.AGAIN})">Dispense more</button><button class="alt" onclick="send(${EV.FINISH})">Finish</button></div>`;
  if(s===ST.PAYING)return `<div class="center"><div class="icon warn">🪙</div><p class="t1">Returning your change</p><p class="big warn">${c}</p></div>`;
  if(s===ST.THANKYOU){if(v.unpaid>0)return `<div class="center"><div class="icon warn">⚠</div><p class="t1">Sorry, your change could not be given</p><p class="big warn">${peso(v.unpaid)}</p><p class="t2">Please contact the attendant</p></div>`;
    return `<div class="center"><div class="icon ok">✔</div><p class="t1">Thank you!</p><p class="t2">Dispensed ${vol(v.total)} · Change ${c}</p></div>`;}
  if(s===ST.FAULT)return `<div class="center"><div class="icon bad">⚠</div><p class="t1">Out of service</p><p class="bad" style="font-weight:800">${FAULTS[m.fault]||""}</p><p class="t2">Please contact the attendant</p></div>`;
  if(s===ST.ADMIN)return `<div class="center"><p class="t1">Service mode</p></div>`;
  return `<div class="center"><div class="icon water">💧</div><p class="t1">Insert coins to start</p><p class="t2">1 peso = 100 mL · up to 20 pesos (2 L)</p></div>
    <div class="foot"><button class="alt small-btn" onclick="admOpen()">Admin</button></div>`;
}

function openStats(){stats=true;statsAt=Date.now();S.ready=false;send(EV.STATS);draw(true)}

function draw(force){
  if(stats&&(Date.now()-statsAt>60000||!idle()))stats=false;
  if(adm&&(Date.now()-admAt>120000||!idle()||lost())){raw("AX");adm="";}
  if(v.state===ST.SELECTING&&lastState!==ST.SELECTING)sel=100;
  lastState=v.state;header();
  const h=body();if(force||h!==drawn){$("scr").innerHTML=h;drawn=h;}
}

function line(t){
  m.seen=Date.now();const a=t.split(" ");
  if(a[0]==="VL"&&a[1] in v)v[a[1]]=parseInt(a[2],10)||0;
  else if(a[0]==="FT")m.fault=parseInt(a[1],10)||0;
  else if(a[0]==="LV"){m.tank=+a[1]||0;m.tankok=+a[2]||0;m.ga=+a[3]||0;m.gb=+a[4]||0;}
  else if(a[0]==="SD"){S.income=+a[1]||0;S.volume=+a[2]||0;S.count=+a[3]||0;S.rows=[];S.ready=false;}
  else if(a[0]==="DT"){S.y=+a[1]||0;S.mo=+a[2]||0;S.d=+a[3]||0;}
  else if(a[0]==="HS"){const i=+a[1];if(i>=0&&i<10)S.rows[i]={ts:+a[2]||0,in:+a[3]||0,ml:+a[4]||0,chg:+a[5]||0,owed:+a[6]||0};}
  else if(a[0]==="HE")S.ready=true;
  else if(a[0]==="AA"){
    if(a[1]==="1"){go("menu");return}
    if(a[1]==="0")amsg=`Wrong PIN (${a[2]} of 3)`;
    else if(a[1]==="L"){lockUntil=Date.now()+(+a[2]||60)*1000;amsg="";}
    else if(a[1]==="P"){go("menu");amsg="PIN changed";draw(true);return}
    else if(a[1]==="N"){adm="pin";pin="";amsg="Session ended, enter the PIN again";}
    draw(true);return;}
  else if(a[0]!=="PG")return;
  draw(false);
}

function connect(){
  ws=new WebSocket(`ws://${location.hostname||"192.168.4.1"}:81/`);
  ws.onclose=()=>setTimeout(connect,1500);
  ws.onmessage=e=>line(String(e.data).trim());
}
setInterval(()=>draw(false),1000);
draw(true);connect();
</script>
</body>
</html>)HTML";
