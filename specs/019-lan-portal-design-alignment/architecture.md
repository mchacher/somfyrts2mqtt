# Architecture 019 — LAN portal design alignment

## Touched modules

| File             | Change                                                                                |
| ---------------- | ------------------------------------------------------------------------------------- |
| `src/web_ui.cpp` | Replace the `<style>` block inside `INDEX_HTML`. No markup / JS / route changes.      |

No other module touched, no new dependencies, no NVS / MQTT impact, no
build flag changes.

## Decisions

**Selected option: only the CSS string changes; HTML and JS stay byte-for-byte
identical.**

`web_ui.cpp` is ~1000 lines of HTML + JS embedded in a single
`R"HTML(...)HTML"` PROGMEM string. The form selectors, IDs, REST endpoints
and the JS event wiring are all referenced from the runtime. Rewriting the
markup is risky and offers zero functional gain — the only thing we want
is a different visual layer. Limiting the change to the `<style>` block
makes the diff trivially reviewable and keeps the JS code paths untouched.

**Rejected alternatives:**

1. *Two separate STYLE constants extracted to a shared header.* — Tempting
   for sharing the palette between `web_ui.cpp` and `captive_portal.cpp`,
   but the two pages have different non-overlapping selectors (the captive
   portal does not have tables, command cells, position cells, status
   pills, OTA progress bar, dot separators, etc.). Sharing only the
   `:root` block via a tiny `web_styles.h` would save a few dozen lines
   but adds a new include that has to be kept in sync — we accept the
   duplication of the palette tokens for now.
2. *Load fonts from Google Fonts.* — The LAN portal could afford it (the
   bridge is online), but using the system stack matches the captive
   portal one-for-one and avoids a third-party fetch on first paint.

## Mapping: existing classes → new visual treatment

| Class / element              | Today                                | New                                                                                                   |
| ---------------------------- | ------------------------------------ | ----------------------------------------------------------------------------------------------------- |
| `body`                       | grey `#1a1a1a`                       | `--bg`, system sans-serif, max-width 880 px container                                                 |
| `h1`                         | small inline header                  | SOWEL brand block (letter-spaced 4 px, `--primary`) + tagline                                         |
| `h2`                         | muted small caps                     | `--primary` 13 px, uppercase, ls 1.2 px, hairline underline                                           |
| `section`                    | `--panel`, 1 px border               | `--card` (white), 1 px `--border`, radius 10 px, padded 18 px                                         |
| `.cards-row`                 | flex two-up                          | flex / wrap two-up; collapses to single column under 720 px                                           |
| `button.primary`             | blue `--accent`                      | `--primary` background, `#fff` text, radius 6 px, hover `--primary-hover`                             |
| `button.danger`              | red `--danger`                       | `--error` background, hover slightly darker                                                           |
| `button` (default)           | grey                                 | white on `--card` with 1 px `--border`, hover `--primary-light`                                       |
| `input`, `select`            | dark grey background                 | `#fff` background, 1 px `--border`, focus ring `--primary-light`                                      |
| `.row`                       | grid 100 px / 1 fr                   | unchanged, but with new colour tokens                                                                 |
| `.pass-wrap` + `.eye`        | grey icon                            | `.eye` uses `currentColor` set to `--muted`, hover `--primary`, background `--primary-light` on hover |
| `.status-line` + `.dot`      | muted small                          | `--muted` text, `.dot` is a `--border` mid-dot                                                        |
| `.pill.ok` / `.pill.bad`     | green / red filled                   | `#fff` text on `--primary` (ok) / `--error` (bad), radius 999 px, font 11 px                          |
| `.kv`                        | grid label / value                   | labels in `--muted`, values in `--ink`                                                                |
| `.msg.ok` / `.msg.err`       | translucent green / red              | `--primary-light` bg + `--primary` text (ok) ; `--error-bg` + `--error` (err)                         |
| `.table-wrap` + `table`      | overflow-x scroll                    | unchanged behaviour ; `<th>` `--muted`, row hairlines `--border`                                      |
| `.cmd-cell button`           | grey 24×24                           | white on `--card`, `--border` outline, hover `--primary`                                              |
| `.pos-cell .pos-cur`         | inline grey                          | monospace, `--ink`, `tabular-nums`                                                                    |
| `.setup-row` background     | dark indigo `#1a1a25`                | `--primary-light`, with `--border` separator on bottom                                                |
| `.setup-group button.prog`   | dark blue                            | `--primary-light` bg, `--primary` ink, hover `--primary` bg + white                                   |
| `.setup-group button.pair`   | dark green                           | green-50ish bg (`#E6F4EA`), success-700 ink (`#1F7A36`), hover swap                                   |
| `.setup-group button.erase`  | dark red                             | `--error-bg`, `--error` ink, hover `--error` + white                                                  |
| `.setup-group button.sync`   | grey                                 | default button style                                                                                  |
| `.add-form` (Remotes add)    | grid id / name / button              | unchanged grid; uses new input/button styling                                                         |
| `<progress>` (OTA)           | browser default                      | full-width, 10 px tall, rounded, value bar `--primary`                                                |
| `footer`                     | muted small                          | `--muted` 11 px, letter-spacing 0.5 px                                                                |
| Body header (above sections) | `<h1>` only                          | `<h1>` + `.tag` line, identical structure to the captive portal                                       |

## Flow

```
Browser GET /
  └── web_ui.cpp serves INDEX_HTML (PROGMEM)
        └── page renders with the NEW style
              └── JS (unchanged) hydrates Status / MQTT / WiFi / Remotes
                    └── WebOTA section + Factory-reset button respond as before
```

No other code path changes. No measurable RAM / flash impact (the CSS
string changes size by a few hundred bytes at most).

## Container width

Captive portal: 480 px (phone-first). LAN portal: keep ~880-1000 px
because the Remotes table needs the room. The visual style is identical
across the two pages; only the page container is wider on LAN.

## Header markup

Replace today's lone `<h1>somfyrts2mqtt</h1>` with:

```html
<header>
  <div class="brand">SOWEL</div>
  <div class="tag">Configuration du bridge Somfy RTS</div>
</header>
```

Strictly additive — no existing element is removed. JS does not query
`<h1>` so the change is safe.
