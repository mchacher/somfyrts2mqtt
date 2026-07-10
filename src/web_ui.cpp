/**
 * @file web_ui.cpp
 * @brief AsyncWebServer-backed implementation. See web_ui.h.
 */
#include "web_ui.h"

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Update.h>           // iter 016 : OTA firmware streamer

#include "logger.h"
#include "mqtt.h"
#include "nvs_store.h"
#include "orchestrator.h"
#include "ota_guard.h"        // iter 021 : reject an OTA image built for another chip

namespace web_ui {

  static AsyncWebServer s_server(80);
  static bool           s_started = false;

  // iter 021 : set at the OTA first chunk when the incoming image does not
  // target this chip. Non-empty => the completion handler answers 400 and no
  // flash write ever happened. One upload runs at a time, so file-scope is safe.
  static String s_ota_reject_msg;

  // The single-page HTML/CSS/JS embedded as a raw string. Kept small,
  // vanilla JS, no framework.
  static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<title>somfyrts2mqtt</title>
<style>
/* Sowel light palette — kept in sync with src/captive_portal.cpp STYLE[]
   (spec 018) so the bridge feels visually identical between the first-run
   captive portal and the post-WiFi LAN portal. Same palette + same DOM
   vocabulary (.card, .field, .with-toggle, .btn) ; the LAN page extends
   it for tables, status pills, command cells and the OTA progress bar. */
:root {
  --primary: #1A4F6E;
  --primary-hover: #13405A;
  --primary-light: #E6F0F6;
  --accent: #D4963F;
  --bg: #F8F9FA;
  --card: #FFFFFF;
  --ink: #1A2A3C;
  --muted: #6B7280;
  --border: #D1D5DB;
  --border-focus: #1A4F6E;
  --error: #DC2626;
  --error-bg: #FEE2E2;
  --success: #1F7A36;
  --success-bg: #E6F4EA;
}
*, *::before, *::after { box-sizing: border-box; }
html, body { margin: 0; padding: 0; background: var(--bg); color: var(--ink);
  font-family: -apple-system, BlinkMacSystemFont, "Inter", "Segoe UI", Roboto, sans-serif;
  font-size: 14px; line-height: 1.5; }
body { padding: 0 16px 32px; max-width: 880px; margin-inline: auto; }
header.brand-header { text-align: center; padding: 24px 8px 16px; }
header.brand-header .brand { font-size: 18px; font-weight: 700;
  letter-spacing: 4px; color: var(--primary); }
header.brand-header .tag { font-size: 12px; color: var(--muted);
  margin-top: 6px; letter-spacing: 1px; text-transform: uppercase; }
h1 { display: none; }
h2 { margin: 0 0 12px; font-size: 13px; color: var(--primary);
  text-transform: uppercase; letter-spacing: 1.2px; font-weight: 700;
  border-bottom: 1px solid var(--border); padding-bottom: 6px; }
section { background: var(--card); border: 1px solid var(--border);
  border-radius: 10px; padding: 18px; margin-bottom: 14px; }
.table-wrap { overflow-x: auto; margin: 0 -0.25rem; }
.table-wrap table { min-width: 720px; }
table { width: 100%; border-collapse: collapse; }
th, td { padding: 0.5rem 0.6rem; text-align: left;
  border-bottom: 1px solid var(--border); font-size: 0.9rem; }
th { color: var(--muted); font-weight: 600; font-size: 11px;
  text-transform: uppercase; letter-spacing: 0.6px; }
tr:last-child td { border-bottom: none; }
input, select, button { font: inherit; }
input, select { background: #fff; border: 1px solid var(--border);
  color: var(--ink); padding: 8px 10px; border-radius: 6px;
  transition: border-color 120ms ease, box-shadow 120ms ease; }
input:focus, select:focus { outline: none; border-color: var(--border-focus);
  box-shadow: 0 0 0 3px var(--primary-light); }
button { background: #fff; border: 1px solid var(--border); color: var(--ink);
  padding: 7px 12px; border-radius: 6px; cursor: pointer;
  transition: background 120ms ease, color 120ms ease, border-color 120ms ease; }
button:hover { background: var(--primary-light); border-color: var(--primary); color: var(--primary); }
button.primary { background: var(--primary); border-color: var(--primary); color: #fff; font-weight: 600; }
button.primary:hover { background: var(--primary-hover); border-color: var(--primary-hover); color: #fff; }
button.danger { background: var(--error); border-color: var(--error); color: #fff; font-weight: 600; }
button.danger:hover { background: #B91C1C; border-color: #B91C1C; color: #fff; }
.row { display: grid; grid-template-columns: 110px 1fr; gap: 0.5rem 1rem;
  align-items: center; margin-bottom: 0.6rem; }
.row label { color: var(--muted); font-size: 13px; font-weight: 500; }
.actions { display: flex; gap: 0.5rem; margin-top: 0.9rem; }
.kv { display: grid; grid-template-columns: 140px 1fr; gap: 0.4rem 1rem; font-size: 0.9rem; }
.kv span:nth-child(odd) { color: var(--muted); }
.pill { display: inline-flex; align-items: center; padding: 3px 10px;
  border-radius: 999px; font-size: 11px; font-weight: 600;
  letter-spacing: 0.3px; border: 1px solid var(--border); }
/* Soft-tinted pills (matches .msg.ok / .msg.err) — colored text on a
   tinted background reads better at 11 px than white-on-saturated. */
.pill.ok { background: var(--success-bg); color: var(--success); border-color: #BBD9C3; }
.pill.bad { background: var(--error-bg); color: var(--error); border-color: #FCA5A5; }
.msg { padding: 0.5rem 0.75rem; border-radius: 6px; margin-top: 0.6rem;
  font-size: 13px; display: none; }
.msg.ok { background: var(--success-bg); color: var(--success); border: 1px solid #BBD9C3; }
.msg.err { background: var(--error-bg); color: var(--error); border: 1px solid #FCA5A5; }
footer { text-align: center; color: var(--muted); font-size: 11px;
  letter-spacing: 0.5px; margin-top: 24px; padding: 16px 8px; }
a, a:visited { color: var(--primary); text-decoration: underline;
  text-underline-offset: 2px; }
a:hover { color: var(--primary-hover); }
.add-form { display: grid; grid-template-columns: 1fr 2fr auto;
  gap: 0.5rem; margin-top: 0.9rem; }
.del { background: #fff; border-color: var(--error); color: var(--error); }
.del:hover { background: var(--error-bg); border-color: var(--error); color: var(--error); }
.cmd-cell { white-space: nowrap; }
.cmd-cell button { padding: 0.3rem 0.45rem; min-width: 30px;
  margin-right: 2px; font-size: 0.85rem; }
.cmd-cell button.gear { margin-left: 0.5rem; }
.cmd-cell button:disabled { opacity: 0.4; cursor: wait; }
.pos-cell { white-space: nowrap; }
.pos-cell .pos-cur { display: inline-block; min-width: 3.5em;
  font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
  font-variant-numeric: tabular-nums; color: var(--ink); }
.pos-cell input { width: 4em; padding: 0.25rem 0.35rem; margin-left: 0.3rem; }
.pos-cell button { padding: 0.2rem 0.45rem; min-width: 26px;
  margin-left: 1px; font-size: 0.85rem; }
.setup-row td { background: var(--primary-light); padding: 0.7rem 0.9rem;
  border-bottom: 1px solid var(--border); }
.setup-panel { display: flex; flex-direction: column; gap: 0.55rem; }
.setup-group { display: flex; align-items: center; gap: 0.75rem; flex-wrap: wrap; }
.setup-group .setup-label { color: var(--primary); font-size: 11px;
  font-weight: 700; min-width: 84px; text-transform: uppercase;
  letter-spacing: 0.8px; }
.setup-group label { display: inline-flex; align-items: center; gap: 0.3rem;
  font-size: 0.85rem; color: var(--ink); }
.setup-group input[type="number"] { width: 4.5em; padding: 0.25rem 0.4rem; }
.setup-group select { padding: 0.25rem 1.6rem 0.25rem 0.5rem; }
.setup-group button { padding: 0.3rem 0.6rem; font-size: 0.85rem; }
.setup-group button.prog  { background: #fff; border-color: var(--primary); color: var(--primary); }
.setup-group button.prog:hover { background: var(--primary); color: #fff; }
.setup-group button.pair  { background: var(--success-bg); border-color: #BBD9C3; color: var(--success); }
.setup-group button.pair:hover { background: var(--success); border-color: var(--success); color: #fff; }
.setup-group button.erase { background: var(--error-bg); border-color: #FCA5A5; color: var(--error); }
.setup-group button.erase:hover { background: var(--error); border-color: var(--error); color: #fff; }
.setup-group button.sync  { background: #fff; }
.status-line { font-size: 13px; color: var(--muted); margin-bottom: 0.9rem;
  padding-bottom: 0.7rem; border-bottom: 1px solid var(--border); }
.status-line strong { color: var(--ink); font-weight: 600; }
.status-line .dot { display: inline-block; margin: 0 0.4rem; color: var(--border); }
.pass-wrap { display: flex; gap: 0.3rem; align-items: stretch; }
.pass-wrap input { flex: 1; min-width: 0; }
.pass-wrap button.eye { padding: 0 0.55rem; background: #fff; min-width: 36px;
  flex-shrink: 0; color: var(--muted);
  display: inline-flex; align-items: center; justify-content: center; }
.pass-wrap button.eye:hover { color: var(--primary); background: var(--primary-light); border-color: var(--primary); }
.pass-wrap button.eye[aria-pressed="true"] { background: var(--primary-light);
  color: var(--primary); border-color: var(--primary); }
.pass-wrap button.eye svg { display: block; }
.pass-wrap button.eye .eye-off { display: none; }
.pass-wrap button.eye[aria-pressed="true"] .eye-open { display: none; }
.pass-wrap button.eye[aria-pressed="true"] .eye-off  { display: block; }
.cards-row { display: grid; grid-template-columns: 1fr 1fr; gap: 14px;
  margin-bottom: 14px; align-items: start; }
.cards-row section { margin-bottom: 0; }
@media (max-width: 900px) { .cards-row { grid-template-columns: 1fr; gap: 0; } }
/* Inline help text under a form row, same vocabulary as the captive portal. */
.hint { display: block; font-size: 11px; color: var(--muted);
  margin-top: 4px; padding-left: calc(110px + 1rem); }
@media (max-width: 480px) { .hint { padding-left: 0; } }
progress { width: 100%; height: 10px; appearance: none;
  border: 1px solid var(--border); border-radius: 999px; overflow: hidden;
  background: #fff; }
progress::-webkit-progress-bar { background: #fff; }
progress::-webkit-progress-value { background: var(--primary);
  transition: width 120ms ease; }
progress::-moz-progress-bar { background: var(--primary); }
</style>
</head>
<body>
<header class="brand-header">
  <div class="brand">SOWEL</div>
  <div class="tag">Configuration du bridge Somfy RTS</div>
</header>
<h1>somfyrts2mqtt</h1>

<section>
  <h2>Status</h2>
  <div class="kv" id="status">
    <span>Version</span><span data-k="version">…</span>
    <span>Variant</span><span data-k="variant">…</span>
    <span>IP</span><span data-k="ip">…</span>
    <span>MAC</span><span data-k="mac">…</span>
    <span>Uptime</span><span data-k="uptime_s">…</span>
    <span>MQTT</span><span data-k="mqtt"><span class="pill">…</span></span>
    <span>Remotes</span><span data-k="remotes_count">…</span>
  </div>
</section>

<div class="cards-row">
<section>
  <h2>MQTT broker</h2>
  <form id="mqtt-form">
    <div class="row"><label>Host</label><input name="host" required maxlength="64"/></div>
    <div class="row"><label>Port</label><input name="port" type="number" min="1" max="65535" required/></div>
    <div class="row"><label>User</label><input name="user" maxlength="64"/></div>
    <div class="row"><label>Pass</label><div class="pass-wrap"><input name="pass" type="password" maxlength="64" placeholder="(unchanged)" autocomplete="off"/><button type="button" class="eye" title="Show/hide password" aria-pressed="false"><svg class="eye-open" viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/><circle cx="12" cy="12" r="3"/></svg><svg class="eye-off" viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M17.94 17.94A10.07 10.07 0 0 1 12 20c-7 0-11-8-11-8a18.45 18.45 0 0 1 5.06-5.94M9.9 4.24A9.12 9.12 0 0 1 12 4c7 0 11 8 11 8a18.5 18.5 0 0 1-2.16 3.19m-6.72-1.07a3 3 0 1 1-4.24-4.24"/><line x1="1" y1="1" x2="23" y2="23"/></svg></button></div></div>
    <div class="row"><label>Topic</label><input name="topic" maxlength="64" pattern="[a-zA-Z0-9_/-]{0,64}" title="MQTT root topic. Empty = default 'somfyrts2mqtt'. Set distinct topics if you run multiple bridges on the same broker. alnum, _, -, / ; no leading/trailing slash, no MQTT wildcards."/></div>
    <div class="row"><label></label><div id="topic-active" style="color:var(--muted);font-size:0.85rem;">…</div></div>
    <div class="actions"><button class="primary" type="submit">Save</button></div>
    <div class="msg" id="mqtt-msg"></div>
  </form>
</section>

<section>
  <h2>WiFi</h2>
  <div class="status-line">
    <div>Connected to <strong id="wifi-current">…</strong></div>
    <div>RSSI <strong id="wifi-rssi">…</strong></div>
  </div>
  <form id="wifi-form">
    <div class="row"><label>SSID</label><input name="ssid" required maxlength="32" autocomplete="off"/></div>
    <div class="row"><label>Password</label><div class="pass-wrap"><input name="pass" type="password" maxlength="64" placeholder="(unchanged)" autocomplete="off"/><button type="button" class="eye" title="Show/hide password" aria-pressed="false"><svg class="eye-open" viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/><circle cx="12" cy="12" r="3"/></svg><svg class="eye-off" viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M17.94 17.94A10.07 10.07 0 0 1 12 20c-7 0-11-8-11-8a18.45 18.45 0 0 1 5.06-5.94M9.9 4.24A9.12 9.12 0 0 1 12 4c7 0 11 8 11 8a18.5 18.5 0 0 1-2.16 3.19m-6.72-1.07a3 3 0 1 1-4.24-4.24"/><line x1="1" y1="1" x2="23" y2="23"/></svg></button></div></div>
    <span class="hint">Leave the password empty if you are keeping the same SSID.</span>
    <div class="actions"><button class="primary" type="submit">Save and reconnect</button></div>
    <div class="msg" id="wifi-msg"></div>
  </form>
</section>
</div>

<section>
  <h2>Remotes</h2>
  <div class="table-wrap">
    <table><thead><tr><th>ID</th><th>Name</th><th>Code</th><th>Position</th><th>Commands</th><th></th></tr></thead><tbody id="remotes-body"></tbody></table>
  </div>
  <form id="remote-form" class="add-form">
    <input name="id_hex" placeholder="A1B2C3" pattern="[0-9A-Fa-f]{6}" required maxlength="6"/>
    <input name="name" placeholder="kitchen_shutter" pattern="[a-zA-Z0-9_-]{1,32}" required maxlength="32" title="MQTT-safe: letters, digits, _, - ; no spaces or special chars"/>
    <button class="primary" type="submit">Add</button>
  </form>
  <div class="msg" id="remotes-msg"></div>
</section>

<section>
  <h2>Update firmware</h2>
  <div class="status-line">
    Download the latest <code>.bin</code> from <a href="https://github.com/mchacher/somfyrts2mqtt/releases/latest" target="_blank" rel="noopener">GitHub Releases</a>, then upload it here. The bridge reboots automatically after a successful update.
  </div>
  <form id="ota-form">
    <input type="file" name="firmware" accept=".bin" required/>
    <button class="primary" type="submit">Upload &amp; reboot</button>
  </form>
  <progress id="ota-progress" value="0" max="100" hidden></progress>
  <div class="msg" id="ota-msg"></div>
</section>

<section>
  <h2>Danger zone</h2>
  <button class="danger" id="factory-btn">Factory reset</button>
  <div class="msg" id="factory-msg"></div>
</section>

<footer>somfyrts2mqtt &middot; LAN only</footer>

<script>
const $ = (s) => document.querySelector(s);
const fmtUptime = (s) => { const h=Math.floor(s/3600), m=Math.floor(s/60)%60, sec=s%60; return `${h}h ${m}m ${sec}s`; };
const show = (id, type, text) => { const el = $(id); el.className = `msg ${type}`; el.textContent = text; el.style.display = 'block'; setTimeout(()=>{el.style.display='none';}, 4000); };

async function fetchJSON(url, opts) {
  const r = await fetch(url, opts);
  if (!r.ok) { const t = await r.text(); throw new Error(t || r.statusText); }
  return r.status === 204 ? null : r.json();
}

async function loadStatus() {
  const s = await fetchJSON('/api/status');
  for (const [k, v] of Object.entries(s)) {
    const el = document.querySelector(`#status [data-k="${k}"]`);
    if (!el) continue;
    if (k === 'uptime_s') el.textContent = fmtUptime(v);
    else if (k === 'mqtt') el.innerHTML = `<span class="pill ${s.mqtt_connected?'ok':'bad'}">${s.mqtt_connected?'connected':'disconnected'}</span>`;
    else el.textContent = v;
  }
  document.querySelector('#status [data-k="mqtt"]').innerHTML = `<span class="pill ${s.mqtt_connected?'ok':'bad'}">${s.mqtt_connected?'connected':'disconnected'}</span>`;
}

// 8-char dummy used to visually indicate a stored password without leaking
// its value. The submit handlers detect "value still equals the dummy" and
// send an empty pass in that case -- the backend interprets that as "keep
// the stored password".
const PASS_DUMMY = '••••••••';

function fillPass(input, isSet) {
  input.value = isSet ? PASS_DUMMY : '';
  // Clear the dummy on first interaction so the user types onto an empty
  // field instead of fighting with the bullets.
  input.onfocus = () => { if (input.value === PASS_DUMMY) input.value = ''; };
}

async function loadMqtt() {
  const m = await fetchJSON('/api/mqtt');
  for (const k of ['host','port','user','topic']) $(`#mqtt-form [name="${k}"]`).value = m[k] ?? '';
  fillPass($(`#mqtt-form [name="pass"]`), !!m.pass_set);
  const active = m.topic_active ?? '';
  const stored = (m.topic ?? '').trim();
  $('#topic-active').textContent = stored
      ? `In use: ${active}`
      : `In use: ${active} (default)`;
}

async function loadWifi() {
  const w = await fetchJSON('/api/wifi');
  const ssid = w.ssid ?? '';
  const rssi = (w.rssi ?? 0);
  $('#wifi-current').textContent = ssid || '(disconnected)';
  $('#wifi-rssi').textContent    = ssid ? `${rssi} dBm` : '—';
  // Pre-fill the SSID field with the currently connected one so the user
  // only needs to type a new password (most common reconfigure case: same
  // SSID, password rotation).
  $(`#wifi-form [name="ssid"]`).value = ssid;
  fillPass($(`#wifi-form [name="pass"]`), !!w.pass_set);
}

// Wire the eye toggles on every password input. Default state : input is
// type=password (masked) and the button reads aria-pressed=false. Click
// flips to type=text and aria-pressed=true (the CSS highlights the button).
// Idempotent : safe to re-run on subsequent loads.
function wirePasswordToggles() {
  document.querySelectorAll('button.eye').forEach(b => {
    if (b.dataset.wired) return;
    b.dataset.wired = '1';
    b.onclick = () => {
      const inp = b.parentElement.querySelector('input');
      const reveal = inp.type === 'password';
      inp.type = reveal ? 'text' : 'password';
      b.setAttribute('aria-pressed', reveal ? 'true' : 'false');
    };
  });
}

let motionActive = false;
async function loadRemotes() {
  // If the user is currently editing a setup-row input (typically calibrating
  // open/close durations while a shutter is moving), skip this refresh tick.
  // The 1 Hz refresh during motion would otherwise wipe the input value the
  // user is still typing into via `body.innerHTML = ''` below.
  const focused = document.activeElement;
  if (focused && focused.closest && focused.closest('.setup-row')) {
    return;
  }
  const remotes = await fetchJSON('/api/remotes');
  motionActive = remotes.some(r => (r.direction || 0) !== 0);
  // Preserve which Setup panels are open so the 1 Hz refresh during motion
  // does not collapse a panel the user has just expanded.
  const expanded = new Set();
  document.querySelectorAll('#remotes-body tr.setup-row').forEach(tr => {
    if (tr.style.display !== 'none') expanded.add(tr.dataset.id);
  });
  const body = $('#remotes-body');
  body.innerHTML = '';
  if (remotes.length === 0) {
    body.innerHTML = '<tr><td colspan="6" style="color:var(--muted);text-align:center">No remotes yet</td></tr>';
    return;
  }
  const dirIcon = (d) => d === 1 ? '&#9650;' : d === -1 ? '&#9660;' : '&mdash;';
  for (const r of remotes) {
    const tr = document.createElement('tr');
    tr.className = 'main-row';
    tr.dataset.id = r.id_hex;
    // iter 022 : a Gate is a single-button toggle, blind (no position feedback).
    const isGate = (r.device_type || 0) !== 0;
    const posCell = isGate
      ? `<td class="pos-cell"><span class="pos-cur" title="Gate — no position feedback">&mdash;</span></td>`
      : `<td class="pos-cell">` +
        `  <span class="pos-cur">${r.position ?? 0}% ${dirIcon(r.direction || 0)}</span>` +
        `  <input type="number" min="0" max="100" step="1" value="${r.position ?? 0}"/>` +
        `  <button data-action="move" title="Move to target %">&rarr;</button>` +
        `</td>`;
    const cmdCell = isGate
      ? `<td class="cmd-cell">` +
        `<button data-cmd="toggle" class="toggle" title="Toggle (open/stop/close cycle)">&#128260;</button>` +
        `<button class="gear" title="Settings &amp; pairing">&#9881;</button>` +
        `</td>`
      : `<td class="cmd-cell">` +
        `<button data-cmd="up"   title="Up">&#9650;</button>` +
        `<button data-cmd="stop" title="Stop">&#9632;</button>` +
        `<button data-cmd="down" title="Down">&#9660;</button>` +
        `<button class="gear" title="Calibration &amp; pairing">&#9881;</button>` +
        `</td>`;
    tr.innerHTML = `<td><code>${r.id_hex}</code></td><td>${r.name}</td><td>${r.rolling_code}</td>` +
      posCell + cmdCell +
      `<td><button class="del">&times;</button></td>`;
    body.appendChild(tr);

    const trSetup = document.createElement('tr');
    trSetup.className = 'setup-row';
    trSetup.dataset.id = r.id_hex;
    trSetup.style.display = expanded.has(r.id_hex) ? '' : 'none';
    trSetup.innerHTML =
      `<td colspan="6">` +
      `<div class="setup-panel">` +
      `  <div class="setup-group">` +
      `    <span class="setup-label">Device</span>` +
      `    <label>Type <select class="dtype">` +
      `      <option value="0"${(r.device_type||0)==0?' selected':''}>Shutter</option>` +
      `      <option value="1"${(r.device_type||0)==1?' selected':''}>Gate (portail)</option>` +
      `    </select></label>` +
      `    <label class="gate-only">Toggle button <select class="tgbtn">` +
      `      <option value="2"${(r.toggle_button||2)==2?' selected':''}>Up &#9650;</option>` +
      `      <option value="1"${(r.toggle_button||2)==1?' selected':''}>My/Stop &#9632;</option>` +
      `      <option value="4"${(r.toggle_button||2)==4?' selected':''}>Down &#9660;</option>` +
      `    </select></label>` +
      `    <label class="cal-only">Open <input class="dur-open" type="number" min="0" max="300" step="0.1" value="${(r.open_duration_s||0).toFixed(1)}"/> s</label>` +
      `    <label class="cal-only">Close <input class="dur-close" type="number" min="0" max="300" step="0.1" value="${(r.close_duration_s||0).toFixed(1)}"/> s</label>` +
      `    <label class="cal-only"><input class="inv" type="checkbox" ${r.invert ? 'checked' : ''}/> Invert Up/Down</label>` +
      `    <button class="sync cal-only" title="Snap state to the % in the Position cell, no RF">&#127919; Sync to %</button>` +
      `  </div>` +
      `  <div class="setup-group">` +
      `    <span class="setup-label">Pairing</span>` +
      `    <button class="prog"  data-cmd="program"   title="PROG brief - confirm pair / delete when motor is in mode">&#128279; Prog</button>` +
      `    <button class="pair"  data-cmd="program3s" title="PROG 3 s - put motor in pair mode">&#10133; Pair</button>` +
      `    <button class="erase" data-cmd="program7s" title="PROG 7 s - put motor in erase mode (requires a different TARGET remote)">&#128465; Erase</button>` +
      `  </div>` +
      `</div>` +
      `</td>`;
    body.appendChild(trSetup);
  }

  // Gear toggle : expand / collapse the per-row Setup panel.
  body.querySelectorAll('button.gear').forEach(b => b.onclick = () => {
    const id = b.closest('tr').dataset.id;
    const setupRow = body.querySelector(`tr.setup-row[data-id="${id}"]`);
    setupRow.style.display = (setupRow.style.display === 'none') ? '' : 'none';
  });

  // Calibration : durations commit on blur. Convert seconds -> ms for the API.
  body.querySelectorAll('.setup-row input.dur-open, .setup-row input.dur-close').forEach(inp => inp.onchange = async () => {
    const id   = inp.closest('tr').dataset.id;
    const kind = inp.classList.contains('dur-open') ? 'open' : 'close';
    const ms   = Math.round(parseFloat(inp.value || '0') * 1000);
    const path = kind === 'open' ? 'open_duration_ms' : 'close_duration_ms';
    try { await fetchJSON(`/api/remotes/${id}/${path}/${ms}`, {method:'POST'}); show('#remotes-msg','ok',`${kind} duration saved`); }
    catch (e) { show('#remotes-msg','err', e.message); }
  });

  // Calibration : invert toggle commits on change.
  body.querySelectorAll('.setup-row input.inv').forEach(inp => inp.onchange = async () => {
    const id = inp.closest('tr').dataset.id;
    const v  = inp.checked ? 1 : 0;
    try { await fetchJSON(`/api/remotes/${id}/invert/${v}`, {method:'POST'}); show('#remotes-msg','ok','invert saved'); }
    catch (e) { show('#remotes-msg','err', e.message); inp.checked = !inp.checked; }
  });

  // Device type : select commits on change. Shutter-only controls (durations,
  // invert, Sync) hide live for a Gate, and the Gate's Toggle-button selector
  // shows; the main row is refreshed so the Position cell / command buttons
  // reflect the new type.
  body.querySelectorAll('.setup-row select.dtype').forEach(sel => {
    const applyVis = () => {
      const gate = sel.value !== '0';
      const row = sel.closest('tr');
      row.querySelectorAll('.cal-only').forEach(el => el.style.display = gate ? 'none' : '');
      row.querySelectorAll('.gate-only').forEach(el => el.style.display = gate ? '' : 'none');
    };
    applyVis();
    sel.onchange = async () => {
      const id = sel.closest('tr').dataset.id;
      try {
        await fetchJSON(`/api/remotes/${id}/type/${sel.value}`, {method:'POST'});
        show('#remotes-msg','ok','type saved');
        applyVis();
        await loadRemotes();
      } catch (e) { show('#remotes-msg','err', e.message); }
    };
  });

  // Gate : toggle-button selector commits on change.
  body.querySelectorAll('.setup-row select.tgbtn').forEach(sel => sel.onchange = async () => {
    const id = sel.closest('tr').dataset.id;
    try { await fetchJSON(`/api/remotes/${id}/toggle_button/${sel.value}`, {method:'POST'}); show('#remotes-msg','ok','toggle button saved'); }
    catch (e) { show('#remotes-msg','err', e.message); }
  });

  // Calibration : Sync = set_position (no RF). Reads the target from the
  // main row's Position cell input -- avoids duplicating an input.
  body.querySelectorAll('.setup-row button.sync').forEach(b => b.onclick = async () => {
    const id = b.closest('tr').dataset.id;
    const mainRow = body.querySelector(`tr.main-row[data-id="${id}"]`);
    const v = Math.max(0, Math.min(100, parseInt(mainRow.querySelector('.pos-cell input').value || '0', 10)));
    try {
      await fetchJSON(`/api/remotes/${id}/set_position/${v}`, {method:'POST'});
      setTimeout(loadRemotes, 800);
    } catch (e) { show('#remotes-msg','err', e.message); }
  });

  // Position cell : Move = position via RF.
  body.querySelectorAll('.pos-cell button[data-action="move"]').forEach(b => b.onclick = async () => {
    const id   = b.closest('tr').dataset.id;
    const cell = b.parentElement;
    const v    = Math.max(0, Math.min(100, parseInt(cell.querySelector('input').value || '0', 10)));
    try {
      await fetchJSON(`/api/remotes/${id}/position/${v}`, {method:'POST'});
      // No fixed wait : tick() will publish telemetry. Just refresh after a moment.
      setTimeout(loadRemotes, 800);
    } catch (e) { show('#remotes-msg','err', e.message); }
  });

  body.querySelectorAll('button.del').forEach(b => b.onclick = async () => {
    const id = b.closest('tr').dataset.id;
    if (!confirm(`Delete remote ${id}?`)) return;
    try { await fetchJSON(`/api/remotes/${id}`, {method:'DELETE'}); await loadRemotes(); await loadStatus(); }
    catch (e) { show('#remotes-msg', 'err', e.message); }
  });

  // Estimated RF emission duration per command, in ms. Matches the
  // repeat counts in handle_post_command: ~143 ms per repeat frame
  // plus a ~220 ms initial frame. Used to keep the row's buttons
  // visually "busy" while the emission runs in the background -- the
  // HTTP queue returns in ~50 ms, much faster than the actual TX.
  const TX_MS = { up: 400, down: 400, stop: 400, toggle: 400, program: 800,
                  program3s: 3300, program7s: 7500 };
  // Any button with data-cmd : up/stop/down in the main row, prog/pair/erase
  // in the Setup panel. Unified handler -- the data-cmd attribute is the
  // single source of truth, regardless of which row the button lives in.
  body.querySelectorAll('button[data-cmd]').forEach(b => b.onclick = async () => {
    const id = b.closest('tr').dataset.id;
    const cmd = b.dataset.cmd;
    // Erase requires a 2-remote workflow (Somfy excludes the issuing remote
    // from deletion candidates). Without this gate, users silently fall into
    // a self-erase attempt that the motor cannot honor.
    if (cmd === 'program7s' && !confirm(
        `Erase workflow\n\n` +
        `1. This emits a 7 s long-press from ${id} (SOURCE).\n` +
        `2. The motor will jog after ~7 s and waits ~10 s.\n` +
        `3. Within that window, click Prog briefly on the TARGET ` +
        `remote (different from ${id}) to erase it.\n\n` +
        `Somfy forbids self-erase: the target must be a different ` +
        `already-paired remote.\n\nProceed?`)) {
      return;
    }
    const siblings = b.parentElement.querySelectorAll('button');
    siblings.forEach(x => x.disabled = true);
    try {
      await fetchJSON(`/api/remotes/${id}/${cmd}`, {method:'POST'});
      // Wait for the RF emission to actually finish before re-rendering.
      // Keeps the user from re-clicking mid-erase (a 7s window).
      await new Promise(r => setTimeout(r, TX_MS[cmd] || 400));
      await loadRemotes();   // re-render replaces the disabled buttons with fresh enabled ones
    } catch (e) {
      show('#remotes-msg', 'err', e.message);
      siblings.forEach(x => x.disabled = false);
    }
  });
}

// If the password field still equals the dummy mask, the user did not edit
// it -- send empty so the backend keeps the stored value.
const passOrEmpty = (v) => (v === PASS_DUMMY) ? '' : (v || '');

$('#mqtt-form').onsubmit = async (e) => {
  e.preventDefault();
  const fd = new FormData(e.target);
  const body = {
    host:  fd.get('host'),
    port:  Number(fd.get('port')),
    user:  fd.get('user'),
    pass:  passOrEmpty(fd.get('pass')),
    topic: fd.get('topic')
  };
  try { await fetchJSON('/api/mqtt', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify(body)}); show('#mqtt-msg','ok','Saved; reconnecting…'); setTimeout(() => { loadStatus(); loadMqtt(); }, 6000); }
  catch (err) { show('#mqtt-msg','err', err.message); }
};

$('#wifi-form').onsubmit = async (e) => {
  e.preventDefault();
  const fd = new FormData(e.target);
  const ssid = (fd.get('ssid') || '').trim();
  const pass = passOrEmpty(fd.get('pass'));
  if (!ssid) { show('#wifi-msg','err','SSID required'); return; }
  if (!confirm(`Save WiFi creds and reboot?\n\nSSID: ${ssid}\n\nIf the new network is unreachable, the box will retry forever -- recover via 4 quick power-cycles.`)) return;
  try {
    await fetchJSON('/api/wifi', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify({ssid, pass})});
    show('#wifi-msg','ok','Saved; rebooting…');
  } catch (err) { show('#wifi-msg','err', err.message); }
};

$('#remote-form').onsubmit = async (e) => {
  e.preventDefault();
  const fd = new FormData(e.target);
  const body = { id_hex: fd.get('id_hex').toUpperCase(), name: fd.get('name') };
  try { await fetchJSON('/api/remotes', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify(body)}); e.target.reset(); await loadRemotes(); await loadStatus(); }
  catch (err) { show('#remotes-msg','err', err.message); }
};

$('#factory-btn').onclick = async () => {
  if (!confirm('Wipe all NVS and reboot?')) return;
  if (!confirm('Really? This deletes MQTT config and every remote.')) return;
  try { await fetchJSON('/api/factory_reset', {method:'POST'}); show('#factory-msg','ok','Rebooting…'); setTimeout(()=>location.reload(), 6000); }
  catch (err) { show('#factory-msg','err', err.message); }
};

// iter 016 : WebOTA firmware upload. XHR (not fetch) because we need the
// upload-progress events to drive the <progress> bar. Two confirm() steps
// so a misclick can never trigger a flash. On success, the bridge reboots ;
// the page reloads ~6 s later to show the new version in the Status card.
$('#ota-form').onsubmit = (e) => {
  e.preventDefault();
  const file = e.target.firmware.files[0];
  if (!file) return;
  if (!confirm(`Flash ${file.name} (${(file.size/1024).toFixed(0)} KB) ?`)) return;
  if (!confirm('Really ? The bridge will reboot. A bad binary can brick the device.')) return;

  const form = new FormData();
  form.append('firmware', file);

  const progress = $('#ota-progress');
  const submit = e.target.querySelector('button[type=submit]');
  submit.disabled = true;
  progress.hidden = false;
  progress.value = 0;

  const xhr = new XMLHttpRequest();
  xhr.upload.onprogress = (ev) => {
    if (ev.lengthComputable) progress.value = Math.round((ev.loaded / ev.total) * 100);
  };
  xhr.onload = () => {
    if (xhr.status === 200) {
      show('#ota-msg', 'ok', 'Upload OK, rebooting…');
      setTimeout(() => location.reload(), 8000);
    } else {
      let msg = `Upload failed (${xhr.status})`;
      try { const j = JSON.parse(xhr.responseText); if (j.error) msg = j.error; } catch (_) {}
      show('#ota-msg', 'err', msg);
      submit.disabled = false;
      progress.hidden = true;
    }
  };
  xhr.onerror = () => {
    show('#ota-msg', 'err', 'Network error during upload');
    submit.disabled = false;
    progress.hidden = true;
  };
  xhr.open('POST', '/api/firmware/upload');
  xhr.send(form);
};

(async () => {
  await loadStatus(); await loadMqtt(); await loadWifi(); await loadRemotes();
  wirePasswordToggles();
  setInterval(loadStatus, 5000);
  // 1 Hz refresh of the Remotes table, but only when a shutter is moving --
  // matches the orchestrator's tick cadence so the UI tracks live position
  // without burning broker / network at idle.
  setInterval(() => { if (motionActive) loadRemotes(); }, 1000);
})();
</script>
</body>
</html>)HTML";

  // --- helpers ---

  static void send_json(AsyncWebServerRequest* req, int code, const JsonDocument& doc) {
    String body;
    serializeJson(doc, body);
    req->send(code, "application/json", body);
  }

  static void send_error(AsyncWebServerRequest* req, int code, const char* msg) {
    JsonDocument doc;
    doc["error"] = msg;
    send_json(req, code, doc);
  }

  static void format_id_hex_upper(uint32_t id, char out[7]) {
    nvs_store::format_id_hex(id, out);
  }

  // --- handlers ---

  static void handle_index(AsyncWebServerRequest* req) {
    AsyncWebServerResponse* res = req->beginResponse_P(200, "text/html", INDEX_HTML);
    res->addHeader("Cache-Control", "no-cache");
    req->send(res);
  }

  static void handle_get_status(AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["version"]         = FW_VERSION;
    doc["variant"]         = FW_VARIANT;
    doc["ip"]              = WiFi.localIP().toString();
    doc["mac"]             = WiFi.macAddress();
    doc["uptime_s"]        = static_cast<uint32_t>(millis() / 1000UL);
    doc["mqtt_connected"]  = mqtt::is_connected();
    doc["remotes_count"]   = static_cast<uint32_t>(nvs_store::remotes_count());
    send_json(req, 200, doc);
  }

  static void handle_get_mqtt(AsyncWebServerRequest* req) {
    const nvs_store::MqttConfig cfg = nvs_store::get_mqtt();
    JsonDocument doc;
    doc["host"]     = cfg.host;
    doc["port"]     = cfg.port;
    doc["user"]     = cfg.user;
    // pass value intentionally omitted ; signal presence so the UI can
    // pre-fill the input with a dummy mask placeholder.
    doc["pass_set"] = !cfg.pass.empty();
    doc["topic"]    = cfg.topic;                  // empty = "use default"
    doc["topic_active"] = mqtt::get_root_topic(); // what the bridge is actually using
    send_json(req, 200, doc);
  }

  static void handle_post_mqtt(AsyncWebServerRequest* req, JsonVariant& json) {
    if (!json.is<JsonObject>()) return send_error(req, 400, "expected JSON object");
    const JsonObject body = json.as<JsonObject>();

    nvs_store::MqttConfig cfg = nvs_store::get_mqtt();
    if (body["host"].is<const char*>()) cfg.host = body["host"].as<const char*>();
    if (body["user"].is<const char*>()) cfg.user = body["user"].as<const char*>();
    // Empty password from the UI = "keep current"; only overwrite if non-empty.
    if (body["pass"].is<const char*>()) {
      const char* p = body["pass"].as<const char*>();
      if (p && p[0] != '\0') cfg.pass = p;
    }
    if (body["port"].is<uint16_t>()) cfg.port = body["port"].as<uint16_t>();
    else if (body["port"].is<int>()) {
      const int p = body["port"].as<int>();
      if (p < 1 || p > 65535) return send_error(req, 400, "port out of range");
      cfg.port = static_cast<uint16_t>(p);
    }
    // Topic : empty string = "use runtime default" ; non-empty must be valid.
    if (body["topic"].is<const char*>()) {
      const char* t = body["topic"].as<const char*>();
      const std::string ts = (t == nullptr) ? std::string{} : std::string(t);
      if (!ts.empty() && !nvs_store::is_valid_topic(ts))
        return send_error(req, 400, "invalid topic (alnum, _, -, /, no leading/trailing slash, no MQTT wildcards)");
      cfg.topic = ts;
    }

    if (cfg.host.empty() || cfg.host.size() > 64) return send_error(req, 400, "host empty or too long");
    if (cfg.port == 0) return send_error(req, 400, "port must be > 0");

    if (!nvs_store::set_mqtt(cfg)) return send_error(req, 500, "set_mqtt failed");
    mqtt::disconnect();
    req->send(204);
    logger::info("web", "mqtt config updated, reconnect triggered");
  }

  // --- WiFi (iter 015) ---

  static void handle_get_wifi(AsyncWebServerRequest* req) {
    JsonDocument doc;
    // The connected SSID is the ground truth -- not the stored one, which
    // could legitimately differ if someone reconfigured via Serial / a stale
    // dev workflow. Empty when disconnected (e.g. during reconnect window).
    const String ssid = WiFi.SSID();
    doc["ssid"] = ssid.c_str();
    doc["rssi"] = WiFi.RSSI();
    // Pass value intentionally omitted ; signal presence so the UI can
    // pre-fill the input with a dummy mask placeholder.
    doc["pass_set"] = !nvs_store::get_wifi_pass().empty();
    send_json(req, 200, doc);
  }

  static void handle_post_wifi(AsyncWebServerRequest* req, JsonVariant& json) {
    if (!json.is<JsonObject>()) return send_error(req, 400, "expected JSON object");
    const JsonObject body = json.as<JsonObject>();

    if (!body["ssid"].is<const char*>()) return send_error(req, 400, "ssid required");
    const char* ssid_c = body["ssid"].as<const char*>();
    const std::string ssid = (ssid_c == nullptr) ? std::string{} : std::string(ssid_c);
    if (ssid.empty() || ssid.size() > 32)
      return send_error(req, 400, "ssid empty or too long (1..32)");

    // Empty password from the UI = "keep current" if SSID matches the stored
    // one (common case : rotate password only). Refuse if it's a new SSID --
    // the user must enter the password for a new network.
    std::string pass;
    if (body["pass"].is<const char*>()) {
      const char* p = body["pass"].as<const char*>();
      pass = (p == nullptr) ? std::string{} : std::string(p);
    }
    if (pass.size() > 64) return send_error(req, 400, "password too long (max 64)");
    if (pass.empty()) {
      const std::string stored_ssid = nvs_store::get_wifi_ssid();
      if (stored_ssid != ssid)
        return send_error(req, 400, "password required for new SSID");
      pass = nvs_store::get_wifi_pass();
    }

    if (!nvs_store::set_wifi_creds(ssid, pass))
      return send_error(req, 500, "set_wifi_creds failed");

    req->send(204);
    logger::warn("web", "wifi creds updated ssid=%s, rebooting in 1 s",
                 ssid.c_str());
    // Defer the reboot so the 204 actually flushes over the LAN before STA
    // tears down.
    delay(1000);
    ESP.restart();
  }

  static void handle_get_remotes(AsyncWebServerRequest* req) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    nvs_store::Remote buf[nvs_store::MAX_REMOTES];
    const size_t n = nvs_store::list_remotes(buf, nvs_store::MAX_REMOTES);
    for (size_t i = 0; i < n; ++i) {
      JsonObject o = arr.add<JsonObject>();
      char hex[7];
      format_id_hex_upper(buf[i].id, hex);
      o["id_hex"]            = hex;
      o["rolling_code"]      = buf[i].rolling_code;
      o["name"]              = buf[i].name;
      o["open_duration_s"]   = buf[i].open_time_ms  / 1000.0;
      o["close_duration_s"]  = buf[i].close_time_ms / 1000.0;
      o["invert"]            = buf[i].invert;
      o["device_type"]       = buf[i].device_type;
      o["toggle_button"]     = buf[i].toggle_button;
      const orchestrator::RuntimeState rt = orchestrator::get_runtime(buf[i].id);
      o["position"]          = rt.position;
      o["direction"]         = rt.direction;
      o["target"]            = rt.target;
    }
    send_json(req, 200, doc);
  }

  static void handle_post_remote(AsyncWebServerRequest* req, JsonVariant& json) {
    if (!json.is<JsonObject>()) return send_error(req, 400, "expected JSON object");
    const JsonObject body = json.as<JsonObject>();

    if (!body["id_hex"].is<const char*>() || !body["name"].is<const char*>())
      return send_error(req, 400, "id_hex and name required");

    const char* id_hex = body["id_hex"].as<const char*>();
    uint32_t id = 0;
    if (!nvs_store::parse_id_hex(id_hex, id) || !nvs_store::is_valid_id(id))
      return send_error(req, 400, "invalid id_hex");

    const std::string name = body["name"].as<const char*>();
    if (!nvs_store::is_valid_name(name)) return send_error(req, 400, "invalid name (1..32 chars)");

    nvs_store::Remote existing;
    const bool already = nvs_store::get_remote(id, existing);
    const uint16_t code = already ? existing.rolling_code : 0;

    if (!already && nvs_store::remotes_count() >= nvs_store::MAX_REMOTES)
      return send_error(req, 409, "capacity reached (16 remotes max)");

    if (!nvs_store::add_remote(id, code, name)) return send_error(req, 500, "add_remote failed");

    JsonDocument resp;
    char hex[7];
    format_id_hex_upper(id, hex);
    resp["id_hex"]       = hex;
    resp["rolling_code"] = code;
    resp["name"]         = name;
    send_json(req, 201, resp);
    logger::info("web", "remote +%s name=%s", hex, name.c_str());
  }

  static void handle_delete_remote(AsyncWebServerRequest* req) {
    const String hex_param = req->pathArg(0);
    uint32_t id = 0;
    if (!nvs_store::parse_id_hex(hex_param.c_str(), id) || !nvs_store::is_valid_id(id))
      return send_error(req, 400, "invalid id_hex");
    if (!nvs_store::delete_remote(id)) return send_error(req, 404, "not found");
    req->send(204);
    logger::info("web", "remote -%s", hex_param.c_str());
  }

  static void handle_post_command(AsyncWebServerRequest* req) {
    const String hex_param = req->pathArg(0);
    const String cmd_param = req->pathArg(1);

    uint32_t id = 0;
    if (!nvs_store::parse_id_hex(hex_param.c_str(), id) || !nvs_store::is_valid_id(id))
      return send_error(req, 400, "invalid id_hex");

    nvs_store::Remote existing;
    if (!nvs_store::get_remote(id, existing))
      return send_error(req, 404, "remote not found");

    // Long-press PROG variants: "program3s" (~3 s, ~21 repeats) puts a Somfy
    // motor in pair mode ; "program7s" (~7 s, ~50 repeats) puts it in erase
    // mode. All web-UI commands go through the async queue so the HTTP
    // response returns in milliseconds, not seconds.
    mqtt::Command cmd;
    int repeat_override = -1;
    if (cmd_param == "program3s") {
      cmd = mqtt::Command::Program;
      repeat_override = 21;
    } else if (cmd_param == "program7s") {
      cmd = mqtt::Command::Program;
      repeat_override = 50;
    } else if (cmd_param == "toggle") {
      cmd = mqtt::Command::Toggle;  // iter 022 : Gate single-button cycle
    } else {
      cmd = orchestrator::command_from_str(cmd_param.c_str());
      if (cmd == mqtt::Command::Invalid)
        return send_error(req, 400, "invalid cmd");
    }

    if (!orchestrator::enqueue_command(id, cmd, repeat_override))
      return send_error(req, 503, "queue full, try again");
    req->send(204);
  }

  // iter 014 : per-remote numeric setters. URL pattern carries the value
  // as a path segment so we keep the existing query-less / body-less style
  // of the command POST endpoint.
  static void handle_post_value(AsyncWebServerRequest* req) {
    const String hex_param   = req->pathArg(0);
    const String action      = req->pathArg(1);
    const String value_str   = req->pathArg(2);

    uint32_t id = 0;
    if (!nvs_store::parse_id_hex(hex_param.c_str(), id) || !nvs_store::is_valid_id(id))
      return send_error(req, 400, "invalid id_hex");

    nvs_store::Remote existing;
    if (!nvs_store::get_remote(id, existing))
      return send_error(req, 404, "remote not found");

    const long value = std::strtol(value_str.c_str(), nullptr, 10);

    if (action == "position") {
      if (value < 0 || value > 100) return send_error(req, 400, "value out of range (0..100)");
      orchestrator::set_position(id, static_cast<uint8_t>(value));
    } else if (action == "set_position") {
      if (value < 0 || value > 100) return send_error(req, 400, "value out of range (0..100)");
      orchestrator::set_calibration_position(id, static_cast<uint8_t>(value));
    } else if (action == "open_duration_ms") {
      if (value < 0 || value > 3600000) return send_error(req, 400, "value out of range");
      orchestrator::set_open_duration(id, static_cast<uint32_t>(value));
    } else if (action == "close_duration_ms") {
      if (value < 0 || value > 3600000) return send_error(req, 400, "value out of range");
      orchestrator::set_close_duration(id, static_cast<uint32_t>(value));
    } else if (action == "invert") {
      if (value != 0 && value != 1) return send_error(req, 400, "value must be 0 or 1");
      if (!nvs_store::set_invert(id, value != 0))
        return send_error(req, 500, "set_invert failed");
    } else if (action == "type") {
      // iter 022 : device type. 0 = Shutter, 1 = Gate.
      if (value < 0 || value > 1) return send_error(req, 400, "value must be 0 (shutter) or 1 (gate)");
      if (!nvs_store::set_device_type(id, static_cast<uint8_t>(value)))
        return send_error(req, 500, "set_device_type failed");
      mqtt::publish_sensor_aggregated();  // reflect the new Type hint in the retained SENSOR
    } else if (action == "toggle_button") {
      // iter 022 : gate toggle RTS button. 0x01 My / 0x02 Up / 0x04 Down.
      if (value != 1 && value != 2 && value != 4)
        return send_error(req, 400, "value must be 1 (My), 2 (Up) or 4 (Down)");
      if (!nvs_store::set_toggle_button(id, static_cast<uint8_t>(value)))
        return send_error(req, 500, "set_toggle_button failed");
    } else {
      return send_error(req, 400, "unknown action");
    }
    req->send(204);
  }

  static void handle_factory_reset(AsyncWebServerRequest* req) {
    nvs_store::factory_reset();
    req->send(204);
    logger::warn("web", "factory reset triggered; rebooting in 500 ms");
    // Defer the reboot so the response actually flushes.
    delay(500);
    ESP.restart();
  }

  // iter 016 : WebOTA firmware upload.
  //
  // AsyncWebServer streams the multipart body chunk-by-chunk to the upload
  // callback below. We feed each chunk straight to Update.write() ; never
  // hold the full binary in RAM. The dual-app partition scheme handles
  // rollback : Update.end(true) only flips the boot partition on success ;
  // any failure (truncated, wrong MD5, magic byte mismatch) leaves the
  // current firmware in charge.

  static void handle_firmware_complete(AsyncWebServerRequest* req) {
    // iter 021 : image built for another chip -> reject up front. No flash
    // write happened (Update was never begun), so the running firmware stays.
    if (!s_ota_reject_msg.isEmpty()) {
      JsonDocument doc;
      doc["error"] = s_ota_reject_msg;
      String body;
      serializeJson(doc, body);
      AsyncWebServerResponse* res = req->beginResponse(400, "application/json", body);
      res->addHeader("Connection", "close");
      req->send(res);
      logger::err("ota", "WebOTA rejected: %s", s_ota_reject_msg.c_str());
      s_ota_reject_msg = "";
      return;
    }
    if (Update.hasError()) {
      JsonDocument doc;
      doc["error"] = Update.errorString();
      String body;
      serializeJson(doc, body);
      AsyncWebServerResponse* res = req->beginResponse(400, "application/json", body);
      res->addHeader("Connection", "close");
      req->send(res);
      logger::err("ota", "WebOTA failed: %s", Update.errorString());
      return;
    }
    JsonDocument doc;
    doc["ok"]        = true;
    doc["rebooting"] = true;
    String body;
    serializeJson(doc, body);
    AsyncWebServerResponse* res = req->beginResponse(200, "application/json", body);
    res->addHeader("Connection", "close");
    req->send(res);
    logger::warn("ota", "WebOTA OK, rebooting in 500 ms");
    delay(500);
    ESP.restart();
  }

  static void handle_firmware_upload(AsyncWebServerRequest* req,
                                     const String& filename, size_t index,
                                     uint8_t* data, size_t len, bool final) {
    if (index == 0) {
      s_ota_reject_msg = "";  // fresh attempt
      logger::warn("ota", "WebOTA start filename=%s", filename.c_str());
      // iter 021 : reject a binary built for another chip before touching the
      // flash. The esp_image_header carries chip_id at offset 12 ; the first
      // chunk is far larger than the header, so we can decide here.
      const int32_t cid = ota_guard::header_chip_id(data, len);
      if (cid == ota_guard::CHIP_ID_INVALID) {
        s_ota_reject_msg = "not an ESP firmware image";
      } else if (cid != ota_guard::EXPECTED_CHIP_ID) {
        s_ota_reject_msg = String("firmware targets ") +
                           ota_guard::chip_name(static_cast<uint16_t>(cid)) +
                           ", this bridge is " + ota_guard::chip_name(ota_guard::EXPECTED_CHIP_ID);
      }
      if (!s_ota_reject_msg.isEmpty()) {
        logger::err("ota", "WebOTA reject: %s (chip 0x%04X, expected 0x%04X)",
                    s_ota_reject_msg.c_str(),
                    cid < 0 ? 0u : static_cast<unsigned>(cid), ota_guard::EXPECTED_CHIP_ID);
        return;
      }
      // First chunk OK : kick off the Update flow. UPDATE_SIZE_UNKNOWN lets
      // the lib accept any size up to the OTA slot limit ; the embedded
      // MD5 is verified at end().
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
        logger::err("ota", "Update.begin failed: %s", Update.errorString());
        return;
      }
    }
    // A wrong-chip image was rejected on the first chunk : swallow the rest of
    // the stream without ever writing to flash.
    if (!s_ota_reject_msg.isEmpty()) return;
    if (len > 0 && Update.write(data, len) != len) {
      Update.printError(Serial);
      logger::err("ota", "Update.write short: %s", Update.errorString());
      return;
    }
    if (final) {
      if (!Update.end(true /*evaluate_with_md5*/)) {
        Update.printError(Serial);
        logger::err("ota", "Update.end failed: %s", Update.errorString());
        return;
      }
      logger::info("ota", "WebOTA wrote %u bytes", static_cast<unsigned>(index + len));
    }
  }

  // --- public API ---

  void init() {
    if (s_started) return;

    s_server.on("/",            HTTP_GET, handle_index);
    s_server.on("/api/status",  HTTP_GET, handle_get_status);
    s_server.on("/api/mqtt",    HTTP_GET, handle_get_mqtt);
    s_server.on("/api/wifi",    HTTP_GET, handle_get_wifi);
    s_server.on("/api/remotes", HTTP_GET, handle_get_remotes);

    // JSON POST endpoints: AsyncCallbackJsonWebHandler attaches to a path + method.
    s_server.addHandler(new AsyncCallbackJsonWebHandler("/api/mqtt",    handle_post_mqtt));
    s_server.addHandler(new AsyncCallbackJsonWebHandler("/api/wifi",    handle_post_wifi));
    s_server.addHandler(new AsyncCallbackJsonWebHandler("/api/remotes", handle_post_remote));

    s_server.on("^/api/remotes/([0-9A-Fa-f]{6})$", HTTP_DELETE, handle_delete_remote);
    s_server.on("^/api/remotes/([0-9A-Fa-f]{6})/(up|down|stop|toggle|program|program3s|program7s)$",
                HTTP_POST, handle_post_command);
    // iter 014 : numeric setters (position, set_position, durations)
    s_server.on("^/api/remotes/([0-9A-Fa-f]{6})/(position|set_position|open_duration_ms|close_duration_ms|invert|type|toggle_button)/([0-9]+)$",
                HTTP_POST, handle_post_value);
    s_server.on("/api/factory_reset", HTTP_POST, handle_factory_reset);

    // iter 016 : WebOTA firmware upload. Completion handler sends the
    // ack JSON ; upload handler streams chunks to Update.write().
    s_server.on(
        "/api/firmware/upload", HTTP_POST,
        handle_firmware_complete,
        handle_firmware_upload);

    s_server.onNotFound([](AsyncWebServerRequest* req) {
      req->send(404, "text/plain", "not found");
    });

    s_server.begin();
    s_started = true;
    logger::info("web", "listening on http://%s/",
                 WiFi.localIP().toString().c_str());
  }

}
