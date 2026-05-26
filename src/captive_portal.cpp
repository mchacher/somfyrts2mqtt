/**
 * @file captive_portal.cpp
 * @brief Captive portal implementation. See captive_portal.h.
 *
 * AP mode + DNSServer (so every DNS lookup resolves to us, triggers iOS /
 * Android captive redirect) + ESPAsyncWebServer for the form.
 *
 * Palette + DOM scaffold copied verbatim from sowel-energy-display's
 * src/portal.cpp so the first-run UX of the somfyrts2mqtt bridge visually
 * matches the rest of the Sowel device family. CSS is inlined because the
 * phone is offline once joined to the AP (no Google Fonts available).
 *
 * Scope is narrower than the energy-display portal: only WiFi credentials
 * are entered here. MQTT broker config + remote management live in the
 * LAN web UI (web_ui.cpp) which is reachable after the first successful
 * association.
 */
// Arduino.h first so sdkconfig (CONFIG_IDF_TARGET_*) is defined before
// config.h's CC1101 pinout dispatch.
#include <Arduino.h>

#include "captive_portal.h"

#include "config.h"
#include "logger.h"
#include "nvs_store.h"

#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>

#include <string>

namespace captive_portal {

  static constexpr const char* TAG = "captive";

  // AP setup. IPAddress is not constexpr-friendly in Arduino-ESP32,
  // so plain `const` here. Matches sowel-energy-display/portal.cpp.
  static const IPAddress AP_IP    (192, 168, 4, 1);
  static const IPAddress AP_GW    (192, 168, 4, 1);
  static const IPAddress AP_MASK  (255, 255, 255, 0);
  static constexpr uint8_t   AP_CHAN  = 1;
  static constexpr bool      AP_HIDDEN= false;
  static constexpr uint8_t   AP_MAXCO = 2;

  static DNSServer        dns_server;
  static AsyncWebServer   http(80);
  static String           last_error;   // re-rendered on next GET after a failed POST
  static String           scan_cache;   // SSID list as <option> tags

  // Deferred reboot. ESP.restart() cannot be called from an AsyncTCP request
  // callback — the task watchdog trips during the ~5 s before the reset
  // completes and silently loses the NVS write that just happened. on_save()
  // schedules the reboot via these flags and the main run() loop fires
  // ESP.restart() once the response has flushed.
  static volatile bool     reboot_pending  = false;
  static volatile uint32_t reboot_after_ms = 0;

  // -------------------------------------------------------------------
  // Helpers
  // -------------------------------------------------------------------

  /// Escape a string for safe inlining as HTML text or attribute value.
  static String html_escape(const String& s) {
    String out;
    out.reserve(s.length() + 16);
    for (size_t i = 0; i < s.length(); ++i) {
      const char c = s[i];
      switch (c) {
        case '&':  out += "&amp;";  break;
        case '<':  out += "&lt;";   break;
        case '>':  out += "&gt;";   break;
        case '"':  out += "&quot;"; break;
        case '\'': out += "&#39;";  break;
        default:   out += c;
      }
    }
    return out;
  }

  /// Synchronous WiFi scan, cached as a string of <option> tags sorted by
  /// the order WiFi.scanNetworks() returns (already RSSI desc). Empty
  /// SSIDs (hidden networks) are skipped.
  static void run_wifi_scan() {
    WiFi.scanDelete();
    const int n = WiFi.scanNetworks(/*async*/ false, /*show_hidden*/ false);
    String out;
    out.reserve(64 * (n > 0 ? n : 1));
    for (int i = 0; i < n; ++i) {
      const String s = WiFi.SSID(i);
      if (s.length() == 0) continue;
      out += "<option value=\"";
      out += html_escape(s);
      out += "\">";
      out += html_escape(s);
      out += " (";
      out += String(WiFi.RSSI(i));
      out += " dBm)</option>";
    }
    if (n <= 0) {
      out = "<option value=\"\" disabled selected>Aucun réseau visible</option>";
    }
    scan_cache = out;
    logger::info(TAG, "wifi scan: %d networks", n);
  }

  // -------------------------------------------------------------------
  // HTML rendering — STYLE copied verbatim from sowel-energy-display
  // -------------------------------------------------------------------

  static const char STYLE[] PROGMEM = R"CSS(
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
    }
    *, *::before, *::after { box-sizing: border-box; }
    html, body { margin: 0; padding: 0; background: var(--bg);
      color: var(--ink); font-family: -apple-system, BlinkMacSystemFont,
      "Inter", "Segoe UI", Roboto, sans-serif; font-size: 14px;
      line-height: 1.5; }
    .container { max-width: 480px; margin: 0 auto; padding: 16px; }
    header { text-align: center; padding: 24px 8px 16px; }
    header .brand { font-size: 18px; font-weight: 700; letter-spacing: 4px;
      color: var(--primary); }
    header .tag { font-size: 12px; color: var(--muted); margin-top: 6px;
      letter-spacing: 1px; text-transform: uppercase; }
    .card { background: var(--card); border-radius: 10px; padding: 18px;
      margin-bottom: 14px; border: 1px solid var(--border); }
    .card h2 { margin: 0 0 12px; font-size: 13px; color: var(--primary);
      text-transform: uppercase; letter-spacing: 1.2px; font-weight: 700;
      border-bottom: 1px solid var(--border); padding-bottom: 6px; }
    .field { margin-bottom: 12px; }
    .field:last-child { margin-bottom: 0; }
    .field label { display: block; font-weight: 500; font-size: 13px;
      margin-bottom: 4px; color: var(--ink); }
    .field .hint { display: block; font-size: 11px; color: var(--muted);
      margin-top: 4px; }
    .field input, .field select { width: 100%; padding: 10px 12px;
      font: inherit; border: 1px solid var(--border); border-radius: 6px;
      background: #fff; color: var(--ink);
      transition: border-color 120ms ease; }
    .field input:focus, .field select:focus { outline: none;
      border-color: var(--border-focus);
      box-shadow: 0 0 0 3px var(--primary-light); }
    .row { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 8px; }
    .row .field { margin-bottom: 0; }
    .btn { display: block; width: 100%; padding: 14px 16px; font: inherit;
      font-weight: 600; border: 0; border-radius: 6px; background: var(--primary);
      color: #fff; cursor: pointer; transition: background 120ms ease; }
    .btn:hover, .btn:active { background: var(--primary-hover); }
    /* Password-with-toggle widget: input + eye button absolutely placed on the right. */
    .with-toggle { position: relative; }
    .with-toggle input { padding-right: 40px; }
    .toggle-pass { position: absolute; right: 6px; top: 50%; transform: translateY(-50%);
      background: none; border: 0; padding: 6px; color: var(--muted); cursor: pointer;
      display: inline-flex; align-items: center; justify-content: center; border-radius: 4px; }
    .toggle-pass:hover { color: var(--primary); background: var(--primary-light); }
    .toggle-pass svg { width: 20px; height: 20px; }
    .error { background: var(--error-bg); color: var(--error); padding: 12px 14px;
      border-radius: 6px; margin-bottom: 14px; font-size: 13px;
      border: 1px solid #FCA5A5; }
    .footer { text-align: center; color: var(--muted); font-size: 11px;
      padding: 16px 8px; }
  )CSS";

  static String render_form() {
    String html;
    html.reserve(4096);
    html += F("<!DOCTYPE html><html lang=\"fr\"><head>"
             "<meta charset=\"UTF-8\">"
             "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
             "<title>somfyrts2mqtt</title><style>");
    html += FPSTR(STYLE);
    html += F("</style></head><body><div class=\"container\">");
    html += F("<header><div class=\"brand\">SOWEL</div>"
             "<div class=\"tag\">Configuration du bridge Somfy RTS</div></header>");

    if (last_error.length() > 0) {
      html += F("<div class=\"error\">");
      html += html_escape(last_error);
      html += F("</div>");
    }

    html += F("<form method=\"POST\" action=\"/save\">"
             "<div class=\"card\"><h2>WiFi</h2>"
             "<div class=\"field\"><label for=\"wifi_ssid\">Réseau</label>"
             "<select name=\"wifi_ssid\" id=\"wifi_ssid\" required>");
    html += scan_cache;
    html += F("</select>"
             "<span class=\"hint\">Choisis ton réseau 2.4 GHz.</span></div>"
             "<div class=\"field\"><label for=\"wifi_pass\">Mot de passe</label>"
             "<div class=\"with-toggle\">"
             "<input type=\"password\" name=\"wifi_pass\" id=\"wifi_pass\""
             " maxlength=\"63\" autocomplete=\"new-password\">"
             "<button type=\"button\" class=\"toggle-pass\""
             " aria-label=\"Afficher le mot de passe\""
             " onclick=\""
             "var i=document.getElementById('wifi_pass');"
             "i.type=i.type==='password'?'text':'password';"
             "this.setAttribute('aria-label',"
             "i.type==='password'?'Afficher le mot de passe':'Masquer le mot de passe');"
             "\">"
             // Inline SVG eye, follows currentColor for the hover state.
             "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\""
             " stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
             "<path d=\"M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z\"/>"
             "<circle cx=\"12\" cy=\"12\" r=\"3\"/>"
             "</svg>"
             "</button>"
             "</div>"
             "<span class=\"hint\">Laisser vide pour un réseau ouvert.</span>"
             "</div></div>"
             "<button class=\"btn\" type=\"submit\">"
             "Valider et redémarrer</button></form>"
             "<div class=\"footer\">somfyrts2mqtt</div>"
             "</div></body></html>");

    return html;
  }

  // -------------------------------------------------------------------
  // Submit handler
  // -------------------------------------------------------------------

  static String param(AsyncWebServerRequest* req, const char* name) {
    if (req->hasParam(name, true)) {
      return req->getParam(name, true)->value();
    }
    return String();
  }

  static void on_save(AsyncWebServerRequest* req) {
    if (reboot_pending) {
      req->send(503, "text/plain", "Reboot already pending");
      return;
    }

    const String ssid = param(req, "wifi_ssid");
    const String pass = param(req, "wifi_pass");

    if (ssid.length() == 0) {
      last_error = "SSID requis";
      logger::warn(TAG, "POST /save rejected: empty SSID");
      req->send(200, "text/html; charset=utf-8", render_form());
      return;
    }
    if (pass.length() > 63) {
      // Server-side guard against a craftier client; the maxlength on the
      // input already enforces this for normal browsers.
      last_error = "Mot de passe WPA2 trop long (max 63 caractères)";
      logger::warn(TAG, "POST /save rejected: password >63 chars");
      req->send(200, "text/html; charset=utf-8", render_form());
      return;
    }

    const std::string s = ssid.c_str();
    const std::string p = pass.c_str();
    if (!nvs_store::set_wifi_creds(s, p)) {
      last_error = "Ecriture NVS echouee, reessaie.";
      logger::err(TAG, "set_wifi_creds() failed for ssid=%s", s.c_str());
      req->send(200, "text/html; charset=utf-8", render_form());
      return;
    }

    logger::info(TAG, "saved new creds ssid=%s, rebooting into STA", s.c_str());

    // "Connection: close" lets the phone tear down the socket before the
    // AP disappears so the success page actually renders end-to-end.
    AsyncWebServerResponse* resp = req->beginResponse(
        200, "text/html; charset=utf-8",
        "<!DOCTYPE html><html lang=\"fr\"><meta charset=\"UTF-8\">"
        "<style>body{font:14px sans-serif;text-align:center;padding:48px;}"
        "h1{color:#1A4F6E}</style>"
        "<h1>OK Configuration enregistree</h1>"
        "<p>Le bridge redemarre&hellip;</p></html>");
    resp->addHeader("Connection", "close");
    req->send(resp);

    reboot_after_ms = millis() + 2000;
    reboot_pending  = true;
  }

  // -------------------------------------------------------------------
  // Captive redirect (any unknown path → root)
  // -------------------------------------------------------------------

  static void on_get_root(AsyncWebServerRequest* req) {
    req->send(200, "text/html; charset=utf-8", render_form());
  }

  static void on_not_found(AsyncWebServerRequest* req) {
    // iOS / macOS / Android / Windows captive-probe URLs all land here
    // and get redirected — the OS treats the redirect as the trigger to
    // pop up the captive-portal sheet automatically.
    req->redirect("/");
  }

  // -------------------------------------------------------------------
  // Public entry point
  // -------------------------------------------------------------------

  [[noreturn]] void run(const char* ap_ssid, uint32_t timeout_s) {
    WiFi.mode(WIFI_AP_STA);  // AP_STA so the scan still works while we host
    WiFi.softAPConfig(AP_IP, AP_GW, AP_MASK);

    const bool ap_ok = WiFi.softAP(ap_ssid, nullptr, AP_CHAN, AP_HIDDEN, AP_MAXCO);

    // C3 Super Mini PA saturation also affects AP-mode beacons + auth
    // response frames. Clamp TX power as soon as the AP is up so the
    // first phone trying to join doesn't hit WL_NO_SSID_AVAIL / auth
    // timeouts. Same value tzapu was using via setAPCallback().
    WiFi.setTxPower(WIFI_POWER_8_5dBm);

    const String ap_ip = WiFi.softAPIP().toString();
    logger::info(TAG, "AP %s -> %s (ok=%d) tx=8.5dBm timeout=%us",
                 ap_ssid, ap_ip.c_str(), static_cast<int>(ap_ok),
                 static_cast<unsigned>(timeout_s));

    run_wifi_scan();

    dns_server.start(53, "*", AP_IP);

    http.on("/",     HTTP_GET,  on_get_root);
    http.on("/save", HTTP_POST, on_save);
    http.onNotFound(on_not_found);
    http.begin();
    logger::info(TAG, "HTTP up. Visit http://%s/ from your phone.",
                 ap_ip.c_str());

    const uint32_t deadline = millis() + (timeout_s * 1000UL);
    while (true) {
      dns_server.processNextRequest();

      // Fire the deferred reboot scheduled by on_save() once the response
      // has had time to flush over the captive-portal socket.
      if (reboot_pending && millis() >= reboot_after_ms) {
        logger::info(TAG, "deferred reboot now");
        delay(200);
        ESP.restart();
      }

      if (millis() > deadline) {
        logger::warn(TAG, "portal idle timeout after %us, rebooting",
                     static_cast<unsigned>(timeout_s));
        delay(200);
        ESP.restart();
      }
      delay(10);
    }
  }

}
