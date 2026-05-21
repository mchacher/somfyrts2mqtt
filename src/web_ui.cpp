/**
 * @file web_ui.cpp
 * @brief AsyncWebServer-backed implementation. See web_ui.h.
 */
#include "web_ui.h"

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

#include "logger.h"
#include "mqtt.h"
#include "nvs_store.h"
#include "orchestrator.h"

namespace web_ui {

  static AsyncWebServer s_server(80);
  static bool           s_started = false;

  // The single-page HTML/CSS/JS embedded as a raw string. Kept small,
  // vanilla JS, no framework.
  static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<title>somfyrts2mqtt</title>
<style>
:root { --bg:#1a1a1a; --fg:#eaeaea; --muted:#888; --accent:#3aa6ff; --danger:#e84545; --ok:#4caf50; --border:#333; --panel:#222; }
* { box-sizing: border-box; }
body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", system-ui, sans-serif; background: var(--bg); color: var(--fg); margin: 0; padding: 1rem; max-width: 720px; margin-inline: auto; }
h1 { font-size: 1.25rem; margin: 0 0 1rem 0; }
h2 { font-size: 1rem; margin: 0 0 0.5rem 0; color: var(--muted); text-transform: uppercase; letter-spacing: 0.5px; }
section { background: var(--panel); border: 1px solid var(--border); border-radius: 8px; padding: 1rem; margin-bottom: 1rem; }
table { width: 100%; border-collapse: collapse; }
th, td { padding: 0.4rem 0.5rem; text-align: left; border-bottom: 1px solid var(--border); font-size: 0.9rem; }
th { color: var(--muted); font-weight: 500; }
tr:last-child td { border-bottom: none; }
input, button { font: inherit; background: #2a2a2a; border: 1px solid var(--border); color: var(--fg); padding: 0.4rem 0.6rem; border-radius: 4px; }
input:focus { outline: 2px solid var(--accent); outline-offset: -2px; }
button { cursor: pointer; }
button:hover { background: #333; }
button.primary { background: var(--accent); border-color: var(--accent); color: #000; }
button.danger { background: var(--danger); border-color: var(--danger); color: #fff; }
.row { display: grid; grid-template-columns: 100px 1fr; gap: 0.5rem 1rem; align-items: center; margin-bottom: 0.5rem; }
.row label { color: var(--muted); font-size: 0.85rem; }
.actions { display: flex; gap: 0.5rem; margin-top: 0.75rem; }
.kv { display: grid; grid-template-columns: 140px 1fr; gap: 0.4rem 1rem; font-size: 0.9rem; }
.kv span:nth-child(odd) { color: var(--muted); }
.pill { display: inline-block; padding: 2px 8px; border-radius: 10px; font-size: 0.8rem; }
.pill.ok { background: var(--ok); color: #fff; }
.pill.bad { background: var(--danger); color: #fff; }
.msg { padding: 0.4rem 0.6rem; border-radius: 4px; margin-top: 0.5rem; font-size: 0.85rem; display: none; }
.msg.ok { background: rgba(76,175,80,0.15); color: #b6e3b8; }
.msg.err { background: rgba(232,69,69,0.15); color: #f5b8b8; }
footer { text-align: center; color: var(--muted); font-size: 0.8rem; margin-top: 2rem; }
.add-form { display: grid; grid-template-columns: 1fr 2fr auto; gap: 0.5rem; margin-top: 0.75rem; }
.del { background: #3a2222; border-color: #5a2222; }
.cmd-cell { white-space: nowrap; }
.cmd-cell button { padding: 0.25rem 0.4rem; min-width: 30px; margin-right: 1px; font-size: 0.9rem; }
.cmd-cell button.prog  { background: #2a3a4a; border-color: #3a5060; }
.cmd-cell button.pair  { background: #1f4030; border-color: #2f7050; color: #c8f0d8; }
.cmd-cell button.erase { background: #4a1f1f; border-color: #7a2f2f; color: #f5b8b8; }
.cmd-cell button:disabled { opacity: 0.4; cursor: wait; }
</style>
</head>
<body>
<h1>somfyrts2mqtt</h1>

<section>
  <h2>Status</h2>
  <div class="kv" id="status">
    <span>Version</span><span data-k="version">…</span>
    <span>IP</span><span data-k="ip">…</span>
    <span>MAC</span><span data-k="mac">…</span>
    <span>Uptime</span><span data-k="uptime_s">…</span>
    <span>MQTT</span><span data-k="mqtt"><span class="pill">…</span></span>
    <span>Remotes</span><span data-k="remotes_count">…</span>
  </div>
</section>

<section>
  <h2>MQTT broker</h2>
  <form id="mqtt-form">
    <div class="row"><label>Host</label><input name="host" required maxlength="64"/></div>
    <div class="row"><label>Port</label><input name="port" type="number" min="1" max="65535" required/></div>
    <div class="row"><label>User</label><input name="user" maxlength="64"/></div>
    <div class="row"><label>Pass</label><input name="pass" type="password" maxlength="64" placeholder="(unchanged)"/></div>
    <div class="actions"><button class="primary" type="submit">Save</button></div>
    <div class="msg" id="mqtt-msg"></div>
  </form>
</section>

<section>
  <h2>Remotes</h2>
  <table><thead><tr><th>ID</th><th>Name</th><th>Code</th><th>Commands</th><th></th></tr></thead><tbody id="remotes-body"></tbody></table>
  <form id="remote-form" class="add-form">
    <input name="id_hex" placeholder="A1B2C3" pattern="[0-9A-Fa-f]{6}" required maxlength="6"/>
    <input name="name" placeholder="kitchen shutter" required maxlength="32"/>
    <button class="primary" type="submit">Add</button>
  </form>
  <div class="msg" id="remotes-msg"></div>
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

async function loadMqtt() {
  const m = await fetchJSON('/api/mqtt');
  for (const k of ['host','port','user']) $(`#mqtt-form [name="${k}"]`).value = m[k] ?? '';
}

async function loadRemotes() {
  const remotes = await fetchJSON('/api/remotes');
  const body = $('#remotes-body');
  body.innerHTML = '';
  if (remotes.length === 0) {
    body.innerHTML = '<tr><td colspan="5" style="color:var(--muted);text-align:center">No remotes yet</td></tr>';
    return;
  }
  for (const r of remotes) {
    const tr = document.createElement('tr');
    tr.innerHTML = `<td><code>${r.id_hex}</code></td><td>${r.name}</td><td>${r.rolling_code}</td>` +
      `<td class="cmd-cell" data-id="${r.id_hex}">` +
      `<button data-cmd="up"   title="Up">&#9650;</button>` +
      `<button data-cmd="stop" title="Stop">&#9632;</button>` +
      `<button data-cmd="down" title="Down">&#9660;</button>` +
      `<button data-cmd="program"   class="prog"  title="PROG brief - confirm pair / delete when motor is in mode">&#128279; Prog</button>` +
      `<button data-cmd="program3s" class="pair"  title="PROG 3 s - put motor in pair mode">&#10133; Pair</button>` +
      `<button data-cmd="program7s" class="erase" title="PROG 7 s - put motor in erase mode">&#128465; Erase</button>` +
      `</td>` +
      `<td><button class="del" data-id="${r.id_hex}">×</button></td>`;
    body.appendChild(tr);
  }
  body.querySelectorAll('button.del').forEach(b => b.onclick = async () => {
    if (!confirm(`Delete remote ${b.dataset.id}?`)) return;
    try { await fetchJSON(`/api/remotes/${b.dataset.id}`, {method:'DELETE'}); await loadRemotes(); await loadStatus(); }
    catch (e) { show('#remotes-msg', 'err', e.message); }
  });
  // Estimated RF emission duration per command, in ms. Matches the
  // repeat counts in handle_post_command: ~143 ms per repeat frame
  // plus a ~220 ms initial frame. Used to keep the row's buttons
  // visually "busy" while the emission runs in the background -- the
  // HTTP queue returns in ~50 ms, much faster than the actual TX.
  const TX_MS = { up: 400, down: 400, stop: 400, program: 800,
                  program3s: 3300, program7s: 7500 };
  body.querySelectorAll('.cmd-cell button').forEach(b => b.onclick = async () => {
    const id = b.parentElement.dataset.id;
    const cmd = b.dataset.cmd;
    const row = b.parentElement.querySelectorAll('button');
    row.forEach(x => x.disabled = true);
    try {
      await fetchJSON(`/api/remotes/${id}/${cmd}`, {method:'POST'});
      // Wait for the RF emission to actually finish before re-rendering.
      // Keeps the user from re-clicking mid-erase (a 7s window).
      await new Promise(r => setTimeout(r, TX_MS[cmd] || 400));
      await loadRemotes();   // re-render replaces the disabled buttons with fresh enabled ones
    } catch (e) {
      show('#remotes-msg', 'err', e.message);
      row.forEach(x => x.disabled = false);
    }
  });
}

$('#mqtt-form').onsubmit = async (e) => {
  e.preventDefault();
  const fd = new FormData(e.target);
  const body = { host: fd.get('host'), port: Number(fd.get('port')), user: fd.get('user'), pass: fd.get('pass') };
  try { await fetchJSON('/api/mqtt', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify(body)}); show('#mqtt-msg','ok','Saved; reconnecting…'); $(`#mqtt-form [name="pass"]`).value = ''; setTimeout(loadStatus, 6000); }
  catch (err) { show('#mqtt-msg','err', err.message); }
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

(async () => { await loadStatus(); await loadMqtt(); await loadRemotes(); setInterval(loadStatus, 5000); })();
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
    doc["host"] = cfg.host;
    doc["port"] = cfg.port;
    doc["user"] = cfg.user;
    // pass intentionally omitted
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

    if (cfg.host.empty() || cfg.host.size() > 64) return send_error(req, 400, "host empty or too long");
    if (cfg.port == 0) return send_error(req, 400, "port must be > 0");

    if (!nvs_store::set_mqtt(cfg)) return send_error(req, 500, "set_mqtt failed");
    mqtt::disconnect();
    req->send(204);
    logger::info("web", "mqtt config updated, reconnect triggered");
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
      o["id_hex"]       = hex;
      o["rolling_code"] = buf[i].rolling_code;
      o["name"]         = buf[i].name;
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
    } else {
      cmd = orchestrator::command_from_str(cmd_param.c_str());
      if (cmd == mqtt::Command::Invalid)
        return send_error(req, 400, "invalid cmd");
    }

    if (!orchestrator::enqueue_command(id, cmd, repeat_override))
      return send_error(req, 503, "queue full, try again");
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

  // --- public API ---

  void init() {
    if (s_started) return;

    s_server.on("/",            HTTP_GET, handle_index);
    s_server.on("/api/status",  HTTP_GET, handle_get_status);
    s_server.on("/api/mqtt",    HTTP_GET, handle_get_mqtt);
    s_server.on("/api/remotes", HTTP_GET, handle_get_remotes);

    // JSON POST endpoints: AsyncCallbackJsonWebHandler attaches to a path + method.
    s_server.addHandler(new AsyncCallbackJsonWebHandler("/api/mqtt",    handle_post_mqtt));
    s_server.addHandler(new AsyncCallbackJsonWebHandler("/api/remotes", handle_post_remote));

    s_server.on("^/api/remotes/([0-9A-Fa-f]{6})$", HTTP_DELETE, handle_delete_remote);
    s_server.on("^/api/remotes/([0-9A-Fa-f]{6})/(up|down|stop|program|program3s|program7s)$",
                HTTP_POST, handle_post_command);
    s_server.on("/api/factory_reset", HTTP_POST, handle_factory_reset);

    s_server.onNotFound([](AsyncWebServerRequest* req) {
      req->send(404, "text/plain", "not found");
    });

    s_server.begin();
    s_started = true;
    logger::info("web", "listening on http://%s/",
                 WiFi.localIP().toString().c_str());
  }

}
