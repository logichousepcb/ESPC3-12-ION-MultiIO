#pragma once

#include <Arduino.h>

// %TITLE% is substituted at request time with BaseConfig::title.
static const char BASE_PAGE_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>%TITLE%</title>
<style>
:root{--bg:#f3f5f8;--card:#fff;--fg:#1d2430;--muted:#667085;--line:#d9dee7;--accent:#1f6feb;--ok:#1a7f37;--bad:#cf222e;--warn:#9a6700}
@media (prefers-color-scheme:dark){:root{--bg:#0f1419;--card:#181f27;--fg:#e6edf3;--muted:#8b98a5;--line:#2b3440;--accent:#4493f8;--ok:#3fb950;--bad:#f85149;--warn:#d29922}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
header{background:var(--card);border-bottom:1px solid var(--line);padding:14px 16px}
header .wrap{display:flex;flex-wrap:wrap;align-items:center;gap:8px 12px}
h1{font-size:19px;margin:0;flex:1 1 auto}
main.wrap{padding:16px}
.wrap{max-width:760px;margin:0 auto}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:16px;margin-bottom:16px}
h2{font-size:15px;margin:0 0 12px;text-transform:uppercase;letter-spacing:.04em;color:var(--muted)}
.pill{font-size:12px;padding:2px 9px;border-radius:999px;border:1px solid var(--line);white-space:nowrap}
.pill.ok{color:var(--ok);border-color:var(--ok)}.pill.warn{color:var(--warn);border-color:var(--warn)}
dl{display:grid;grid-template-columns:max-content 1fr;gap:6px 16px;margin:0}
dt{color:var(--muted)}dd{margin:0;word-break:break-all}
label{display:block;font-size:13px;color:var(--muted);margin:10px 0 4px}
input[type=text],input[type=password]{width:100%;padding:9px 10px;border:1px solid var(--line);border-radius:7px;background:var(--bg);color:var(--fg);font:inherit}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-top:12px}
button,.filebtn{font:inherit;padding:8px 14px;border-radius:7px;border:1px solid var(--line);background:var(--bg);color:var(--fg);cursor:pointer}
button.primary{background:var(--accent);border-color:var(--accent);color:#fff}
button.danger{color:var(--bad);border-color:var(--bad)}
button:disabled{opacity:.5;cursor:default}
.nets{list-style:none;margin:10px 0 0;padding:0;border:1px solid var(--line);border-radius:7px;max-height:220px;overflow:auto}
.nets:empty{display:none}
.nets li{display:flex;justify-content:space-between;gap:8px;padding:8px 10px;border-bottom:1px solid var(--line);cursor:pointer}
.nets li:last-child{border-bottom:0}.nets li:hover{background:var(--bg)}
.nets span.s{color:var(--muted);font-size:13px;white-space:nowrap}
.msg{margin-top:10px;font-size:14px;min-height:1em}
.msg.ok{color:var(--ok)}.msg.bad{color:var(--bad)}
.bar{height:10px;border-radius:5px;background:var(--bg);border:1px solid var(--line);overflow:hidden;margin-top:12px;display:none}
.bar>div{height:100%;width:0;background:var(--accent);transition:width .2s}
.hint{font-size:13px;color:var(--muted);margin:6px 0 0}
input[type=file]{max-width:100%}
.chk{display:flex;align-items:center;gap:6px;font-size:13px;color:var(--muted);margin-top:8px}
.toggle{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-top:6px}
.toggle-label{font-size:14px}
.switch{position:relative;display:inline-block;width:52px;height:30px;flex:0 0 auto}
.switch input{opacity:0;width:0;height:0}
.slider{position:absolute;inset:0;border-radius:999px;background:var(--line);transition:.2s;cursor:pointer}
.slider:before{content:"";position:absolute;height:22px;width:22px;left:4px;top:4px;border-radius:50%;background:#fff;transition:.2s}
.switch input:checked + .slider{background:var(--accent)}
.switch input:focus-visible + .slider{outline:2px solid var(--accent);outline-offset:2px}
.switch input:checked + .slider:before{transform:translateX(22px)}
.switch input:disabled + .slider{opacity:.55;cursor:default}
.nav{display:flex;gap:8px;flex-wrap:wrap}
.nav a{text-decoration:none}
.nav .pill.active{background:var(--accent);border-color:var(--accent);color:#fff}
.gpio-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:10px}
.gpio-chip{border:1px solid var(--line);border-radius:8px;padding:12px;background:var(--bg)}
.gpio-chip-head{display:flex;justify-content:space-between;gap:8px;align-items:center;margin-bottom:8px}
.gpio-chip h3{margin:0;font-size:14px}
.gpio-pins{display:grid;grid-template-columns:repeat(auto-fit,minmax(96px,1fr));gap:8px}
.gpio-pin{border:1px solid var(--line);border-radius:8px;padding:8px}
.gpio-pin-head{display:flex;align-items:center;gap:8px;margin-bottom:2px}
.gpio-led{width:10px;height:10px;border-radius:50%;background:var(--line);flex:0 0 auto}
.gpio-int{display:inline-flex;align-items:center;gap:6px;font-size:12px;color:var(--muted);padding:4px 8px;border:1px solid var(--line);border-radius:999px;white-space:nowrap}
.gpio-int.active{color:var(--warn);border-color:var(--warn)}
.gpio-int-dot{width:8px;height:8px;border-radius:50%;background:var(--muted);flex:0 0 auto}
.gpio-int.active .gpio-int-dot{background:var(--warn);box-shadow:0 0 0 3px rgba(154,103,0,.18)}
.gpio-pin b{display:block;font-size:13px}
.gpio-pin span{display:block;font-size:12px;color:var(--muted)}
.gpio-pin.open{border-color:#1f6feb}
.gpio-pin.open .gpio-led{background:#1f6feb;box-shadow:0 0 0 3px rgba(31,111,235,.18)}
.gpio-pin.low{border-color:var(--ok)}
.gpio-pin.low .gpio-led{background:var(--ok);box-shadow:0 0 0 3px rgba(26,127,55,.18)}
[data-page="home"] .configOnly,[data-page="home"] .gpioOnly{display:none}
[data-page="config"] .homeOnly,[data-page="config"] .gpioOnly{display:none}
[data-page="gpio"] .homeOnly,[data-page="gpio"] .configOnly{display:none}
</style>
</head>
<body data-page="%PAGE%">
<header><div class="wrap">
  <h1>%TITLE%</h1>
  <span class="pill" id="ver">v-</span>
  <span class="pill" id="mode">-</span>
  <nav class="nav" aria-label="Pages">
    <a href="/" class="pill" id="homeLink">Power</a>
    <a href="/gpio" class="pill" id="gpioLink">GPIO Control</a>
    <a href="/config" class="pill" id="configLink">Configuration</a>
  </nav>
</div></header>
<main class="wrap">

<section class="card configOnly">
  <h2>Status</h2>
  <dl id="status"><dt>Loading</dt><dd>…</dd></dl>
</section>

<section class="card homeOnly" id="powerCard" hidden>
  <h2>Power Control</h2>
  <div class="toggle">
    <div>
      <div class="toggle-label" id="powerLabel">Power</div>
      <p class="hint" id="powerHint" style="margin-bottom:0"></p>
    </div>
    <label class="switch" for="powerSwitch">
      <input type="checkbox" id="powerSwitch">
      <span class="slider"></span>
    </label>
  </div>
  <div class="msg" id="powerMsg"></div>
</section>

<section class="card homeOnly" id="monitorCard" hidden>
  <h2>Power Monitor</h2>
  <dl id="monitorStatus"><dt>Loading</dt><dd>…</dd></dl>
  <p class="hint" id="monitorHint"></p>
</section>

<section class="card gpioOnly" id="gpioCard">
  <h2>GPIO Control</h2>
  <div class="toggle" style="margin-top:0">
    <div>
      <div class="toggle-label">Use MCP interrupts</div>
      <p class="hint" id="gpioModeHint" style="margin-bottom:0">Loading GPIO update mode…</p>
    </div>
    <label class="switch" for="gpioModeSwitch">
      <input type="checkbox" id="gpioModeSwitch">
      <span class="slider"></span>
    </label>
  </div>
  <p class="hint" id="gpioHint">Loading GPIO status…</p>
  <div class="gpio-grid" id="gpioGrid"></div>
  <div class="msg" id="gpioMsg"></div>
</section>

<section class="card configOnly">
  <h2>WiFi Setup</h2>
  <div class="row" style="margin-top:0">
    <button id="scanBtn" type="button">Scan for networks</button>
    <span class="hint" id="scanMsg"></span>
  </div>
  <ul class="nets" id="nets"></ul>
  <form id="wifiForm">
    <label for="ssid">Network name (SSID)</label>
    <input type="text" id="ssid" name="ssid" maxlength="32" autocomplete="off" required>
    <label for="pass">Password</label>
    <input type="password" id="pass" name="pass" maxlength="63" autocomplete="off">
    <label class="chk"><input type="checkbox" id="showPass"> Show password</label>
    <div class="row"><button class="primary" type="submit">Save &amp; Connect</button></div>
    <div class="msg" id="wifiMsg"></div>
  </form>
</section>

<section class="card configOnly">
  <h2>I2C Devices</h2>
  <div class="row" style="margin-top:0">
    <button id="i2cBtn" type="button">Scan I2C bus</button>
    <span class="hint" id="i2cHint"></span>
  </div>
  <ul class="nets" id="i2cList"></ul>
  <div class="msg" id="i2cMsg"></div>
</section>

<section class="card configOnly">
  <h2>Firmware Update</h2>
  <p class="hint" style="margin:0 0 10px">Upload an ESP32-C3 application image (<code>firmware.bin</code> from <code>.pio/build/&lt;env&gt;/</code>). It must be built with the same partition table as this firmware. Do not power off while the upload runs.</p>
  <form id="fwForm">
    <input type="file" id="fwFile" accept=".bin,application/octet-stream" required>
    <div class="row"><button class="primary" type="submit" id="fwBtn">Upload &amp; Install</button></div>
    <div class="bar" id="fwBar"><div></div></div>
    <div class="msg" id="fwMsg"></div>
  </form>
</section>

<section class="card configOnly">
  <h2>Device Settings</h2>
  <form id="devForm">
    <label for="hostname">Hostname (reachable as <i>hostname</i>.local)</label>
    <input type="text" id="hostname" name="hostname" maxlength="32" autocomplete="off">
    <label for="admin">New admin password (user: <b>admin</b>)</label>
    <input type="password" id="admin" name="admin" maxlength="64" autocomplete="new-password" placeholder="leave blank to keep current">
    <label class="chk"><input type="checkbox" id="adminClear" name="adminClear" value="1"> Remove admin password</label>
    <p class="hint" id="authHint"></p>
    <div class="row"><button class="primary" type="submit">Save</button></div>
    <div class="msg" id="devMsg"></div>
  </form>
</section>

<section class="card configOnly">
  <h2>Maintenance</h2>
  <div class="row" style="margin-top:0">
    <button type="button" id="rebootBtn">Reboot</button>
    <button type="button" class="danger" id="forgetBtn">Forget WiFi</button>
  </div>
  <p class="hint">Holding the BOOT button for 5 seconds while running also erases the saved WiFi credentials.</p>
  <div class="msg" id="mntMsg"></div>
</section>

</main>
<script>
const $ = id => document.getElementById(id);
const esc = s => String(s).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const pageKind = document.body.dataset.page || 'home';
const isConfigPage = pageKind === 'config';
const isGpioPage = pageKind === 'gpio';
let rebooting = false;
let gpioLoaded = false;
let gpioLastSnapshot = '';
let gpioLastSeq = -1;
let gpioRequestInFlight = false;

function setMsg(el, text, ok) { el.textContent = text; el.className = 'msg' + (ok === undefined ? '' : ok ? ' ok' : ' bad'); }

function fmtBytes(n) { return n >= 1048576 ? (n / 1048576).toFixed(2) + ' MB' : (n / 1024).toFixed(1) + ' KB'; }
function fmtVolts(v) { return Number(v).toFixed(3) + ' V'; }
function fmtMilliamps(ma) { return Math.abs(ma) >= 1000 ? (ma / 1000).toFixed(3) + ' A' : Number(ma).toFixed(1) + ' mA'; }
function fmtMilliwatts(mw) { return Math.abs(mw) >= 1000 ? (mw / 1000).toFixed(3) + ' W' : Number(mw).toFixed(1) + ' mW'; }
function fmtUptime(s) {
  const d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600), m = Math.floor(s % 3600 / 60);
  return (d ? d + 'd ' : '') + h + 'h ' + m + 'm ' + (s % 60) + 's';
}

$('homeLink').classList.toggle('active', pageKind === 'home');
$('gpioLink').classList.toggle('active', isGpioPage);
$('configLink').classList.toggle('active', pageKind === 'config');

async function loadStatus() {
  if (rebooting) return;
  try {
    const r = await fetch('/api/status', {cache: 'no-store'});
    if (!r.ok) return;
    const s = await r.json();
    $('ver').textContent = 'v' + s.version;
    const m = $('mode');
    m.textContent = s.mode === 'portal' ? 'Setup AP' : (s.connected ? 'Connected' : 'Reconnecting');
    m.className = 'pill ' + (s.mode === 'portal' ? 'warn' : s.connected ? 'ok' : 'warn');
    const rows = [];
    if (s.mode === 'portal') {
      rows.push(['Setup AP', s.apSsid + ' (' + s.apIp + ')'], ['AP clients', s.apClients]);
      rows.push(['Saved network', s.savedSsid || '(none)']);
    } else {
      rows.push(['Network', s.savedSsid], ['IP address', s.ip || '-'], ['Signal', s.connected ? s.rssi + ' dBm' : '-']);
    }
    if (s.ina226Enabled && s.ina226Present) {
      rows.push(['PoE voltage', fmtVolts(s.ina226BusVoltage)], ['PoE current', fmtMilliamps(s.ina226CurrentMa)],
        ['PoE load', fmtMilliwatts(s.ina226PowerMw)]);
    } else if (s.ina226Enabled) {
      rows.push(['PoE monitor', 'Unavailable']);
    }
    rows.push(['Hostname', s.hostname + '.local'], ['MAC', s.mac], ['Uptime', fmtUptime(s.uptime)],
      ['Firmware', 'v' + s.version + ' (' + s.build + ')'], ['Running slot', s.partition],
      ['PoE power', s.powerSwitchEnabled ? (s.powerSwitchOn ? 'on' : 'off') : 'not available'],
      ['WiFi disable input', s.wifiDisableEnabled ? ('GPIO' + s.wifiDisablePin + (s.wifiDisabledByPin ? ' active' : ' idle') + ', debounce ' + s.wifiDisableDebounceMs + ' ms') : 'disabled'],
      ['I2C bus', s.i2cEnabled ? ('GPIO' + s.i2cSdaPin + ' SDA / GPIO' + s.i2cSclPin + ' SCL @ ' + s.i2cClockHz + ' Hz') : 'disabled'],
      ['I2C devices', s.i2cEnabled ? String(s.i2cDeviceCount) : '-'],
      ['App size / slot', fmtBytes(s.sketch) + ' / ' + fmtBytes(s.otaSpace)],
      ['Chip', s.chip + ', ' + fmtBytes(s.flash) + ' flash'], ['Free heap', fmtBytes(s.heap)], ['SDK', s.sdk]);
    $('status').innerHTML = rows.map(([k, v]) => '<dt>' + esc(k) + '</dt><dd>' + esc(v) + '</dd>').join('');
    $('powerCard').hidden = !s.powerSwitchEnabled;
    if (s.powerSwitchEnabled) {
      $('powerLabel').textContent = s.powerSwitchLabel;
      $('powerHint').textContent = 'GPIO' + s.powerSwitchPin + ' is currently ' + (s.powerSwitchOn ? 'on' : 'off') + '.';
      if (!$('powerSwitch').dataset.busy) $('powerSwitch').checked = !!s.powerSwitchOn;
    }
    $('monitorCard').hidden = !s.ina226Enabled;
    if (s.ina226Enabled) {
      const detailRows = s.ina226Present
        ? [['Bus voltage', fmtVolts(s.ina226BusVoltage)], ['Shunt voltage', Number(s.ina226ShuntVoltageMv).toFixed(3) + ' mV'],
           ['Current', fmtMilliamps(s.ina226CurrentMa)], ['Power', fmtMilliwatts(s.ina226PowerMw)]]
        : [['Status', s.ina226Error || 'INA226 not detected']];
      $('monitorStatus').innerHTML = detailRows.map(([k, v]) => '<dt>' + esc(k) + '</dt><dd>' + esc(v) + '</dd>').join('');
      $('monitorHint').textContent = (s.ina226Label || 'Power monitor') + ' on I2C address 0x' +
        Number(s.ina226Address).toString(16).toUpperCase().padStart(2, '0') +
        ', shunt ' + Number(s.ina226ShuntMilliOhms).toFixed(1) + ' mΩ.';
    }
    if (document.activeElement !== $('hostname') && !$('hostname').dataset.touched) $('hostname').value = s.hostname;
    $('authHint').textContent = s.authEnabled ? 'An admin password is set; this page requires login.'
      : 'No admin password set: anyone on this network can change settings or upload firmware.';
    if (!$('ssid').value && s.savedSsid) $('ssid').value = s.savedSsid;
  } catch (e) {}
}

async function post(url, data) {
  const r = await fetch(url, {method: 'POST', body: new URLSearchParams(data)});
  let j; try { j = await r.json(); } catch (e) { j = {ok: false, message: 'HTTP ' + r.status}; }
  return j;
}

function waitForReboot(msgEl, text) {
  rebooting = true;
  setMsg(msgEl, text, true);
  let tries = 0;
  setTimeout(function poll() {
    tries++;
    fetch('/api/status', {cache: 'no-store'}).then(r => { if (r.ok) location.reload(); else throw 0; })
      .catch(() => {
        if (tries < 40) setTimeout(poll, 2000);
        else setMsg(msgEl, text + ' The device did not come back at this address. It may have joined your network; look for it at its hostname.local or in your router.', true);
      });
  }, 4000);
}

async function handleResult(j, msgEl) {
  setMsg(msgEl, j.message, j.ok);
  if (j.ok && j.restart) waitForReboot(msgEl, j.message);
}

async function loadI2c() {
  const btn = $('i2cBtn');
  const hint = $('i2cHint');
  const list = $('i2cList');
  btn.disabled = true;
  hint.textContent = 'Scanning…';
  setMsg($('i2cMsg'), '');
  list.innerHTML = '';
  try {
    const r = await fetch('/api/i2c', {cache: 'no-store'});
    if (!r.ok) throw new Error('HTTP ' + r.status);
    const j = await r.json();
    if (!j.enabled) {
      hint.textContent = 'I2C scan is disabled.';
      setMsg($('i2cMsg'), j.message || 'I2C scan is disabled.', false);
      return;
    }
    hint.textContent = 'SDA GPIO' + j.sdaPin + ' / SCL GPIO' + j.sclPin + ' @ ' + j.clockHz + ' Hz';
    if (!j.ready) {
      setMsg($('i2cMsg'), j.message || 'I2C bus initialization failed.', false);
      return;
    }
    const devices = j.devices || [];
    list.innerHTML = devices.map(d => '<li><span>' + esc(d.hex) + '</span><span class="s">decimal ' + esc(String(d.address)) + '</span></li>').join('');
    setMsg($('i2cMsg'), devices.length ? (devices.length + ' I2C device(s) found.') : 'No I2C devices found.', true);
  } catch (err) {
    hint.textContent = '';
    setMsg($('i2cMsg'), 'I2C scan failed.', false);
  } finally {
    btn.disabled = false;
  }
}

async function loadGpio() {
  const grid = $('gpioGrid');
  const hint = $('gpioHint');
  if (!grid || gpioRequestInFlight) return;
  gpioRequestInFlight = true;
  if (!gpioLoaded) hint.textContent = 'Loading GPIO status…';
  try {
    const r = await fetch('/api/gpio', {cache: 'no-store'});
    if (!r.ok) throw new Error('HTTP ' + r.status);
    const j = await r.json();
    const chips = j.chips || [];
    $('gpioModeHint').textContent = j.useInterrupts
      ? 'Interrupt mode reads the triggered expander when INTA changes.'
      : 'Polling mode ignores MCP interrupts and refreshes both expanders repeatedly.';
    if (!$('gpioModeSwitch').dataset.busy) $('gpioModeSwitch').checked = !!j.useInterrupts;
    if (!chips.length) {
      gpioLoaded = true;
      gpioLastSnapshot = 'no-chips';
      gpioLastSeq = Number(j.seq || 0);
      hint.textContent = 'No MCP23017 chips are configured.';
      grid.innerHTML = '';
      return;
    }
    if (gpioLoaded && gpioLastSeq === Number(j.seq || 0)) {
      hint.textContent = 'Showing live status for 32 MCP23017 GPIO pins.';
      setMsg($('gpioMsg'), '');
      return;
    }
    const markup = chips.map(chip => {
      const addr = '0x' + Number(chip.address).toString(16).toUpperCase().padStart(2, '0');
      const interruptText = chip.interruptPin >= 0
        ? 'INT GPIO' + chip.interruptPin + (chip.interruptActive ? ' active' : ' idle')
        : 'No interrupt pin configured';
      const interruptBadge = chip.interruptPin >= 0
        ? '<div class="gpio-int' + (chip.interruptActive ? ' active' : '') + '"><span class="gpio-int-dot" aria-hidden="true"></span>' +
          esc(interruptText) + '</div>'
        : '<div class="gpio-int"><span class="gpio-int-dot" aria-hidden="true"></span>' + esc(interruptText) + '</div>';
      if (!chip.present) {
        return '<section class="gpio-chip"><div class="gpio-chip-head"><h3>' + esc(chip.label) + ' (' + esc(addr) + ')</h3>' +
          interruptBadge + '</div><div class="msg bad">' +
          esc(chip.error || 'Device not responding.') + '</div></section>';
      }
      const pins = (chip.pins || []).map(pin => '<div class="gpio-pin ' + (pin.high ? 'open' : 'low') + '">' +
        '<div class="gpio-pin-head"><span class="gpio-led" aria-hidden="true"></span><b>' + esc(chip.label) + ' ' + esc(pin.name) + '</b></div>' +
        '<span>' + esc(pin.mode) + '</span>' +
        '<span>' + (pin.high ? 'OPEN' : 'LOW') + '</span>' +
        '</div>').join('');
      return '<section class="gpio-chip"><div class="gpio-chip-head"><h3>' + esc(chip.label) + ' (' + esc(addr) + ')</h3>' +
        interruptBadge + '</div><div class="gpio-pins">' + pins + '</div></section>';
    }).join('');
    gpioLoaded = true;
    gpioLastSeq = Number(j.seq || 0);
    hint.textContent = 'Showing live status for 32 MCP23017 GPIO pins.';
    setMsg($('gpioMsg'), '');
    if (markup !== gpioLastSnapshot) {
      grid.innerHTML = markup;
      gpioLastSnapshot = markup;
    }
  } catch (err) {
    gpioLoaded = true;
    gpioLastSnapshot = '';
    gpioLastSeq = -1;
    hint.textContent = 'GPIO status unavailable.';
    setMsg($('gpioMsg'), 'GPIO status read failed.', false);
  } finally {
    gpioRequestInFlight = false;
  }
}

// WiFi scan
$('scanBtn').onclick = async () => {
  const btn = $('scanBtn'); btn.disabled = true; $('scanMsg').textContent = 'Scanning…'; $('nets').innerHTML = '';
  let url = '/api/scan?fresh=1';
  for (let i = 0; i < 20; i++) {
    try {
      const j = await (await fetch(url, {cache: 'no-store'})).json();
      if (!j.scanning) {
        const nets = j.networks.sort((a, b) => b.rssi - a.rssi);
        $('nets').innerHTML = nets.map(n => '<li data-ssid="' + esc(n.ssid) + '"><span>' + (n.secure ? '&#128274; ' : '') + esc(n.ssid) +
          '</span><span class="s">' + n.rssi + ' dBm</span></li>').join('');
        $('scanMsg').textContent = nets.length ? nets.length + ' network(s) found. Tap one to select.' : 'No networks found.';
        btn.disabled = false; return;
      }
    } catch (e) {}
    url = '/api/scan';
    await new Promise(r => setTimeout(r, 1500));
  }
  $('scanMsg').textContent = 'Scan timed out.'; btn.disabled = false;
};
$('nets').onclick = e => {
  const li = e.target.closest('li'); if (!li) return;
  $('ssid').value = li.dataset.ssid; $('pass').value = ''; $('pass').focus();
};
$('showPass').onchange = e => { $('pass').type = e.target.checked ? 'text' : 'password'; };
$('i2cBtn').onclick = () => { loadI2c(); };

$('gpioModeSwitch').onchange = async e => {
  const sw = e.target;
  sw.disabled = true;
  sw.dataset.busy = '1';
  setMsg($('gpioMsg'), 'Updating GPIO mode…');
  try {
    const j = await post('/api/gpio-mode', {useInterrupts: sw.checked ? '1' : '0'});
    setMsg($('gpioMsg'), j.message, j.ok);
    if (!j.ok) sw.checked = !sw.checked;
    gpioLastSeq = -1;
    gpioLastSnapshot = '';
    await loadGpio();
  } catch (err) {
    sw.checked = !sw.checked;
    setMsg($('gpioMsg'), 'Request failed.', false);
  } finally {
    delete sw.dataset.busy;
    sw.disabled = false;
  }
};

$('powerSwitch').onchange = async e => {
  const sw = e.target;
  if (!sw.checked && !confirm('Turn off PoE power? This may remove power from downstream equipment.')) {
    sw.checked = true;
    return;
  }
  sw.disabled = true;
  sw.dataset.busy = '1';
  setMsg($('powerMsg'), 'Updating…');
  try {
    const j = await post('/api/power', {on: sw.checked ? '1' : '0'});
    setMsg($('powerMsg'), j.message, j.ok);
    if (!j.ok) sw.checked = !sw.checked;
    await loadStatus();
  } catch (err) {
    sw.checked = !sw.checked;
    setMsg($('powerMsg'), 'Request failed.', false);
  } finally {
    delete sw.dataset.busy;
    sw.disabled = false;
  }
};

$('wifiForm').onsubmit = async e => {
  e.preventDefault();
  setMsg($('wifiMsg'), 'Saving…');
  try { await handleResult(await post('/api/wifi', {ssid: $('ssid').value, pass: $('pass').value}), $('wifiMsg')); }
  catch (err) { setMsg($('wifiMsg'), 'Request failed.', false); }
};

// Device settings
$('hostname').oninput = () => { $('hostname').dataset.touched = '1'; };
$('devForm').onsubmit = async e => {
  e.preventDefault();
  const d = {hostname: $('hostname').value};
  if ($('adminClear').checked) d.adminClear = '1'; else if ($('admin').value) d.admin = $('admin').value;
  setMsg($('devMsg'), 'Saving…');
  try {
    const j = await post('/api/device', d);
    $('admin').value = ''; $('adminClear').checked = false; delete $('hostname').dataset.touched;
    await handleResult(j, $('devMsg'));
    if (j.ok && !j.restart) loadStatus();
  } catch (err) { setMsg($('devMsg'), 'Request failed.', false); }
};

// Firmware upload
$('fwForm').onsubmit = e => {
  e.preventDefault();
  const f = $('fwFile').files[0];
  if (!f) return;
  if (!/\.bin$/i.test(f.name) && !confirm('"' + f.name + '" does not end in .bin. Upload anyway?')) return;
  const fd = new FormData(); fd.append('firmware', f, f.name);
  const xhr = new XMLHttpRequest();
  const bar = $('fwBar'), fill = bar.firstElementChild;
  bar.style.display = 'block'; fill.style.width = '0'; $('fwBtn').disabled = true;
  setMsg($('fwMsg'), 'Uploading ' + f.name + ' (' + fmtBytes(f.size) + ')…');
  xhr.upload.onprogress = ev => {
    if (!ev.lengthComputable) return;
    const p = Math.round(ev.loaded / ev.total * 100);
    fill.style.width = p + '%';
    setMsg($('fwMsg'), p < 100 ? 'Uploading… ' + p + '%' : 'Finishing and verifying…');
  };
  xhr.onload = () => {
    let j; try { j = JSON.parse(xhr.responseText); } catch (err) { j = {ok: false, message: 'HTTP ' + xhr.status}; }
    $('fwBtn').disabled = false;
    if (!j.ok) bar.style.display = 'none';
    handleResult(j, $('fwMsg'));
  };
  xhr.onerror = () => { $('fwBtn').disabled = false; bar.style.display = 'none'; setMsg($('fwMsg'), 'Upload failed (connection lost).', false); };
  xhr.open('POST', '/update');
  xhr.send(fd);
};

// Maintenance
$('rebootBtn').onclick = async () => {
  if (!confirm('Reboot the device?')) return;
  try { await handleResult(await post('/api/reboot', {}), $('mntMsg')); } catch (e) { setMsg($('mntMsg'), 'Request failed.', false); }
};
$('forgetBtn').onclick = async () => {
  if (!confirm('Erase the saved WiFi credentials? The device will restart into setup AP mode.')) return;
  try { await handleResult(await post('/api/forget', {}), $('mntMsg')); } catch (e) { setMsg($('mntMsg'), 'Request failed.', false); }
};

loadStatus();
if (isConfigPage) loadI2c();
if (isGpioPage) loadGpio();
setInterval(loadStatus, 5000);
if (isGpioPage) setInterval(loadGpio, 250);
</script>
</body>
</html>
)rawliteral";
