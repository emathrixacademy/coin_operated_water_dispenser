"use strict";
// Transaction records (a simulation tool for testing) and the phone link.
// The phone shows the ESP32's own phone page (phone/index.html, copied unchanged from
// esp32_bridge/src/page.h). On the machine it is served over the ESP32's WiFi hotspot; here
// the link below feeds it the same text lines the Mega sends, and takes its touches back.

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

  // ---- the phone link ------------------------------------------------------------------
  // Speaks the ESP32 bridge's line protocol (esp32_bridge/src/main.cpp): VL / FT / LV / PG
  // out, "EV <event> [payload]" in, and the bridge's own Admin login ("AD <pin>" -> "AA ...").
  // The PIN is the machine's Admin PIN; three wrong tries lock the phone login for 5 minutes.
  const PEV = { SELECT: 1, AGAIN: 2, FINISH: 3, BACK: 4, STATS: 6, SET_P1: 10, SET_P5: 11, CONFIRM: 15, CLEAR: 16 };
  const sockets = new Set();
  const sess = new Map();   // socket -> { admin, fails, lockUntil, p1 }
  const sendTo = (ws, line) => ws.deliver(line);
  const values = () => ({
    state: m.state, credit: m.credit, inserted: m.inserted, maxml: m.maxml(), target: m.target,
    poured: Math.floor(m.segment), total: m.total, unpaid: m.unpaid, p1: m.inv.p1, p5: m.inv.p5,
    b10: m.inv.box10, b20: m.inv.box20, fill: m.fillPump ? 1 : 0,
  });
  const levelLine = () => `LV ${m.tankPct()} 1 ${m.gallonLow("a") ? 1 : 0} ${m.gallonLow("b") ? 1 : 0}`;
  function sendStats(ws) {
    const s = m.sales, d = new Date();
    sendTo(ws, `SD ${s.income} ${s.volume} ${s.count}`);
    sendTo(ws, `DT ${d.getFullYear()} ${d.getMonth() + 1} ${d.getDate()}`);
    // The page reads the timestamp as UTC, as the RTC's local time is sent on the machine.
    s.list.slice(0, 10).forEach((r, i) => {
      const ts = Math.floor((r.ts - new Date(r.ts).getTimezoneOffset() * 60000) / 1000);
      sendTo(ws, `HS ${i} ${ts} ${r.in} ${r.ml} ${r.change} ${r.owed}`);
    });
    sendTo(ws, "HE");
  }
  let last = {};
  function push(all) {
    const v = values(), lines = [];
    for (const k in v) if (all || last[k] !== v[k]) lines.push(`VL ${k} ${v[k]}`);
    if (all || last.fault !== m.fault) lines.push(`FT ${m.fault}`);
    lines.push(levelLine());   // also the page's "still connected" heartbeat
    last = Object.assign({ fault: m.fault }, v);
    lines.push("PG");
    for (const ws of sockets) for (const l of lines) sendTo(ws, l);
  }
  window.vendoPhoneLink = {
    attach(ws) { sockets.add(ws); sess.set(ws, { admin: false, fails: 0, lockUntil: 0, p1: null }); last = {}; push(true); },
    detach(ws) { sockets.delete(ws); sess.delete(ws); },
    receive(ws, text) {
      const a = text.trim().split(/\s+/), s = sess.get(ws); if (!s) return;
      if (a[0] === "EV") {
        const ev = +a[1], p = a[2] === undefined ? -1 : +a[2];
        if (ev === PEV.STATS) { sendStats(ws); return; }
        if (ev === PEV.SET_P1 || ev === PEV.SET_P5 || ev === PEV.CLEAR) {
          if (!s.admin) return;   // the bridge drops admin events from a phone that has not logged in
          if (ev === PEV.SET_P1) s.p1 = p;
          else if (ev === PEV.SET_P5) { m.adminSetChange(s.p1 === null ? m.inv.p1 : s.p1, p); s.p1 = null; }
          else m.say("Phone: " + m.adminClearFault());
          push(false); return;
        }
        if (ev === PEV.CONFIRM && m.state === ST.ACCEPTING) m.touch(EV.CONFIRM);
        else m.touch(ev, p);
        push(false);
      } else if (a[0] === "AD") {
        const now = Date.now();
        if (now < s.lockUntil) { sendTo(ws, `AA L ${Math.ceil((s.lockUntil - now) / 1000)}`); return; }
        if (a[1] === m.pin) {
          s.admin = true; s.fails = 0;
          m.log({ kind: "admin", text: "Admin login (phone)" });
          sendTo(ws, "AA 1");
        } else if (++s.fails >= CFG.PIN_TRIES) {
          s.fails = 0; s.lockUntil = now + CFG.PIN_LOCK_MS;
          m.log({ kind: "security", text: `Phone Admin locked for ${CFG.PIN_LOCK_MS / 60000} min after ${CFG.PIN_TRIES} wrong PINs` });
          sendTo(ws, `AA L ${CFG.PIN_LOCK_MS / 1000}`);
        } else {
          m.log({ kind: "security", text: `Wrong Admin PIN on the phone (${s.fails} of ${CFG.PIN_TRIES})` });
          sendTo(ws, `AA 0 ${s.fails}`);
        }
      } else if (a[0] === "AP") {
        if (!s.admin) { sendTo(ws, "AA N"); return; }
        if (m.adminSetPin(a[1] || "")) sendTo(ws, "AA P");
      } else if (a[0] === "AX") {
        if (s.admin) m.log({ kind: "admin", text: "Admin logout (phone)" });
        s.admin = false;
      }
    },
  };
  $("bPhone").addEventListener("click", () => {
    const on = $("phone").style.display !== "flex";
    $("phone").style.display = on ? "flex" : "none";
    $("bPhone").classList.toggle("lit", on); $("lPhone").textContent = on ? "Phone on" : "Phone";
    if (on && !$("phFrame").src) $("phFrame").src = "phone/index.html";
  });

  // ---- refresh ------------------------------------------------------------------------
  let cardKey = "", recRef = null, lastPush = 0;
  function refresh(now) {
    if (recRef !== m.records) { recRef = m.records; cardKey = ""; }
    const ck = `${m.recVersion}|${m.records.length}|${Math.round(m.tank / 50)}|${m.inv.p1}|${m.inv.p5}|${m.inv.box10}|${m.inv.box20}`;
    if (ck !== cardKey) {
      cardKey = ck;
      $("recTable").innerHTML = tableHTML(m.records);
      $("recTotals").textContent = totalsLines().map(([k, v]) => (k + " ".repeat(15)).slice(0, 15) + v).join("\n");
    }
    // The Mega sends its lines a few times a second; so does the link.
    if (sockets.size && now - lastPush > 250) { lastPush = now; push(false); }
    $("phClock").textContent = new Date().toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
    requestAnimationFrame(refresh);
  }
  requestAnimationFrame(refresh);
})();
