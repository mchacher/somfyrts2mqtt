# Plan 019 — LAN portal design alignment

## Steps

1. **Replace the `<style>` block** of `INDEX_HTML` in `src/web_ui.cpp`
   with the Sowel light theme — see the mapping table in
   `architecture.md`. Preserve every existing class name so the JS keeps
   working unchanged.
2. **Promote the `<h1>` to a SOWEL header block** by adding the
   `.brand` + `.tag` lines. Other existing elements stay byte-for-byte.
3. **Build** with `~/.platformio/penv/bin/pio run -d .` (both envs).
4. **Static check** with `~/.platformio/penv/bin/pio check -d .`.
5. **Native tests** with `~/.platformio/penv/bin/pio test -d . -e
   native` (the LAN portal has no native-testable logic but the rest
   must keep passing).
6. **Flash to the bench** with `~/.platformio/penv/bin/pio run -d . -t
   upload` and run the HW checklist.

## Test plan (HW)

Open `http://<bridge-ip>/` on:

### Desktop browser (Chrome / Safari, 1440 px wide)

- [ ] Page background is `#F8F9FA`, not dark grey.
- [ ] Header reads "SOWEL" with letter-spacing + the "Configuration
      du bridge Somfy RTS" tagline, in `--primary` colour.
- [ ] Each section is a white card with rounded corners and a faint
      grey border.
- [ ] Status block: MQTT / WiFi pills rendered as 999-px-rounded
      capsules, white text, `--primary` or `--error` background
      depending on state.
- [ ] MQTT form: inputs are white with `--border`, focus ring is
      `--primary-light`. Save button is `--primary`, hover darker.
- [ ] WiFi section: same. Eye-toggle button shows / hides the password.
- [ ] Remotes table: header row in `--muted`, row hairlines
      `--border`, position cell uses monospace digits.
- [ ] Setup row (per-remote programming controls) has a
      `--primary-light` background, with the four action buttons
      colour-coded (prog / pair / erase / sync) using the new tokens.
- [ ] Add-remote form: grid layout unchanged, Add button is primary.
- [ ] Update firmware section: file input + Upload button restyled,
      `<progress>` bar fills in `--primary`.
- [ ] Danger zone: Factory reset button uses `--error`.

### Mobile (iPhone, 390 px wide)

- [ ] Page collapses to a single column. Cards stack vertically.
- [ ] Remotes table scrolls horizontally inside its `.table-wrap`
      container without breaking the rest of the layout.
- [ ] Tap targets are at least 36 px tall.

### Functional regression (still works after the restyle)

- [ ] Status row updates every 5 s.
- [ ] MQTT save → green `.msg.ok`, status pill flips to connected
      within a few seconds.
- [ ] Wi-Fi save → red `.msg.err` if a bad SSID is entered (validation
      already done server-side).
- [ ] Add remote, then delete it.
- [ ] Trigger an OPEN / STOP / CLOSE command from a remote row,
      observe MQTT, observe the live position cell update.
- [ ] Upload a dummy `.bin` via the OTA form, watch the progress bar
      go to 100 % then the success message.
- [ ] Factory reset (only on a disposable device!) reboots the bridge
      and brings up the spec-018 captive portal.

## Edge cases

- [ ] Bridge disconnected from MQTT → red `.pill.bad` reads as
      "disconnected" on the new background.
- [ ] Remotes table empty → first row is the setup row, still legible
      on the new card background.
- [ ] Inline message after a 4xx (`.msg.err`) on `--error-bg` is
      readable.
- [ ] Long MQTT topic name does not blow the layout (text wraps or
      ellipsises within its container).
