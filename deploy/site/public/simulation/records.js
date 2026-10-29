"use strict";
// Transaction records and the owner's phone: simulation tools for testing, not machine features.
// The machine has no network link (WiFi, Bluetooth and remote monitoring are out of scope), so
// the phone stands in for "someone watching the test", reading the same records as the side card.

(() => {
  const m = machine;
  const $ = id => document.getElementById(id);
  const peso = c => "₱" + (c / 100).toLocaleString("en-PH", { minimumFractionDigits: 0, maximumFractionDigits: 2 });
  const esc = s => String(s).replace(/[&<>"]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
  const simTime = ms => { const s = Math.floor(ms / 1000); return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, "0")}`; };
  const wallTime = t => new Date(t).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit" });
  const coinsText = cs => cs.length ? cs.map(c => "₱" + c).join(" + ") : "–";
  const sum = cs => cs.reduce((a, c) => a + c, 0);
  const paidText = p => (p[5] || p[1]) ? [p[5] ? `${p[5]}×₱5` : "", p[1] ? `${p[1]}×₱1` : ""].filter(Boolean).join(" + ") : "–";
  const resultCls = r => /jam/i.test(r) ? "bad" : /^Completed/.test(r) ? "ok" : "warn";

  // ---- side card: table, totals, CSV -------------------------------------------------
  function tableHTML(rows) {
    if (!rows.length) return `<div class="note">No transactions yet. Insert coins, pick an amount on the screen and place a bottle.</div>`;
    return `<table class="rec"><thead><tr><th>#</th><th>Time</th><th>Coins in</th><th>Paid</th><th>Bought</th>
      <th>Poured</th><th>Change</th><th>Result</th></tr></thead><tbody>` +
      rows.map(r => `<tr><td>${r.no}</td><td title="clock ${wallTime(r.wall)}">${simTime(r.simMs)}</td>
        <td>${esc(coinsText(r.coins))}${r.rejected.length ? `<div class="rej">returned ${esc(coinsText(r.rejected))}</div>` : ""}</td>
        <td>₱${sum(r.coins)}</td><td>${r.charged} mL</td><td>${Math.floor(r.poured)} mL</td>
        <td>${r.changeDue ? peso(r.changeDue) : "–"}${r.changeDue ? `<div class="sub2">${esc(paidText(r.paid))}</div>` : ""}</td>
        <td class="${resultCls(r.result)}">${esc(r.result)}</td></tr>`).join("") + `</tbody></table>`;
  }

  function totalsLines() {
    const t = m.recordTotals(), box = m.inv.box10 * 10 + m.inv.box20 * 20;
    return [
      ["Customers", `${t.n}`],
      ["Money in", `₱${t.in}  (₱1×${t.coins[1]}  ₱5×${t.coins[5]}  ₱10×${t.coins[10]}  ₱20×${t.coins[20]})`],
      ["Coins returned", `${t.rejected}  (slot closed or out of service)`],
      ["Change paid", `₱${t.changePaid} of ₱${t.changeDue} due${t.owed ? `, ₱${t.owed} OWED` : ""}`],
      ["Profit", `₱${t.in - t.changePaid}  (= ${t.charged} mL charged ÷ 100${t.owed ? ` + ₱${t.owed} owed` : ""})`],
      ["Water", `${t.charged} mL charged, ${Math.floor(t.poured)} mL poured`],
      ["Now in machine", `₱1×${m.inv.p1}  ₱5×${m.inv.p5}  box ₱${box}`],
      ["Cold tank", `${Math.round(m.tank)} mL (${m.tankPct()}%)`],
    ];
  }

  function csv() {
    const head = ["No", "Sim time", "Clock", "Coins in", "Paid (PHP)", "Coins returned", "Bought (mL)", "Poured (mL)",
                  "Change due (PHP)", "P5 paid", "P1 paid", "Owed (PHP)", "Result", "Tank after (mL)"];
    const q = v => /[",\n]/.test(String(v)) ? `"${String(v).replace(/"/g, '""')}"` : v;
    const rows = m.records.slice().reverse().map(r => [r.no, simTime(r.simMs), wallTime(r.wall),
      r.coins.map(c => "P" + c).join(" + "), sum(r.coins), r.rejected.map(c => "P" + c).join(" + "), r.charged,
      Math.floor(r.poured), r.changeDue / 100, r.paid[5], r.paid[1], r.owed / 100, r.result.replace(/₱/g, "P"), r.tankAfter]);
    return [head, ...rows].map(r => r.map(q).join(",")).join("\r\n");
  }
  $("bCsv").addEventListener("click", () => {
    const a = document.createElement("a");
    a.href = URL.createObjectURL(new Blob(["﻿" + csv()], { type: "text/csv" }));
    a.download = "water-vendo-records.csv"; a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 1000);
  });

  // ---- the phone ----------------------------------------------------------------------
  // Logs in with the machine's Admin PIN. Its own lockout (3 wrong, 5 min) so it cannot be
  // used to guess the PIN faster than the machine's own keypad allows.
  const ph = { on: false, logged: false, digits: "", msg: "", fails: 0, lockUntil: 0, tab: "live" };
  const phone = $("phone");
  $("bPhone").addEventListener("click", () => {
    ph.on = !ph.on; phone.style.display = ph.on ? "flex" : "none";
    $("bPhone").classList.toggle("lit", ph.on); $("lPhone").textContent = ph.on ? "Phone on" : "Phone";
    key = "";
  });

  function lockScreen() {
    const left = Math.max(0, ph.lockUntil - Date.now());
    const dots = Array.from({ length: CFG.PIN_LEN }, (_, i) => `<i class="${i < ph.digits.length ? "f" : ""}"></i>`).join("");
    const keys = ["1", "2", "3", "4", "5", "6", "7", "8", "9", "C", "0", "OK"];
    return `<div class="ph-lock">
      <div class="ph-logo">💧</div><div class="ph-title">Water Vendo</div><div class="ph-sub">Owner login</div>
      ${left ? `<div class="ph-msg bad">Locked after ${CFG.PIN_TRIES} wrong PINs.<br>Try again in ${simTime(left)}</div>`
             : `<div class="ph-dots">${dots}</div><div class="ph-msg">${esc(ph.msg || "Enter the Admin PIN")}</div>
                <div class="ph-keys">${keys.map(k => `<button data-k="${k}">${k}</button>`).join("")}</div>`}
      <div class="ph-foot">Demo PIN 1234 (the machine's Admin PIN)</div></div>`;
  }

  function tryLogin() {
    if (ph.digits === m.pin) { ph.logged = true; ph.fails = 0; ph.msg = ""; ph.tab = "live"; }
    else {
      ph.fails++;
      if (ph.fails >= CFG.PIN_TRIES) { ph.fails = 0; ph.lockUntil = Date.now() + CFG.PIN_LOCK_MS; ph.msg = ""; }
      else ph.msg = `Wrong PIN, ${CFG.PIN_TRIES - ph.fails} tr${CFG.PIN_TRIES - ph.fails === 1 ? "y" : "ies"} left`;
    }
    ph.digits = "";
  }

  function liveHTML() {
    const c = m.cur, box = m.inv.box10 * 10 + m.inv.box20 * 20;
    return `<div class="ph-card"><div class="ph-k">Customer screen now</div><canvas id="phLcd" width="240" height="160"></canvas></div>
      <div class="ph-card"><div class="ph-k">Machine</div>
        <div class="ph-big">${STATE_NAME[m.state]}${m.fault ? `<span class="bad"> · ${FAULT_TEXT[m.fault]}</span>` : ""}</div>
        <div>Credit ${peso(m.credit)} · slot ${m.acceptorOpen() ? "open" : "closed"}</div></div>
      <div class="ph-card"><div class="ph-k">Current customer</div>
        ${c ? `<div>Coins ${esc(coinsText(c.coins))} = ₱${sum(c.coins)}</div><div>Bought ${c.ordered} mL · poured ${Math.floor(c.poured + (m.state === ST.DISPENSING || m.state === ST.PAUSED ? m.segment : 0))} mL</div>
              ${c.changeDue ? `<div>Change ${peso(c.changeDue)}: ${esc(paidText(c.paid))} out</div>` : ""}`
            : `<div class="dim">Nobody at the machine</div>`}</div>
      <div class="ph-card"><div class="ph-k">Stock</div>
        <div>₱1 hopper ${m.inv.p1} · ₱5 hopper ${m.inv.p5} · box ₱${box}</div>
        <div>Tank ${Math.round(m.tank)} mL (${m.tankPct()}%) · gallons A ${m.gallonLow("a") ? "LOW" : "OK"}, B ${m.gallonLow("b") ? "LOW" : "OK"}</div></div>
      <div class="ph-card"><div class="ph-k">What is happening</div><div class="dim">${esc(m.story)}</div></div>`;
  }

  function recordsPhoneHTML() {
    if (!m.records.length) return `<div class="ph-card dim">No transactions yet.</div>`;
    return m.records.slice(0, 50).map(r => `<div class="ph-card">
      <div class="ph-row"><b>#${r.no}</b><span class="dim">${simTime(r.simMs)} · ${wallTime(r.wall)}</span></div>
      <div>${esc(coinsText(r.coins))}${r.coins.length ? ` = ₱${sum(r.coins)}` : ""}${r.rejected.length ? ` <span class="dim">(returned ${esc(coinsText(r.rejected))})</span>` : ""}</div>
      ${r.coins.length ? `<div>${r.charged} mL bought · ${Math.floor(r.poured)} mL poured</div>
      <div>Change ${r.changeDue ? `${peso(r.changeDue)} (${esc(paidText(r.paid))})` : "none"}</div>` : ""}
      <div class="${resultCls(r.result)}">${esc(r.result)}</div></div>`).join("");
  }

  function totalsPhoneHTML() {
    return `<div class="ph-card">${totalsLines().map(([k, v]) => `<div class="ph-tl"><span class="ph-k">${k}</span><span>${esc(v)}</span></div>`).join("")}</div>`;
  }

  function phoneHTML() {
    if (!ph.logged) return lockScreen();
    const tabs = [["live", "Live"], ["records", "Records"], ["totals", "Totals"]];
    return `<div class="ph-head"><b>Water Vendo</b><button data-out>Log out</button></div>
      <div class="ph-tabs">${tabs.map(([k, t]) => `<button data-tab="${k}" class="${ph.tab === k ? "on" : ""}">${t}</button>`).join("")}</div>
      <div class="ph-body">${ph.tab === "live" ? liveHTML() : ph.tab === "records" ? recordsPhoneHTML() : totalsPhoneHTML()}</div>`;
  }

  $("phScreen").addEventListener("click", e => {
    const b = e.target.closest("button"); if (!b) return;
    if (b.dataset.k) {
      const k = b.dataset.k;
      if (k === "C") ph.digits = "";
      else if (k === "OK") { if (ph.digits.length === CFG.PIN_LEN) tryLogin(); else ph.msg = `Enter ${CFG.PIN_LEN} digits`; }
      else if (ph.digits.length < CFG.PIN_LEN) ph.digits += k;
    } else if (b.dataset.tab) ph.tab = b.dataset.tab;
    else if ("out" in b.dataset) { ph.logged = false; ph.digits = ""; ph.msg = "Logged out"; }
    key = "";
  });

  // ---- refresh ------------------------------------------------------------------------
  let key = "", cardKey = "", recRef = null;
  function refresh() {
    if (recRef !== m.records) { recRef = m.records; cardKey = ""; }
    const ck = `${m.recVersion}|${m.records.length}|${Math.round(m.tank / 50)}|${m.inv.p1}|${m.inv.p5}|${m.inv.box10}|${m.inv.box20}`;
    if (ck !== cardKey) {
      cardKey = ck;
      $("recTable").innerHTML = tableHTML(m.records);
      $("recTotals").textContent = totalsLines().map(([k, v]) => (k + " ".repeat(15)).slice(0, 15) + v).join("\n");
    }
    if (ph.on) {
      const lock = Math.ceil(Math.max(0, ph.lockUntil - Date.now()) / 1000);
      const pk = !ph.logged ? `L|${ph.digits}|${ph.msg}|${lock}`
        : ph.tab === "live" ? `V|${m.state}|${m.fault}|${m.credit}|${m.story}|${ck}|${m.cur ? m.cur.ordered + "," + m.cur.coins.length + "," + Math.floor(m.cur.poured + m.segment) + "," + m.cur.paid[1] + "," + m.cur.paid[5] + "," + m.cur.changeDue : ""}|${m.acceptorOpen()}`
        : `${ph.tab}|${ck}`;
      if (pk !== key) {
        const body = $("phScreen").querySelector(".ph-body"), scroll = body ? body.scrollTop : 0;
        key = pk; $("phScreen").innerHTML = phoneHTML();
        const nb = $("phScreen").querySelector(".ph-body"); if (nb) nb.scrollTop = scroll;
      }
      const mirror = $("phLcd");
      if (mirror) mirror.getContext("2d").drawImage($("lcd"), 0, 0, 240, 160);
      $("phClock").textContent = new Date().toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
    }
    requestAnimationFrame(refresh);
  }
  requestAnimationFrame(refresh);
})();
