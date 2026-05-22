# Architecture 016

## Touched modules

| File | Change |
|---|---|
| `src/web_ui.cpp` | Add `/api/firmware/upload` POST handler (multipart streaming) + Update section in the embedded HTML / JS |
| `src/wifi_manager.cpp` | ArduinoOTA setup in `STA_GOT_IP` event handler ; `ArduinoOTA.handle()` in `wifi::loop()` |
| `include/config.h` | New `OTA_PASSWORD` compile-time string with a "change me" default |
| `.github/workflows/release.yml` | New workflow : on tag push, build firmware for both envs, attach .bin to a GitHub Release |
| `docs/setup.md`, `docs/web-ui.md` | "Update from a GitHub Release" section ; WebOTA walkthrough |
| `README.md` | Link to the Releases page in the Quickstart |

No new lib dependency (Update, ArduinoOTA both ship with Arduino-ESP32).

## WebOTA path

```
Browser                                    Bridge (admin UI)
   │                                            │
   ├── GET /                            ────────▶│ Render Update section + Status / MQTT / WiFi / Remotes
   │                                            │
   ├── user picks firmware.bin                  │
   ├── user confirms (2-step dialog)            │
   ├── POST /api/firmware/upload      ─────────▶│ AsyncWebServer onUpload callback :
   │   Content-Type: multipart/form-data        │   - first chunk : Update.begin(UPDATE_SIZE_UNKNOWN)
   │   <binary chunks>                          │   - each chunk  : Update.write(buf, len)
   │                                            │   - last chunk  : Update.end(true)
   │                                            │
   │◀────────── 200 {"ok":true,"rebooting":true}
   │                                            │
   │                                            ├── delay(500)  -- let HTTP flush
   │                                            └── ESP.restart()
   │
   │ (browser waits ~5 s, then polls /api/status until the new version appears)
```

`Update.end(true)` returns true only after :
- Wrote at least one chunk
- Embedded MD5 (Arduino-ESP32 appends one) matches the computed MD5
- The OTA slot fits the binary

On failure : the bridge keeps booting from the current partition. The OTA slot is left in an "invalid" state and the next attempt overwrites it.

## ArduinoOTA path

```cpp
// In wifi_manager.cpp, on STA_GOT_IP, alongside MDNS.begin :
ArduinoOTA.setHostname(WiFi.getHostname());          // "somfyrts2mqtt"
ArduinoOTA.setPassword(OTA_PASSWORD);                // from config.h
ArduinoOTA.onStart(...) / onProgress(...) / onEnd(...) / onError(...) ;
ArduinoOTA.begin();

// In wifi::loop() :
ArduinoOTA.handle();
```

PlatformIO discovers the bridge via mDNS (`_arduino._tcp` advertised by ArduinoOTA.begin()) and pushes firmware over a TCP / UDP combo. Auth via the password.

`platformio.ini` example for the dev :

```ini
[env:esp32-c3-mini-ota]
extends = env:esp32-c3-mini
upload_protocol = espota
upload_port     = somfyrts2mqtt.local
upload_flags    = --auth=${env:SOMFYRTS_OTA_PASSWORD}
```

## GitHub Releases workflow

`.github/workflows/release.yml` outline :

```yaml
on:
  push:
    tags: ['v*']
jobs:
  release:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v6
      - run: pip install platformio
      - run: pio run -e esp32-c3-mini -e esp32-wroom
      - name: Rename binaries
        run: |
          VERSION=${GITHUB_REF#refs/tags/v}
          mkdir -p release
          cp .pio/build/esp32-c3-mini/firmware.bin release/somfyrts2mqtt-${VERSION}-esp32-c3-mini.bin
          cp .pio/build/esp32-wroom/firmware.bin   release/somfyrts2mqtt-${VERSION}-esp32-wroom.bin
      - uses: softprops/action-gh-release@v2
        with:
          files: release/*.bin
          generate_release_notes: true
```

The Release body uses GitHub's auto-generated notes (PR titles / commits since the previous tag). A more curated CHANGELOG.md can replace this later.

## Update section in the admin UI

Sits in a new Card between `WiFi` and `Remotes` (or under `Danger zone` if we want to keep it less prominent). Markup :

```html
<section>
  <h2>Update</h2>
  <p class="kv">
    <span>Current version</span><span data-k="version_now">0.1.0</span>
    <span>Latest release</span>
       <a href="https://github.com/mchacher/somfyrts2mqtt/releases/latest" target="_blank">GitHub</a>
  </p>
  <form id="ota-form">
    <input type="file" name="firmware" accept=".bin" required/>
    <button class="primary" type="submit">Upload &amp; reboot</button>
  </form>
  <progress id="ota-progress" value="0" max="100" hidden></progress>
  <div class="msg" id="ota-msg"></div>
</section>
```

JS uses `XMLHttpRequest` (instead of `fetch`) because XHR exposes upload-progress events natively ; updates the `<progress>` bar. Two `confirm()` calls before sending (same Pattern as Factory reset).

## Partition layout sanity check

`min_spiffs.csv` (PlatformIO default for boards with 4 MB flash) :

| Name | Type | Subtype | Offset | Size |
|---|---|---|---|---|
| nvs | data | nvs | 0x9000 | 0x5000 (20 KB) |
| otadata | data | ota | 0xe000 | 0x2000 (8 KB) |
| app0 | app | ota_0 | 0x10000 | 0x1E0000 (1.875 MB) |
| app1 | app | ota_1 | 0x1F0000 | 0x1E0000 (1.875 MB) |
| spiffs | data | spiffs | 0x3D0000 | 0x30000 (192 KB) |

Verified : ample headroom (current firmware uses ~1.1 MB), 2 OTA slots active, otadata partition present. No `partitions.csv` change needed.
