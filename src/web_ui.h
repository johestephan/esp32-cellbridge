#pragma once
#include <pgmspace.h>

static const char LOGIN_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Login · ESP32 CellBridge</title>
<style>
:root{--bg:#0b101b;--card:#131b2e;--border:#263252;--text:#e6ecf5;--muted:#8a99b8;--accent:#3b82f6;--danger:#ef4444;}
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--text);min-height:100vh;display:flex;align-items:center;justify-content:center;padding:16px}
.card{width:100%;max-width:380px;background:var(--card);border:1px solid var(--border);border-radius:14px;padding:24px;box-shadow:0 16px 40px rgba(0,0,0,.45)}
h1{font-size:1.25rem;margin-bottom:4px}
.sub{font-size:.85rem;color:var(--muted);margin-bottom:18px}
label{display:block;font-size:.8rem;color:var(--muted);margin:10px 0 5px}
input{width:100%;background:var(--bg);border:1px solid var(--border);color:var(--text);border-radius:8px;padding:10px 12px;font-size:.95rem}
input:focus{outline:none;border-color:var(--accent)}
button{width:100%;margin-top:16px;background:var(--accent);color:#fff;border:none;border-radius:8px;padding:10px;font-weight:600;font-size:.95rem;cursor:pointer}
button:hover{filter:brightness(1.1)}
.err{color:var(--danger);font-size:.85rem;margin-top:10px;min-height:1.2em}
</style>
</head>
<body>
<form class="card" onsubmit="doLogin(event)">
  <h1>ESP32 CellBridge</h1>
  <div class="sub">Sign in to manage SMS, Calls &amp; 4G Proxy</div>
  <label>Username</label>
  <input id="u" value="admin" autocomplete="username" required>
  <label>Password</label>
  <input id="p" type="password" autocomplete="current-password" required autofocus>
  <button type="submit">Sign In</button>
  <div id="err" class="err"></div>
</form>
<script>
async function doLogin(e){
  e.preventDefault();
  const err=document.getElementById('err'); err.textContent='';
  try{
    const r=await fetch('/api/auth/login',{
      method:'POST',headers:{'Content-Type':'application/json'},
      body:JSON.stringify({username:document.getElementById('u').value.trim(),password:document.getElementById('p').value})
    });
    const d=await r.json();
    if(!r.ok){err.textContent=d.error||'Login failed';return;}
    location.href='/';
  }catch{err.textContent='Connection error';}
}
</script>
</body>
</html>
)rawliteral";

static const char WEB_UI_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 CellBridge</title>
<style>
:root{
  --bg:#0b101b;--card:#131b2e;--card2:#19233c;--border:#263252;
  --text:#e6ecf5;--muted:#8a99b8;--accent:#3b82f6;--accent2:#10b981;
  --danger:#ef4444;--warn:#f59e0b;--radius:12px;
}
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--text);line-height:1.45;padding:14px}
.wrap{max-width:1040px;margin:0 auto}
header{display:flex;flex-wrap:wrap;gap:12px;align-items:center;justify-content:space-between;background:var(--card);border:1px solid var(--border);border-radius:var(--radius);padding:14px 18px;margin-bottom:14px}
.brand{display:flex;align-items:center;gap:10px}
.dot{width:11px;height:11px;border-radius:50%;background:var(--danger);box-shadow:0 0 10px currentColor}
.dot.ok{background:var(--accent2);color:var(--accent2)}
h1{font-size:1.15rem;font-weight:700}
.sub{font-size:.8rem;color:var(--muted)}
.pills{display:flex;flex-wrap:wrap;gap:8px;align-items:center}
.pill{background:var(--card2);border:1px solid var(--border);border-radius:999px;padding:4px 11px;font-size:.8rem;color:var(--text)}
.pill b{color:var(--accent2)}
nav{display:flex;flex-wrap:wrap;gap:6px;margin-bottom:14px}
nav button{background:var(--card);border:1px solid var(--border);color:var(--muted);padding:9px 15px;border-radius:10px;cursor:pointer;font-weight:600;font-size:.9rem;transition:.15s}
nav button.active,nav button:hover{background:var(--accent);border-color:var(--accent);color:#fff}
.badge{background:var(--danger);color:#fff;border-radius:999px;padding:1px 7px;font-size:.75rem;margin-left:5px}
.tab{display:none}.tab.active{display:block}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:14px}
.card{background:var(--card);border:1px solid var(--border);border-radius:var(--radius);padding:16px;margin-bottom:14px}
.card h2{font-size:1rem;margin-bottom:12px;color:#fff;display:flex;justify-content:space-between;align-items:center}
label{display:block;font-size:.8rem;color:var(--muted);margin:8px 0 4px}
input,textarea,select{width:100%;background:var(--bg);border:1px solid var(--border);color:var(--text);border-radius:8px;padding:9px 11px;font-size:.9rem;font-family:inherit}
input:focus,textarea:focus{outline:none;border-color:var(--accent)}
.row{display:flex;gap:10px;align-items:center}.row>*{flex:1}
.btn{display:inline-flex;align-items:center;justify-content:center;gap:6px;background:var(--accent);color:#fff;border:none;border-radius:8px;padding:9px 15px;font-weight:600;font-size:.88rem;cursor:pointer;margin-top:10px}
.btn:hover{filter:brightness(1.1)}
.btn.ok{background:var(--accent2)}.btn.danger{background:var(--danger)}.btn.sec{background:var(--card2);border:1px solid var(--border)}
.btn.sm{padding:5px 10px;font-size:.78rem;margin-top:0}
.msg{background:var(--card2);border:1px solid var(--border);border-radius:10px;padding:11px 13px;margin-bottom:9px}
.msg.unread{border-left:4px solid var(--accent)}
.msg-head{display:flex;justify-content:space-between;gap:8px;font-size:.8rem;color:var(--muted);margin-bottom:5px}
.msg-peer{font-weight:700;color:#fff}
.msg-body{white-space:pre-wrap;word-break:break-word;font-size:.92rem}
.tag{display:inline-block;padding:1px 7px;border-radius:6px;font-size:.72rem;font-weight:700;text-transform:uppercase}
.tag.in{background:#1e3a8a;color:#93c5fd}.tag.out{background:#064e3b;color:#6ee7b7}.tag.fail{background:#7f1d1d;color:#fca5a5}
pre{background:var(--bg);border:1px solid var(--border);border-radius:8px;padding:10px;overflow-x:auto;font-size:.82rem;color:#a5f3fc;max-height:280px}
.call-banner{background:#451a03;border:1px solid var(--warn);border-radius:10px;padding:12px 16px;margin-bottom:14px;display:none;align-items:center;justify-content:space-between;gap:10px}
.call-banner.show{display:flex}
.toast{position:fixed;bottom:18px;right:18px;background:var(--card2);border:1px solid var(--accent);color:#fff;padding:10px 16px;border-radius:10px;display:none;z-index:99;box-shadow:0 8px 24px rgba(0,0,0,.5)}
.chk{display:flex;align-items:center;gap:8px;margin-top:8px;font-size:.88rem;color:var(--text)}
.chk input{width:auto}
.bat-bar{height:8px;background:var(--bg);border:1px solid var(--border);border-radius:999px;overflow:hidden;margin:6px 0 8px}
.bat-fill{height:100%;background:var(--accent2);transition:width .3s}
</style>
</head>
<body>
<div class="wrap">
  <header>
    <div class="brand">
      <div id="mDot" class="dot"></div>
      <div>
        <h1>ESP32 CellBridge</h1>
        <div class="sub" id="hdrSub">Connecting...</div>
      </div>
    </div>
    <div class="pills">
      <div class="pill">Signal: <b id="pSig">--</b></div>
      <div class="pill">Net: <b id="pNet">--</b></div>
      <div class="pill">Credit: <b id="pBal">--</b></div>
      <div class="pill">Bat: <b id="pBat">--</b></div>
      <div class="pill">LTE IP: <b id="pLteIp">--</b></div>
      <button class="btn sec sm" onclick="logout()">Logout</button>
    </div>
  </header>

  <div id="callBanner" class="call-banner">
    <div><b>Call:</b> <span id="cbText">--</span></div>
    <div style="display:flex;gap:8px">
      <button class="btn ok sm" onclick="apiPost('/api/call/answer',{})">Answer</button>
      <button class="btn danger sm" onclick="apiPost('/api/call/hangup',{})">Hangup</button>
    </div>
  </div>

  <nav>
    <button class="active" onclick="showTab('sms',this)">SMS <span id="unreadBadge" class="badge" style="display:none">0</span></button>
    <button onclick="showTab('calls',this)">Calls &amp; USSD</button>
    <button onclick="showTab('proxy',this)">4G Proxy</button>
    <button onclick="showTab('at',this)">AT Console</button>
    <button onclick="showTab('settings',this)">Settings &amp; OTA</button>
  </nav>

  <!-- SMS TAB -->
  <div id="tab-sms" class="tab active">
    <div class="grid">
      <div class="card">
        <h2>Send SMS</h2>
        <label>Recipient (E.164 e.g. +31612345678)</label>
        <input id="smsTo" placeholder="+31612345678">
        <label>Message <span id="smsCount" style="float:right">0 chars</span></label>
        <textarea id="smsBody" rows="4" placeholder="Type SMS (supports GSM-7, € & emojis)..." oninput="updateSmsCount()"></textarea>
        <button class="btn" onclick="sendSms()">Send SMS</button>
      </div>
      <div class="card">
        <h2>Gateway, Battery &amp; Credit</h2>
        <div style="display:flex;justify-content:space-between;font-size:.82rem">
          <span>Prepaid Credit: <b id="balLabel">--</b></span>
          <span id="balState" class="sub">--</span>
        </div>
        <div style="display:flex;justify-content:space-between;font-size:.82rem;margin-top:8px">
          <span>Battery: <b id="batLabel">--</b></span>
          <span id="batState" class="sub">--</span>
        </div>
        <div class="bat-bar"><div id="batFill" class="bat-fill" style="width:0%"></div></div>
        <div id="infoBox" class="sub" style="line-height:1.8">Loading...</div>
        <div style="display:flex;gap:8px;flex-wrap:wrap;margin-top:8px">
          <button class="btn sm" onclick="checkBalance()">Check Balance</button>
          <button class="btn sec sm" onclick="apiPost('/api/sms/scan',{}).then(loadAll)">Scan SIM SMS</button>
          <button class="btn sec sm" onclick="apiPost('/api/sms/read',{all:true}).then(loadSms)">Mark All Read</button>
          <button class="btn danger sm" onclick="if(confirm('Clear all stored SMS?')) apiDel('/api/sms').then(loadSms)">Clear History</button>
        </div>
      </div>
    </div>
    <div class="card">
      <h2>Messages
        <select id="smsFilter" style="width:auto;padding:4px 8px;font-size:.8rem" onchange="loadSms()">
          <option value="">All</option>
          <option value="in">Inbox only</option>
          <option value="out">Sent only</option>
        </select>
      </h2>
      <div id="smsList">Loading...</div>
    </div>
  </div>

  <!-- CALLS & USSD TAB -->
  <div id="tab-calls" class="tab">
    <div class="grid">
      <div class="card">
        <h2>Voice Call Control</h2>
        <label>Phone Number</label>
        <input id="callTo" placeholder="+31612345678">
        <label>Auto-Hangup after seconds (0 = manual, e.g. 15 for ring-alert)</label>
        <input id="callTimeout" type="number" value="0" min="0" max="300">
        <div style="display:flex;gap:8px;flex-wrap:wrap">
          <button class="btn ok" onclick="dialCall()">Dial</button>
          <button class="btn" onclick="apiPost('/api/call/answer',{})">Answer</button>
          <button class="btn danger" onclick="apiPost('/api/call/hangup',{})">Hangup</button>
        </div>
        <label style="margin-top:14px">Send DTMF Digits</label>
        <div class="row">
          <input id="dtmfDigits" placeholder="1234#">
          <button class="btn sec" style="margin-top:0;flex:0 0 auto" onclick="sendDtmf()">Send DTMF</button>
        </div>
      </div>
      <div class="card">
        <h2>USSD Code</h2>
        <label>USSD String (e.g. *100# or *101#)</label>
        <input id="ussdCode" placeholder="*101#">
        <button class="btn" onclick="runUssd()">Send USSD</button>
        <label>Response</label>
        <pre id="ussdOut">(waiting)</pre>
      </div>
    </div>
    <div class="card">
      <h2>Call History <button class="btn danger sm" onclick="apiDel('/api/calls').then(loadCalls)">Clear</button></h2>
      <div id="callList">Loading...</div>
    </div>
  </div>

  <!-- PROXY TAB -->
  <div id="tab-proxy" class="tab">
    <div class="card">
      <h2>4G LTE Internet Proxy</h2>
      <p class="sub" style="margin-bottom:10px">Route LAN traffic through your SIM card's 4G LTE connection via HTTP/HTTPS CONNECT or SOCKS5.</p>
      <div id="proxyStats" class="sub" style="line-height:1.8;margin-bottom:10px">Loading...</div>
      <div style="display:flex;gap:8px;flex-wrap:wrap">
        <button class="btn ok" onclick="toggleProxy(true)">Enable Proxy &amp; LTE Data</button>
        <button class="btn danger" onclick="toggleProxy(false)">Disable Proxy &amp; Close LTE</button>
        <button class="btn sec" onclick="testCellularIp()">Test 4G Public IP</button>
      </div>
      <pre id="proxyTestOut" style="margin-top:12px">Usage examples will appear here...</pre>
    </div>
  </div>

  <!-- AT CONSOLE TAB -->
  <div id="tab-at" class="tab">
    <div class="card">
      <h2>AT Command Console</h2>
      <div class="row">
        <input id="atCmd" placeholder="AT+CSQ" onkeydown="if(event.key==='Enter')sendAt()">
        <button class="btn" style="margin-top:0;flex:0 0 auto" onclick="sendAt()">Send</button>
      </div>
      <div style="display:flex;gap:6px;flex-wrap:wrap;margin:10px 0">
        <button class="btn sec sm" onclick="quickAt('ATI')">ATI</button>
        <button class="btn sec sm" onclick="quickAt('AT+CSQ')">AT+CSQ</button>
        <button class="btn sec sm" onclick="quickAt('AT+COPS?')">AT+COPS?</button>
        <button class="btn sec sm" onclick="quickAt('AT+CPSI?')">AT+CPSI?</button>
        <button class="btn sec sm" onclick="quickAt('AT+CPIN?')">AT+CPIN?</button>
        <button class="btn sec sm" onclick="quickAt('AT+CSCA?')">AT+CSCA?</button>
        <button class="btn sec sm" onclick="quickAt('AT+CPMS?')">AT+CPMS?</button>
      </div>
      <pre id="atOut">Ready.</pre>
    </div>
  </div>

  <!-- SETTINGS & OTA TAB -->
  <div id="tab-settings" class="tab">
    <div class="grid">
      <div class="card">
        <h2>Wi-Fi (Primary &amp; Fallback)</h2>
        <label>Primary Wi-Fi SSID</label><input id="c_wifi_ssid">
        <label>Primary Wi-Fi Password</label><input id="c_wifi_pass" type="password">
        <label>Fallback Wi-Fi SSID (used if primary not found)</label><input id="c_wifi_fb_ssid">
        <label>Fallback Wi-Fi Password</label><input id="c_wifi_fb_pass" type="password">
        <label>Hostname (mDNS)</label><input id="c_hostname">
        <label>REST API Key (X-API-Key / Bearer)</label><input id="c_api_key" type="password">
      </div>
      <div class="card">
        <h2>Change Web UI Login</h2>
        <label>Username</label><input id="p_user">
        <label>Current Password</label><input id="p_cur" type="password" autocomplete="current-password">
        <label>New Password (min 4 chars)</label><input id="p_new" type="password" autocomplete="new-password">
        <label>Confirm New Password</label><input id="p_conf" type="password" autocomplete="new-password">
        <button class="btn" onclick="changePassword()">Update Password</button>
      </div>
      <div class="card">
        <h2>Cellular &amp; Odido SIM</h2>
        <label>APN (Odido NL: internet.mobiel)</label><input id="c_apn">
        <label>APN Username</label><input id="c_apn_user">
        <label>APN Password</label><input id="c_apn_pass" type="password">
        <label>SIM PIN (leave blank if unlocked)</label><input id="c_sim_pin" type="password">
        <label class="chk"><input type="checkbox" id="c_auto_del"> Auto-delete SMS from SIM after saving</label>
        <div class="row">
          <div><label>Balance Method</label><select id="c_bal_method"><option value="sms">SMS (Odido: 1144)</option><option value="ussd">USSD (*101#)</option></select></div>
          <div><label>Target / Code</label><input id="c_bal_target" placeholder="1144"></div>
        </div>
        <label>SMS Keyword (for SMS balance check)</label><input id="c_bal_msg" placeholder="SALDO">
        <h2 style="margin-top:16px">4G Proxy Settings</h2>
        <label class="chk"><input type="checkbox" id="c_proxy_en"> Enable HTTP/SOCKS5 Proxy</label>
        <div class="row">
          <div><label>HTTP Port</label><input id="c_http_port" type="number"></div>
          <div><label>SOCKS5 Port</label><input id="c_socks_port" type="number"></div>
        </div>
        <div class="row">
          <div><label>Proxy User</label><input id="c_proxy_user"></div>
          <div><label>Proxy Pass</label><input id="c_proxy_pass" type="password"></div>
        </div>
      </div>
      <div class="card">
        <h2>Webhook (Node-Red / HA)</h2>
        <label class="chk"><input type="checkbox" id="c_wh_en"> Enable Webhook POST on SMS/Call</label>
        <label>Webhook URL</label><input id="c_wh_url" placeholder="http://192.168.1.50:1880/a7670e-webhook">
        <label>Webhook Secret (X-Webhook-Secret)</label><input id="c_wh_sec" type="password">
        <h2 style="margin-top:16px">MQTT &amp; Home Assistant</h2>
        <label class="chk"><input type="checkbox" id="c_mq_en"> Enable MQTT</label>
        <div class="row">
          <div><label>Broker Host</label><input id="c_mq_host" placeholder="192.168.1.50"></div>
          <div><label>Port</label><input id="c_mq_port" type="number"></div>
        </div>
        <div class="row">
          <div><label>MQTT User</label><input id="c_mq_user"></div>
          <div><label>MQTT Pass</label><input id="c_mq_pass" type="password"></div>
        </div>
        <label>Topic Prefix</label><input id="c_mq_pre">
        <label class="chk"><input type="checkbox" id="c_ha_disc"> Enable Home Assistant MQTT Discovery</label>
        <label>HA Discovery Prefix</label><input id="c_ha_pre">
      </div>
    </div>
    <div class="card" style="display:flex;gap:10px;flex-wrap:wrap;align-items:center;justify-content:space-between">
      <div>
        <button class="btn ok" style="margin-top:0" onclick="saveConfig()">Save Configuration</button>
        <button class="btn sec" style="margin-top:0" onclick="if(confirm('Reboot Modem?')) apiPost('/api/modem/reboot',{})">Reboot Modem</button>
        <button class="btn danger" style="margin-top:0" onclick="if(confirm('Reboot ESP32?')) apiPost('/api/system/reboot',{})">Reboot ESP32</button>
      </div>
      <form onsubmit="uploadOta(event)" style="display:flex;gap:8px;align-items:center">
        <input type="file" id="otaFile" accept=".bin" style="width:auto">
        <button class="btn" style="margin-top:0" type="submit">Upload OTA .bin</button>
      </form>
    </div>
  </div>
</div>

<div id="toast" class="toast"></div>

<script>
const $ = id => document.getElementById(id);
function toast(msg, err=false){
  const t=$('toast'); t.textContent=msg; t.style.borderColor=err?'var(--danger)':'var(--accent2)';
  t.style.display='block'; setTimeout(()=>t.style.display='none', 3200);
}
function showTab(name, btn){
  document.querySelectorAll('.tab').forEach(e=>e.classList.remove('active'));
  document.querySelectorAll('nav button').forEach(e=>e.classList.remove('active'));
  $('tab-'+name).classList.add('active'); btn.classList.add('active');
}
async function apiReq(url, opts={}){
  const r = await fetch(url, opts);
  if(r.status === 401){ location.href='/login'; throw new Error('Session expired'); }
  const txt = await r.text();
  let data={}; try{data=JSON.parse(txt)}catch{data={raw:txt}}
  if(!r.ok) throw new Error(data.error||('HTTP '+r.status));
  return data;
}
function apiPost(url, body){
  return apiReq(url,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)})
    .then(d=>{toast(d.message||'Done');loadStatus();return d;})
    .catch(e=>{toast(e.message,true);throw e;});
}
function apiDel(url){
  return apiReq(url,{method:'DELETE'}).then(d=>{toast('Deleted');return d;}).catch(e=>{toast(e.message,true);throw e;});
}
async function logout(){
  await fetch('/api/auth/logout',{method:'POST'});
  location.href='/login';
}
function esc(s){return (s||'').replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));}

function updateSmsCount(){
  const s=$('smsBody').value;
  const nonGsm=/[^\x00-\x7F£¥èéùìòÇØøÅåΔ_ΦΓΛΩΠΨΣΘΞÆæßÉ¤¡ÄÖÑÜ§¿äöñüà€^{}\\[~\]|]/.test(s);
  const len=[...s].length;
  const lim=nonGsm?(len<=70?70:67):(len<=160?160:153);
  const parts=len===0?0:Math.ceil(len/lim);
  $('smsCount').textContent=`${len} chars · ${nonGsm?'UCS-2':'GSM-7'} · ${parts} part(s)`;
}

async function sendSms(){
  const to=$('smsTo').value.trim(), message=$('smsBody').value;
  if(!to||!message) return toast('Enter recipient & message',true);
  await apiPost('/api/sms/send',{to,message});
  $('smsBody').value=''; updateSmsCount(); loadSms();
}

async function checkBalance(){
  try{
    const d=await apiPost('/api/balance/check',{});
    if(d.pending){
      setTimeout(loadAll,3000);
      setTimeout(()=>apiReq('/api/sms/scan',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}'}).catch(()=>{}).then(loadAll),6000);
      setTimeout(loadAll,10000);
    } else {
      loadAll();
    }
  }catch(e){}
}

async function loadStatus(){
  try{
    const d=await apiReq('/api/status');
    $('mDot').className='dot'+(d.modem.ready&&d.modem.sim_ready?' ok':'');
    $('hdrSub').textContent=`${d.board} · FW ${d.version} · Wi-Fi: ${d.wifi_ssid||''} (${d.wifi_ip}) · SIM: ${d.modem.sim_state}`;
    $('pSig').textContent=`${d.modem.signal_pct}% (${d.modem.rssi_dbm} dBm)`;
    $('pNet').textContent=(d.modem.operator||'Searching')+' '+(d.modem.tech||'');
    const bal=d.balance||{};
    $('pBal').textContent=bal.pending?'Checking...':(bal.display||'--');
    $('balLabel').textContent=bal.pending?'Checking...':(bal.display||'Unknown');
    $('balState').textContent=bal.updated_at?('Updated: '+bal.updated_at):`Via ${bal.method||'sms'} (${bal.target||'1144'})`;
    const batV=(d.modem.battery_mv/1000).toFixed(2);
    $('pBat').textContent=d.modem.battery_state==='none'?'No Battery':`${d.modem.battery_pct}% (${batV}V)`;
    $('pLteIp').textContent=d.modem.cellular_ip||'Off';

    $('batLabel').textContent=d.modem.battery_state==='none'?'No Battery Detected':`${d.modem.battery_pct}% · ${batV} V (${d.modem.battery_mv} mV)`;
    $('batState').textContent=d.modem.battery_state==='charging_or_usb'?'USB / Charging':(d.modem.battery_state==='battery'?'On Battery':'--');
    const bf=$('batFill'); bf.style.width=d.modem.battery_pct+'%';
    bf.style.background=d.modem.battery_pct<20?'var(--danger)':(d.modem.battery_pct<50?'var(--warn)':'var(--accent2)');

    const ub=$('unreadBadge');
    if(d.sms_unread>0){ub.textContent=d.sms_unread;ub.style.display='inline-block';}
    else ub.style.display='none';

    const cb=$('callBanner');
    if(d.modem.call_state && d.modem.call_state!=='idle'){
      $('cbText').textContent=`${d.modem.call_state.toUpperCase()} — ${d.modem.active_call_peer||'Unknown'}`;
      cb.classList.add('show');
    } else cb.classList.remove('show');

    $('infoBox').innerHTML=`Model: <b>${esc(d.modem.model||'-')}</b> | IMEI: <b>${esc(d.modem.imei||'-')}</b><br>`+
      `MQTT: <b>${d.mqtt_connected?'Connected':'Disconnected'}</b> | Heap: <b>${Math.round(d.free_heap/1024)} KB</b> | Uptime: <b>${d.uptime_sec}s</b>`;

    const p=d.proxy;
    $('proxyStats').innerHTML=`Status: <b>${p.enabled?'ENABLED':'DISABLED'}</b> · Active Tunnels: <b>${p.active_tunnels}</b> · Total Connections: <b>${p.total_connections}</b><br>`+
      `Traffic: <b>↑ ${(p.bytes_up/1024).toFixed(1)} KB</b> / <b>↓ ${(p.bytes_down/1024).toFixed(1)} KB</b>`;
    if($('proxyTestOut').textContent.startsWith('Usage examples')){
      $('proxyTestOut').textContent=
        `HTTP/HTTPS Proxy: http://${d.wifi_ip}:${p.http_port}\n`+
        `SOCKS5 Proxy:     socks5://${d.wifi_ip}:${p.socks_port}\n\n`+
        `Test with curl:\n  curl -x http://${d.wifi_ip}:${p.http_port} https://api.ipify.org\n`+
        `  curl --socks5 ${d.wifi_ip}:${p.socks_port} https://api.ipify.org`;
    }
  }catch(e){}
}

async function loadSms(){
  try{
    const dir=$('smsFilter').value;
    const d=await apiReq('/api/sms'+(dir?'?direction='+dir:''));
    const list=d.messages||[];
    if(!list.length){$('smsList').innerHTML='<div class="sub">No messages yet.</div>';return;}
    $('smsList').innerHTML=list.map(m=>`
      <div class="msg ${m.direction==='in'&&!m.read?'unread':''}" onclick="markRead(${m.id},${m.read})">
        <div class="msg-head">
          <span><span class="tag ${m.status==='failed'?'fail':m.direction}">${m.direction==='in'?'IN':'OUT'}</span> <span class="msg-peer">${esc(m.peer)}</span></span>
          <span>${esc(m.timestamp)} · <a href="#" style="color:var(--danger)" onclick="event.stopPropagation();delSms(${m.id})">Delete</a></span>
        </div>
        <div class="msg-body">${esc(m.text)}</div>
      </div>`).join('');
  }catch(e){$('smsList').textContent=e.message;}
}
async function markRead(id,isRead){if(!isRead){await apiReq('/api/sms/read',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({id})});loadSms();loadStatus();}}
async function delSms(id){await apiDel('/api/sms?id='+id);loadSms();loadStatus();}

async function loadCalls(){
  try{
    const d=await apiReq('/api/calls');
    const list=d.calls||[];
    if(!list.length){$('callList').innerHTML='<div class="sub">No calls logged.</div>';return;}
    $('callList').innerHTML=list.map(c=>`
      <div class="msg">
        <div class="msg-head">
          <span><span class="tag ${c.direction}">${c.direction.toUpperCase()}</span> <span class="msg-peer">${esc(c.peer)}</span> (${esc(c.status)})</span>
          <span>${esc(c.timestamp)} · ${c.duration_sec}s</span>
        </div>
      </div>`).join('');
  }catch(e){}
}

function dialCall(){
  const to=$('callTo').value.trim(), timeout_sec=parseInt($('callTimeout').value||'0',10);
  if(!to) return toast('Enter phone number',true);
  apiPost('/api/call/dial',{to,timeout_sec}).then(loadCalls);
}
function sendDtmf(){const digits=$('dtmfDigits').value.trim(); if(digits) apiPost('/api/call/dtmf',{digits});}
async function runUssd(){
  const code=$('ussdCode').value.trim(); if(!code) return;
  $('ussdOut').textContent='Sending...';
  try{const d=await apiReq('/api/ussd',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({code})}); $('ussdOut').textContent=d.response||JSON.stringify(d);}
  catch(e){$('ussdOut').textContent='Error: '+e.message;}
}
function toggleProxy(enabled){apiPost('/api/proxy/toggle',{enabled}).then(loadStatus);}
async function testCellularIp(){
  $('proxyTestOut').textContent='Testing cellular IP via api.ipify.org...';
  try{const d=await apiReq('/api/proxy/test',{method:'POST'}); $('proxyTestOut').textContent=`4G Public IP: ${d.external_ip}\nModem Local IP: ${d.cellular_ip}`;}
  catch(e){$('proxyTestOut').textContent='Error: '+e.message;}
}
function quickAt(c){$('atCmd').value=c; sendAt();}
async function sendAt(){
  const command=$('atCmd').value.trim(); if(!command) return;
  $('atOut').textContent='> '+command+'\n...';
  try{const d=await apiReq('/api/at',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({command})}); $('atOut').textContent='> '+command+'\n'+(d.response||'');}
  catch(e){$('atOut').textContent='Error: '+e.message;}
}

async function loadConfig(){
  try{
    const c=await apiReq('/api/config');
    $('c_wifi_ssid').value=c.wifi_ssid||''; $('c_wifi_pass').value=c.wifi_pass||'';
    $('c_wifi_fb_ssid').value=c.wifi_fallback_ssid||''; $('c_wifi_fb_pass').value=c.wifi_fallback_pass||'';
    $('c_hostname').value=c.hostname||'';
    $('c_api_key').value=c.api_key||''; $('p_user').value=c.web_user||'admin';
    $('c_apn').value=c.apn||''; $('c_apn_user').value=c.apn_user||''; $('c_apn_pass').value=c.apn_pass||'';
    $('c_sim_pin').value=c.sim_pin||''; $('c_auto_del').checked=!!c.auto_delete_sim_sms;
    $('c_bal_method').value=c.balance_method||'sms'; $('c_bal_target').value=c.balance_target||'1144'; $('c_bal_msg').value=c.balance_message||'SALDO';
    $('c_proxy_en').checked=!!c.proxy_enabled; $('c_http_port').value=c.http_proxy_port||8080; $('c_socks_port').value=c.socks_proxy_port||1080;
    $('c_proxy_user').value=c.proxy_user||''; $('c_proxy_pass').value=c.proxy_pass||'';
    $('c_wh_en').checked=!!c.webhook_enabled; $('c_wh_url').value=c.webhook_url||''; $('c_wh_sec').value=c.webhook_secret||'';
    $('c_mq_en').checked=!!c.mqtt_enabled; $('c_mq_host').value=c.mqtt_host||''; $('c_mq_port').value=c.mqtt_port||1883;
    $('c_mq_user').value=c.mqtt_user||''; $('c_mq_pass').value=c.mqtt_pass||''; $('c_mq_pre').value=c.mqtt_prefix||'';
    $('c_ha_disc').checked=!!c.ha_discovery; $('c_ha_pre').value=c.ha_discovery_prefix||'homeassistant';
  }catch(e){}
}

async function changePassword(){
  const username=$('p_user').value.trim(), current_password=$('p_cur').value, new_password=$('p_new').value, conf=$('p_conf').value;
  if(new_password.length<4) return toast('Password must be at least 4 characters',true);
  if(new_password!==conf) return toast('New passwords do not match',true);
  await apiPost('/api/auth/password',{username,current_password,new_password});
  $('p_cur').value=''; $('p_new').value=''; $('p_conf').value='';
}

async function saveConfig(){
  const body={
    wifi_ssid:$('c_wifi_ssid').value, wifi_pass:$('c_wifi_pass').value,
    wifi_fallback_ssid:$('c_wifi_fb_ssid').value, wifi_fallback_pass:$('c_wifi_fb_pass').value,
    hostname:$('c_hostname').value, api_key:$('c_api_key').value,
    apn:$('c_apn').value, apn_user:$('c_apn_user').value, apn_pass:$('c_apn_pass').value,
    sim_pin:$('c_sim_pin').value, auto_delete_sim_sms:$('c_auto_del').checked,
    balance_method:$('c_bal_method').value, balance_target:$('c_bal_target').value, balance_message:$('c_bal_msg').value,
    proxy_enabled:$('c_proxy_en').checked, http_proxy_port:+$('c_http_port').value, socks_proxy_port:+$('c_socks_port').value,
    proxy_user:$('c_proxy_user').value, proxy_pass:$('c_proxy_pass').value,
    webhook_enabled:$('c_wh_en').checked, webhook_url:$('c_wh_url').value, webhook_secret:$('c_wh_sec').value,
    mqtt_enabled:$('c_mq_en').checked, mqtt_host:$('c_mq_host').value, mqtt_port:+$('c_mq_port').value,
    mqtt_user:$('c_mq_user').value, mqtt_pass:$('c_mq_pass').value, mqtt_prefix:$('c_mq_pre').value,
    ha_discovery:$('c_ha_disc').checked, ha_discovery_prefix:$('c_ha_pre').value
  };
  await apiPost('/api/config',body);
}

async function uploadOta(ev){
  ev.preventDefault();
  const f=$('otaFile').files[0]; if(!f) return toast('Select .bin file first',true);
  const fd=new FormData(); fd.append('update',f);
  toast('Uploading firmware...');
  const r=await fetch('/api/ota',{method:'POST',body:fd});
  if(r.ok) toast('OTA complete, rebooting...');
  else toast('OTA failed',true);
}

function loadAll(){loadStatus();loadSms();loadCalls();}
loadAll(); loadConfig();
setInterval(()=>{loadStatus(); if($('tab-sms').classList.contains('active')) loadSms();}, 5000);
</script>
</body>
</html>
)rawliteral";
