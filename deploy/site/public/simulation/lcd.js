"use strict";
// The 3.5" 480 x 320 customer screen, ported from esp32_bridge/src/ui_lcd.cpp: same coordinates,
// colours, words and buttons. The amount picker is local to the screen until DISPENSE, as there.

const LCD_W = 480, LCD_H = 320, TOP = 44, MAX_BUY_ML = 1500;
const C = { BG: "rgb(12,24,36)", CARD: "rgb(22,38,56)", LINE: "rgb(48,70,92)", INK: "rgb(236,243,248)",
            MUTE: "rgb(140,163,181)", ACC: "rgb(30,136,229)", WATER: "rgb(38,198,218)", OK: "rgb(46,204,113)",
            WARN: "rgb(245,166,35)", BAD: "rgb(229,83,61)", DIM: "rgb(42,59,77)" };
// The board's built-in fonts, matched by size.
const F = { F9: "13px Arial, Helvetica, sans-serif", F9B: "bold 13px Arial, Helvetica, sans-serif",
            F12B: "bold 17px Arial, Helvetica, sans-serif", F18B: "bold 24px Arial, Helvetica, sans-serif",
            F24B: "bold 33px Arial, Helvetica, sans-serif", FONT2: "12px Arial, Helvetica, sans-serif" };
const FAULT_TEXT = ["", "Out of water", "Not enough change in the machine", "Coin box is full",
                    "Change dispenser jammed", "Water flow problem", "Refill pump problem", "Coin slot problem"];
const UI = { SET: -3, STATS_CLOSE: -4, STATS_OPEN: -5,
             // Admin (SPEC 8). Handled on the screen, which then calls the machine.
             A_PAGE: -10, A_STEP: -11, A_CONFIRM: -12, A_CLEAR: -13, A_RESET: -14, A_EXIT: -15,
             K_DIGIT: -16, K_CLR: -17, K_OK: -18, K_CANCEL: -19 };

function money(c) { return c % 100 ? `PHP ${Math.floor(c / 100)}.${String(c % 100).padStart(2, "0")}` : `PHP ${c / 100}`; }
function volume(ml) {
  ml = Math.floor(ml);
  if (ml < 1000) return `${ml} mL`;
  if (ml % 1000 === 0) return `${ml / 1000} L`;
  return `${Math.floor(ml / 1000)}.${Math.floor((ml % 1000) / 100)} L`;
}

class Lcd {
  constructor(canvas, machine) {
    this.cv = canvas; this.g = canvas.getContext("2d"); this.m = machine;
    this.btn = []; this.sel = 100; this.lastState = -1; this.statsOpen = false; this.statsSince = 0;
    this.holdSince = null; this.iconHoldSince = null;
    this.admin = { page: "menu", p1: 0, p5: 0, msg: "" };
    canvas.addEventListener("pointerdown", e => this.down(e));
    const up = () => { this.holdSince = null; this.iconHoldSince = null; };
    canvas.addEventListener("pointerup", up);
    canvas.addEventListener("pointerleave", up);
  }

  // --- Admin (SPEC 8) -------------------------------------------------------------
  // The gesture (or the sidebar button) only opens the PIN pad; Admin opens on the right PIN.
  openAdmin() {
    if (!this.m.canEnterAdmin()) { this.m.say("Admin opens only when no customer is using the machine."); return; }
    this.statsOpen = false;
    this.pinEntry = { digits: "", msg: "" };
  }

  keypad(x, y) {
    const keys = ["1", "2", "3", "4", "5", "6", "7", "8", "9", "Clear", "0", "OK"];
    keys.forEach((k, i) => {
      const bx = x + (i % 3) * 78, by = y + Math.floor(i / 3) * 50;
      const ev = k === "Clear" ? UI.K_CLR : k === "OK" ? UI.K_OK : UI.K_DIGIT;
      this.button(bx, by, 72, 44, k, ev, k, true, k === "OK", k.length > 1 ? F.F9B : F.F12B);
    });
  }

  pinDots(cx, y, n) {
    for (let i = 0; i < CFG.PIN_LEN; i++) {
      const x = cx + (i - (CFG.PIN_LEN - 1) / 2) * 30;
      this.g.beginPath(); this.g.arc(x, y, 9, 0, Math.PI * 2);
      if (i < n) { this.g.fillStyle = C.INK; this.g.fill(); } else { this.g.strokeStyle = C.MUTE; this.g.lineWidth = 2; this.g.stroke(); }
    }
  }

  drawPin() {
    const p = this.pinEntry, m = this.m, locked = m.pinLockedMs();
    this.text("Admin PIN", 14, TOP + 21, F.F12B, C.INK, "left");
    this.button(LCD_W - 104, TOP + 4, 94, 34, "Cancel", UI.K_CANCEL, 0, true, false, F.F9B);
    if (locked > 0) {
      const s = Math.ceil(locked / 1000);
      this.iconWarn(120, 150, 34, C.BAD);
      this.text("Admin locked", 120, 205, F.F18B, C.INK);
      this.text(`Too many wrong PINs. Try again in ${Math.floor(s / 60)}:${String(s % 60).padStart(2, "0")}`, 120, 236, F.FONT2, C.MUTE);
    } else {
      this.text("Enter the 4-digit PIN", 120, 110, F.F9, C.MUTE);
      this.pinDots(120, 145, p.digits.length);
      if (p.msg) this.text(p.msg, 120, 190, F.F9B, C.BAD);
    }
    this.keypad(244, TOP + 50);
  }

  pinAction(b) {
    const p = this.pinEntry, m = this.m;
    if (b.ev === UI.K_CANCEL) { this.pinEntry = null; return; }
    if (m.pinLockedMs() > 0) return;
    if (b.ev === UI.K_DIGIT && p.digits.length < CFG.PIN_LEN) { p.digits += b.payload; p.msg = ""; }
    else if (b.ev === UI.K_CLR) p.digits = "";
    else if (b.ev === UI.K_OK) {
      const r = m.tryPin(p.digits);
      p.digits = "";
      if (r.ok) { this.pinEntry = null; this.admin = { page: "menu", p1: m.inv.p1, p5: m.inv.p5, msg: "", pin: "" }; }
      else p.msg = r.msg;
    }
  }

  adminHeader(title) {
    this.text(title, 14, TOP + 21, F.F12B, C.INK, "left");
    if (this.admin.page === "menu") this.button(LCD_W - 104, TOP + 4, 94, 34, "Exit", UI.A_EXIT, 0, true, false, F.F9B);
    else this.button(LCD_W - 104, TOP + 4, 94, 34, "Back", UI.A_PAGE, "menu", true, false, F.F9B);
  }

  counter(y, label, key) {
    const a = this.admin;
    this.text(label, 14, y + 26, F.F12B, C.INK, "left");
    this.button(140, y, 52, 52, "-10", UI.A_STEP, { key, d: -10 }, a[key] > 0, false, F.F9B);
    this.button(198, y, 52, 52, "-1", UI.A_STEP, { key, d: -1 }, a[key] > 0, false, F.F9B);
    this.rr(256, y, 84, 52, 10, C.CARD, C.LINE);
    this.text(String(a[key]), 298, y + 27, F.F18B, a[key] !== this.m.inv[key] ? C.WARN : C.INK);
    this.button(346, y, 52, 52, "+1", UI.A_STEP, { key, d: 1 }, a[key] < CFG.HOPPER_CAPACITY, false, F.F9B);
    this.button(404, y, 66, 52, "+10", UI.A_STEP, { key, d: 10 }, a[key] < CFG.HOPPER_CAPACITY, false, F.F9B);
  }

  drawAdmin() {
    const m = this.m, a = this.admin, W = LCD_W, H = LCD_H;
    if (a.page === "menu") {
      this.adminHeader("Service mode");
      const f = m.fault ? FAULT_TEXT[m.fault] : "No fault";
      this.text(f, 14, TOP + 52, F.F9B, m.fault ? C.BAD : C.OK, "left");
      const t = m.stats.today;
      this.text(`Today: profit ${money(t.in - t.change)}  ·  ${t.sales} sales`, W - 14, TOP + 52, F.FONT2, C.MUTE, "right");
      const items = [["Profit", "profit", true], ["Coins", "coins", true], ["Water", "water", true],
                     ["Load change", "change", true], ["Clear fault", "clear", !!m.fault], ["History", "history", true]];
      items.forEach(([lab, page, en], i) => {
        const x = 10 + (i % 3) * 155, y = TOP + 68 + Math.floor(i / 3) * 60;
        if (page === "clear") this.button(x, y, 150, 54, lab, UI.A_CLEAR, 0, en, false);
        else this.button(x, y, 150, 54, lab, UI.A_PAGE, page, en, i < 3);
      });
      this.button(10, TOP + 188, 150, 40, "Change PIN", UI.A_PAGE, "pin", true, false, F.F9B);
      if (a.msg) this.text(a.msg, 320, TOP + 208, F.F9B, C.WARN);
    } else if (a.page === "profit") {
      const t = m.stats.today, all = m.stats.all;
      this.adminHeader("Profit");
      const rows = [["Money in", t.in, all.in, "m"], ["Change paid out", t.change, all.change, "m"],
                    ["Profit", t.in - t.change, all.in - all.change, "m"], ["Sales", t.sales, all.sales, "n"],
                    ["Water sold", t.ml, all.ml, "v"], ["Change owed", t.owed, all.owed, "m"]];
      this.text("Today", 300, TOP + 50, F.F9B, C.MUTE, "right");
      this.text("All time", 456, TOP + 50, F.F9B, C.MUTE, "right");
      rows.forEach(([lab, a1, a2, k], i) => {
        const y = TOP + 74 + i * 25, fmt = v => k === "m" ? money(v) : k === "v" ? volume(v) : String(v);
        const c = lab === "Profit" ? C.OK : lab === "Change owed" && (a1 || a2) ? C.WARN : C.INK;
        this.text(lab, 14, y, lab === "Profit" ? F.F9B : F.F9, C.MUTE, "left");
        this.text(fmt(a1), 300, y, F.F9B, c, "right");
        this.text(fmt(a2), 456, y, F.F9B, c, "right");
      });
      this.button(10, LCD_H - 52, 220, 44, "Reset today's totals", UI.A_PAGE, "reset", true, false, F.F9B);
      this.text("Profit = money in - change paid out", 466, LCD_H - 30, F.FONT2, C.MUTE, "right");
    } else if (a.page === "coins") {
      const t = m.stats.today, all = m.stats.all;
      this.adminHeader("Coins");
      this.text("Inserted", 150, TOP + 50, F.F9B, C.MUTE, "right");
      this.text("today", 150, TOP + 64, F.FONT2, C.MUTE, "right");
      this.text("all time", 250, TOP + 64, F.FONT2, C.MUTE, "right");
      this.text("Now in the machine", 456, TOP + 50, F.F9B, C.MUTE, "right");
      const where = { 1: `P1 hopper  x ${m.inv.p1}`, 5: `P5 hopper  x ${m.inv.p5}`,
                      10: `coin box  x ${m.inv.box10}`, 20: `coin box  x ${m.inv.box20}` };
      [1, 5, 10, 20].forEach((p, i) => {
        const y = TOP + 88 + i * 26;
        this.text(`P${p}`, 14, y, F.F12B, C.INK, "left");
        this.text(`${t.coins[p]}`, 150, y, F.F9B, C.INK, "right");
        this.text(`${all.coins[p]}`, 250, y, F.F9B, C.INK, "right");
        this.text(where[p], 456, y, F.F9, C.WATER, "right");
      });
      const box = m.inv.box10 * 10 + m.inv.box20 * 20, change = m.inv.p1 + m.inv.p5 * 5;
      this.text(`Coin box PHP ${box}   ·   change float PHP ${change}`, 14, TOP + 202, F.F9B, C.INK, "left");
      this.text(`Returned to tray (slot closed): today ${t.returned}, all time ${all.returned}`, 14, TOP + 226, F.FONT2, C.MUTE, "left");
      this.text("Fake coins are rejected by the coinslot itself and not counted.", 14, TOP + 246, F.FONT2, C.MUTE, "left");
    } else if (a.page === "water") {
      const t = m.stats.today, all = m.stats.all, pct = m.tankPct();
      this.adminHeader("Water");
      this.text("Cold tank", 14, TOP + 56, F.F9B, C.MUTE, "left");
      this.text(`${Math.round(m.tank)} mL  (${pct}%)`, 456, TOP + 56, F.F12B, pct < 30 ? C.WARN : C.WATER, "right");
      this.rr(14, TOP + 70, 442, 18, 9, C.DIM);
      if (pct > 0) this.rr(14, TOP + 70, Math.max(18, 442 * pct / 100), 18, 9, pct < 30 ? C.WARN : C.WATER);
      const gal = g => m.gallonLow(g) ? ["LOW - replace", C.WARN] : ["OK", C.OK];
      [["Gallon A", "a"], ["Gallon B", "b"]].forEach(([lab, g], i) => {
        const [txt, c] = gal(g), y = TOP + 112 + i * 26;
        this.text(lab, 14, y, F.F9B, C.MUTE, "left");
        this.text(txt, 456, y, F.F9B, c, "right");
      });
      this.text("Refill pump", 14, TOP + 164, F.F9B, C.MUTE, "left");
      this.text(m.fillPump ? "running" : (m.gallonLow("a") || m.gallonLow("b")) ? "stopped: a gallon is low" : "off",
                456, TOP + 164, F.F9B, C.INK, "right");
      this.text("Water sold", 14, TOP + 190, F.F9B, C.MUTE, "left");
      this.text(`today ${volume(t.ml)}  ·  all time ${volume(all.ml)}`, 456, TOP + 190, F.F9B, C.INK, "right");
      this.text("Gallon sensors only say OK or low; the tank level is measured.", 14, TOP + 222, F.FONT2, C.MUTE, "left");
    } else if (a.page === "pin") {
      this.adminHeader("Change PIN");
      this.text(`New ${CFG.PIN_LEN}-digit PIN`, 120, 110, F.F9, C.MUTE);
      this.pinDots(120, 145, a.pin.length);
      if (a.msg) this.text(a.msg, 120, 190, F.F9B, a.msg.startsWith("PIN") ? C.OK : C.BAD);
      this.keypad(244, TOP + 50);
    } else if (a.page === "change") {
      this.adminHeader("Load change");
      this.text("Count the coins in each hopper, enter it, then CONFIRM.", 14, TOP + 52, F.FONT2, C.MUTE, "left");
      this.counter(TOP + 68, "P1 hopper", "p1");
      this.counter(TOP + 128, "P5 hopper", "p5");
      const changed = a.p1 !== m.inv.p1 || a.p5 !== m.inv.p5;
      this.button(10, H - 58, 150, 52, "Cancel", UI.A_PAGE, "menu", true, false);
      this.button(170, H - 58, 300, 52, "CONFIRM", UI.A_CONFIRM, 0, changed, true, F.F18B);
    } else if (a.page === "history") {
      this.adminHeader("History (last 20)");
      if (!m.history.length) { this.text("Nothing recorded yet", W / 2, 180, F.F9, C.MUTE); return; }
      const p2 = n => String(n).padStart(2, "0");
      m.history.slice(0, 10).forEach((e, i) => {
        const t = new Date(e.ts), y = TOP + 58 + i * 21;
        this.text(`${p2(t.getHours())}:${p2(t.getMinutes())}`, 14, y, F.FONT2, C.MUTE, "left");
        this.text(e.text.replace(/₱/g, "P").slice(0, 60), 60, y, F.FONT2,
                  e.kind === "jam" || /OWED/.test(e.text) ? C.WARN : e.kind === "sale" ? C.INK : C.WATER, "left");
      });
      if (m.history.length > 10) this.text(`+ ${m.history.length - 10} older`, W - 14, H - 12, F.FONT2, C.MUTE, "right");
    } else if (a.page === "totals") {
      const s = m.sales;
      this.adminHeader("Daily totals");
      this.statCard(10, TOP + 46, "Total income", money(s.income), C.OK);
      this.statCard(166, TOP + 46, "Water dispensed", volume(s.volume), C.WATER);
      this.statCard(322, TOP + 46, "Purchases", String(s.count), C.INK);
      this.button(110, H - 78, 260, 58, "Reset daily totals", UI.A_PAGE, "reset", true, false);
    } else if (a.page === "reset") {
      this.adminHeader("Reset daily totals?");
      this.iconWarn(W / 2, 120, 30, C.WARN);
      this.text("Today's income, volume and count go back to zero.", W / 2, 178, F.F9, C.INK);
      this.text("The reset is written to the history.", W / 2, 200, F.F9, C.MUTE);
      this.button(10, H - 64, 222, 56, "Cancel", UI.A_PAGE, "profit", true, false);
      this.button(248, H - 64, 222, 56, "Reset", UI.A_RESET, 0, true, true);
    }
  }

  adminAction(b) {
    const a = this.admin, m = this.m;
    m.adminTouched();
    if (a.page === "pin" && (b.ev === UI.K_DIGIT || b.ev === UI.K_CLR || b.ev === UI.K_OK)) {
      if (b.ev === UI.K_DIGIT && a.pin.length < CFG.PIN_LEN) { a.pin += b.payload; a.msg = ""; }
      else if (b.ev === UI.K_CLR) a.pin = "";
      else if (b.ev === UI.K_OK) {
        if (m.adminSetPin(a.pin)) { a.page = "menu"; a.msg = "PIN changed"; } else a.msg = `Needs ${CFG.PIN_LEN} digits`;
        a.pin = "";
      }
      return;
    }
    if (b.ev === UI.A_PAGE) {
      a.page = b.payload; a.msg = ""; a.pin = "";
      if (a.page === "change") { a.p1 = m.inv.p1; a.p5 = m.inv.p5; }
    } else if (b.ev === UI.A_STEP) {
      a[b.payload.key] = Math.max(0, Math.min(CFG.HOPPER_CAPACITY, a[b.payload.key] + b.payload.d));
    } else if (b.ev === UI.A_CONFIRM) {
      m.adminSetChange(a.p1, a.p5); a.page = "menu"; a.msg = "Change count saved";
    } else if (b.ev === UI.A_CLEAR) {
      a.msg = m.adminClearFault();
    } else if (b.ev === UI.A_RESET) {
      m.adminResetDaily(); a.page = "profit";
    } else if (b.ev === UI.A_EXIT) {
      m.exitAdmin();
    }
  }

  // --- drawing primitives -----------------------------------------------------
  rr(x, y, w, h, r, fill, stroke) {
    const g = this.g; g.beginPath(); g.roundRect(x, y, w, h, r);
    if (fill) { g.fillStyle = fill; g.fill(); }
    if (stroke) { g.strokeStyle = stroke; g.lineWidth = 1; g.stroke(); }
  }
  text(s, x, y, font, color, align = "center") {
    const g = this.g; g.font = font; g.fillStyle = color; g.textAlign = align; g.textBaseline = "middle"; g.fillText(s, x, y);
  }
  button(x, y, w, h, label, ev, payload, enabled, primary, font = F.F12B) {
    const fill = !enabled ? C.DIM : primary ? C.ACC : C.CARD;
    this.rr(x, y, w, h, 12, fill, enabled && !primary ? C.LINE : null);
    this.text(label, x + w / 2, y + h / 2 + 1, font, enabled ? C.INK : C.MUTE);
    this.btn.push({ x, y, w, h, ev, payload, enabled });
  }
  circle(cx, cy, r, c) { const g = this.g; g.beginPath(); g.arc(cx, cy, r, 0, Math.PI * 2); g.fillStyle = c; g.fill(); }
  tri(x1, y1, x2, y2, x3, y3, c) { const g = this.g; g.beginPath(); g.moveTo(x1, y1); g.lineTo(x2, y2); g.lineTo(x3, y3); g.closePath(); g.fillStyle = c; g.fill(); }

  iconDrop(cx, cy, r, c) {
    this.circle(cx, cy + r / 3, r, c);
    this.tri(cx, cy - r * 3 / 2, cx - r * 0.9, cy, cx + r * 0.9, cy, c);
    this.circle(cx - r / 3, cy + r / 4, r / 4, C.INK);
  }
  iconCheck(cx, cy, r, c) {
    this.circle(cx, cy, r, c);
    const g = this.g; g.strokeStyle = C.BG; g.lineWidth = (r / 6 + 1) * 2; g.lineCap = "butt";
    g.beginPath(); g.moveTo(cx - r / 2, cy); g.lineTo(cx - r / 8, cy + r / 3); g.lineTo(cx + r / 2, cy - r / 3); g.stroke();
  }
  iconWarn(cx, cy, r, c) {
    this.tri(cx, cy - r, cx - r, cy + r * 3 / 4, cx + r, cy + r * 3 / 4, c);
    this.rr(cx - r / 10, cy - r / 2, r / 5 + 1, r * 3 / 4, 2, C.BG);
    this.circle(cx, cy + r / 2, r / 8 + 1, C.BG);
  }
  iconBottle(cx, cy, h, c) {
    const w = h / 2;
    this.rr(cx - w / 2, cy - h / 4, w, h * 3 / 4, 8, c);
    this.g.fillStyle = c; this.g.fillRect(cx - w / 5, cy - h / 2, w * 2 / 5, h / 4 + 2);
    this.g.fillStyle = C.ACC; this.g.fillRect(cx - w / 4, cy - h / 2 - 6, w / 2, 8);
    this.g.fillStyle = C.WATER; this.g.fillRect(cx - w / 2 + 4, cy + h / 8, w - 8, h * 3 / 8 - 6);
  }
  iconCoin(cx, cy, r) {
    this.circle(cx, cy, r, C.WARN);
    const g = this.g; g.beginPath(); g.arc(cx, cy, r - 5, 0, Math.PI * 2); g.strokeStyle = C.BG; g.lineWidth = 1; g.stroke();
    this.text("P", cx, cy + 2, F.F18B, C.BG);
  }

  // --- screens ----------------------------------------------------------------
  drawTop() {
    const m = this.m, g = this.g;
    g.fillStyle = C.CARD; g.fillRect(0, 0, LCD_W, TOP - 4);
    g.fillStyle = C.LINE; g.fillRect(0, TOP - 4, LCD_W, 1);
    this.iconDrop(20, 19, 7, C.WATER);
    this.text("WATER VENDO", 36, 21, F.F12B, C.INK, "left");
    const a = m.gallonLow("a"), b = m.gallonLow("b");
    if (a || b) {
      this.rr(210, 9, 130, 22, 11, C.WARN);
      this.text(a && b ? "Both gallons low" : a ? "Gallon A low" : "Gallon B low", 275, 21, F.FONT2, C.BG);
    }
    this.rr(352, 11, 76, 18, 9, null, C.LINE);
    const pct = m.tankPct(), fill = Math.floor(72 * pct / 100);
    if (fill > 0) this.rr(354, 13, fill, 14, 7, pct < 25 ? C.WARN : C.WATER);
    this.text(`${pct}%`, 470, 21, F.FONT2, C.INK, "right");
  }

  drawProgress() {
    const m = this.m, t = m.target > 0 ? m.target : 1;
    const pc = Math.min(100, m.target > 0 ? Math.floor(m.segment * 100 / t) : 100);
    this.text(`${pc}%`, LCD_W / 2, 150, F.F24B, C.INK);
    this.rr(40, 190, 400, 24, 12, C.DIM);
    if (pc > 0) this.rr(40, 190, Math.max(24, 400 * pc / 100), 24, 12, C.WATER);
    this.text(`${volume(m.segment)} of ${volume(m.target)}`, LCD_W / 2, 232, F.F9, C.MUTE);
  }

  clampSel() {
    const max = this.m.maxml();
    if (this.sel > max) this.sel = max;
    if (this.sel > MAX_BUY_ML) this.sel = MAX_BUY_ML;
    if (this.sel < 100) this.sel = 100;
  }

  tile(x, y, w, h, ml) {
    const enabled = ml <= this.m.maxml(), chosen = enabled && ml === this.sel;
    this.rr(x, y, w, h, 10, !enabled ? C.DIM : chosen ? C.ACC : C.CARD, enabled && !chosen ? C.LINE : null);
    this.text(volume(ml), x + w / 2, y + h / 2 - 8, F.F9B, enabled ? C.INK : C.MUTE);
    this.text(`PHP ${ml / 100}`, x + w / 2, y + h / 2 + 13, F.FONT2, enabled ? (chosen ? C.INK : C.WATER) : C.MUTE);
    this.btn.push({ x, y, w, h, ev: UI.SET, payload: ml, enabled });
  }

  drawSelect() {
    const m = this.m;
    this.text(`Credit  ${money(m.credit)}`, 14, TOP + 18, F.F12B, C.INK, "left");
    this.clampSel();
    const canBuy = m.maxml() >= 100;
    if (canBuy) this.text(volume(this.sel), LCD_W - 14, TOP + 18, F.F12B, C.WATER, "right");
    for (let i = 0; i < MAX_BUY_ML / 100; i++) this.tile(10 + (i % 5) * 93, TOP + 40 + Math.floor(i / 5) * 58, 86, 52, (i + 1) * 100);
    this.button(10, LCD_H - 58, 150, 52, "Refund", EV.FINISH, -1, true, false);
    this.button(170, LCD_H - 58, 300, 52, "DISPENSE", EV.SELECT, this.sel, canBuy, true, F.F18B);
  }

  statCard(x, y, label, value, c) {
    this.rr(x, y, 148, 66, 10, C.CARD);
    this.text(label, x + 12, y + 17, F.FONT2, C.MUTE, "left");
    this.text(value, x + 12, y + 45, F.F12B, c, "left");
  }

  drawStats() {
    const s = this.m.sales, d = new Date();
    const MON = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];
    this.text(`Sales today  -  ${d.getDate()} ${MON[d.getMonth()]} ${d.getFullYear()}`, 14, TOP + 21, F.F12B, C.INK, "left");
    this.button(LCD_W - 104, TOP + 4, 94, 34, "Back", UI.STATS_CLOSE, 0, true, false, F.F9B);
    this.statCard(10, TOP + 46, "Total income", money(s.income), C.OK);
    this.statCard(166, TOP + 46, "Water dispensed", volume(s.volume), C.WATER);
    this.statCard(322, TOP + 46, "Purchases", String(s.count), C.INK);
    this.text("Last 10 purchases", 14, TOP + 128, F.FONT2, C.MUTE, "left");
    this.g.fillStyle = C.LINE; this.g.fillRect(14, TOP + 138, LCD_W - 28, 1);
    if (!s.list.length) { this.text("No purchases yet", LCD_W / 2, TOP + 190, F.F9, C.MUTE); return; }
    s.list.forEach((r, i) => {
      const x = 14 + Math.floor(i / 5) * 234, y = TOP + 153 + (i % 5) * 25, t = new Date(r.ts);
      const p2 = n => String(n).padStart(2, "0");
      this.text(`${p2(t.getDate())}/${p2(t.getMonth() + 1)} ${p2(t.getHours())}:${p2(t.getMinutes())}`, x, y, F.FONT2, C.MUTE, "left");
      if (r.owed > 0) { this.text(`${volume(r.ml)} owed ${money(r.owed).slice(4)}`, x + 96, y, F.FONT2, C.WARN, "left"); return; }
      this.text(money(r.in), x + 96, y, F.FONT2, C.INK, "left");
      this.text(volume(r.ml), x + 160, y, F.FONT2, C.WATER, "left");
    });
  }

  drawBody() {
    const m = this.m, W = LCD_W, H = LCD_H, c = money(m.credit);
    if (m.state === ST.ADMIN) { this.drawAdmin(); return; }
    if (this.pinEntry) { this.drawPin(); return; }
    if (this.statsOpen) { this.drawStats(); return; }
    switch (m.state) {
      case ST.ACCEPTING:
        this.text("Credit", W / 2, TOP + 36, F.F9, C.MUTE);
        this.text(c, W / 2, TOP + 84, F.F24B, C.INK);
        this.text("Insert more coins, or tap Continue", W / 2, TOP + 132, F.F9, C.MUTE);
        this.button(120, H - 92, 240, 70, "CONTINUE", EV.CONFIRM, -1, true, true, F.F18B);
        break;
      case ST.SELECTING: this.drawSelect(); break;
      case ST.AWAITING:
        this.iconBottle(W / 2, 112, 84, C.INK);
        this.text("Place your bottle under the nozzle", W / 2, 192, F.F12B, C.INK);
        this.text(`${volume(m.target)} will be dispensed`, W / 2, 222, F.F9, C.MUTE);
        this.button(150, H - 70, 180, 54, "Change amount", EV.BACK, -1, true, false, F.F9B);
        break;
      case ST.DISPENSING: case ST.SETTLING:
        this.text("Dispensing", W / 2, TOP + 30, F.F12B, C.MUTE);
        this.drawProgress();
        break;
      case ST.PAUSED:
        this.iconWarn(W / 2, 118, 34, C.WARN);
        this.text("Bottle removed", W / 2, 192, F.F18B, C.INK);
        this.text("Put it back within 10 seconds to continue", W / 2, 230, F.F9, C.MUTE);
        break;
      case ST.COMPLETE:
        this.iconCheck(60, TOP + 46, 26, C.OK);
        this.text(`${volume(m.total)} dispensed`, 100, TOP + 46, F.F18B, C.INK, "left");
        this.text("Credit left", W / 2, TOP + 104, F.F9, C.MUTE);
        this.text(c, W / 2, TOP + 140, F.F24B, C.INK);
        this.button(12, H - 78, 222, 64, "Dispense more", EV.AGAIN, -1, true, true);
        this.button(246, H - 78, 222, 64, "Finish", EV.FINISH, -1, true, false);
        break;
      case ST.PAYING:
        this.iconCoin(W / 2, 120, 32);
        this.text("Returning your change", W / 2, 192, F.F12B, C.INK);
        this.text(c, W / 2, 232, F.F18B, C.WARN);
        break;
      case ST.THANKYOU:
        this.iconCheck(W / 2, 118, 36, C.OK);
        this.text("Thank you!", W / 2, 192, F.F24B, C.INK);
        this.text(`Dispensed ${volume(m.total)}   -   Change ${money(m.changePaid)}`, W / 2, 240, F.F9, C.MUTE);
        break;
      case ST.FAULT:
        this.iconWarn(W / 2, 112, 34, C.BAD);
        this.text("Out of service", W / 2, 180, F.F18B, C.INK);
        if (m.fault > 0 && m.fault < FAULT_TEXT.length) this.text(FAULT_TEXT[m.fault], W / 2, 216, F.F9B, C.BAD);
        this.text("Please contact the attendant", W / 2, 250, F.F9, C.MUTE);
        break;
      default:
        this.iconDrop(W / 2, 118, 32, C.WATER);
        this.text("Insert coins to start", W / 2, 200, F.F18B, C.INK);
        this.text("1 peso = 100 mL   -   up to 15 pesos (1.5 L)", W / 2, 238, F.F9, C.MUTE);
        this.button(W - 112, H - 48, 102, 38, "Sales", UI.STATS_OPEN, 0, true, false, F.F9B);
    }
  }

  draw(realNow) {
    const m = this.m;
    // A fresh visit to the picker starts at the smallest size, 100 mL (team decision 2026-10-27).
    if (m.state === ST.SELECTING && this.lastState !== ST.SELECTING) this.sel = 100;
    this.lastState = m.state;
    const idle = m.state === ST.STANDBY || m.state === ST.FAULT;
    if (this.statsOpen && (!idle || realNow - this.statsSince > 60000)) this.statsOpen = false;
    if (this.holdSince !== null && realNow - this.holdSince >= 2000 && idle && !this.statsOpen) {
      this.statsOpen = true; this.statsSince = realNow; this.holdSince = null;
    }
    // The Admin gesture: hold the big icon on the idle or out-of-service screen for 3 s.
    if (this.iconHoldSince !== null && realNow - this.iconHoldSince >= CFG.ADMIN_HOLD_MS && idle && !this.statsOpen) {
      this.iconHoldSince = null; this.openAdmin();
    }
    if (m.state !== ST.ADMIN && this.admin.page !== "menu") this.admin.page = "menu";
    // The PIN pad gives way to a customer: it only stays up on the idle or out-of-service screen.
    if (this.pinEntry && !idle) this.pinEntry = null;
    this.btn = [];
    this.g.fillStyle = C.BG; this.g.fillRect(0, 0, LCD_W, LCD_H);
    this.drawTop();
    this.drawBody();
  }

  down(e) {
    const r = this.cv.getBoundingClientRect();
    const x = (e.clientX - r.left) * LCD_W / r.width, y = (e.clientY - r.top) * LCD_H / r.height;
    if (y < TOP && x < 260) this.holdSince = performance.now();   // hold the title for the sales screen
    if (!this.statsOpen && !this.pinEntry && (this.m.state === ST.STANDBY || this.m.state === ST.FAULT) &&
        x > 180 && x < 300 && y > 64 && y < 168) this.iconHoldSince = performance.now();   // hold the icon for Admin
    for (const b of this.btn) {
      if (!b.enabled || x < b.x || x >= b.x + b.w || y < b.y || y >= b.y + b.h) continue;
      if (b.ev <= UI.A_PAGE && this.pinEntry) this.pinAction(b);
      else if (b.ev <= UI.A_PAGE) this.adminAction(b);
      else if (b.ev === UI.SET) { this.sel = b.payload; this.clampSel(); }
      else if (b.ev === UI.STATS_OPEN) { this.statsOpen = true; this.statsSince = performance.now(); }
      else if (b.ev === UI.STATS_CLOSE) this.statsOpen = false;
      else this.m.touch(b.ev, b.payload);
      if (this.onTouch) this.onTouch();
      break;
    }
  }
}
