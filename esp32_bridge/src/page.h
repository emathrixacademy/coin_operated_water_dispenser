// The test screen, served by the bridge at http://192.168.4.1
//
// Mirrors the machine: it draws only what the Mega reports (PG / VL / FT / LV)
// and sends touches back as "EV <event> [payload]". The numbers below are the
// enum values in the main project -- state_t (types.h), hmi_event_t (hmi.h),
// fault_t (types.h). If those enums change, change this page too.

#pragma once
#include <pgmspace.h>

static const char PAGE_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Water Vendo Test</title>
<style>
:root{--bg:#f3f5f7;--card:#fff;--ink:#14212b;--mute:#5d6b76;--line:#dde3e8;
--accent:#0a6ebd;--ok:#1d8a4e;--warn:#b26a00;--bad:#c2321f;--dim:#c9d1d7}
@media (prefers-color-scheme:dark){:root{--bg:#0f161c;--card:#18222b;--ink:#e6edf2;
--mute:#93a3ae;--line:#2a3742;--accent:#4aa3e8;--ok:#3ec27a;--warn:#e0a03a;--bad:#ef6a55;--dim:#33424e}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.4 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
main{max-width:520px;margin:0 auto;padding:16px}
header{display:flex;align-items:center;justify-content:space-between;margin-bottom:12px}
h1{font-size:17px;margin:0;letter-spacing:.02em}
.tag{font-size:12px;color:var(--mute)}
.dot{display:inline-block;width:9px;height:9px;border-radius:50%;background:var(--bad);margin-right:6px}
.dot.on{background:var(--ok)}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:16px;margin-bottom:12px}
.levels{display:flex;gap:12px;align-items:center}
.bar{flex:1;height:12px;border-radius:6px;background:var(--dim);overflow:hidden}
.bar>i{display:block;height:100%;background:var(--accent);width:0;transition:width .4s}
.small{font-size:13px;color:var(--mute)}
.notice{margin-top:10px;font-size:13px;color:var(--warn);display:none}
.notice.show{display:block}
.title{font-size:22px;font-weight:650;margin:0 0 4px}
.credit{font-size:40px;font-weight:700;margin:6px 0;font-variant-numeric:tabular-nums}
.grid{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;margin-top:12px}
button{font:inherit;border:0;border-radius:10px;padding:12px 8px;background:var(--accent);color:#fff;
font-weight:600;cursor:pointer;min-height:48px}
button.alt{background:transparent;color:var(--ink);border:1px solid var(--line)}
button:disabled{background:var(--dim);color:var(--mute);cursor:default}
.grid button{padding:10px 4px;font-size:14px}
.grid button small{display:block;font-weight:400;font-size:11px;opacity:.85}
.row{display:flex;gap:8px;margin-top:12px}.row button{flex:1}
.fault{color:var(--bad);font-size:22px;font-weight:700}
.prog{height:18px;border-radius:9px;background:var(--dim);overflow:hidden;margin:12px 0 6px}
.prog>i{display:block;height:100%;background:var(--ok);width:0;transition:width .25s}
details{font-size:12px;color:var(--mute)}
pre{white-space:pre-wrap;max-height:220px;overflow:auto;margin:8px 0 0}
</style>
</head>
<body>
<main>
<header><h1>WATER VENDO</h1><span class="tag"><span id="dot" class="dot"></span><span id="conn">offline</span> · bench test</span></header>

<section class="card">
  <div class="levels"><span class="small">Tank</span><div class="bar"><i id="tankbar"></i></div><b id="tankpct">--%</b></div>
  <div id="sensor" class="notice">⚠ No reading from the tank sensor</div>
  <div id="gallon" class="notice"></div>
</section>

<section class="card" id="screen"><p class="title">Connecting…</p></section>

<details class="card"><summary>Raw log</summary><pre id="log"></pre></details>
</main>
<script>
const ST={BOOT:0,STANDBY:1,ACCEPTING:2,SELECTING:3,AWAITING:4,DISPENSING:5,PAUSED:6,SETTLING:7,COMPLETE:8,PAYING:9,THANKYOU:10,FAULT:11,ADMIN:12};
const EV={SELECT:1,AGAIN:2,FINISH:3,BACK:4,CONFIRM:15};
const FAULTS={1:"OUT OF WATER — PLEASE REFILL",2:"LOW CHANGE — SERVICE REQUIRED",3:"COIN STORAGE FULL",
4:"CHANGE JAM — SERVICE REQUIRED",5:"SERVICE REQUIRED (flow stall)",6:"SERVICE REQUIRED (pump run time)",7:"SERVICE REQUIRED (coin acceptor)"};
const v={state:0,credit:0,inserted:0,maxml:0,target:0,poured:0,total:0},m={fault:0,tank:0,tankok:0,ga:0,gb:0};
let ws,drawn="";
const $=id=>document.getElementById(id);
const peso=c=>"₱"+Math.floor(c/100)+(c%100?"."+String(c%100).padStart(2,"0"):"");
function send(ev,p){if(ws&&ws.readyState===1)ws.send(p===undefined?`EV ${ev}`:`EV ${ev} ${p}`)}
function log(s){const l=$("log");l.textContent=(s+"\n"+l.textContent).slice(0,4000)}

function levels(){
  $("tankbar").style.width=m.tank+"%";$("tankpct").textContent=m.tankok?m.tank+"%":"--";
  $("sensor").classList.toggle("show",!m.tankok);
  const g=$("gallon"),n=m.ga+m.gb;
  g.textContent=n===2?"⚠ Both gallons are low — replace them":n?`⚠ Gallon ${m.ga?"A":"B"} is low`:"";
  g.classList.toggle("show",n>0);
}

function screen(){
  const s=v.state;let h="";
  if(m.fault&&s===ST.FAULT){h=`<p class="title">Out of service</p><p class="fault">${FAULTS[m.fault]||("FAULT "+m.fault)}</p>`;}
  else if(s===ST.STANDBY||s===ST.BOOT){h=`<p class="title">Insert coins</p><p class="small">₱1 = 100 mL · up to ₱15 (1.5 L)</p>`;}
  else if(s===ST.ACCEPTING){h=`<p class="small">Credit</p><p class="credit">${peso(v.credit)}</p><p class="small">Insert more coins, or press CONFIRM</p>
    <div class="row"><button onclick="send(${EV.CONFIRM})">CONFIRM</button></div>`;}
  else if(s===ST.SELECTING){let b="";for(let p=1;p<=20;p++){const ml=p*100;
      b+=`<button ${ml>v.maxml?"disabled":""} onclick="send(${EV.SELECT},${ml})">${ml>=1000?(ml/1000)+" L":ml+" mL"}<small>₱${p}</small></button>`;}
    h=`<p class="small">Credit</p><p class="credit">${peso(v.credit)}</p><p class="small">Choose an amount</p><div class="grid">${b}</div>
    <div class="row"><button class="alt" onclick="send(${EV.FINISH})">Done · return my change</button></div>`;}
  else if(s===ST.AWAITING){h=`<p class="title">Place your bottle</p><p class="small">${v.target} mL will be dispensed</p>
    <div class="row"><button class="alt" onclick="send(${EV.BACK})">Back</button></div>`;}
  else if(s===ST.DISPENSING||s===ST.SETTLING){const pc=v.target?Math.min(100,Math.round(v.poured*100/v.target)):100;
    h=`<p class="title">Dispensing water…</p><div class="prog"><i style="width:${pc}%"></i></div><p class="small">${v.poured} / ${v.target} mL</p>`;}
  else if(s===ST.PAUSED){h=`<p class="title">Put the bottle back</p><p class="small">You have 10 seconds ang transaksyon</p>`;}
  else if(s===ST.COMPLETE){h=`<p class="title">Done!</p><p class="small">Dispensed: ${v.total} mL · Credit left</p><p class="credit">${peso(v.credit)}</p>
    <div class="row"><button onclick="send(${EV.AGAIN})">More</button><button class="alt" onclick="send(${EV.FINISH})">Finish</button></div>`;}
  else if(s===ST.PAYING){h=`<p class="title">Returning your change…</p><p class="credit">${peso(v.credit)}</p>`;}
  else if(s===ST.THANKYOU){h=`<p class="title">Thank you!</p><p class="small">Take your change: ${peso(v.credit)} · Dispensed: ${v.total} mL</p>`;}
  else if(s===ST.FAULT){h=`<p class="title">Out of service</p>`;}
  else {h=`<p class="title">Admin</p>`;}
  // Redraw only when something changed, so a button is not replaced under a finger.
  if(h!==drawn){$("screen").innerHTML=h;drawn=h;}
}

function line(t){
  log(t);const a=t.split(" ");
  if(a[0]==="VL"&&a[1] in v){v[a[1]]=parseInt(a[2],10)||0;screen();}
  else if(a[0]==="FT"){m.fault=parseInt(a[1],10)||0;screen();}
  else if(a[0]==="LV"){m.tank=+a[1]||0;m.tankok=+a[2]||0;m.ga=+a[3]||0;m.gb=+a[4]||0;levels();}
}

function connect(){
  ws=new WebSocket(`ws://${location.hostname||"192.168.4.1"}:81/`);
  ws.onopen=()=>{$("dot").classList.add("on");$("conn").textContent="online"};
  ws.onclose=()=>{$("dot").classList.remove("on");$("conn").textContent="offline";setTimeout(connect,1500)};
  ws.onmessage=e=>line(String(e.data).trim());
}
levels();connect();
</script>
</body>
</html>)HTML";
