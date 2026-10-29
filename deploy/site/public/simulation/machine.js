"use strict";
// The machine's behaviour, following the firmware (src/main.cpp, billing.cpp, change_plan.cpp,
// include/config.h). Money is integer centavos and volume integer mL, as on the Mega.
// The scene and the screen only read this state and send it the same events the
// touchscreen and sensors send the Mega.

const CFG = {
  ML_PER_PESO: 100,
  MAX_CENTAVOS: 1500,             // P15 ceiling (team decision 2026-10-27)
  MAX_ML: 1500,
  WORST_CASE_CHANGE: 1500 - 100 + 2000,   // P14 then a P20: the LOW CHANGE gate
  GATE_ROUTE_MS: 900,
  GATE_RETURN_MS: 400,
  PULSE_MS: 50,                   // one coinslot pulse, for the identify delay only
  BOTTLE_WARN1_MS: 15000,
  BOTTLE_WARN2_MS: 18000,
  BOTTLE_CANCEL_MS: 20000,
  GRACE_MS: 10000,
  SETTLE_MS: 500,
  COMPLETE_TIMEOUT_MS: 60000,
  THANKYOU_MS: 8000,
  HOP_ON_MS: 1200,
  HOP_OFF_MS: 400,
  HOP_TIMEOUT_MS: 5000,
  HOP_RETRY_MAX: 3,
  DISPENSE_MS_PER_100ML: 7000,    // placeholder in config.h, to be measured
  FILL_ML_PER_S: 30,              // illustrative
  TANK_ML: 6000,
  TANK_REFILL_ML: 5700,           // REFILL / FULL band
  TANK_MIN_SERVE_ML: 1800,        // a full P15 pour plus margin
  GALLON_ML: 18900,
  GALLON_LOW_ML: 2500,            // capacitive sensor at the reserve line
  HOPPER_CAPACITY: 500,           // bounds the Admin change edit
  HISTORY_N: 20,                  // Admin history ring
  ADMIN_HOLD_MS: 3000,            // the gesture: hold the screen icon this long
  PIN_LEN: 4,
  PIN_TRIES: 3,                   // wrong PINs before Admin locks
  PIN_LOCK_MS: 300000,            // lockout after too many wrong PINs
  ADMIN_IDLE_MS: 60000,           // Admin logs itself out after this long untouched
};
const DEMO_PIN = "1234";          // simulation only; each machine's PIN is set by its owner
function blankStats() { return { in: 0, change: 0, sales: 0, ml: 0, owed: 0, returned: 0, coins: { 1: 0, 5: 0, 10: 0, 20: 0 } }; }
const FAULT_NAMES = ["", "OUT OF WATER", "LOW CHANGE", "COIN STORAGE FULL", "CHANGE JAM"];

const ST = { BOOT: 0, STANDBY: 1, ACCEPTING: 2, SELECTING: 3, AWAITING: 4, DISPENSING: 5, PAUSED: 6,
             SETTLING: 7, COMPLETE: 8, PAYING: 9, THANKYOU: 10, FAULT: 11, ADMIN: 12 };
const EV = { SELECT: 1, AGAIN: 2, FINISH: 3, BACK: 4, CONFIRM: 15 };
const FAULT = { NONE: 0, OUT_OF_WATER: 1, LOW_CHANGE: 2, STORAGE_FULL: 3, CHANGE_JAM: 4 };
const COIN_PULSES = { 1: 1, 5: 5, 10: 10, 20: 20 };

function changePlan(centavos, p1, p5) {
  // Largest coin first; P5 reserve is 0 in the delivered build. P1 is the coin of last resort.
  if (centavos < 0 || centavos % 100) return null;
  const pesos = centavos / 100;
  const n5 = Math.min(Math.floor(pesos / 5), p5);
  const n1 = pesos - n5 * 5;
  if (n1 > p1) return null;
  return { p1: n1, p5: n5 };
}

class Machine {
  constructor() { this.listeners = []; this.reset(); }

  reset() {
    this.now = 0;
    this.timers = []; this.hopperMotor = null; this.pendingConfirm = false;
    this.state = ST.STANDBY; this.enteredAt = 0;
    this.credit = 0; this.inserted = 0; this.target = 0; this.total = 0;
    this.segment = 0;          // mL poured toward the current target
    this.unpaid = 0; this.changePaid = 0; this.fault = FAULT.NONE;
    this.inv = { p1: 60, p5: 20, box10: 0, box20: 0 };
    this.tray = [];            // coins in the change / return tray
    this.coinBusyUntil = 0;    // acceptor inhibited (pulse train + gate cycle)
    this.inFlight = 0;         // coins identified but not yet routed
    this.gates = { g1: "open", g2: "open" };
    this.tank = CFG.TANK_ML; this.galA = 15000; this.galB = 13000;
    this.refillOwed = 0; this.fillPump = false; this.dispensePump = false;
    this.bottle = false; this.warns = 0;
    this.boxFull = false; this.jamNext = false;
    this.pay = null;
    this.sales = { income: 0, volume: 0, count: 0, list: [] };
    this.history = [];
    this.stats = { today: blankStats(), all: blankStats() };
    this.pin = DEMO_PIN; this.pinFails = 0; this.pinLockUntil = 0; this.adminSeen = 0;
    this.story = "The machine is ready. Insert a coin to start.";
    this.events = [];          // things the 3D scene animates: {type, ...}
    this.emit({ type: "reset" });
  }

  emit(e) { this.events.push(e); }
  say(s) { this.story = s; }
  elapsed() { return this.now - this.enteredAt; }
  go(s) { this.state = s; this.enteredAt = this.now; this.onEnter(s); }

  maxml() { return Math.min(Math.floor(this.credit / 100) * 100, CFG.MAX_ML); }
  acceptorOpen() {
    return (this.state === ST.STANDBY || this.state === ST.ACCEPTING) &&
           this.now >= this.coinBusyUntil && this.inserted < CFG.MAX_CENTAVOS;
  }
  gallonLow(g) { return (g === "a" ? this.galA : this.galB) < CFG.GALLON_LOW_ML; }
  tankPct() { return Math.max(0, Math.min(100, Math.round(this.tank * 100 / CFG.TANK_ML))); }

  // --- inputs ---------------------------------------------------------------
  insertCoin(peso) {
    if (!this.acceptorOpen()) {
      this.emit({ type: "coin", peso, dest: "tray" });
      this.tray.push(peso);
      this.stats.today.returned++; this.stats.all.returned++;
      const why = this.state === ST.FAULT ? "The machine is out of service, so the coin slot is switched off"
        : (this.state === ST.STANDBY || this.state === ST.ACCEPTING) && this.now < this.coinBusyUntil
          ? "The slot was still closed while the gates moved for the last coin"
          : this.inserted >= CFG.MAX_CENTAVOS ? "The ₱15 ceiling is reached, so the slot is switched off"
          : "The coin slot only takes coins before an amount is chosen";
      this.say(`${why}. The ₱${peso} coin went straight to the tray.`);
      return;
    }
    // The coinslot only says what the coin was once its pulse train ends.
    const idMs = 150 + COIN_PULSES[peso] * CFG.PULSE_MS * 2;
    const dest = peso === 5 ? "p5" : peso === 1 ? "p1" : "box";
    this.coinBusyUntil = this.now + idMs + CFG.GATE_ROUTE_MS + CFG.GATE_RETURN_MS;
    this.inFlight++;
    this.timers.push({ at: this.now + idMs, fn: () => this.coinIdentified(peso, dest) });
    this.emit({ type: "coin", peso, dest, idMs, routeMs: CFG.GATE_ROUTE_MS });
  }

  coinIdentified(peso, dest) {
    if (this.state === ST.STANDBY) this.go(ST.ACCEPTING);
    this.credit += peso * 100; this.inserted += peso * 100;
    this.stats.today.coins[peso]++; this.stats.all.coins[peso]++;
    this.gates = { g1: dest === "p5" ? "closed" : "open", g2: dest === "p1" ? "closed" : "open" };
    const where = dest === "p5" ? "Gate 1 closes and sends it to the ₱5 change hopper"
      : dest === "p1" ? "Gate 1 stays open, gate 2 closes and sends it to the ₱1 change hopper"
      : "Both gates stay open, so it runs on to the locked coin box";
    this.say(`₱${peso} identified (${COIN_PULSES[peso]} pulse${peso > 1 ? "s" : ""}). ${where}. Credit ₱${this.credit / 100}.`);
    this.timers.push({ at: this.now + CFG.GATE_ROUTE_MS, fn: () => {
      if (dest === "p1") this.inv.p1++; else if (dest === "p5") this.inv.p5++;
      else if (peso === 10) this.inv.box10++; else this.inv.box20++;
      this.gates = { g1: "open", g2: "open" };
      this.inFlight--;
    } });
  }

  placeBottle(on) {
    this.bottle = on;
    if (!on && this.state === ST.THANKYOU && !this.unpaid) this.go(ST.STANDBY);
  }

  takeTray() { const coins = this.tray; this.tray = []; this.emit({ type: "trayEmpty" }); return coins; }

  touch(ev, payload) {
    const s = this.state;
    if (ev === EV.CONFIRM && s === ST.ACCEPTING) { this.pendingConfirm = true; return; }
    if (s === ST.SELECTING) {
      if (ev === EV.SELECT && payload > 0 && payload <= CFG.MAX_ML && payload % 100 === 0 &&
          payload * 1 <= this.credit) {
        this.credit -= payload; this.target = payload; this.segment = 0;   // price = mL in centavos
        this.go(ST.AWAITING);
      } else if (ev === EV.FINISH || ev === EV.BACK) {
        this.go(ST.PAYING);
      }
    } else if (s === ST.AWAITING && ev === EV.BACK) {
      this.credit += this.target; this.target = 0;
      this.say("Amount cancelled before any water poured. The whole price goes back to the credit.");
      this.go(ST.SELECTING);
    } else if (s === ST.COMPLETE) {
      if (ev === EV.AGAIN) this.go(ST.SELECTING);
      else if (ev === EV.FINISH || ev === EV.BACK) this.go(ST.PAYING);
    }
  }

  // --- Admin (SPEC 8) ----------------------------------------------------------
  // Entered by a gesture not reachable in normal use, from STANDBY or FAULT only, so never
  // with a customer's money in the machine. Arriving does NOT clear a fault.
  canEnterAdmin() { return this.state === ST.STANDBY || this.state === ST.FAULT; }
  enterAdmin() {
    if (!this.canEnterAdmin()) { this.say("Admin opens only when no customer is using the machine."); return false; }
    this.go(ST.ADMIN); this.adminSeen = this.now;
    this.say("Admin (service mode). The coin slot is closed while it is open; it logs out after 60 s untouched.");
    return true;
  }
  exitAdmin() {
    if (this.state !== ST.ADMIN) return;
    this.log({ kind: "admin", text: "Admin logout" });
    this.say("Admin closed. The machine re-checks water, coin box and change before it takes money.");
    this.go(this.fault ? ST.FAULT : ST.STANDBY);
  }
  // Load change: the one write that can put the records out of step with the hoppers, so it
  // is committed only on an explicit confirm, and bounded by what a hopper can hold.
  adminSetChange(p1, p5) {
    const was = { p1: this.inv.p1, p5: this.inv.p5 };
    this.inv.p1 = Math.max(0, Math.min(CFG.HOPPER_CAPACITY, p1 | 0));
    this.inv.p5 = Math.max(0, Math.min(CFG.HOPPER_CAPACITY, p5 | 0));
    this.log({ kind: "change", text: `Change set: ₱1 ${was.p1} → ${this.inv.p1}, ₱5 ${was.p5} → ${this.inv.p5}` });
    // LOW CHANGE is condition-driven: it clears the moment the count covers the worst case.
    if (this.fault === FAULT.LOW_CHANGE && changePlan(CFG.WORST_CASE_CHANGE, this.inv.p1, this.inv.p5)) this.fault = FAULT.NONE;
    this.say(`Change count confirmed and saved: ${this.inv.p1} × ₱1, ${this.inv.p5} × ₱5.` +
             (this.fault === FAULT.LOW_CHANGE ? " Still not enough for the worst case (₱34), so LOW CHANGE stays." : ""));
  }
  // Clear a fault: refused while its physical cause is still detectable.
  adminClearFault() {
    const f = this.fault;
    if (!f) return "No fault to clear.";
    if (f === FAULT.LOW_CHANGE) return "LOW CHANGE clears by loading change, not by clearing it.";
    if (f === FAULT.OUT_OF_WATER && this.tank < CFG.TANK_MIN_SERVE_ML) return "The tank is still below the serve line.";
    if (f === FAULT.STORAGE_FULL && this.boxFull) return "The coin box sensor still reads full. Empty the box first.";
    this.fault = FAULT.NONE;
    this.log({ kind: "clear", text: `Fault cleared: ${FAULT_NAMES[f]}` });
    this.say(`Attendant cleared ${FAULT_NAMES[f]}.`);
    return `${FAULT_NAMES[f]} cleared.`;
  }
  adminResetDaily() {
    const s = this.sales;
    this.log({ kind: "day", text: `Daily totals reset by hand (were ${s.count} sales, ₱${s.income / 100}, ${s.volume} mL)` });
    this.sales = { income: 0, volume: 0, count: 0, list: [] };
    this.stats.today = blankStats();
    this.say("Daily totals reset by the attendant. All-time totals are kept, and the history records the reset.");
  }

  // --- Admin security -------------------------------------------------------------
  // PIN before Admin opens; a few wrong tries lock it out; it logs itself out when left open.
  pinLockedMs() { return Math.max(0, this.pinLockUntil - this.now); }
  tryPin(p) {
    if (!this.canEnterAdmin()) return { ok: false, msg: "Admin opens only when no customer is using the machine" };
    if (this.pinLockedMs() > 0) return { ok: false, msg: "Admin is locked" };
    if (p === this.pin) {
      this.pinFails = 0;
      this.log({ kind: "admin", text: "Admin login" });
      this.enterAdmin();
      return { ok: true };
    }
    this.pinFails++;
    this.log({ kind: "security", text: `Wrong Admin PIN (${this.pinFails} of ${CFG.PIN_TRIES})` });
    if (this.pinFails >= CFG.PIN_TRIES) {
      this.pinFails = 0; this.pinLockUntil = this.now + CFG.PIN_LOCK_MS;
      this.log({ kind: "security", text: `Admin locked for ${CFG.PIN_LOCK_MS / 60000} min after ${CFG.PIN_TRIES} wrong PINs` });
      this.say(`${CFG.PIN_TRIES} wrong PINs: Admin is locked for ${CFG.PIN_LOCK_MS / 60000} minutes. The attempts are in the history.`);
      return { ok: false, msg: "Too many wrong PINs" };
    }
    this.say(`Wrong Admin PIN. ${CFG.PIN_TRIES - this.pinFails} tr${CFG.PIN_TRIES - this.pinFails === 1 ? "y" : "ies"} left before Admin locks.`);
    return { ok: false, msg: `Wrong PIN, ${CFG.PIN_TRIES - this.pinFails} left` };
  }
  adminSetPin(p) {
    if (!/^\d+$/.test(p) || p.length !== CFG.PIN_LEN) return false;
    this.pin = p;
    this.log({ kind: "security", text: "Admin PIN changed" });
    this.say("Admin PIN changed.");
    return true;
  }
  adminTouched() { this.adminSeen = this.now; }
  log(e) {
    this.history.unshift(Object.assign({ ts: Date.now() }, e));
    if (this.history.length > CFG.HISTORY_N) this.history.length = CFG.HISTORY_N;   // 20-entry ring
  }
  setGallon(g, ml) {
    if (g === "a") this.galA = ml; else this.galB = ml;
    this.say(ml >= CFG.GALLON_ML ? `Gallon ${g.toUpperCase()} replaced with a full one.`
      : `Gallon ${g.toUpperCase()} is nearly empty: its sensor reads low, so the refill pump will not run.`);
  }
  emptyBox() { this.inv.box10 = 0; this.inv.box20 = 0; this.emit({ type: "boxEmpty" }); this.say("Attendant emptied the coin box."); }

  // --- the state machine ------------------------------------------------------
  onEnter(s) {
    this.dispensePump = (s === ST.DISPENSING);
    if (s === ST.STANDBY) {
      this.credit = 0; this.inserted = 0; this.target = 0; this.total = 0; this.unpaid = 0;
      this.changePaid = 0; this.pendingConfirm = false;
      if (this.fault === FAULT.NONE) this.say("Ready. Insert coins to start.");
    } else if (s === ST.SELECTING) {
      this.say(this.inserted >= CFG.MAX_CENTAVOS
        ? `₱15 reached, so the slot closes and the amount picker opens by itself. Credit ₱${this.credit / 100}.`
        : `Choose an amount and tap DISPENSE. Up to ${this.maxml()} mL for ₱${this.credit / 100}.`);
    } else if (s === ST.AWAITING) {
      this.warns = 0;
      this.say(`₱${this.target / 100} is taken off the credit now, before any water pours. Place a bottle under the nozzle: ${this.target} mL will be dispensed.`);
    } else if (s === ST.DISPENSING) {
      this.say(`Bottle detected. The dispense pump runs; it stops when ${this.target} mL is poured (timed: 7 s per 100 mL).`);
    } else if (s === ST.PAUSED) {
      this.say("Bottle removed mid-pour. The pump stops at once. Put it back within 10 seconds to continue.");
    } else if (s === ST.PAYING) {
      this.startPayout();
    } else if (s === ST.THANKYOU) {
      this.commit();
    }
  }

  startPayout() {
    const due = this.credit;
    if (due <= 0) { this.say("No change due."); this.pay = null; return; }
    const plan = changePlan(due, this.inv.p1, this.inv.p5);
    this.pay = { plan: plan || { p1: 0, p5: 0 }, leg: 0, left: 0, phase: "idle", t: 0, retries: 0, counted: 0 };
    this.say(`Paying ₱${due / 100} change: ${this.pay.plan.p5} × ₱5 first, then ${this.pay.plan.p1} × ₱1. ` +
             "Each coin is counted out; the hopper is never just told to run.");
  }

  commit() {
    const paid = this.changePaid;
    this.sales.income += this.inserted - paid;
    this.sales.volume += this.total;
    this.sales.count += 1;
    this.sales.list.unshift({ ts: Date.now(), in: this.inserted, ml: this.total, change: paid, owed: this.unpaid });
    this.sales.list.length = Math.min(this.sales.list.length, 10);
    for (const s of [this.stats.today, this.stats.all]) {
      s.in += this.inserted; s.change += paid; s.sales += 1; s.ml += this.total; s.owed += this.unpaid;
    }
    this.log({ kind: "sale", text: `Sale ₱${this.inserted / 100} in, ${this.total} mL, change ₱${paid / 100}` +
                                   (this.unpaid ? `, OWED ₱${this.unpaid / 100}` : "") });
  }

  tick(dt) {
    this.now += dt;
    for (const t of this.timers.splice(0)) {
      if (t.at <= this.now) t.fn(); else this.timers.push(t);
    }
    this.water(dt);

    switch (this.state) {
      case ST.STANDBY: {
        if (this.fault) { this.go(ST.FAULT); break; }
        let f = FAULT.NONE;
        if (this.tank < CFG.TANK_MIN_SERVE_ML) f = FAULT.OUT_OF_WATER;
        else if (this.boxFull) f = FAULT.STORAGE_FULL;
        else if (!changePlan(CFG.WORST_CASE_CHANGE, this.inv.p1, this.inv.p5)) f = FAULT.LOW_CHANGE;
        if (f && this.inFlight === 0) {
          this.fault = f;
          this.say(f === FAULT.OUT_OF_WATER ? "The cold tank is below a full ₱15 pour. Out of service until the pump refills it."
            : f === FAULT.STORAGE_FULL ? "The coin box is full. Out of service: the machine refuses money before the first coin."
            : "The hoppers cannot cover the worst-case change (₱34). Out of service until change is loaded.");
          this.go(ST.FAULT);
        }
        break;
      }
      case ST.ACCEPTING:
        if (this.inserted >= CFG.MAX_CENTAVOS && this.inFlight === 0) { this.go(ST.SELECTING); break; }
        if (this.pendingConfirm && this.inFlight === 0 && this.now >= this.coinBusyUntil) {
          this.pendingConfirm = false; this.go(ST.SELECTING);
        }
        break;
      case ST.AWAITING:
        if (this.bottle) { this.go(ST.DISPENSING); break; }
        if (this.warns === 0 && this.elapsed() >= CFG.BOTTLE_WARN1_MS) { this.warns = 1; this.emit({ type: "beep" }); this.say("15 s and no bottle: the buzzer sounds."); }
        else if (this.warns === 1 && this.elapsed() >= CFG.BOTTLE_WARN2_MS) { this.warns = 2; this.emit({ type: "beep" }); this.say("18 s: the buzzer sounds again. At 20 s the sale is cancelled and refunded."); }
        if (this.elapsed() >= CFG.BOTTLE_CANCEL_MS) {
          this.credit += this.target; this.target = 0;
          this.say("20 s and no bottle: cancelled. Everything paid is refunded in full.");
          this.go(ST.PAYING);
        }
        break;
      case ST.DISPENSING:
        if (!this.bottle) { this.go(ST.PAUSED); break; }
        this.segment = Math.min(this.target, this.segment + dt * 100 / CFG.DISPENSE_MS_PER_100ML);
        if (this.segment >= this.target) { this.dispensePump = false; this.go(ST.SETTLING); }
        break;
      case ST.PAUSED:
        if (this.bottle) { this.go(ST.DISPENSING); break; }
        if (this.elapsed() >= CFG.GRACE_MS) this.go(ST.SETTLING);
        break;
      case ST.SETTLING:
        if (this.elapsed() < CFG.SETTLE_MS) break;
        {
          const delivered = Math.floor(this.segment);
          let charged = delivered >= this.target ? this.target : Math.floor(delivered / 100) * 100;
          charged = Math.min(charged, this.target);
          const refund = this.target - charged;
          this.credit += refund;
          this.total += charged;
          this.refillOwed += delivered;
          if (delivered < this.target) {
            this.say(`Bottle not put back. ${delivered} mL poured is charged as ${charged} mL (always rounded down), ` +
                     `so ₱${refund / 100} goes back to the credit.`);
          } else {
            this.say(`${charged} mL poured. The refill pump will put the same volume back into the cold tank.`);
          }
          this.target = 0; this.segment = 0;
        }
        this.go(this.credit > 0 ? ST.COMPLETE : ST.PAYING);
        if (this.state === ST.COMPLETE && this.credit > 0) {
          this.story += ` Credit left ₱${this.credit / 100}: dispense more, or Finish for the change.`;
        }
        break;
      case ST.COMPLETE:
        if (this.credit <= 0) { this.go(ST.PAYING); break; }
        if (this.elapsed() >= CFG.COMPLETE_TIMEOUT_MS) { this.say("No answer for 60 s: the change is paid out."); this.go(ST.PAYING); }
        break;
      case ST.PAYING:
        this.payTick(dt);
        break;
      case ST.THANKYOU:
        if (this.elapsed() >= CFG.THANKYOU_MS) this.go(ST.STANDBY);
        break;
      case ST.ADMIN:
        if (this.now - this.adminSeen >= CFG.ADMIN_IDLE_MS) {
          this.exitAdmin();
          this.say("Admin was left open for 60 s, so it logged itself out.");
        }
        break;
      case ST.FAULT:
        if (this.fault === FAULT.OUT_OF_WATER && this.tank >= CFG.TANK_MIN_SERVE_ML) this.fault = FAULT.NONE;
        if (this.fault === FAULT.STORAGE_FULL && !this.boxFull) this.fault = FAULT.NONE;
        if (this.fault === FAULT.NONE) this.go(ST.STANDBY);
        break;
    }
  }

  payTick(dt) {
    const p = this.pay;
    if (!p) { this.go(ST.THANKYOU); return; }
    const hopper = p.leg === 0 ? "p5" : "p1";
    const want = p.leg === 0 ? p.plan.p5 : p.plan.p1;
    if (p.leg > 1) {
      this.go(ST.THANKYOU);
      this.say(this.changePaid ? `₱${this.changePaid / 100} change counted out into the tray. Thank you!` : "Thank you!");
      return;
    }
    if (p.left === 0 && p.phase === "idle") {
      if (want === 0) { p.leg++; return; }
      p.left = want; p.phase = "on"; p.t = 0; p.retries = 0;
      p.jam = this.jamNext; this.jamNext = false;
      this.emit({ type: "jamUsed" });
    }
    p.t += dt;
    if (p.jam) {
      // Motor runs, nothing comes out: retry after the timeout, then lock.
      this.hopperMotor = hopper;
      if (p.t >= CFG.HOP_TIMEOUT_MS) {
        p.t = 0; p.retries++;
        this.say(`The ${hopper === "p5" ? "₱5" : "₱1"} hopper ran but its outlet counted no coin. Retry ${p.retries} of ${CFG.HOP_RETRY_MAX}.`);
        if (p.retries > CFG.HOP_RETRY_MAX) {
          this.hopperMotor = null;
          this.unpaid = this.credit - this.changePaid;
          this.commit();
          this.fault = FAULT.CHANGE_JAM;
          this.log({ kind: "jam", text: `CHANGE JAM on the ${hopper === "p5" ? "₱5" : "₱1"} hopper, ₱${this.unpaid / 100} owed` });
          this.say(`Change jam. ₱${this.unpaid / 100} of change could not be paid; it is recorded as owed, and the machine ` +
                   "locks until an attendant clears the jam. It never assumes a payout succeeded.");
          this.pay = null;
          this.go(ST.FAULT);
        }
      }
      return;
    }
    if (p.phase === "on") {
      this.hopperMotor = hopper;
      if (p.t >= CFG.HOP_ON_MS) {
        // One pulse = one coin out and counted.
        const peso = hopper === "p5" ? 5 : 1;
        this.inv[hopper]--; p.left--; this.changePaid += peso * 100;
        this.tray.push(peso);
        this.emit({ type: "payout", peso, hopper });
        p.phase = "off"; p.t = 0; this.hopperMotor = null;
      }
    } else if (p.phase === "off" && p.t >= CFG.HOP_OFF_MS) {
      if (p.left > 0) { p.phase = "on"; p.t = 0; } else { p.phase = "idle"; p.leg++; }
    }
  }

  water(dt) {
    const s = dt / 1000;
    if (this.state === ST.DISPENSING && this.dispensePump) {
      this.tank = Math.max(0, this.tank - s * 1000 * 100 / CFG.DISPENSE_MS_PER_100ML);
    }
    // Refill: put back what was poured, and keep the tank in its REFILL / FULL band. Both gallons
    // feed one Y fitting, so EITHER gallon at its reserve line stops the pump: running it dry destroys it.
    const supplyLow = this.gallonLow("a") || this.gallonLow("b");
    if (!this.fillPump && (this.refillOwed > 0 || this.tank < CFG.TANK_REFILL_ML) && this.tank < CFG.TANK_ML) this.fillPump = true;
    if (this.fillPump && (this.tank >= CFG.TANK_ML || supplyLow)) { this.fillPump = false; if (this.tank >= CFG.TANK_ML) this.refillOwed = 0; }
    if (this.fillPump) {
      const ml = Math.min(s * CFG.FILL_ML_PER_S, CFG.TANK_ML - this.tank);
      this.tank += ml; this.galA -= ml / 2; this.galB -= ml / 2;
      this.refillOwed = Math.max(0, this.refillOwed - ml);
    }
  }
}
