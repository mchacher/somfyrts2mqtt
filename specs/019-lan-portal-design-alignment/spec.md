# 019 — LAN portal design alignment with sowel-energy-display

## Goal

Bring the LAN web portal (`web_ui.cpp`, served on `http://<bridge-ip>/`
once the bridge has joined Wi-Fi) under the same Sowel light design as
the new captive portal (spec 018) — same palette tokens, same system
font stack, same `.card` / `.field` / `.with-toggle` / `.btn` vocabulary.
Today the LAN portal is still a generic dark grey theme (panels `#222`,
ink `#eaeaea`, blue accent), inherited from the iter 014 first cut, and
visually contradicts the new captive portal.

## Scope

In scope:
- Replace the `<style>` block of `INDEX_HTML` in `src/web_ui.cpp` with
  the Sowel light palette (`--primary #1A4F6E`, `--primary-light #E6F0F6`,
  `--accent #D4963F`, `--bg #F8F9FA`, `--card #FFFFFF`, `--ink #1A2A3C`,
  `--muted #6B7280`, `--border #D1D5DB`, `--error #DC2626`,
  `--error-bg #FEE2E2`).
- System sans-serif font stack (no Google Fonts), same as the captive
  portal. The page is on the LAN so the internet is reachable, but we
  pick system fonts on purpose to keep the visual identical to the
  captive portal and to remove an external dependency that would slow
  the first paint on a slow router.
- Restyle every existing class **without touching the markup or the JS**.
  Selectors that must keep working with no changes: `.kv`, `.cards-row`,
  `.row`, `.pass-wrap`, `.eye` (+ `.eye-open` / `.eye-off`),
  `.status-line`, `.actions`, `.msg` (+ `.msg.ok` / `.msg.err`),
  `.table-wrap`, `.add-form`, `.cmd-cell`, `.pos-cell` (+ `.pos-cur`),
  `.pill` (+ `.pill.ok` / `.pill.bad`), `.dot`, `.del`, `.setup-row`,
  `.setup-panel`, `.setup-group` (+ `.setup-label`), `.setup-group
  button.prog` / `.pair` / `.erase` / `.sync`, `footer`, and the
  `<progress>` element used for the WebOTA upload.
- Page header (`<h1>somfyrts2mqtt</h1>`) becomes a SOWEL brand block in
  the same visual style as the captive portal (uppercase letter-spaced
  brand + tagline "Configuration du bridge Somfy RTS"), but with a
  wider container so the Remotes table still has room.
- WebOTA section (`Update firmware` form + `<progress>` bar) gets the
  same primary button + bordered field styling.

Out of scope:
- Any change to the JS, HTML structure, or REST endpoints. JS selectors
  must still resolve to the same elements.
- Changing the language of the page (mixed FR / EN labels stay as-is;
  a future iter may unify the copy).
- Refactoring the section ordering / removing sections.
- Aligning the captive portal (already done in spec 018).

## Acceptance criteria

- [ ] `:root` in `web_ui.cpp` uses the captive portal palette tokens
      verbatim (no fork between the two pages).
- [ ] Body background is `#F8F9FA`, no longer dark grey.
- [ ] Page header reads `SOWEL` (letter-spaced 4 px, color
      `--primary`) + tagline "Configuration du bridge Somfy RTS".
- [ ] Each `<section>` is a white card on `#F8F9FA` with a `--border`
      hairline, rounded 10 px, padded.
- [ ] `<h2>` is uppercase Sowel-primary 13 px, letter-spacing 1.2 px,
      with a `--border` underline (identical to the captive portal
      `.card h2`).
- [ ] Inputs / selects use the new `--border` + `--primary-light` focus
      ring; no more dark backgrounds.
- [ ] Primary buttons (Save, Save and reconnect, Add, Upload & reboot)
      use `--primary` background, `--primary-hover` on hover.
- [ ] Danger button (Factory reset) uses `--error` background.
- [ ] Pills (`.pill.ok`, `.pill.bad`) are rounded 999 px with the same
      color treatment as the captive portal.
- [ ] Eye-toggle widget on the MQTT pass + Wi-Fi pass fields renders
      consistently with the captive portal version.
- [ ] WebOTA `<progress>` bar uses Sowel primary as the fill color.
- [ ] Remotes table is readable: row hairlines `--border`, headers in
      `--muted`, monospace-friendly position cell.
- [ ] No regression in JS behaviour: status loads, MQTT save, Wi-Fi
      save, remotes add/del/cmd, OTA upload, factory reset all work.
- [ ] Build clean on both envs (`pio run`), `pio check` clean,
      `pio test -e native` 74 cases still pass.
- [ ] Visual check on the bench device: `http://<bridge-ip>/` looks
      like a sibling of the spec-018 captive portal.
