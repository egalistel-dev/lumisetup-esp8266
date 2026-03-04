/*
 * ╔══════════════════════════════════════════════╗
 * ║       Lampe Connectée — LumiSetup            ║
 * ║  ESP8266 + SR505 + WS2812B — Fichier unique  ║
 * ╠══════════════════════════════════════════════╣
 * ║  Auteur  : Egalistel                         ║
 * ║  Site    : https://egamaker.be               ║
 * ╚══════════════════════════════════════════════╝
 *
 * Bibliothèques requises (gestionnaire Arduino) :
 *   - FastLED           (par Daniel Garcia)
 *   - ESPAsyncWebServer (par me-no-dev, via GitHub)
 *   - ESPAsyncTCP       (par me-no-dev, via GitHub)
 *   - ArduinoJson       v6 (par Benoit Blanchon)
 *   - NTPClient         (par Fabrice Weinberg)
 *   - ESP8266mDNS       (inclus dans le package ESP8266, rien à installer)
 *
 * !! NE PAS installer WiFiManager — conflit avec ESPAsyncWebServer !!
 *    Le portail WiFi est géré directement dans ce code.
 *
 * Câblage NodeMCU :
 *   SR505   VCC → 3.3V  |  GND → GND  |  OUT → GPIO14 (D5)
 *   WS2812B DIN → GPIO12 (D6)  |  5V → Alim 5V externe
 *   GND alim 5V → GND ESP8266  (GND COMMUN obligatoire)
 *
 * Premier démarrage :
 *   → Rejoindre le WiFi "LumiSetup" (mdp: lumi1234)
 *   → Ouvrir http://192.168.4.1 pour configurer le WiFi
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// ─────────────────────────────────────────────
//  PINS  (GPIO directs — fonctionne sur toutes les cartes ESP8266)
// ─────────────────────────────────────────────
#define PIR_PIN      14   // D5 sur NodeMCU
#define LED_DATA_PIN 12   // D6 sur NodeMCU

// ─────────────────────────────────────────────
//  CONSTANTES
// ─────────────────────────────────────────────
#define MAX_LEDS      300
#define CONFIG_FILE   "/config.json"
#define WIFI_FILE     "/wifi.json"
#define AP_NAME       "LumiSetup"
#define AP_PASS       "lumi1234"
#define AP_IP_STR     "192.168.4.1"
#define HOSTNAME      "lumisetup"    // accessible via http://lumisetup.local

// ─────────────────────────────────────────────
//  CONFIG
// ─────────────────────────────────────────────
struct Config {
  uint16_t numLeds      = 30;
  uint8_t  red          = 255;
  uint8_t  green        = 200;
  uint8_t  blue         = 100;
  uint8_t  brightness   = 80;
  uint16_t duration     = 120;
  bool     fadeEffect   = true;
  uint8_t  startHour    = 18;
  uint8_t  startMin     = 0;
  uint8_t  endHour      = 23;
  uint8_t  endMin       = 0;
  int8_t   tzOffset     = 1;
  bool     systemActive = true;
  bool     modeOn       = false;
  uint8_t  pirDebounce  = 3;
};

// ─────────────────────────────────────────────
//  VARIABLES
// ─────────────────────────────────────────────
Config         cfg;
CRGB           leds[MAX_LEDS];
AsyncWebServer server(80);
DNSServer      dnsServer;
WiFiUDP        ntpUDP;
NTPClient      timeClient(ntpUDP, "pool.ntp.org");

bool          portalMode  = false;
bool          ledsOn      = false;
bool          overrideOn  = false;
bool          pirState    = false;
unsigned long lightOffTime = 0;
unsigned long lastPirTime  = 0;

// ─────────────────────────────────────────────
//  PAGE PORTAIL WIFI (HTML minimal)
// ─────────────────────────────────────────────
const char PORTAL_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html><html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>LumiSetup — WiFi</title>
<style>
*{box-sizing:border-box;margin:0;padding:0;}
body{background:#0f0f13;color:#e8e8f0;font-family:'Segoe UI',system-ui,sans-serif;
     display:flex;align-items:center;justify-content:center;min-height:100vh;padding:20px;}
.card{background:#1a1a24;border:1px solid #2a2a3a;border-radius:16px;padding:32px;
      max-width:380px;width:100%;text-align:center;}
h1{color:#f5a623;font-size:1.5rem;margin-bottom:6px;}
p{color:#7a7a9a;font-size:.85rem;margin-bottom:28px;line-height:1.6;}
label{display:block;text-align:left;font-size:.82rem;color:#7a7a9a;margin-bottom:4px;}
input{width:100%;background:#0f0f13;border:1px solid #2a2a3a;border-radius:8px;
      color:#e8e8f0;padding:12px;font-size:.95rem;margin-bottom:16px;outline:none;}
input:focus{border-color:#f5a623;}
button{width:100%;padding:14px;border-radius:10px;border:none;
       background:#f5a623;color:#111;font-size:1rem;font-weight:700;cursor:pointer;transition:opacity .2s;}
button:disabled{opacity:.5;cursor:not-allowed;}
.msg{margin-top:16px;font-size:.85rem;color:#4caf7d;display:none;line-height:1.6;}
.msg span{display:block;font-size:.78rem;color:#7a7a9a;margin-top:4px;}
footer{margin-top:28px;font-size:.75rem;color:#4a4a6a;}
footer a{color:#f5a623;text-decoration:none;}
</style>
</head>
<body>
<div class="card">
  <h1>💡 LumiSetup</h1>
  <p>Configurez la connexion WiFi<br>pour votre lampe connectée.</p>
  <label>Nom du réseau WiFi (SSID)</label>
  <input type="text" id="ssid" placeholder="Mon réseau WiFi">
  <label>Mot de passe</label>
  <input type="password" id="pass" placeholder="Mot de passe">
  <button id="submitBtn" onclick="save()">Se connecter</button>
  <div class="msg" id="msg">
    ✓ Identifiants enregistrés !
    <span>L'appareil redémarre et tente de rejoindre votre réseau.<br>
    Reconnectez-vous à votre WiFi habituel puis ouvrez<br>
    <strong>http://lumisetup.local</strong></span>
  </div>
  <footer>Egalistel &nbsp;·&nbsp; <a href="https://egamaker.be" target="_blank">egamaker.be</a></footer>
</div>
<script>
async function save(){
  const ssid=document.getElementById('ssid').value.trim();
  const pass=document.getElementById('pass').value;
  if(!ssid){alert('Entrez un nom de réseau.');return;}
  // Afficher le message ET désactiver le bouton immédiatement
  const btn=document.getElementById('submitBtn');
  btn.disabled=true;
  btn.textContent='Enregistrement...';
  document.getElementById('msg').style.display='block';
  // Envoyer en arrière-plan (l'ESP peut redémarrer avant la réponse, c'est normal)
  try{
    const b=new URLSearchParams({ssid,pass});
    await fetch('/wifi/save',{method:'POST',body:b});
  }catch(e){/* redémarrage ESP normal */}
}
</script>
</body></html>
)rawhtml";

// ─────────────────────────────────────────────
//  PAGE PRINCIPALE (HTML embarqué)
// ─────────────────────────────────────────────
const char INDEX_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>LumiSetup</title>
<style>
:root{
  --bg:#0f0f13;--card:#1a1a24;--border:#2a2a3a;
  --accent:#f5a623;--accent2:#e05c5c;--green:#4caf7d;
  --text:#e8e8f0;--muted:#7a7a9a;--radius:14px;
}
*{box-sizing:border-box;margin:0;padding:0;}
body{
  background:var(--bg);color:var(--text);
  font-family:'Segoe UI',system-ui,sans-serif;
  min-height:100vh;padding:16px;
}
.page-grid{
  display:grid;grid-template-columns:1fr;
  gap:16px;max-width:960px;margin:0 auto;
}
@media(min-width:700px){
  .page-grid{grid-template-columns:1fr 1fr;}
  .full-width{grid-column:1/-1;}
}
header{text-align:center;padding:20px 0 8px;}
header h1{font-size:1.6rem;color:var(--accent);letter-spacing:.06em;}
header p{color:var(--muted);font-size:.85rem;margin-top:4px;}
.card{
  background:var(--card);border:1px solid var(--border);
  border-radius:var(--radius);padding:20px;
}
.card-title{
  font-size:.72rem;text-transform:uppercase;
  letter-spacing:.1em;color:var(--muted);margin-bottom:16px;
}
.status-row{display:flex;align-items:center;gap:10px;margin-bottom:10px;font-size:.95rem;}
.dot{width:10px;height:10px;border-radius:50%;background:var(--muted);flex-shrink:0;}
.dot.on{background:var(--green);box-shadow:0 0 8px var(--green);}
.dot.warn{background:var(--accent);box-shadow:0 0 8px var(--accent);}
.toggle-row{display:flex;align-items:center;justify-content:space-between;margin-bottom:14px;gap:12px;}
.toggle-label{font-size:.95rem;}
.toggle-sub{font-size:.76rem;color:var(--muted);margin-top:2px;}
.switch{position:relative;width:52px;height:28px;flex-shrink:0;}
.switch input{opacity:0;width:0;height:0;}
.slider{position:absolute;inset:0;background:var(--border);border-radius:28px;cursor:pointer;transition:background .3s;}
.slider::before{content:'';position:absolute;width:20px;height:20px;left:4px;top:4px;background:white;border-radius:50%;transition:transform .3s;}
input:checked+.slider{background:var(--green);}
input:checked+.slider::before{transform:translateX(24px);}
.mode-row{display:flex;gap:10px;margin-bottom:8px;}
.mode-btn{
  flex:1;padding:14px;border:2px solid var(--border);border-radius:10px;
  background:transparent;color:var(--muted);
  font-size:.95rem;font-weight:700;cursor:pointer;
  transition:all .2s;letter-spacing:.05em;
}
.mode-btn.active{border-color:var(--accent);color:var(--accent);background:rgba(245,166,35,.1);}
.override-btn{
  width:100%;padding:16px;border-radius:10px;
  border:2px solid var(--accent2);background:transparent;
  color:var(--accent2);font-size:.95rem;font-weight:700;
  cursor:pointer;transition:all .25s;letter-spacing:.04em;
}
.override-btn.active{background:var(--accent2);color:white;box-shadow:0 0 18px rgba(224,92,92,.4);}
.form-row{display:flex;align-items:center;justify-content:space-between;margin-bottom:14px;gap:12px;}
.form-label{font-size:.88rem;flex:1;}
.form-label small{display:block;color:var(--muted);font-size:.74rem;}
input[type=range]{
  -webkit-appearance:none;flex:1.2;height:6px;
  background:var(--border);border-radius:3px;outline:none;
}
input[type=range]::-webkit-slider-thumb{
  -webkit-appearance:none;width:20px;height:20px;
  border-radius:50%;background:var(--accent);cursor:pointer;
}
input[type=number],input[type=time],select{
  background:var(--bg);border:1px solid var(--border);
  border-radius:8px;color:var(--text);
  padding:8px 10px;font-size:.88rem;width:90px;text-align:center;
}
input[type=time]{width:112px;}
select{width:auto;padding:8px 6px;}
.color-row{display:flex;align-items:center;justify-content:space-between;margin-bottom:14px;}
input[type=color]{width:52px;height:40px;border:none;border-radius:8px;cursor:pointer;padding:2px;background:var(--bg);}
.save-btn{
  width:100%;padding:14px;border-radius:10px;border:none;
  background:var(--accent);color:#111;
  font-size:1rem;font-weight:700;cursor:pointer;
  transition:opacity .2s;letter-spacing:.04em;margin-top:4px;
}
.save-btn:hover{opacity:.88;}
.danger-btn{
  width:100%;padding:12px;border-radius:10px;
  border:1px solid var(--accent2);background:transparent;
  color:var(--accent2);font-size:.88rem;font-weight:600;
  cursor:pointer;transition:all .2s;margin-top:10px;
}
.danger-btn:hover{background:rgba(224,92,92,.1);}
hr{border:none;border-top:1px solid var(--border);margin:14px 0;}
.hint{font-size:.76rem;color:var(--muted);margin-top:6px;line-height:1.5;}
.toast{
  position:fixed;bottom:24px;left:50%;transform:translateX(-50%);
  background:var(--green);color:white;
  padding:10px 28px;border-radius:24px;font-size:.9rem;
  opacity:0;transition:opacity .3s;pointer-events:none;z-index:99;white-space:nowrap;
}
.toast.show{opacity:1;}
.modal-bg{
  display:none;position:fixed;inset:0;
  background:rgba(0,0,0,.7);z-index:50;
  align-items:center;justify-content:center;
}
.modal-bg.open{display:flex;}
.modal{
  background:var(--card);border:1px solid var(--border);
  border-radius:var(--radius);padding:28px;
  max-width:340px;width:90%;text-align:center;
}
.modal h2{font-size:1.1rem;margin-bottom:10px;}
.modal p{font-size:.85rem;color:var(--muted);margin-bottom:20px;line-height:1.6;}
.modal-btns{display:flex;gap:10px;justify-content:center;}
.modal-btns button{padding:10px 22px;border-radius:8px;font-size:.9rem;cursor:pointer;border:none;font-weight:600;}
.btn-cancel{background:var(--border);color:var(--text);}
.btn-confirm{background:var(--accent2);color:white;}
footer{
  text-align:center;padding:24px 0 12px;
  font-size:.78rem;color:var(--muted);
  grid-column:1/-1;
}
footer a{color:var(--accent);text-decoration:none;}
footer a:hover{text-decoration:underline;}
</style>
</head>
<body>

<header class="full-width">
  <h1>💡 LumiSetup</h1>
  <p id="clockDisplay">--:--</p>
</header>

<div class="page-grid">

  <div class="card">
    <div class="card-title">État du système</div>
    <div class="status-row"><div class="dot" id="dotLeds"></div><span id="statusLeds">LEDs éteintes</span></div>
    <div class="status-row"><div class="dot" id="dotPir"></div><span id="statusPir">Aucun mouvement</span></div>
    <div class="status-row"><div class="dot" id="dotRange"></div><span id="statusRange">Hors plage horaire</span></div>
    <div class="status-row"><div class="dot" id="dotOverride"></div><span id="statusOverride">Override inactif</span></div>
  </div>

  <div class="card">
    <div class="card-title">Contrôles</div>
    <div class="toggle-row">
      <div>
        <div class="toggle-label">Système actif</div>
        <div class="toggle-sub">Désactiver éteint tout</div>
      </div>
      <label class="switch">
        <input type="checkbox" id="toggleSystem" onchange="postToggle('/toggle/system')">
        <span class="slider"></span>
      </label>
    </div>
    <hr>
    <div class="card-title" style="margin-top:4px">Mode</div>
    <div class="mode-row">
      <button class="mode-btn" id="btnAuto" onclick="setMode(false)">AUTO</button>
      <button class="mode-btn" id="btnOn"   onclick="setMode(true)">ON</button>
    </div>
    <p class="hint">AUTO : PIR actif dans la plage horaire &nbsp;|&nbsp; ON : toujours allumé</p>
    <hr>
    <div class="card-title" style="margin-top:4px">Override</div>
    <button class="override-btn" id="btnOverride" onclick="postToggle('/toggle/override')">
      🔆 Maintenir allumé
    </button>
    <p class="hint">Force l'allumage hors plage ou hors durée. Appuyez à nouveau pour revenir au comportement normal.</p>
  </div>

  <div class="card">
    <div class="card-title">LEDs</div>
    <div class="color-row">
      <span class="form-label">Couleur</span>
      <input type="color" id="colorPicker" value="#ffc864">
    </div>
    <div class="form-row">
      <span class="form-label">Intensité<small id="lblBrightness">80%</small></span>
      <input type="range" id="brightness" min="0" max="100" value="80"
             oninput="document.getElementById('lblBrightness').textContent=this.value+'%'">
    </div>
    <div class="form-row">
      <span class="form-label">Nombre de LEDs</span>
      <input type="number" id="numLeds" min="1" max="300" value="30">
    </div>
    <div class="toggle-row">
      <div>
        <div class="toggle-label">Effet fondu</div>
        <div class="toggle-sub">Fade-in / fade-out</div>
      </div>
      <label class="switch">
        <input type="checkbox" id="fadeEffect" checked>
        <span class="slider"></span>
      </label>
    </div>
  </div>

  <div class="card">
    <div class="card-title">Détection & Plage horaire</div>
    <div class="form-row">
      <span class="form-label">Durée allumage<small>secondes après détection</small></span>
      <input type="number" id="duration" min="5" max="3600" value="120">
    </div>
    <div class="form-row">
      <span class="form-label">Anti-rebond PIR<small>délai min entre détections (s)</small></span>
      <input type="number" id="pirDebounce" min="1" max="60" value="3">
    </div>
    <hr>
    <div class="form-row">
      <span class="form-label">Début plage horaire</span>
      <input type="time" id="startTime" value="18:00">
    </div>
    <div class="form-row">
      <span class="form-label">Fin plage horaire</span>
      <input type="time" id="endTime" value="23:00">
    </div>
    <div class="form-row">
      <span class="form-label">Fuseau horaire</span>
      <select id="tzOffset">
        <option value="-12">UTC-12</option><option value="-11">UTC-11</option>
        <option value="-10">UTC-10</option><option value="-9">UTC-9</option>
        <option value="-8">UTC-8</option><option value="-7">UTC-7</option>
        <option value="-6">UTC-6</option><option value="-5">UTC-5</option>
        <option value="-4">UTC-4</option><option value="-3">UTC-3</option>
        <option value="-2">UTC-2</option><option value="-1">UTC-1</option>
        <option value="0">UTC+0</option>
        <option value="1" selected>UTC+1</option>
        <option value="2">UTC+2</option><option value="3">UTC+3</option>
        <option value="4">UTC+4</option><option value="5">UTC+5</option>
        <option value="6">UTC+6</option><option value="7">UTC+7</option>
        <option value="8">UTC+8</option><option value="9">UTC+9</option>
        <option value="10">UTC+10</option><option value="11">UTC+11</option>
        <option value="12">UTC+12</option>
      </select>
    </div>
  </div>

  <div class="card full-width">
    <button class="save-btn" onclick="saveConfig()">💾 Enregistrer la configuration</button>
    <button class="danger-btn" onclick="openWifiModal()">📶 Réinitialiser la connexion WiFi</button>
  </div>

  <footer class="full-width">
    Conçu par <strong>Egalistel</strong> &nbsp;·&nbsp;
    <a href="https://egamaker.be" target="_blank">egamaker.be</a>
  </footer>

</div>

<div class="modal-bg" id="wifiModal">
  <div class="modal">
    <h2>Réinitialiser le WiFi ?</h2>
    <p>Les identifiants WiFi seront effacés.<br>
       L'ESP redémarrera et ouvrira le point d'accès <strong>LumiSetup</strong>.<br>
       Connectez-vous dessus pour reconfigurer.</p>
    <div class="modal-btns">
      <button class="btn-cancel" onclick="closeWifiModal()">Annuler</button>
      <button class="btn-confirm" onclick="confirmWifiReset()">Confirmer</button>
    </div>
  </div>
</div>

<div class="toast" id="toast"></div>

<script>
function hexToRgb(h){return{r:parseInt(h.slice(1,3),16),g:parseInt(h.slice(3,5),16),b:parseInt(h.slice(5,7),16)};}
function rgbToHex(r,g,b){return'#'+[r,g,b].map(v=>v.toString(16).padStart(2,'0')).join('');}
function showToast(msg){const t=document.getElementById('toast');t.textContent=msg;t.classList.add('show');setTimeout(()=>t.classList.remove('show'),2800);}

async function postToggle(url){
  try{await fetch(url,{method:'POST'});await refreshStatus();}
  catch(e){console.error(e);}
}

async function setMode(on){
  const isOn=document.getElementById('btnOn').classList.contains('active');
  if(isOn===on)return;
  await postToggle('/toggle/mode');
}

async function refreshStatus(){
  try{
    const d=await(await fetch('/status')).json();
    document.getElementById('clockDisplay').textContent=d.time||'--:--';
    document.getElementById('toggleSystem').checked=d.systemActive;
    document.getElementById('btnAuto').classList.toggle('active',!d.modeOn);
    document.getElementById('btnOn').classList.toggle('active',d.modeOn);
    const ov=document.getElementById('btnOverride');
    ov.classList.toggle('active',d.overrideOn);
    ov.textContent=d.overrideOn?'🔆 Override actif — appuyer pour désactiver':'🔆 Maintenir allumé';
    sd('dotLeds',   d.ledsOn?'on':'off');   document.getElementById('statusLeds').textContent   =d.ledsOn?'LEDs allumées':'LEDs éteintes';
    sd('dotPir',    d.pirState?'warn':'off');document.getElementById('statusPir').textContent    =d.pirState?'Mouvement détecté !':'Aucun mouvement';
    sd('dotRange',  d.inRange?'on':'off');  document.getElementById('statusRange').textContent  =d.inRange?'Dans la plage horaire':'Hors plage horaire';
    sd('dotOverride',d.overrideOn?'warn':'off');document.getElementById('statusOverride').textContent=d.overrideOn?'Override actif':'Override inactif';
  }catch(e){console.error(e);}
}
function sd(id,cls){document.getElementById(id).className='dot '+cls;}

async function loadConfig(){
  try{
    const d=await(await fetch('/config')).json();
    document.getElementById('colorPicker').value  =rgbToHex(d.red||255,d.green||200,d.blue||100);
    document.getElementById('brightness').value   =d.brightness??80;
    document.getElementById('lblBrightness').textContent=(d.brightness??80)+'%';
    document.getElementById('numLeds').value      =d.numLeds??30;
    document.getElementById('duration').value     =d.duration??120;
    document.getElementById('pirDebounce').value  =d.pirDebounce??3;
    document.getElementById('fadeEffect').checked =d.fadeEffect!==false;
    const sh=String(d.startHour??18).padStart(2,'0'),sm=String(d.startMin??0).padStart(2,'0');
    const eh=String(d.endHour??23).padStart(2,'0'),em=String(d.endMin??0).padStart(2,'0');
    document.getElementById('startTime').value=sh+':'+sm;
    document.getElementById('endTime').value  =eh+':'+em;
    document.getElementById('tzOffset').value =d.tzOffset??1;
  }catch(e){console.error(e);}
}

async function saveConfig(){
  const rgb=hexToRgb(document.getElementById('colorPicker').value);
  const [sh,sm]=document.getElementById('startTime').value.split(':').map(Number);
  const [eh,em]=document.getElementById('endTime').value.split(':').map(Number);
  const body=new URLSearchParams({
    red:rgb.r,green:rgb.g,blue:rgb.b,
    brightness:document.getElementById('brightness').value,
    numLeds:document.getElementById('numLeds').value,
    duration:document.getElementById('duration').value,
    pirDebounce:document.getElementById('pirDebounce').value,
    fadeEffect:document.getElementById('fadeEffect').checked?'1':'0',
    startHour:sh,startMin:sm,endHour:eh,endMin:em,
    tzOffset:document.getElementById('tzOffset').value,
  });
  try{await fetch('/config',{method:'POST',body});showToast('✓ Configuration enregistrée');}
  catch(e){alert('Erreur sauvegarde');}
}

function openWifiModal(){document.getElementById('wifiModal').classList.add('open');}
function closeWifiModal(){document.getElementById('wifiModal').classList.remove('open');}
async function confirmWifiReset(){
  closeWifiModal();
  try{await fetch('/wifi/reset',{method:'POST'});showToast('📶 Redémarrage...');}
  catch(e){}
}

loadConfig();
refreshStatus();
setInterval(refreshStatus,3000);
</script>
</body>
</html>
)rawhtml";

// ─────────────────────────────────────────────
//  PROTOTYPES
// ─────────────────────────────────────────────
void loadConfig();
void saveConfig();
void loadWifi(String &ssid, String &pass);
void saveWifi(const String &ssid, const String &pass);
void startPortalMode();
void startNormalMode();
void applyLeds(bool on, bool smooth = true);
bool isInTimeRange();
void handlePir();
void setupPortalRoutes();
void setupAppRoutes();
String buildStatusJson();
String buildConfigJson();

// ─────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n[BOOT] LumiSetup démarrage...");

  if (!LittleFS.begin()) {
    Serial.println("[FS] Formatage...");
    LittleFS.format();
    LittleFS.begin();
  }
  loadConfig();

  // LEDs
  FastLED.addLeds<WS2812B, LED_DATA_PIN, GRB>(leds, MAX_LEDS);
  FastLED.clear();
  FastLED.show();

  // PIR
  pinMode(PIR_PIN, INPUT);

  // Petit délai pour laisser le filesystem se stabiliser après un reset
  delay(200);

  // Lecture credentials WiFi sauvegardés
  String ssid, pass;
  loadWifi(ssid, pass);

  if (ssid.length() > 0) {
    Serial.println("[WiFi] Tentative connexion à : " + ssid);
    WiFi.hostname(HOSTNAME);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 20000) {  // 20s timeout
      delay(500);
      Serial.print(".");
    }
    Serial.println();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WiFi] Connecté — IP : " + WiFi.localIP().toString());
    startNormalMode();
  } else {
    Serial.println("[WiFi] Échec — ouverture du portail " AP_NAME);
    startPortalMode();
  }
}

// ─────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────
void loop() {
  if (portalMode) {
    dnsServer.processNextRequest();
    delay(10);
    return;
  }

  // NTP toutes les 60s
  static unsigned long lastNtp = 0;
  if (millis() - lastNtp > 60000) {
    timeClient.update();
    lastNtp = millis();
  }

  // mDNS
  MDNS.update();

  if (!cfg.systemActive) {
    if (ledsOn) applyLeds(false, false);
    delay(200);
    return;
  }

  if (cfg.modeOn) {
    if (!ledsOn) applyLeds(true);
    delay(100);
    return;
  }

  if (overrideOn) {
    if (!ledsOn) applyLeds(true);
    delay(100);
    return;
  }

  handlePir();

  if (ledsOn && lightOffTime > 0 && millis() >= lightOffTime) {
    applyLeds(false);
    lightOffTime = 0;
    Serial.println("[LED] Extinction automatique");
  }

  delay(50);
}

// ─────────────────────────────────────────────
//  MODES DÉMARRAGE
// ─────────────────────────────────────────────
void startPortalMode() {
  portalMode = true;
  WiFi.mode(WIFI_AP);
  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_NAME, AP_PASS);

  dnsServer.start(53, "*", apIP);   // DNS catch-all → portail captif
  setupPortalRoutes();
  server.begin();
  Serial.println("[Portal] Actif sur http://" AP_IP_STR);
}

void startNormalMode() {
  portalMode = false;
  timeClient.begin();
  timeClient.setTimeOffset(cfg.tzOffset * 3600);
  timeClient.update();
  Serial.println("[NTP] " + timeClient.getFormattedTime());

  // mDNS → http://lumisetup.local
  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("[mDNS] Accessible sur http://" HOSTNAME ".local");
  }

  setupAppRoutes();
  server.begin();
  Serial.println("[HTTP] Serveur démarré — IP : " + WiFi.localIP().toString());
}

// ─────────────────────────────────────────────
//  PIR
// ─────────────────────────────────────────────
void handlePir() {
  bool motion = (digitalRead(PIR_PIN) == HIGH);
  unsigned long now = millis();

  if (motion && !pirState) {
    if (now - lastPirTime > (unsigned long)(cfg.pirDebounce * 1000UL)) {
      pirState    = true;
      lastPirTime = now;
      Serial.println("[PIR] Mouvement !");
      if (isInTimeRange()) {
        applyLeds(true);
        lightOffTime = millis() + (unsigned long)(cfg.duration * 1000UL);
      }
    }
  } else if (!motion && pirState) {
    pirState = false;
  }
}

// ─────────────────────────────────────────────
//  PLAGE HORAIRE
// ─────────────────────────────────────────────
bool isInTimeRange() {
  int now   = timeClient.getHours() * 60 + timeClient.getMinutes();
  int start = cfg.startHour * 60 + cfg.startMin;
  int end   = cfg.endHour   * 60 + cfg.endMin;
  if (start <= end) return (now >= start && now < end);
  return (now >= start || now < end);
}

// ─────────────────────────────────────────────
//  LEDS
// ─────────────────────────────────────────────
void applyLeds(bool on, bool smooth) {
  ledsOn = on;
  uint8_t target = map(cfg.brightness, 0, 100, 0, 255);

  if (on) {
    for (int i = 0; i < cfg.numLeds && i < MAX_LEDS; i++)
      leds[i] = CRGB(cfg.red, cfg.green, cfg.blue);
    if (smooth && cfg.fadeEffect) {
      for (int b = 0; b <= target; b += 4) { FastLED.setBrightness(b); FastLED.show(); delay(12); }
    }
    FastLED.setBrightness(target);
    FastLED.show();
  } else {
    if (smooth && cfg.fadeEffect) {
      for (int b = target; b >= 0; b -= 4) { FastLED.setBrightness(b); FastLED.show(); delay(12); }
    }
    FastLED.clear();
    FastLED.setBrightness(0);
    FastLED.show();
  }
}

// ─────────────────────────────────────────────
//  CONFIG — Chargement / Sauvegarde
// ─────────────────────────────────────────────
void loadConfig() {
  if (!LittleFS.exists(CONFIG_FILE)) { saveConfig(); return; }
  File f = LittleFS.open(CONFIG_FILE, "r");
  if (!f) return;
  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, f) == DeserializationError::Ok) {
    cfg.numLeds      = doc["numLeds"]      | cfg.numLeds;
    cfg.red          = doc["red"]          | cfg.red;
    cfg.green        = doc["green"]        | cfg.green;
    cfg.blue         = doc["blue"]         | cfg.blue;
    cfg.brightness   = doc["brightness"]   | cfg.brightness;
    cfg.duration     = doc["duration"]     | cfg.duration;
    cfg.fadeEffect   = doc["fadeEffect"]   | cfg.fadeEffect;
    cfg.startHour    = doc["startHour"]    | cfg.startHour;
    cfg.startMin     = doc["startMin"]     | cfg.startMin;
    cfg.endHour      = doc["endHour"]      | cfg.endHour;
    cfg.endMin       = doc["endMin"]       | cfg.endMin;
    cfg.tzOffset     = doc["tzOffset"]     | cfg.tzOffset;
    cfg.systemActive = doc["systemActive"] | cfg.systemActive;
    cfg.modeOn       = doc["modeOn"]       | cfg.modeOn;
    cfg.pirDebounce  = doc["pirDebounce"]  | cfg.pirDebounce;
  }
  f.close();
  Serial.println("[Config] Chargée");
}

void saveConfig() {
  File f = LittleFS.open(CONFIG_FILE, "w");
  if (!f) return;
  StaticJsonDocument<512> doc;
  doc["numLeds"]      = cfg.numLeds;
  doc["red"]          = cfg.red;
  doc["green"]        = cfg.green;
  doc["blue"]         = cfg.blue;
  doc["brightness"]   = cfg.brightness;
  doc["duration"]     = cfg.duration;
  doc["fadeEffect"]   = cfg.fadeEffect;
  doc["startHour"]    = cfg.startHour;
  doc["startMin"]     = cfg.startMin;
  doc["endHour"]      = cfg.endHour;
  doc["endMin"]       = cfg.endMin;
  doc["tzOffset"]     = cfg.tzOffset;
  doc["systemActive"] = cfg.systemActive;
  doc["modeOn"]       = cfg.modeOn;
  doc["pirDebounce"]  = cfg.pirDebounce;
  serializeJson(doc, f);
  f.close();
}

void loadWifi(String &ssid, String &pass) {
  ssid = ""; pass = "";
  if (!LittleFS.exists(WIFI_FILE)) {
    Serial.println("[WiFi] Pas de fichier wifi.json");
    return;
  }
  File f = LittleFS.open(WIFI_FILE, "r");
  if (!f) {
    Serial.println("[WiFi] Impossible d'ouvrir wifi.json");
    return;
  }
  StaticJsonDocument<128> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    Serial.println("[WiFi] Erreur lecture wifi.json : " + String(err.c_str()));
    return;
  }
  ssid = doc["ssid"] | "";
  pass = doc["pass"] | "";
  Serial.println("[WiFi] Credentials lus — SSID : " + ssid);
}

void saveWifi(const String &ssid, const String &pass) {
  File f = LittleFS.open(WIFI_FILE, "w");
  if (!f) return;
  StaticJsonDocument<128> doc;
  doc["ssid"] = ssid;
  doc["pass"] = pass;
  serializeJson(doc, f);
  f.flush();  // force l'écriture physique avant fermeture
  f.close();
  Serial.println("[WiFi] Credentials sauvegardés : " + ssid);
}

// ─────────────────────────────────────────────
//  JSON
// ─────────────────────────────────────────────
String buildStatusJson() {
  StaticJsonDocument<256> doc;
  doc["systemActive"] = cfg.systemActive;
  doc["modeOn"]       = cfg.modeOn;
  doc["overrideOn"]   = overrideOn;
  doc["ledsOn"]       = ledsOn;
  doc["pirState"]     = pirState;
  doc["inRange"]      = isInTimeRange();
  doc["time"]         = timeClient.getFormattedTime().substring(0, 5);
  String out; serializeJson(doc, out); return out;
}

String buildConfigJson() {
  StaticJsonDocument<512> doc;
  doc["numLeds"]     = cfg.numLeds;
  doc["red"]         = cfg.red;
  doc["green"]       = cfg.green;
  doc["blue"]        = cfg.blue;
  doc["brightness"]  = cfg.brightness;
  doc["duration"]    = cfg.duration;
  doc["fadeEffect"]  = cfg.fadeEffect;
  doc["startHour"]   = cfg.startHour;
  doc["startMin"]    = cfg.startMin;
  doc["endHour"]     = cfg.endHour;
  doc["endMin"]      = cfg.endMin;
  doc["tzOffset"]    = cfg.tzOffset;
  doc["pirDebounce"] = cfg.pirDebounce;
  String out; serializeJson(doc, out); return out;
}

// ─────────────────────────────────────────────
//  ROUTES — PORTAIL WIFI
// ─────────────────────────────────────────────
void setupPortalRoutes() {

  // Routes de détection portail captif — Android, iOS, Windows
  // Ces URLs sont sondées automatiquement par les OS pour détecter un portail
  auto portalRedirect = [](AsyncWebServerRequest* req) {
    req->send_P(200, "text/html", PORTAL_HTML);
  };

  server.on("/",                          HTTP_GET, portalRedirect);
  server.on("/generate_204",              HTTP_GET, portalRedirect); // Android
  server.on("/gen_204",                   HTTP_GET, portalRedirect); // Android ancien
  server.on("/hotspot-detect.html",       HTTP_GET, portalRedirect); // iOS / macOS
  server.on("/library/test/success.html", HTTP_GET, portalRedirect); // iOS
  server.on("/ncsi.txt",                  HTTP_GET, portalRedirect); // Windows
  server.on("/connecttest.txt",           HTTP_GET, portalRedirect); // Windows 10+
  server.on("/redirect",                  HTTP_GET, portalRedirect); // Windows

  // Catch-all → portail
  server.onNotFound([](AsyncWebServerRequest* req) {
    AsyncWebServerResponse* res = req->beginResponse_P(200, "text/html", PORTAL_HTML);
    res->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    res->addHeader("Pragma", "no-cache");
    res->addHeader("Expires", "-1");
    req->send(res);
  });

  // Sauvegarde credentials + redémarrage
  server.on("/wifi/save", HTTP_POST, [](AsyncWebServerRequest* req) {
    String ssid = "", pass = "";
    if (req->hasParam("ssid", true)) ssid = req->getParam("ssid", true)->value();
    if (req->hasParam("pass", true)) pass = req->getParam("pass", true)->value();
    saveWifi(ssid, pass);
    req->send(200, "text/plain", "OK");
    delay(1000);
    ESP.restart();
  });
}

// ─────────────────────────────────────────────
//  ROUTES — APPLICATION
// ─────────────────────────────────────────────
void setupAppRoutes() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send_P(200, "text/html", INDEX_HTML);
  });

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "application/json", buildStatusJson());
  });

  server.on("/config", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "application/json", buildConfigJson());
  });

  server.on("/config", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (req->hasParam("numLeds",     true)) cfg.numLeds     = req->getParam("numLeds",     true)->value().toInt();
    if (req->hasParam("red",         true)) cfg.red         = req->getParam("red",         true)->value().toInt();
    if (req->hasParam("green",       true)) cfg.green       = req->getParam("green",       true)->value().toInt();
    if (req->hasParam("blue",        true)) cfg.blue        = req->getParam("blue",        true)->value().toInt();
    if (req->hasParam("brightness",  true)) cfg.brightness  = req->getParam("brightness",  true)->value().toInt();
    if (req->hasParam("duration",    true)) cfg.duration    = req->getParam("duration",    true)->value().toInt();
    if (req->hasParam("fadeEffect",  true)) cfg.fadeEffect  = req->getParam("fadeEffect",  true)->value() == "1";
    if (req->hasParam("startHour",   true)) cfg.startHour   = req->getParam("startHour",   true)->value().toInt();
    if (req->hasParam("startMin",    true)) cfg.startMin    = req->getParam("startMin",    true)->value().toInt();
    if (req->hasParam("endHour",     true)) cfg.endHour     = req->getParam("endHour",     true)->value().toInt();
    if (req->hasParam("endMin",      true)) cfg.endMin      = req->getParam("endMin",      true)->value().toInt();
    if (req->hasParam("tzOffset",    true)) cfg.tzOffset    = req->getParam("tzOffset",    true)->value().toInt();
    if (req->hasParam("pirDebounce", true)) cfg.pirDebounce = req->getParam("pirDebounce", true)->value().toInt();
    saveConfig();
    timeClient.setTimeOffset(cfg.tzOffset * 3600);
    req->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/toggle/system", HTTP_POST, [](AsyncWebServerRequest* req) {
    cfg.systemActive = !cfg.systemActive;
    if (!cfg.systemActive) { overrideOn = false; applyLeds(false, false); }
    saveConfig();
    req->send(200, "application/json", buildStatusJson());
  });

  server.on("/toggle/mode", HTTP_POST, [](AsyncWebServerRequest* req) {
    cfg.modeOn = !cfg.modeOn;
    overrideOn = false;
    applyLeds(cfg.modeOn);
    saveConfig();
    req->send(200, "application/json", buildStatusJson());
  });

  server.on("/toggle/override", HTTP_POST, [](AsyncWebServerRequest* req) {
    overrideOn = !overrideOn;
    applyLeds(overrideOn);
    if (!overrideOn) lightOffTime = 0;
    req->send(200, "application/json", buildStatusJson());
  });

  server.on("/wifi/reset", HTTP_POST, [](AsyncWebServerRequest* req) {
    req->send(200, "application/json", "{\"ok\":true}");
    delay(500);
    LittleFS.remove(WIFI_FILE);
    ESP.restart();
  });

  server.onNotFound([](AsyncWebServerRequest* req) {
    req->send(404, "text/plain", "Non trouvé");
  });
}
