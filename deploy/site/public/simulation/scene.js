"use strict";
// 3D cabinet and the glue between the machine model, the screen and the controls.
// Concept geometry from the bench photos and the README (178 x 69 x 61 cm). Metres, y up, front = +z.

const machine = new Machine();
const lcd = new Lcd(document.getElementById("lcd"), machine);

// ---------------------------------------------------------------------------
// Scene
// ---------------------------------------------------------------------------
const view = document.getElementById("view");
const renderer = new THREE.WebGLRenderer({ antialias: true, preserveDrawingBuffer: true });
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
renderer.outputEncoding = THREE.sRGBEncoding;
view.appendChild(renderer.domElement);
const scene = new THREE.Scene();
scene.background = new THREE.Color(0xe9eef2);
const camera = new THREE.PerspectiveCamera(36, 1, 0.01, 30);
const controls = new THREE.OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;
scene.add(new THREE.HemisphereLight(0xffffff, 0x8a959e, 0.85));
const sun = new THREE.DirectionalLight(0xffffff, 0.75); sun.position.set(2, 3, 2.5); scene.add(sun);
const fillLight = new THREE.DirectionalLight(0xffffff, 0.35); fillLight.position.set(-2, 1.5, -1); scene.add(fillLight);
const floor = new THREE.Mesh(new THREE.CircleGeometry(2.2, 64), new THREE.MeshStandardMaterial({ color: 0xd9e0e5, roughness: 1 }));
floor.rotation.x = -Math.PI / 2; scene.add(floor);

const VIEWS = {
  whole: { pos: [1.75, 1.55, 2.35], target: [0, 0.9, 0] },
  front: { pos: [0, 1.05, 2.1], target: [0, 1.0, 0] },
  coins: { pos: [0.72, 1.15, 0.85], target: [0.19, 0.93, 0.05] },
  bay:   { pos: [-0.12, 1.02, 0.95], target: [-0.12, 0.96, 0.15] },
  water: { pos: [1.15, 0.95, 1.25], target: [0, 0.75, -0.05] },
};
function setView(name) {
  const v = VIEWS[name]; camera.position.set(...v.pos); controls.target.set(...v.target); controls.update();
  document.querySelectorAll("[data-view]").forEach(b => b.classList.toggle("on", b.dataset.view === name));
}
function resize() {
  const r = view.getBoundingClientRect();
  renderer.setSize(r.width, r.height); camera.aspect = r.width / Math.max(r.height, 1); camera.updateProjectionMatrix();
}
window.addEventListener("resize", resize);

// --- helpers ------------------------------------------------------------------
const mats = {};
function mat(color, o = {}) {
  const key = color + JSON.stringify(o);
  if (!mats[key]) mats[key] = new THREE.MeshStandardMaterial(Object.assign({ color, roughness: 0.6, metalness: 0.1 }, o));
  return mats[key];
}
function box(w, h, d, m, x, y, z, parent = scene) {
  const b = new THREE.Mesh(new THREE.BoxGeometry(w, h, d), m); b.position.set(x, y, z); parent.add(b); return b;
}
function cyl(rt, rb, h, m, x, y, z, parent = scene, seg = 32) {
  const c = new THREE.Mesh(new THREE.CylinderGeometry(rt, rb, h, seg), m); c.position.set(x, y, z); parent.add(c); return c;
}
function tube(points, r, m) {
  const curve = new THREE.CatmullRomCurve3(points.map(p => new THREE.Vector3(...p)));
  const t = new THREE.Mesh(new THREE.TubeGeometry(curve, 64, r, 8, false), m); scene.add(t); return { mesh: t, curve };
}
const labels = [];
function label(text, x, y, z) {
  const el = document.createElement("div"); el.className = "label3d"; el.textContent = text; document.body.appendChild(el);
  labels.push({ el, pos: new THREE.Vector3(x, y, z) });
}

// --- dimensions -------------------------------------------------------------------
const W = 0.69, D = 0.61, HB = 0.72, HT = 1.74, FZ = D / 2;   // base top, body top, front face z
const STEEL = 0x76838d;

// Frame: corner posts and rails, always visible.
const frameMat = mat(0x5d6870, { metalness: 0.4, roughness: 0.5 });
for (const sx of [-1, 1]) for (const sz of [-1, 1]) box(0.025, HT, 0.025, frameMat, sx * (W / 2 - 0.0125), HT / 2, sz * (D / 2 - 0.0125));
for (const y of [0.03, HB, HT - 0.0125]) {
  for (const sz of [-1, 1]) box(W, 0.025, 0.025, frameMat, 0, y, sz * (D / 2 - 0.0125));
  for (const sx of [-1, 1]) box(0.025, 0.025, D, frameMat, sx * (W / 2 - 0.0125), y, 0);
}
box(W + 0.06, 0.04, D + 0.06, mat(0x8b979f, { metalness: 0.3 }), 0, HT + 0.02, 0);   // roof slab overhang
box(W - 0.05, 0.012, D - 0.05, mat(0x6b7780), 0, HB + 0.006, 0);                      // shelf between base and body
box(W - 0.05, 0.012, D - 0.05, mat(0x9aa4ab), 0, 0.045, 0);                           // base floor

// Outer sheet panels (toggled: see-through / solid / hidden).
const panelMat = new THREE.MeshStandardMaterial({ color: STEEL, roughness: 0.55, metalness: 0.25, transparent: true, opacity: 0.22,
                                                  side: THREE.DoubleSide, depthWrite: false });
const panels = new THREE.Group(); scene.add(panels);
function panelWithHoles(w, h, holes) {
  const s = new THREE.Shape(); s.moveTo(-w / 2, -h / 2); s.lineTo(w / 2, -h / 2); s.lineTo(w / 2, h / 2); s.lineTo(-w / 2, h / 2); s.closePath();
  for (const [cx, cy, hw, hh] of holes) {
    const p = new THREE.Path(); p.moveTo(cx - hw / 2, cy - hh / 2); p.lineTo(cx - hw / 2, cy + hh / 2); p.lineTo(cx + hw / 2, cy + hh / 2);
    p.lineTo(cx + hw / 2, cy - hh / 2); p.closePath(); s.holes.push(p);
  }
  return new THREE.Mesh(new THREE.ShapeGeometry(s), panelMat);
}
// front of the upper body: holes for the LCD window, the dispensing bay and the change slot
const bodyH = HT - HB, bodyCY = (HT + HB) / 2;
const front = panelWithHoles(W, bodyH, [
  [-0.12, 1.42 - bodyCY, 0.11, 0.076],
  [-0.12, 0.98 - bodyCY, 0.26, 0.36],
  [0.20, 0.79 - bodyCY, 0.14, 0.045],
]);
front.position.set(0, bodyCY, FZ); panels.add(front);
const baseDoor = panelWithHoles(W - 0.04, HB - 0.06, []); baseDoor.position.set(0, HB / 2 + 0.01, FZ); panels.add(baseDoor);
for (const sx of [-1, 1]) { const p = panelWithHoles(D, HT - 0.03, []); p.rotation.y = Math.PI / 2; p.position.set(sx * W / 2, HT / 2 + 0.01, 0); panels.add(p); }
{ const p = panelWithHoles(W, HT - 0.03, []); p.position.set(0, HT / 2 + 0.01, -FZ); panels.add(p); }
// door hardware and louvres (on the panels group, hidden with them)
for (const [x, y, z] of [[0.24, 0.42, FZ + 0.01], [W / 2 + 0.01, 1.05, 0.18]]) {
  const l = cyl(0.014, 0.014, 0.02, mat(0xc9ced2, { metalness: 0.9, roughness: 0.25 }), x, y, z, panels); l.rotation.x = Math.PI / 2;
  if (x > W / 2) l.rotation.set(0, 0, Math.PI / 2);
}
for (let i = 0; i < 7; i++) box(0.004, 0.006, 0.11, mat(0x3a4249), W / 2 + 0.003, 1.55 + i * 0.016, -0.12, panels);

// Front fittings: LCD bezel, coinslot face, bay, change tray.
const lcdTex = new THREE.CanvasTexture(document.getElementById("lcd")); lcdTex.encoding = THREE.sRGBEncoding;
box(0.13, 0.095, 0.012, mat(0x1b2227), -0.12, 1.42, FZ - 0.004);
const lcdFace = new THREE.Mesh(new THREE.PlaneGeometry(0.0735, 0.049), new THREE.MeshBasicMaterial({ map: lcdTex, toneMapped: false }));
lcdFace.position.set(-0.12, 1.42, FZ + 0.003); scene.add(lcdFace);
label("3.5\" touchscreen (ESP32 LCD)", -0.12, 1.47, FZ);

const chrome = mat(0xd5d9dc, { metalness: 0.9, roughness: 0.22 });
box(0.075, 0.15, 0.012, chrome, 0.20, 1.12, FZ + 0.006);
box(0.004, 0.032, 0.014, mat(0x111111), 0.20, 1.165, FZ + 0.008);      // coin slot
cyl(0.013, 0.013, 0.014, chrome, 0.20, 1.085, FZ + 0.014).rotation.x = Math.PI / 2;   // return lever
label("Coinslot (₱1, ₱5, ₱10, ₱20)", 0.20, 1.21, FZ);

// dispensing bay
const bayMat = mat(0x2c3439, { roughness: 0.8 });
box(0.26, 0.36, 0.006, bayMat, -0.12, 0.98, FZ - 0.22);
for (const sx of [-1, 1]) box(0.006, 0.36, 0.22, bayMat, -0.12 + sx * 0.13, 0.98, FZ - 0.11);
box(0.26, 0.006, 0.22, bayMat, -0.12, 1.16, FZ - 0.11);
const grate = box(0.25, 0.012, 0.21, mat(0xb7bec3, { metalness: 0.6 }), -0.12, 0.806, FZ - 0.11);
const nozzle = cyl(0.007, 0.01, 0.03, chrome, -0.12, 1.14, FZ - 0.09);
const irSensor = box(0.03, 0.014, 0.012, mat(0x1d4f9c), -0.24, 0.86, FZ - 0.09);
irSensor.rotation.y = Math.PI / 2;
const irLed = cyl(0.003, 0.003, 0.004, new THREE.MeshStandardMaterial({ color: 0x330000, emissive: 0x000000 }), -0.232, 0.866, FZ - 0.09);
irLed.rotation.z = Math.PI / 2;
label("Dispensing bay · IR bottle sensor", -0.12, 1.20, FZ);

// change / return tray
box(0.14, 0.006, 0.08, mat(0x8a959c, { metalness: 0.5 }), 0.20, 0.768, FZ - 0.04);
box(0.14, 0.04, 0.006, mat(0x8a959c, { metalness: 0.5 }), 0.20, 0.79, FZ - 0.08);
label("Change tray", 0.20, 0.74, FZ + 0.02);

// --- coin path ------------------------------------------------------------------------
const darkMetal = mat(0x41494f, { metalness: 0.5, roughness: 0.45 });
box(0.07, 0.13, 0.09, darkMetal, 0.20, 1.12, FZ - 0.055);                       // acceptor body
box(0.012, 0.09, 0.03, mat(0x9aa3a9, { metalness: 0.4, transparent: true, opacity: 0.6 }), 0.20, 1.02, 0.19);   // chute
const gate1 = new THREE.Group(), gate2 = new THREE.Group();
for (const [g, y, z] of [[gate1, 1.0, 0.19], [gate2, 0.965, 0.17]]) {
  g.position.set(0.20, y, z); scene.add(g);
  box(0.05, 0.004, 0.035, mat(0x7a4cc2), 0.022, 0, 0, g);
  box(0.03, 0.04, 0.02, mat(0x1d1d1d), -0.03, 0.0, 0, g);                         // MG996R body
}
gate1.rotation.z = 0.5; gate2.rotation.z = 0.5 + Math.PI;
label("Gate 1 → ₱5 hopper (servo)", 0.27, 1.03, 0.19);
label("Gate 2 → ₱1 hopper (servo)", 0.12, 0.99, 0.17);

const HOP = { p5: { x: 0.28, label: "₱5 change hopper" }, p1: { x: 0.11, label: "₱1 change hopper" } };
const coinColor = { 1: 0xc0c4c8, 5: 0xd4b25a, 10: 0xc7a24a, 20: 0xd9bf6a };
const coinRadius = { 1: 0.0115, 5: 0.0125, 10: 0.0135, 20: 0.015 };
for (const k of ["p1", "p5"]) {
  const h = HOP[k];
  box(0.105, 0.13, 0.13, mat(0x3c4a55, { metalness: 0.3, transparent: true, opacity: 0.55 }), h.x, 0.875, 0.06);
  h.motor = box(0.05, 0.035, 0.035, new THREE.MeshStandardMaterial({ color: 0x555b60, metalness: 0.4 }), h.x, 0.80, -0.03);
  h.pile = cyl(0.04, 0.045, 1, mat(k === "p1" ? coinColor[1] : coinColor[5], { metalness: 0.8, roughness: 0.3 }), h.x, 0.82, 0.06);
  h.outlet = box(0.03, 0.03, 0.012, mat(0x1d4f9c), h.x, 0.825, 0.13);
  label(h.label, h.x, 0.96, 0.06);
}
const coinBox = box(0.14, 0.11, 0.15, mat(0x50575c, { metalness: 0.4, transparent: true, opacity: 0.7 }), 0.20, 0.79, -0.19);
const boxPile = cyl(0.05, 0.06, 1, mat(0xcaa957, { metalness: 0.8, roughness: 0.3 }), 0.20, 0.74, -0.19);
label("Locked coin box (₱10, ₱20)", 0.20, 0.87, -0.19);

// --- water ---------------------------------------------------------------------------
const waterMat = new THREE.MeshStandardMaterial({ color: 0x2f8fd8, transparent: true, opacity: 0.55, roughness: 0.1 });
// cold tank: steel can in foam insulation, compressor coil below
const TANK = { x: -0.05, y0: 1.35, h: 0.21, r: 0.095, z: -0.08 };
cyl(TANK.r + 0.035, TANK.r + 0.035, TANK.h + 0.02, mat(0xeeeeea, { roughness: 0.9, transparent: true, opacity: 0.35 }), TANK.x, TANK.y0 + TANK.h / 2, TANK.z);
cyl(TANK.r, TANK.r, TANK.h, mat(0xc8cdd0, { metalness: 0.8, roughness: 0.3, transparent: true, opacity: 0.35 }), TANK.x, TANK.y0 + TANK.h / 2, TANK.z);
const tankWater = cyl(TANK.r - 0.003, TANK.r - 0.003, 1, waterMat, TANK.x, TANK.y0, TANK.z);
box(0.05, 0.02, 0.03, mat(0x222222), TANK.x, TANK.y0 + TANK.h + 0.015, TANK.z);           // ultrasonic
for (const dx of [-0.012, 0.012]) cyl(0.008, 0.008, 0.006, mat(0xc0c0c0, { metalness: 0.8 }), TANK.x + dx, TANK.y0 + TANK.h + 0.004, TANK.z);
label("Cold tank 6 L · ultrasonic level sensor", TANK.x, TANK.y0 + TANK.h + 0.06, TANK.z);
const compressor = cyl(0.07, 0.07, 0.14, mat(0x1f2a33, { metalness: 0.3 }), -0.17, 0.80, -0.18);
label("Compressor (cooling)", -0.17, 0.90, -0.18);

// two 5-gallon jugs in the base
const GAL = { a: { x: -0.155 }, b: { x: 0.155 } };
const jugMat = new THREE.MeshStandardMaterial({ color: 0x3a6fe0, transparent: true, opacity: 0.28, roughness: 0.15, depthWrite: false });
const jugWaterMat = new THREE.MeshStandardMaterial({ color: 0x2f7fe8, transparent: true, opacity: 0.6, roughness: 0.1 });
for (const k of ["a", "b"]) {
  const g = GAL[k];
  cyl(0.135, 0.135, 0.38, jugMat, g.x, 0.05 + 0.19, 0.02);
  cyl(0.03, 0.135, 0.08, jugMat, g.x, 0.05 + 0.42, 0.02);
  cyl(0.028, 0.028, 0.05, jugMat, g.x, 0.05 + 0.485, 0.02);
  g.water = cyl(0.13, 0.13, 1, jugWaterMat, g.x, 0.05, 0.02);
  g.sensor = box(0.03, 0.012, 0.004, new THREE.MeshStandardMaterial({ color: 0xe8d7b0, emissive: 0x000000 }), g.x, 0.11, 0.157);
  label(`Gallon ${k.toUpperCase()} · capacitive sensor`, g.x, 0.62, 0.02);
}
// pumps and hoses
const fillPump = cyl(0.03, 0.03, 0.08, new THREE.MeshStandardMaterial({ color: 0x2b4f6e, emissive: 0x000000 }), 0.0, 0.6, -0.23);
fillPump.rotation.z = Math.PI / 2;
label("Fill pump (gallons → tank)", 0.0, 0.67, -0.23);
const hoseMat = mat(0xe8ecee, { roughness: 0.4, transparent: true, opacity: 0.85 });
const hoseA = tube([[GAL.a.x, 0.52, 0.02], [GAL.a.x, 0.62, -0.05], [-0.04, 0.6, -0.2], [0, 0.6, -0.23]], 0.006, mat(0xd8c84a));
const hoseB = tube([[GAL.b.x, 0.52, 0.02], [GAL.b.x, 0.62, -0.05], [0.04, 0.6, -0.2], [0, 0.6, -0.23]], 0.006, hoseMat);
const hoseUp = tube([[0.04, 0.6, -0.23], [0.12, 0.7, -0.26], [0.12, 1.3, -0.26], [0.04, 1.62, -0.15], [TANK.x, TANK.y0 + TANK.h, TANK.z]], 0.006, hoseMat);
const dispPump = cyl(0.025, 0.025, 0.07, new THREE.MeshStandardMaterial({ color: 0x2b4f6e, emissive: 0x000000 }), -0.22, 1.24, -0.12);
dispPump.rotation.z = Math.PI / 2;
label("Dispense pump", -0.22, 1.29, -0.12);
const hoseOut = tube([[TANK.x, TANK.y0, TANK.z], [-0.12, 1.30, -0.12], [-0.20, 1.24, -0.12], [-0.12, 1.20, 0.05], [-0.12, 1.16, FZ - 0.09]], 0.006, hoseMat);

// electronics on the right inner wall
const pcb = (w, h, c, x, y, z) => { const b = box(0.004, h, w, mat(c), x, y, z); return b; };
pcb(0.10, 0.053, 0x0f6e74, W / 2 - 0.03, 1.32, -0.05);
label("Arduino Mega 2560", W / 2 - 0.03, 1.37, -0.05);
pcb(0.075, 0.055, 0x1d4f9c, W / 2 - 0.03, 1.22, -0.05);
label("4-channel relay", W / 2 - 0.03, 1.27, -0.05);
box(0.06, 0.11, 0.2, mat(0x8d969c, { metalness: 0.6 }), W / 2 - 0.06, 1.5, -0.17);
label("12 V supply + buck converters", W / 2 - 0.06, 1.58, -0.17);
const buzzer = cyl(0.012, 0.012, 0.01, new THREE.MeshStandardMaterial({ color: 0x111111, emissive: 0x000000 }), W / 2 - 0.03, 1.12, -0.05);
buzzer.rotation.z = Math.PI / 2;

// moving things: water stream, bottle, coins
const stream = cyl(0.0035, 0.0035, 1, new THREE.MeshStandardMaterial({ color: 0x6fb7f2, transparent: true, opacity: 0.75 }), -0.12, 1, FZ - 0.09);
stream.visible = false;
const bottle = new THREE.Group(); bottle.position.set(-0.12, 0.812, FZ - 0.09); scene.add(bottle); bottle.visible = false;
const BOT = { r: 0.043, h: 0.26 };
cyl(BOT.r, BOT.r, BOT.h, new THREE.MeshStandardMaterial({ color: 0xcfe8ff, transparent: true, opacity: 0.3, roughness: 0.05, depthWrite: false }), 0, BOT.h / 2, 0, bottle);
cyl(0.015, BOT.r, 0.05, new THREE.MeshStandardMaterial({ color: 0xcfe8ff, transparent: true, opacity: 0.3, depthWrite: false }), 0, BOT.h + 0.025, 0, bottle);
const bottleWater = cyl(BOT.r - 0.002, BOT.r - 0.002, 1, waterMat, 0, 0, 0, bottle);
let bottleMl = 0;

const flying = [];   // coins in the air: {mesh, pts, t, dur}
const trayCoins = new THREE.Group(); scene.add(trayCoins);
function coinMesh(peso) {
  const m = new THREE.Mesh(new THREE.CylinderGeometry(coinRadius[peso], coinRadius[peso], 0.002, 24),
                           mat(coinColor[peso], { metalness: 0.85, roughness: 0.3 }));
  m.rotation.x = Math.PI / 2; scene.add(m); return m;
}
function flyCoin(peso, pts, dur, delay = 0, onEnd) {
  const mesh = coinMesh(peso); mesh.visible = delay <= 0; mesh.position.set(...pts[0]);
  flying.push({ mesh, pts: pts.map(p => new THREE.Vector3(...p)), t: -delay, dur, onEnd });
}
function addTrayCoin(peso) {
  const n = trayCoins.children.length;
  const m = coinMesh(peso); scene.remove(m); trayCoins.add(m);
  m.rotation.set(-Math.PI / 2 + 0.3 * ((n % 3) - 1), 0, 0);
  m.position.set(0.15 + (n % 6) * 0.018, 0.773 + Math.floor(n / 6) * 0.003, FZ - 0.045 + ((n * 7) % 3) * 0.012);
}

const SLOT = [0.20, 1.18, FZ + 0.06], ACC = [0.20, 1.12, FZ - 0.05], G1 = [0.20, 1.0, 0.19], G2 = [0.20, 0.965, 0.17];
const TRAY = [0.20, 0.776, FZ - 0.04];
function coinPath(dest) {
  if (dest === "tray") return [SLOT, [0.20, 1.165, FZ], ACC, [0.22, 0.95, FZ - 0.04], TRAY];
  if (dest === "p5") return [SLOT, [0.20, 1.165, FZ], ACC, G1, [0.25, 0.97, 0.12], [HOP.p5.x, 0.92, 0.06]];
  if (dest === "p1") return [SLOT, [0.20, 1.165, FZ], ACC, G1, G2, [0.14, 0.95, 0.10], [HOP.p1.x, 0.92, 0.06]];
  return [SLOT, [0.20, 1.165, FZ], ACC, G1, G2, [0.20, 0.90, 0.0], [0.20, 0.82, -0.19]];
}

// ---------------------------------------------------------------------------
// Controls
// ---------------------------------------------------------------------------
let speed = 5, paused = false, showLabels = true, panelMode = 0;   // 0 see-through, 1 solid, 2 hidden
const $ = id => document.getElementById(id);
document.querySelectorAll("[data-coin]").forEach(b => b.addEventListener("click", () => machine.insertCoin(+b.dataset.coin)));
$("bBottle").addEventListener("click", () => machine.placeBottle(!machine.bottle));
$("bTray").addEventListener("click", () => {
  const coins = machine.takeTray();
  const sum = coins.reduce((a, c) => a + c, 0);
  machine.say(coins.length ? `Customer took ${coins.length} coin${coins.length > 1 ? "s" : ""} from the tray: ₱${sum}.` : "The tray is empty.");
});
$("bLoad").addEventListener("click", () => machine.loadChange(+$("inP1").value, +$("inP5").value));
document.querySelectorAll("[data-gal]").forEach(b => b.addEventListener("click", () => machine.setGallon(b.dataset.gal, +b.dataset.ml)));
$("cBoxFull").addEventListener("change", e => { machine.boxFull = e.target.checked; });
$("bEmptyBox").addEventListener("click", () => { machine.emptyBox(); $("cBoxFull").checked = false; machine.boxFull = false; });
$("cJam").addEventListener("change", e => { machine.jamNext = e.target.checked; });
$("bClear").addEventListener("click", () => machine.clearFaults());
document.querySelectorAll("[data-view]").forEach(b => b.addEventListener("click", () => setView(b.dataset.view)));

$("bPlay").addEventListener("click", () => {
  paused = !paused; $("iPlay").textContent = paused ? "▶" : "❚❚"; $("lPlay").textContent = paused ? "Play" : "Pause";
});
$("bReset").addEventListener("click", () => {
  machine.reset(); bottleMl = 0; trayCoins.clear(); flying.splice(0).forEach(f => scene.remove(f.mesh));
  $("cBoxFull").checked = false; $("cJam").checked = false; $("inP1").value = 60; $("inP5").value = 20;
});
document.querySelectorAll("[data-speed]").forEach(b => b.addEventListener("click", () => {
  speed = +b.dataset.speed; document.querySelectorAll("[data-speed]").forEach(x => x.classList.toggle("lit", x === b));
}));
$("bLabels").addEventListener("click", () => {
  showLabels = !showLabels; $("bLabels").classList.toggle("lit", showLabels); $("lLabels").textContent = showLabels ? "Labels on" : "Labels off";
});
$("bPanels").addEventListener("click", () => {
  panelMode = (panelMode + 1) % 3;
  panels.visible = panelMode !== 2;
  panelMat.opacity = panelMode === 1 ? 1 : 0.22; panelMat.depthWrite = panelMode === 1; panelMat.needsUpdate = true;
  $("bPanels").classList.toggle("lit", panelMode !== 2);
  $("lPanels").textContent = ["See-through", "Solid", "Hidden"][panelMode];
});
$("bPhoto").addEventListener("click", () => {
  renderer.render(scene, camera);
  const a = document.createElement("a"); a.href = renderer.domElement.toDataURL("image/jpeg", 0.92); a.download = "water-vendo.jpg"; a.click();
});
let recorder = null, chunks = [];
$("bRec").addEventListener("click", () => {
  if (recorder) { recorder.stop(); return; }
  const stream2 = renderer.domElement.captureStream(30);
  recorder = new MediaRecorder(stream2, { mimeType: MediaRecorder.isTypeSupported("video/webm;codecs=vp9") ? "video/webm;codecs=vp9" : "video/webm" });
  chunks = []; recorder.ondataavailable = e => chunks.push(e.data);
  recorder.onstop = () => {
    const a = document.createElement("a"); a.href = URL.createObjectURL(new Blob(chunks, { type: "video/webm" })); a.download = "water-vendo.webm"; a.click();
    recorder = null; $("bRec").classList.remove("recording"); $("lRec").textContent = "Record";
  };
  recorder.start(); $("bRec").classList.add("recording"); $("lRec").textContent = "Stop rec.";
});

// ---------------------------------------------------------------------------
// Frame loop
// ---------------------------------------------------------------------------
const STATE_NAME = ["BOOT", "STANDBY", "ACCEPTING", "SELECTING", "AWAITING BOTTLE", "DISPENSING", "PAUSED", "SETTLING",
                    "COMPLETE", "PAYING CHANGE", "THANK YOU", "FAULT", "ADMIN"];
let last = performance.now(), beepUntil = 0;

function handleEvents() {
  for (const e of machine.events.splice(0)) {
    if (e.type === "coin") {
      const pts = coinPath(e.dest), sp = Math.max(speed, 1);
      if (e.dest === "tray") flyCoin(e.peso, pts, 900 / sp, 0, () => addTrayCoin(e.peso));
      else flyCoin(e.peso, pts, (e.idMs + e.routeMs) / sp);
    } else if (e.type === "payout") {
      const h = HOP[e.hopper];
      flyCoin(e.peso, [[h.x, 0.83, 0.13], [h.x, 0.80, 0.2], [0.20, 0.79, FZ - 0.06], TRAY], 500 / Math.max(speed, 1), 0, () => addTrayCoin(e.peso));
    } else if (e.type === "trayEmpty") {
      trayCoins.clear();
    } else if (e.type === "beep") {
      beepUntil = performance.now() + 900;
    } else if (e.type === "jamUsed") {
      $("cJam").checked = machine.jamNext;
    } else if (e.type === "reset") {
      trayCoins.clear();
    }
  }
}

function updateScene(now, dtReal) {
  const m = machine;
  // gates: CLOSED swings the flap across the chute
  gate1.rotation.z += ((m.gates.g1 === "closed" ? -0.6 : 0.5) - gate1.rotation.z) * Math.min(1, dtReal * 12);
  gate2.rotation.z += ((m.gates.g2 === "closed" ? 0.6 + Math.PI : 0.5 + Math.PI) - gate2.rotation.z) * Math.min(1, dtReal * 12);
  // coin piles
  for (const k of ["p1", "p5"]) {
    const h = HOP[k], n = m.inv[k], hh = Math.max(0.002, Math.min(0.1, n * 0.0011));
    h.pile.scale.y = hh; h.pile.position.y = 0.812 + hh / 2;
    h.motor.material.emissive.setHex(m.hopperMotor === k ? 0x226622 : 0x000000);
  }
  const bx = Math.max(0.001, Math.min(0.09, (m.inv.box10 + m.inv.box20) * 0.004));
  boxPile.scale.y = bx; boxPile.position.y = 0.737 + bx / 2;
  // water
  const th = TANK.h * m.tank / CFG.TANK_ML;
  tankWater.scale.y = Math.max(0.001, th); tankWater.position.y = TANK.y0 + th / 2;
  for (const k of ["a", "b"]) {
    const g = GAL[k], ml = k === "a" ? m.galA : m.galB, h = 0.45 * Math.max(0, ml) / CFG.GALLON_ML;
    g.water.scale.y = Math.max(0.001, h); g.water.position.y = 0.055 + h / 2;
    g.sensor.material.emissive.setHex(m.gallonLow(k) ? 0xc06000 : 0x000000);
  }
  fillPump.material.emissive.setHex(m.fillPump ? 0x1d6a3a : 0x000000);
  dispPump.material.emissive.setHex(m.state === ST.DISPENSING ? 0x1d6a3a : 0x000000);
  buzzer.material.emissive.setHex(now < beepUntil ? 0xff8800 : 0x000000);
  irLed.material.emissive.setHex(m.bottle ? 0xff2200 : 0x000000);
  // bottle and stream
  bottle.visible = m.bottle;
  if (m.state === ST.STANDBY) bottleMl = 0;
  if (m.state === ST.DISPENSING) bottleMl = Math.max(bottleMl, m.total + m.segment);
  const fill = Math.min(1, bottleMl / 1500) * (BOT.h - 0.01);
  bottleWater.scale.y = Math.max(0.0005, fill); bottleWater.position.y = 0.004 + fill / 2;
  stream.visible = m.state === ST.DISPENSING && m.bottle;
  if (stream.visible) {
    const top = 1.125, bottom = bottle.position.y + 0.006 + fill;
    stream.scale.y = top - bottom; stream.position.y = (top + bottom) / 2;
  }
  // flying coins
  for (let i = flying.length - 1; i >= 0; i--) {
    const f = flying[i]; f.t += dtReal * 1000;
    if (f.t < 0) continue;
    f.mesh.visible = true;
    const u = Math.min(1, f.t / f.dur), seg = (f.pts.length - 1) * u, k = Math.min(f.pts.length - 2, Math.floor(seg));
    f.mesh.position.lerpVectors(f.pts[k], f.pts[k + 1], seg - k);
    f.mesh.rotation.y += dtReal * 8;
    if (u >= 1) { scene.remove(f.mesh); flying.splice(i, 1); if (f.onEnd) f.onEnd(); }
  }
}

function readout() {
  const m = machine, s = Math.round;
  const tray = m.tray.reduce((a, c) => a + c, 0);
  return [
    `State          ${STATE_NAME[m.state]}${m.fault ? "  (" + FAULT_TEXT[m.fault] + ")" : ""}`,
    `Credit         ₱${(m.credit / 100).toFixed(2)}    inserted ₱${(m.inserted / 100).toFixed(2)}`,
    `Coin slot      ${m.acceptorOpen() ? "open" : "closed"}`,
    `Change hoppers ₱1 × ${m.inv.p1}    ₱5 × ${m.inv.p5}`,
    `Coin box       ₱10 × ${m.inv.box10}    ₱20 × ${m.inv.box20}    (₱${m.inv.box10 * 10 + m.inv.box20 * 20})`,
    `Tray           ${m.tray.length} coin${m.tray.length === 1 ? "" : "s"}, ₱${tray}`,
    `Cold tank      ${s(m.tank)} mL (${m.tankPct()}%)`,
    `Gallons        A ${s(m.galA / 100) / 10} L${m.gallonLow("a") ? " LOW" : ""}    B ${s(m.galB / 100) / 10} L${m.gallonLow("b") ? " LOW" : ""}`,
    `Pumps          dispense ${m.state === ST.DISPENSING ? "ON" : "off"}    fill ${m.fillPump ? "ON" : "off"}`,
  ].join("\n");
}

let lastStory = "", lastReadout = "";
function frame(now) {
  const dtReal = Math.min(0.1, (now - last) / 1000); last = now;
  if (!paused) {
    let dt = dtReal * 1000 * speed;
    while (dt > 0) { const step = Math.min(dt, 50); machine.tick(step); dt -= step; }
  }
  handleEvents();
  lcd.draw(now); lcdTex.needsUpdate = true;
  updateScene(now, paused ? 0 : dtReal);
  $("bBottle").textContent = machine.bottle ? "Remove bottle" : "Place bottle";
  $("beep").style.display = now < beepUntil ? "block" : "none";
  if (machine.story !== lastStory) { lastStory = machine.story; $("story").textContent = lastStory; }
  const r = readout(); if (r !== lastReadout) { lastReadout = r; $("readout").textContent = r; }

  controls.update();
  renderer.render(scene, camera);
  const rect = renderer.domElement.getBoundingClientRect(), v = new THREE.Vector3();
  for (const l of labels) {
    v.copy(l.pos).project(camera);
    const on = showLabels && v.z < 1;
    l.el.style.display = on ? "block" : "none";
    if (on) { l.el.style.left = (rect.left + (v.x + 1) / 2 * rect.width) + "px"; l.el.style.top = (rect.top + (1 - v.y) / 2 * rect.height) + "px"; }
  }
  requestAnimationFrame(frame);
}

$("loading").remove();
resize(); setView("whole");
requestAnimationFrame(frame);
