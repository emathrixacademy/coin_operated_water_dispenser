// verify_port.js -- EMX-2026-WATERVENDO-01, WO-002-A.
//
// Checks the JavaScript transcription of the money path inside
// watervendo-hmi.html against the compiled C in src/, input for input.
//
// src/ is authoritative. Any difference means the JavaScript is wrong.
//
//   g++ -std=gnu++11 -Wall -Wextra -I include -o port_oracle \
//       sim/hmi/port_oracle.cpp src/change_plan.cpp src/billing.cpp src/fault_mask.cpp
//   ./port_oracle > oracle.txt
//   node sim/hmi/verify_port.js oracle.txt
//
// Exit status 0 only if every line matches.

'use strict';
const fs = require('fs');
const path = require('path');

const html = fs.readFileSync(path.join(__dirname, 'watervendo-hmi.html'), 'utf8');

// Everything from the start of the script through the last ported block: the
// config.h mirror, idiv(), the enums, and the three BEGIN/END PORT blocks.
const start = html.indexOf("'use strict';");
const end = html.indexOf('// END PORT fault_mask');
if (start < 0 || end < 0) { console.error('port markers not found'); process.exit(2); }
const src = html.slice(start, end);

const js = new Function(src + `
  return { change_plan, billing_round_down, fault_highest, fault_persistent_subset,
           billing_reset, billing_add_coin, billing_can_select, billing_select,
           billing_settle_partial, billing_settle_complete, billing_cancel_selection,
           billing_store, billing_change_due, billing_at_ceiling, billing_max_selectable_ml,
           COIN_P1, COIN_P5, COIN_P10, COIN_P20, COIN_UNKNOWN, COIN_NONE };`)();

let rng = 12345;
function rnd(n) {
  rng = (Math.imul(rng, 1103515245) + 12345) >>> 0;
  return (rng >>> 16) % n;
}

const out = [];
for (let c = -200; c <= 2600; c += 50) {
  for (let p1 = 0; p1 <= 60; p1++) {
    for (let p5 = 0; p5 <= 40; p5++) {
      const r = js.change_plan(c, p1, p5);
      out.push(`cp ${c} ${p1} ${p5} ${r.ok ? 1 : 0} ${r.p1} ${r.p5}`);
    }
  }
}
for (let v = -50; v <= 2600; v++) out.push(`rd ${v} ${js.billing_round_down(v)}`);
for (let m = 0; m < 256; m++) out.push(`fm ${m} ${js.fault_highest(m)} ${js.fault_persistent_subset(m)}`);

const dump = op => { const t = js.billing_store(); out.push(`bill ${op} ${t.credit} ${t.inserted} ${t.target_ml} ${t.total_ml}`); };
const COINS = [js.COIN_P1, js.COIN_P5, js.COIN_P10, js.COIN_P20, js.COIN_UNKNOWN, js.COIN_NONE];
for (let run = 0; run < 400; run++) {
  js.billing_reset();
  for (let step = 0; step < 30; step++) {
    switch (rnd(6)) {
      case 0: case 1: js.billing_add_coin(COINS[rnd(6)]); dump('add'); break;
      case 2: {
        let v = rnd(23) * 100 - 100;
        if (rnd(4) === 0) v += 50;
        const can = js.billing_can_select(v);
        const got = js.billing_select(v);
        out.push(`sel ${v} ${can ? 1 : 0} ${got}`);
        dump('sel');
        break;
      }
      case 3: js.billing_settle_partial(rnd(2300) - 50); dump('part'); break;
      case 4: js.billing_settle_complete(0); dump('comp'); break;
      default: js.billing_cancel_selection(); dump('canc'); break;
    }
    out.push(`due ${js.billing_change_due()} ${js.billing_at_ceiling() ? 1 : 0} ${js.billing_max_selectable_ml()}`);
  }
}

const oracle = fs.readFileSync(process.argv[2], 'utf8').split(/\r?\n/).filter(Boolean);
let diffs = 0;
if (oracle.length !== out.length) {
  console.error(`line count differs: C ${oracle.length}, JS ${out.length}`);
  diffs++;
}
const n = Math.min(oracle.length, out.length);
for (let i = 0; i < n; i++) {
  if (oracle[i] !== out[i]) {
    if (diffs < 20) console.error(`line ${i + 1}\n  C : ${oracle[i]}\n  JS: ${out[i]}`);
    diffs++;
  }
}
const kinds = {};
for (const l of out) { const k = l.split(' ')[0]; kinds[k] = (kinds[k] || 0) + 1; }
console.log(`compared ${n} lines (${Object.entries(kinds).map(([k, v]) => `${k} ${v}`).join(', ')})`);
console.log(diffs ? `MISMATCH: ${diffs} line(s) differ -- the JavaScript is wrong` : 'IDENTICAL to src/');
process.exit(diffs ? 1 : 0);
