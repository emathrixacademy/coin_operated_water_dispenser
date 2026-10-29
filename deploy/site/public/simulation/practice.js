"use strict";
// Practice: hands-on use of the 3D machine, its sounds, and guided lessons for customers and
// attendants. Every lesson drives the same machine model and screen as free play, so what a
// learner does here is what the machine does.

(() => {
  const q = id => document.getElementById(id);
  const m = machine;

  // ---- sound --------------------------------------------------------------------------
  // Browsers only allow audio after a click, so the context starts on the first press.
  let ac = null, soundOn = true;
  function audio() {
    if (!ac) { try { ac = new (window.AudioContext || window.webkitAudioContext)(); } catch (e) { ac = null; } }
    if (ac && ac.state === "suspended") ac.resume();
    return ac;
  }
  document.addEventListener("pointerdown", audio, { capture: true });
  function tone(freq, dur, type, vol, when = 0) {
    if (!soundOn || !ac) return;
    const t = ac.currentTime + Math.max(0, when), o = ac.createOscillator(), g = ac.createGain();
    o.type = type; o.frequency.value = freq;
    g.gain.setValueAtTime(vol, t); g.gain.exponentialRampToValueAtTime(0.0001, t + dur);
    o.connect(g).connect(ac.destination); o.start(t); o.stop(t + dur + 0.05);
  }
  const clink = (when = 0) => { tone(2900, 0.12, "triangle", 0.12, when); tone(4300, 0.09, "sine", 0.05, when + 0.012); };
  const buzz = () => { tone(2600, 0.4, "square", 0.05); };
  const tap = () => tone(1500, 0.03, "square", 0.03);
  const motors = {};
  function motor(name, freq, vol) {
    if (!ac) return;
    let mo = motors[name];
    if (!mo) {
      const o = ac.createOscillator(), f = ac.createBiquadFilter(), g = ac.createGain();
      o.type = "sawtooth"; o.frequency.value = freq; f.type = "lowpass"; f.frequency.value = 420; g.gain.value = 0;
      o.connect(f).connect(g).connect(ac.destination); o.start();
      mo = motors[name] = { g, on: false };
    }
    const want = soundOn && !paused && vol > 0;
    if (want !== mo.on) { mo.on = want; mo.g.gain.setTargetAtTime(want ? vol : 0, ac.currentTime, 0.06); }
  }
  simHooks.push(e => {
    const sp = Math.max(speed, 1);
    if (e.type === "coin") { clink(); clink(e.dest === "tray" ? 0.9 / sp : (e.idMs + e.routeMs) / 1000 / sp); }
    else if (e.type === "payout") clink(0.5 / sp);
    else if (e.type === "beep") buzz();
  });
  const prevTouch = lcd.onTouch;
  lcd.onTouch = () => { tap(); if (prevTouch) prevTouch(); };
  q("bSound").addEventListener("click", () => {
    soundOn = !soundOn; audio();
    q("bSound").classList.toggle("lit", soundOn); q("lSound").textContent = soundOn ? "Sound on" : "Sound off";
  });

  // ---- touching the 3D machine ----------------------------------------------------------
  const hitMat = new THREE.MeshBasicMaterial({ visible: false });
  function hit(name, w, h, d, x, y, z) {
    const o = new THREE.Mesh(new THREE.BoxGeometry(w, h, d), hitMat);
    o.position.set(x, y, z); o.userData.name = name; scene.add(o); return o;
  }
  lcdFace.userData.name = "lcd";
  const targets = [lcdFace,
    hit("slot", 0.09, 0.17, 0.04, 0.20, 1.12, FZ + 0.01),
    hit("bay", 0.26, 0.36, 0.24, -0.12, 0.98, FZ - 0.11),
    hit("tray", 0.16, 0.07, 0.1, 0.20, 0.78, FZ - 0.03),
    hit("galA", 0.28, 0.52, 0.28, GAL.a.x, 0.3, 0.02),
    hit("galB", 0.28, 0.52, 0.28, GAL.b.x, 0.3, 0.02),
    hit("box", 0.15, 0.12, 0.16, 0.20, 0.79, -0.19)];
  const TIPS = {
    lcd: "Touchscreen: tap it like the real one",
    slot: "Coin slot: click to drop a coin",
    bay: () => m.bottle ? "Dispensing bay: click to take your bottle out" : "Dispensing bay: click to put your bottle in",
    tray: "Change tray: click to take your coins",
    galA: "Gallon A: click to replace it with a full gallon (attendant)",
    galB: "Gallon B: click to replace it with a full gallon (attendant)",
    box: "Locked coin box: click to empty it (attendant)",
  };
  const ray = new THREE.Raycaster(), ndc = new THREE.Vector2();
  function pick(e) {
    const r = renderer.domElement.getBoundingClientRect();
    ndc.set((e.clientX - r.left) / r.width * 2 - 1, -(e.clientY - r.top) / r.height * 2 + 1);
    ray.setFromCamera(ndc, camera);
    return ray.intersectObjects(targets, false)[0] || null;
  }
  let downAt = null, lcdHeld = false;
  // Capture phase on the view's container, so a press on the screen stops the camera turning
  // before OrbitControls sees it.
  view.addEventListener("pointerdown", e => {
    if (e.button !== 0) return;
    const h = pick(e);
    downAt = { x: e.clientX, y: e.clientY, h };
    if (h && h.object === lcdFace && h.uv) {
      controls.enabled = false; lcdHeld = true;
      lcd.pressAt(h.uv.x * LCD_W, (1 - h.uv.y) * LCD_H);
    }
  }, true);
  window.addEventListener("pointerup", e => {
    if (lcdHeld) { lcdHeld = false; lcd.release(); controls.enabled = true; downAt = null; return; }
    if (!downAt) return;
    const moved = Math.hypot(e.clientX - downAt.x, e.clientY - downAt.y), h = downAt.h;
    downAt = null;
    if (moved > 6 || !h) return;
    act(h.object.userData.name, e);
  });
  function act(name, e) {
    if (name === "slot") showCoins(e.clientX, e.clientY);
    else if (name === "bay") m.placeBottle(!m.bottle);
    else if (name === "tray") q("bTray").click();
    else if (name === "galA" || name === "galB") m.setGallon(name === "galA" ? "a" : "b", CFG.GALLON_ML);
    else if (name === "box") q("bEmptyBox").click();
  }
  let hoverReq = null;
  view.addEventListener("pointermove", e => {
    if (e.buttons) { q("tip").style.display = "none"; return; }
    if (hoverReq) return;
    hoverReq = requestAnimationFrame(() => {
      hoverReq = null;
      const h = pick(e), tip = q("tip");
      renderer.domElement.style.cursor = h ? "pointer" : "";
      if (!h) { tip.style.display = "none"; return; }
      const t = TIPS[h.object.userData.name];
      tip.textContent = typeof t === "function" ? t() : t;
      tip.style.display = "block"; tip.style.left = (e.clientX + 14) + "px"; tip.style.top = (e.clientY + 14) + "px";
    });
  });
  view.addEventListener("pointerleave", () => { q("tip").style.display = "none"; });

  // the coin picker that opens at the slot
  const picker = q("coinPick");
  function showCoins(x, y) {
    picker.style.display = "block";
    const w = picker.offsetWidth, h = picker.offsetHeight;
    picker.style.left = Math.min(window.innerWidth - w - 8, Math.max(8, x - w / 2)) + "px";
    picker.style.top = Math.max(8, y - h - 18) + "px";
  }
  picker.querySelectorAll("[data-pick]").forEach(b => b.addEventListener("click", () => m.insertCoin(+b.dataset.pick)));
  q("pickClose").addEventListener("click", () => { picker.style.display = "none"; });
  document.addEventListener("pointerdown", e => {
    if (picker.style.display === "block" && !picker.contains(e.target) && e.target !== renderer.domElement) picker.style.display = "none";
  });
  document.addEventListener("keydown", e => { if (e.key === "Escape") picker.style.display = "none"; });

  // a view from where a customer stands
  VIEWS.use = { pos: [0.05, 1.06, FZ + 1.45], target: [0.05, 1.06, 0.2] };

  // ---- lesson building blocks -------------------------------------------------------------
  const vol = ml => ml >= 1000 ? `${ml / 1000} L` : `${ml} mL`;
  const S = s => m.state === s;
  const inS = (...ss) => ss.includes(m.state);
  const inSale = () => m.state >= ST.ACCEPTING && m.state <= ST.THANKYOU;
  const credit = () => m.cur ? m.cur.coins.reduce((a, c) => a + c, 0) : 0;
  const lcdBtn = (ev, payload) => b => b.ev === ev && (payload === undefined || b.payload === payload);

  const coins = (n, words, coin) => ({
    say: `Drop ${words} into the coin slot.`,
    why: "Click the coin slot on the machine and pick the coin, or use the coin buttons in the Customer card. " +
         "Watch the screen count each coin before you drop the next one.",
    three: "slot", dom: [`[data-coin="${coin}"]`], done: () => credit() >= n || m.state >= ST.SELECTING && inSale(),
    hint: () => m.tray.length && inS(ST.ACCEPTING, ST.STANDBY)
      ? "A coin came back to the tray: it went in while the slot was still closed for the last coin. Drop it again, a little slower." : "",
  });
  const cont = { say: "Tap CONTINUE on the screen.",
    why: "You can keep adding coins until you tap Continue. At ₱15 the machine moves on by itself.",
    three: "lcd", dom: ["#lcdWrap"], lcd: lcdBtn(EV.CONFIRM), done: () => inSale() && !S(ST.ACCEPTING) };
  const choose = ml => ({ say: `Tap ${vol(ml)} on the screen.`,
    why: `Each 100 mL costs ₱1, so ${vol(ml)} is ₱${ml / 100}. Sizes you have not paid for are greyed out.`,
    three: "lcd", dom: ["#lcdWrap"], lcd: lcdBtn(UI.SET, ml),
    done: () => (S(ST.SELECTING) && lcd.sel === ml) || (inS(ST.AWAITING, ST.DISPENSING, ST.PAUSED) && m.target === ml) });
  const dispense = { say: "Tap DISPENSE.", why: "The price comes off your credit now, before any water pours.",
    three: "lcd", dom: ["#lcdWrap"], lcd: lcdBtn(EV.SELECT), done: () => m.state >= ST.AWAITING && inSale() };
  const place = (say = "Put your bottle under the nozzle.") => ({ say,
    why: "Click inside the dispensing bay on the machine, or press Place bottle. A sensor sees the bottle and the pump starts.",
    three: "bay", dom: ["#bBottle"], done: () => m.state >= ST.DISPENSING && inSale() });
  const waitFill = (done, say = "Keep the bottle in place while it fills.") => ({ say,
    why: "The screen shows how much has poured. Leave the bottle in until it stops.",
    three: "bay", wait: true, done,
    hint: () => S(ST.PAUSED) ? "You took the bottle out, so the pump stopped. Put it back within 10 seconds to keep filling." : "" });
  const takeBottle = (say = "Take your bottle out.") => ({ say, why: "Click the bay again, or press Remove bottle.",
    three: "bay", dom: ["#bBottle"], done: () => !m.bottle });
  const finish = { say: "Tap Finish to get your change.",
    why: "Dispense more would let you spend the rest on more water instead.",
    three: "lcd", dom: ["#lcdWrap"], lcd: lcdBtn(EV.FINISH), done: () => !S(ST.COMPLETE) };
  const waitChange = { say: "Wait while your change is counted out.",
    why: "Every coin is counted as it leaves the hopper. The machine only finishes when the count is right.",
    wait: true, done: () => inS(ST.THANKYOU, ST.STANDBY) };
  const takeTray = { say: "Take your coins from the change tray.",
    why: "Click the change tray under the coin slot, or press Take coins from the tray.",
    three: "tray", dom: ["#bTray"], done: () => !m.tray.length && !S(ST.PAYING) };
  const gotIt = (say, why) => ({ say, why, next: "Got it" });

  const holdDrop = { say: "Hold your finger on the big icon in the middle of the screen for 3 seconds.",
    why: "This hidden gesture opens the Admin PIN pad, so customers never see a button for it. " +
         "(On this page, Open Admin in the Service card does the same.)",
    three: "lcd", dom: ["#lcdWrap"], lcdRect: [180, 64, 120, 104], done: () => !!lcd.pinEntry || S(ST.ADMIN) };
  const pin = { say: () => `Enter the PIN ${m.pin.split("").join(" ")} and tap OK.`,
    why: "Three wrong PINs lock Admin for 5 minutes, and every try is written to the history.",
    three: "lcd", dom: ["#lcdWrap"],
    lcd: b => { const p = lcd.pinEntry; if (!p) return false;
      return p.digits.length < CFG.PIN_LEN ? b.ev === UI.K_DIGIT && b.payload === m.pin[p.digits.length] : b.ev === UI.K_OK; },
    done: () => S(ST.ADMIN) };
  const page = (name, label, why) => ({
    say: () => lcd.admin.page === "menu" ? `Tap ${label}.` : `Tap Back, then ${label}.`, why,
    three: "lcd", dom: ["#lcdWrap"],
    lcd: b => lcd.admin.page === "menu" ? b.ev === UI.A_PAGE && b.payload === name : b.ev === UI.A_PAGE && b.payload === "menu",
    done: () => S(ST.ADMIN) && lcd.admin.page === name });
  const exitAdmin = { say: () => lcd.admin.page === "menu" ? "Tap Exit to leave Admin." : "Tap Back, then Exit.",
    why: "Admin also logs itself out after 60 seconds untouched, so it is never left open.",
    three: "lcd", dom: ["#lcdWrap"],
    lcd: b => lcd.admin.page === "menu" ? b.ev === UI.A_EXIT : b.ev === UI.A_PAGE && b.payload === "menu",
    done: () => !S(ST.ADMIN) };

  // setup helpers: play time forward instantly, without animation
  const fast = ms => { for (let t = 0; t < ms; t += 50) m.tick(50); };
  function quickSale(cs, ml) {
    for (const c of cs) { m.insertCoin(c); fast(3000); }
    m.touch(EV.CONFIRM); fast(300); m.touch(EV.SELECT, ml); m.placeBottle(true);
    fast(ml / 100 * CFG.DISPENSE_MS_PER_100ML + 2000); m.placeBottle(false);
    if (S(ST.COMPLETE)) m.touch(EV.FINISH);
    fast(40000); m.takeTray();
  }

  const LESSONS = [
    { id: "c1", who: "Customer", title: "Buy water", blurb: "Three ₱1 coins for 300 mL, exact money", view: "use",
      steps: () => [coins(3, "three ₱1 coins, one at a time", 1), cont, choose(300), dispense, place(),
        waitFill(() => m.state >= ST.PAYING && inSale() || S(ST.STANDBY)), takeBottle()],
      end: "That is a whole sale: ₱3 bought 300 mL, no change. It is now a row in Transaction records." },
    { id: "c2", who: "Customer", title: "Get your change", blurb: "Pay ₱10, buy 400 mL, collect ₱6 change", view: "use",
      steps: () => [coins(10, "a ₱10 coin", 10), cont, choose(400), dispense, place(),
        waitFill(() => m.state >= ST.COMPLETE && inSale(), "Keep the bottle in until the screen shows the credit left."),
        takeBottle(), finish, waitChange, takeTray],
      end: "₱10 − ₱4 = ₱6, paid as one ₱5 and one ₱1. The machine pays the biggest coins first." },
    { id: "c3", who: "Customer", title: "Two bottles, one payment", blurb: "₱10 split into two 500 mL bottles", view: "use",
      steps: () => [coins(10, "a ₱10 coin", 10), cont, choose(500), dispense, place(),
        waitFill(() => m.state >= ST.COMPLETE && inSale()), takeBottle("Take the full bottle out."),
        { say: "Tap Dispense more.", why: "Your ₱5 of credit is still on the screen.", three: "lcd", dom: ["#lcdWrap"],
          lcd: lcdBtn(EV.AGAIN), done: () => inS(ST.SELECTING, ST.AWAITING, ST.DISPENSING) },
        choose(500), dispense, place("Put the empty bottle in."),
        waitFill(() => m.state >= ST.PAYING && inSale() || S(ST.STANDBY)), takeBottle()],
      end: "One payment, two bottles. Credit stays on the machine until you spend it or tap Finish." },
    { id: "c4", who: "Customer", title: "Bottle taken out too early", blurb: "What happens if you pull the bottle mid-pour", view: "use",
      steps: () => [coins(5, "a ₱5 coin", 5), cont, choose(500), dispense, place(),
        { say: "When the screen shows about half, take the bottle out.", why: "In real life: someone bumps it, or you grab it too soon.",
          three: "bay", dom: ["#bBottle"], done: () => S(ST.PAUSED) || m.state > ST.PAUSED },
        { say: "The pump stopped. Put the bottle back within 10 seconds.", why: "The machine waits 10 seconds for you, then stops for good.",
          three: "bay", dom: ["#bBottle"], done: () => !S(ST.PAUSED) },
        waitFill(() => m.state >= ST.COMPLETE && inSale() || S(ST.STANDBY)), takeBottle(),
        { say: "Tap Finish to get the rest back as change.", three: "lcd", dom: ["#lcdWrap"], lcd: lcdBtn(EV.FINISH),
          done: () => !S(ST.COMPLETE) }, takeTray],
      end: "Put back in time, it carries on. If not, you pay only for full 100 mL steps poured (305 mL counts as 300 mL) and the rest comes back as change." },
    { id: "c5", who: "Customer", title: "Forgot the bottle", blurb: "The buzzer, then a full refund at 20 s", view: "use",
      steps: () => [coins(2, "two ₱1 coins, one at a time", 1), cont, choose(200), dispense,
        { say: "Now do NOT put a bottle in. Wait and listen.", why: "After 15 seconds the buzzer reminds you.", wait: true,
          done: () => m.warns >= 1 || m.state >= ST.PAYING },
        { say: "It buzzes again at 18 seconds. Keep waiting.", wait: true, done: () => m.warns >= 2 || m.state >= ST.PAYING },
        { say: "At 20 seconds the sale is cancelled.", why: "Every peso you paid comes back to the tray.", wait: true,
          done: () => inS(ST.THANKYOU, ST.STANDBY) }, takeTray],
      end: "No bottle, no charge. The full ₱2 came back." },
    { id: "c6", who: "Customer", title: "Out of service screen", blurb: "Why the machine sometimes will not take coins", view: "use",
      setup: () => { m.tank = 1000; m.galA = 2000; m.galB = 2000; fast(200); },
      steps: () => [gotIt("Read the screen: Out of service, Out of water.",
          "When the machine cannot serve you, it switches the coin slot off before you pay. It never takes money it cannot honour."),
        { say: "Try dropping a ₱5 coin anyway.", three: "slot", dom: ['[data-coin="5"]'],
          done: () => m.stats.all.returned > P.base.ret }, takeTray],
      end: "The coin came straight back. You will also see this screen for: not enough change, coin box full, change dispenser jammed. Call the attendant." },

    { id: "a1", who: "Attendant", title: "Admin: today's sales", blurb: "Log in with the PIN, read profit, coins, water", view: "use",
      setup: () => { quickSale([10, 1, 1], 300); quickSale([5], 500); },
      steps: () => [holdDrop, pin,
        page("profit", "Profit", "Money in minus change paid out is the profit. It always equals the water sold at ₱1 per 100 mL."),
        page("coins", "Coins", "How many of each coin came in, what is in each change hopper now, and what is in the locked coin box."),
        page("water", "Water", "Cold tank level, both gallons, and whether the refill pump is running."),
        page("history", "History", "Every sale, login, wrong PIN and fault, newest first."), exitAdmin],
      end: "Admin locks for 5 minutes after 3 wrong PINs, and logs itself out after 60 seconds untouched." },
    { id: "a2", who: "Attendant", title: "Load change", blurb: "Fix NOT ENOUGH CHANGE", view: "use",
      setup: () => { m.inv.p1 = 2; m.inv.p5 = 3; fast(200); },
      steps: () => [gotIt("The screen says: Not enough change in the machine.",
          "Before it takes any money, the machine checks that it could pay the worst-case change (₱34). On the real machine, open the cabinet and pour ₱1 coins into the ₱1 hopper first."),
        holdDrop, pin, page("change", "Load change", "Here you record how many coins are now in each hopper."),
        { say: "Set the ₱1 count to 60: tap +10 until it shows 60.", three: "lcd", dom: ["#lcdWrap"],
          why: "Enter what you really put in. The machine plans every payout from these counts.",
          lcd: b => b.ev === UI.A_STEP && b.payload.key === "p1" && b.payload.d === 10,
          done: () => S(ST.ADMIN) && (lcd.admin.page === "change" && lcd.admin.p1 >= 60 || m.inv.p1 >= 60) },
        { say: "Tap CONFIRM to save the count.", three: "lcd", dom: ["#lcdWrap"], lcd: lcdBtn(UI.A_CONFIRM),
          done: () => m.fault !== FAULT.LOW_CHANGE }, exitAdmin],
      end: "Back in service. The fault cleared itself as soon as the count covered the worst case." },
    { id: "a3", who: "Attendant", title: "Clear a change jam", blurb: "A customer is owed change", view: "use",
      setup: () => {
        m.jamNext = true; m.insertCoin(10); fast(3000); m.touch(EV.CONFIRM); fast(300); m.touch(EV.SELECT, 300);
        m.placeBottle(true); fast(25000); m.placeBottle(false); if (S(ST.COMPLETE)) m.touch(EV.FINISH); fast(40000); m.takeTray();
        q("cJam").checked = false;
      },
      steps: () => [gotIt("The screen says: Change dispenser jammed.",
          "The hopper ran but its counter saw no coin come out. The machine retried 3 times, then locked itself. It never assumes change was paid."),
        gotIt("Open the cabinet and clear the stuck coin from the hopper outlet.", "On the real machine. Here, just carry on."),
        holdDrop, pin, page("history", "History", "The jam entry shows how much the customer is owed. Pay it to them by hand."),
        { say: () => lcd.admin.page === "menu" ? "Tap Clear fault." : "Tap Back, then Clear fault.", three: "lcd", dom: ["#lcdWrap"],
          lcd: b => lcd.admin.page === "menu" ? b.ev === UI.A_CLEAR : b.ev === UI.A_PAGE && b.payload === "menu",
          done: () => !m.fault }, exitAdmin],
      end: "The machine is back in service. The record shows 'Change jam: ₱7 owed' until you pay the customer." },
    { id: "a4", who: "Attendant", title: "Replace an empty gallon", blurb: "GALLON LOW and the refill pump", view: "water",
      setup: () => { m.galA = 1500; m.tank = 4800; fast(200); },
      steps: () => [gotIt("The top bar of the screen says Gallon A low, and the refill pump has stopped.",
          "Both gallons feed the pump through one Y fitting, so if either runs low the pump stops and never runs dry. The machine keeps selling from the cold tank."),
        { say: "Replace gallon A with a full one.", why: "Click gallon A in the machine, or press Gallon A: Full in the Service card.",
          three: "galA", dom: ['[data-gal="a"][data-ml="18900"]'], done: () => !m.gallonLow("a") },
        { say: "Watch the refill pump top up the cold tank.", why: "It stops on its own when the tank is full.", wait: true,
          done: () => m.tank >= CFG.TANK_REFILL_ML }],
      end: "Swap a gallon as soon as the warning shows, so the cold tank never runs down to OUT OF WATER." },
    { id: "a5", who: "Attendant", title: "Empty the coin box", blurb: "Fix COIN BOX FULL", view: "coins",
      setup: () => { m.inv.box10 = 30; m.inv.box20 = 12; m.boxFull = true; q("cBoxFull").checked = true; fast(200); },
      steps: () => [gotIt("The screen says: Coin box is full.",
          "₱10 and ₱20 coins go to the locked coin box. When its sensor reads full, the machine stops taking money."),
        { say: "Unlock the coin box and empty it.", why: "Click the coin box inside the machine, or press Empty the coin box in the Service card.",
          three: "box", dom: ["#bEmptyBox"], done: () => !m.boxFull },
        { say: "The machine goes back in service by itself.", wait: true, done: () => S(ST.STANDBY) }],
      end: "Count the money you took out and compare it with Admin > Coins." },
  ];

  // ---- the lesson runner ----------------------------------------------------------------
  const P = { L: null, steps: [], i: 0, doneAt: 0, base: {}, ended: false };
  const doneSet = (() => { try { return new Set(JSON.parse(localStorage.getItem("vendoLessons") || "[]")); } catch (e) { return new Set(); } })();
  function markDone(id) { doneSet.add(id); try { localStorage.setItem("vendoLessons", JSON.stringify([...doneSet])); } catch (e) { /* private window */ } }

  function startLesson(L) {
    const keep = { records: m.records, recNo: m.recNo };
    q("bReset").click();
    m.records = keep.records; m.recNo = keep.recNo;
    if (paused) q("bPlay").click();
    document.querySelector('[data-speed="1"]').click();
    if (L.setup) {
      L.setup();
      for (const r of m.records) if (r.no > keep.recNo && !/lesson setup/.test(r.result)) r.result += " (lesson setup)";
      m.events.length = 0; trayCoins.clear(); m.tray = [];
    }
    m.recVersion++;
    setView(L.view || "use");
    Object.assign(P, { L, steps: L.steps(), i: 0, doneAt: 0, ended: false, base: { ret: m.stats.all.returned } });
    q("lessons").style.display = "none"; q("coach").style.display = "block"; q("side").scrollTop = 0;
    render();
  }
  function endLesson() { P.L = null; q("coach").style.display = "none"; clearHighlights(); }
  const step = () => P.L && !P.ended ? P.steps[P.i] : null;
  const txt = v => typeof v === "function" ? v() : (v || "");

  function render() {
    const L = P.L; if (!L) return;
    q("cTitle").textContent = `${L.who} lesson: ${L.title}`;
    q("cProg").innerHTML = P.steps.map((_, i) => `<i class="${i < P.i || P.ended ? "d" : i === P.i ? "c" : ""}"></i>`).join("");
    const s = step();
    if (P.ended) {
      const idx = LESSONS.indexOf(L), nx = LESSONS[idx + 1];
      q("cSay").textContent = "Well done!"; q("cWhy").textContent = L.end; q("cHint").textContent = "";
      q("cNext").style.display = nx ? "" : "none"; q("cNext").textContent = nx ? `Next: ${nx.title}` : "";
      q("cSkip").style.display = "none"; q("cWait").style.display = "none";
      return;
    }
    q("cSay").textContent = (P.doneAt ? "✓ " : "") + txt(s.say);
    q("cWhy").textContent = txt(s.why);
    q("cNext").style.display = s.next && !P.doneAt ? "" : "none"; q("cNext").textContent = s.next || "";
    q("cSkip").style.display = "";
    q("cWait").style.display = s.wait ? "" : "none";
    q("coach").classList.toggle("ok", !!P.doneAt);
  }

  q("cNext").addEventListener("click", () => {
    if (P.ended) { const nx = LESSONS[LESSONS.indexOf(P.L) + 1]; if (nx) startLesson(nx); return; }
    const s = step(); if (s && s.next) s.ok = true;
  });
  q("cSkip").addEventListener("click", () => { if (step()) { P.i++; P.doneAt = 0; if (P.i >= P.steps.length) finishLesson(); render(); } });
  q("cMenu").addEventListener("click", openMenu);
  q("cEnd").addEventListener("click", endLesson);
  function finishLesson() { P.ended = true; markDone(P.L.id); clearHighlights(); render(); }

  // the lesson list
  function openMenu() {
    const group = who => LESSONS.filter(L => L.who === who).map(L =>
      `<button class="lesson" data-l="${L.id}"><b>${doneSet.has(L.id) ? "✓ " : ""}${L.title}</b><span>${L.blurb}</span></button>`).join("");
    q("lessonList").innerHTML = `<h3>For customers</h3>${group("Customer")}<h3>For the attendant</h3>${group("Attendant")}`;
    q("lessons").style.display = "flex";
  }
  q("lessonList").addEventListener("click", e => {
    const b = e.target.closest("[data-l]"); if (b) startLesson(LESSONS.find(L => L.id === b.dataset.l));
  });
  q("lessonsClose").addEventListener("click", () => { q("lessons").style.display = "none"; });
  q("bPractice").addEventListener("click", openMenu);
  q("bPractice2").addEventListener("click", openMenu);
  q("bFree").addEventListener("click", () => { endLesson(); setView("use"); });

  // ---- highlights: the screen button, the page control and the machine part --------------
  let lit = [];
  function clearHighlights() { lit.forEach(el => el.classList.remove("hl")); lit = []; arrow.visible = false; }
  const arrow = new THREE.Mesh(new THREE.ConeGeometry(0.016, 0.045, 18),
    new THREE.MeshBasicMaterial({ color: 0xffb300, depthTest: false, transparent: true }));
  arrow.rotation.x = Math.PI; arrow.renderOrder = 999; arrow.visible = false; scene.add(arrow);
  const ARROW = { slot: [0.20, 1.25, FZ + 0.03], lcd: [-0.12, 1.49, FZ + 0.03], bay: [-0.12, 1.06, FZ - 0.04],
                  tray: [0.20, 0.85, FZ + 0.01], galA: [GAL.a.x, 0.7, 0.02], galB: [GAL.b.x, 0.7, 0.02], box: [0.20, 0.95, -0.19] };

  const lcdDraw = lcd.draw.bind(lcd);
  lcd.draw = now => {
    lcdDraw(now);
    const s = step(); if (!s || P.doneAt) return;
    const g = lcd.g, a = 0.55 + 0.45 * Math.sin(now / 160);
    g.save(); g.strokeStyle = `rgba(255,196,0,${a})`; g.lineWidth = 4;
    if (s.lcd) for (const b of lcd.btn) if (b.enabled && s.lcd(b)) { g.beginPath(); g.roundRect(b.x - 3, b.y - 3, b.w + 6, b.h + 6, 13); g.stroke(); }
    if (s.lcdRect && !lcd.pinEntry && !S(ST.ADMIN)) { const [x, y, w, h] = s.lcdRect; g.beginPath(); g.roundRect(x, y, w, h, 16); g.stroke(); }
    g.restore();
  };

  function loop(now) {
    requestAnimationFrame(loop);
    // motors you can hear
    if (ac) {
      motor("pump", 95, m.dispensePump ? 0.05 : 0);
      motor("fill", 70, m.fillPump ? 0.025 : 0);
      motor("hopper", 140, m.hopperMotor ? 0.04 : 0);
    }
    const s = step();
    if (!s) { if (lit.length || arrow.visible) clearHighlights(); return; }
    if (P.doneAt) {
      if (now - P.doneAt > 700) { P.doneAt = 0; P.i++; if (P.i >= P.steps.length) finishLesson(); render(); }
    } else if (s.next ? s.ok : s.done()) { P.doneAt = now; render(); }
    const say = (P.doneAt ? "✓ " : "") + txt(s.say);
    if (q("cSay").textContent !== say) q("cSay").textContent = say;
    const hint = s.hint ? s.hint() : "";
    if (q("cHint").textContent !== hint) q("cHint").textContent = hint;
    // page controls
    const want = P.doneAt ? [] : (s.dom || []).flatMap(sel => [...document.querySelectorAll(sel)]);
    for (const el of lit) if (!want.includes(el)) el.classList.remove("hl");
    for (const el of want) el.classList.add("hl");
    lit = want;
    // the part on the machine
    const p = !P.doneAt && s.three && ARROW[s.three];
    arrow.visible = !!p;
    if (p) arrow.position.set(p[0], p[1] + 0.012 * Math.sin(now / 220), p[2]);
  }
  requestAnimationFrame(loop);
  window.vendoPractice = { lessons: LESSONS, start: startLesson, state: P };   // used by the page tests
})();
