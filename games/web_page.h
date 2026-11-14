// Page web de la manette (servie par l'ESP32 sur http://games.local)
#pragma once
#include <pgmspace.h>

const char WEB_PAGE[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="fr"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<meta name="mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="theme-color" content="#0b0b14">
<title>Game hub</title>
<style>
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent;user-select:none;-webkit-user-select:none}
html,body{margin:0;height:100%;background:#0b0b14;color:#e6e6f0;font-family:system-ui,sans-serif;overflow:hidden;touch-action:none}
body{display:flex;flex-direction:column}
header{display:flex;justify-content:space-between;align-items:center;padding:8px 12px;gap:8px;background:#141428;border-bottom:1px solid #26264a}
.stat{text-align:center;min-width:56px}
.stat b{display:block;font:700 20px/1.1 ui-monospace,monospace;color:#fff}
.stat span{font-size:11px;letter-spacing:.08em;color:#ff5050}
#msg{flex:1;display:flex;align-items:center;justify-content:center;text-align:center;font-size:18px;color:#00dcff;padding:8px}
#msg.play{color:#7a7a99;font-size:14px}
#msg.over{color:#ff5050;font-size:22px;font-weight:700}
#dot{width:10px;height:10px;border-radius:50%;background:#a33;display:inline-block;margin-right:6px}
#dot.on{background:#2c5}
.pad{display:flex;justify-content:space-between;align-items:flex-end;padding:12px 16px 22px;gap:12px}
.dir{display:flex;gap:12px}
button.k{width:min(26vw,120px);height:min(26vw,120px);border-radius:22px;border:2px solid #3a3a6a;background:#1c1c38;color:#cfd2ff;font-size:38px;touch-action:none}
button.k.fire{width:min(34vw,150px);height:min(34vw,150px);border-radius:50%;background:#5a1020;border-color:#c02040;color:#fff;font-size:22px;font-weight:700;letter-spacing:.06em}
button.k.down{filter:brightness(1.8);transform:scale(.96)}
details{padding:0 12px 10px;color:#9a9ab8;font-size:13px}
details summary{cursor:pointer;padding:4px 0}
.row{display:flex;align-items:center;gap:10px;margin:6px 0}
input[type=range]{flex:1}
fieldset{border:1px solid #3a3a6a;border-radius:8px;margin:8px 0;padding:6px 10px}
legend{color:#cfd2ff;padding:0 4px}
#apply{margin-left:auto;background:#2a4a8a;color:#fff;border:0;border-radius:6px;padding:6px 14px}
select{background:#1c1c38;color:#e6e6f0;border:1px solid #3a3a6a;border-radius:6px;padding:4px}
.hint{font-size:12px;color:#6a6a88}
@media (orientation:landscape) and (max-height:500px){
 body{flex-direction:row;flex-wrap:wrap}
 header{width:100%}
 #msg{order:2}
 .pad{width:100%;position:fixed;bottom:0;padding-bottom:12px}
 details{display:none}
}
</style></head><body>
<header>
 <div class="stat"><span>SCORE</span><b id="s">0</b></div>
 <div class="stat"><span>RECORD</span><b id="h">0</b></div>
 <div class="stat"><span>VIES</span><b id="l">-</b></div>
 <div class="stat"><span>VAGUE</span><b id="w">-</b></div>
</header>
<div id="msg"><span><span id="dot"></span><span id="txt">Connexion...</span></span></div>
<details><summary>Réglages</summary>
 <div class="row">Luminosité <input id="bri" type="range" min="5" max="255"></div>
 <fieldset><legend>Dalles 128x64</legend>
  <div class="row">Nombre <select id="nb"><option>1</option><option>2</option><option>3</option><option>4</option></select></div>
  <div class="row">Disposition <select id="arr"><option value="0">l'une sous l'autre</option><option value="1">côte à côte</option><option value="2">carré 2x2 (4 dalles)</option></select></div>
  <label class="row"><input type="checkbox" id="flip"> Écran retourné (180°)</label>
  <label class="row"><input type="checkbox" id="serp"> Une dalle sur deux retournée (serpentin)</label>
  <div class="row"><span id="res" class="hint"></span><button id="apply">Appliquer</button></div>
  <div class="hint">La 1re dalle est celle reliée à l'ESP32 (en haut à gauche). Retourné : si elle est en bas ou à droite. L'ESP32 redémarre.</div>
 </fieldset>
 <div class="hint">Clavier : ← → ou Q D, espace pour tirer. Manette de jeu reconnue.</div>
</details>
<div class="pad">
 <div class="dir"><button class="k" data-b="1">◀</button><button class="k" data-b="2">▶</button></div>
 <button class="k fire" data-b="4">FEU</button>
</div>
<script>
let ws, keys = 0, touch = 0, kb = 0, pad = 0, mode = -1, cfg = '';
const $ = id => document.getElementById(id);
function connect() {
  ws = new WebSocket('ws://' + location.host + '/ws');
  ws.onopen = () => { $('dot').className = 'on'; send(true); };
  ws.onclose = () => { $('dot').className = ''; $('txt').textContent = 'Connexion perdue...'; setTimeout(connect, 1000); };
  ws.onmessage = e => {
    const d = JSON.parse(e.data);
    $('s').textContent = d.s; $('h').textContent = d.h;
    $('l').textContent = d.m ? d.l : '-'; $('w').textContent = d.m ? d.w : '-';
    if (document.activeElement !== $('bri')) $('bri').value = d.b;
    if (d.c !== cfg) { cfg = d.c; showCfg(cfg); }
    if (d.m !== mode) {
      mode = d.m;
      $('msg').className = ['', 'play', 'over'][mode];
      $('txt').textContent = ['Appuie sur FEU pour jouer', 'Partie en cours', 'GAME OVER'][mode];
    }
  };
}
function send(force) {
  const k = touch | kb | pad;
  if ((k !== keys || force) && ws && ws.readyState === 1) ws.send('k' + k);
  keys = k;
}
// boutons tactiles (plusieurs doigts a la fois)
const held = new Map();
document.querySelectorAll('button.k').forEach(b => {
  const bit = +b.dataset.b;
  b.addEventListener('pointerdown', e => { e.preventDefault(); b.setPointerCapture(e.pointerId); held.set(e.pointerId, bit); b.classList.add('down'); upd(); });
  const up = e => { if (held.delete(e.pointerId)) { b.classList.remove('down'); upd(); } };
  b.addEventListener('pointerup', up); b.addEventListener('pointercancel', up); b.addEventListener('lostpointercapture', up);
  b.addEventListener('contextmenu', e => e.preventDefault());
});
function upd() { touch = 0; held.forEach(v => touch |= v); send(); }
// clavier
const KEYS = { ArrowLeft: 1, KeyA: 1, KeyQ: 1, ArrowRight: 2, KeyD: 2, Space: 4, ArrowUp: 4, KeyW: 4, KeyZ: 4, Enter: 4 };
addEventListener('keydown', e => { const b = KEYS[e.code]; if (b) { e.preventDefault(); kb |= b; send(); } });
addEventListener('keyup', e => { const b = KEYS[e.code]; if (b) { kb &= ~b; send(); } });
addEventListener('blur', () => { kb = 0; touch = 0; held.clear(); send(); });
// manette (API Gamepad)
function poll() {
  let p = 0;
  for (const g of navigator.getGamepads ? navigator.getGamepads() : []) {
    if (!g) continue;
    const x = g.axes[0] || 0, bt = i => g.buttons[i] && g.buttons[i].pressed;
    if (x < -0.4 || bt(14)) p |= 1;
    if (x > 0.4 || bt(15)) p |= 2;
    if (bt(0) || bt(1) || bt(2) || bt(3) || bt(12)) p |= 4;
  }
  if (p !== pad) { pad = p; send(); }
  requestAnimationFrame(poll);
}
poll();
setInterval(() => { if (keys) send(true); }, 1000);  // touches tenues : signale qu'on est toujours la
// reglages
$('bri').addEventListener('input', e => ws.readyState === 1 && ws.send('v' + e.target.value));
$('bri').addEventListener('change', e => ws.readyState === 1 && ws.send('V' + e.target.value));
// menu Dalles : resolution affichee, envoi c<nombre><disposition><retourne><serpentin>
function showCfg(c) {
  $('nb').value = c[0]; $('arr').value = c[1]; $('flip').checked = c[2] === '1'; $('serp').checked = c[3] === '1'; res();
}
function res() {
  const n = +$('nb').value, a = +$('arr').value, sq = a === 2 && n === 4;
  if (a === 2 && n !== 4) $('arr').value = '0';
  const cols = sq ? 2 : a === 1 ? n : 1, rows = n / cols;
  $('res').textContent = 'Écran ' + 128 * cols + ' x ' + 64 * rows;
}
['nb', 'arr'].forEach(id => $(id).addEventListener('change', res));
$('apply').addEventListener('click', () => {
  res();
  const c = $('nb').value + $('arr').value + ($('flip').checked ? 1 : 0) + ($('serp').checked ? 1 : 0);
  if (c !== cfg && confirm("Appliquer ces dalles ? L'ESP32 va redémarrer.")) ws.send('c' + c);
});
connect();
</script>
</body></html>
)HTML";
