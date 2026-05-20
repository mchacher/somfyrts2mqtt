---
name: somfy-iterate
description: |
  Iterative dev workflow for somfyrts2mqtt (ESP32-C3 firmware).
  Use when the user asks to "add X", "implement Y", "fix Z" on the firmware.
  Simple but rigorous discipline: short spec / architecture / plan, branch, HW test, PR.
disable-model-invocation: true
argument-hint: "[task description]"
---

# Dev loop somfyrts2mqtt

Task: $ARGUMENTS

Read [CLAUDE.md](../../../CLAUDE.md) if you don't already have the context (hardware, pinout, libraries, gotchas, code style, tooling).

All written content stays in English (code, comments, specs, commits, PR bodies). Conversation with the maintainer may be in French.

---

## Phase 1 — Spec

Before writing code, create the folder `specs/XXX-<kebab-name>/` (3-digit sequential number) with **three short files**:

### `spec.md`
```markdown
# XXX — <name>

## Goal
1-2 sentences: what and why.

## Scope
- In scope: ...
- Out of scope: ...

## Acceptance criteria
- [ ] verifiable criterion 1
- [ ] verifiable criterion 2
```

### `architecture.md`
```markdown
# Architecture XXX

## Touched modules
`src/...`, `include/config.h`, `platformio.ini` (libs)

## Decisions
- Selected option: ... (rejected alternative: ... because ...)
- Affected MQTT topic / NVS key / format

## Flow
ASCII diagram or bullets: who calls whom, in what order.
```

### `plan.md`
```markdown
# Plan XXX

## Steps
1. ...
2. ...
3. ...

## Test plan (HW)
How to verify on the board:
- Expected serial output: `[xxx] ...`
- External tool: RF sniffer / MQTT Explorer / browser
- Nominal case + 1-2 edge cases
```

**Before Phase 2**: present a summary to the user and wait for explicit OK ("yes", "go") before starting to code.

---

## Phase 2 — Branch

```bash
git checkout main && git pull
git checkout -b <prefix>/<short-name>
```

Prefixes: `feat/`, `fix/`, `refactor/`, `chore/`. Never commit directly to `main`. Verify the branch **right before** each commit (`git branch --show-current`).

---

## Phase 3 — Implement

- Keep code **simple**. One feature = one module if possible (`.h` + `.cpp` in `src/`).
- Pins, constants, topics in `include/config.h`; never hardcoded in business code.
- No blocking `delay()` in `loop()` once MQTT/Web are running — use non-blocking timers (`millis()`).
- Log via `logger::info` / `warn` / `err` with a `[tag]` prefix.
- New library → add to `platformio.ini` (`lib_deps`) and mention it in the commit message.
- Follow the Doxygen comment convention (see CLAUDE.md).

If a decision changes during implementation, **update `architecture.md`** rather than letting the change live only in the conversation.

---

## Phase 4 — Build, flash, HW test

```bash
~/.platformio/penv/bin/pio run -d .              # compile, zero warnings, zero errors
~/.platformio/penv/bin/pio check -d .            # static analysis (cppcheck)
~/.platformio/penv/bin/pio test -d . -e native   # native unit tests
~/.platformio/penv/bin/pio run -d . -t upload    # flash
~/.platformio/penv/bin/pio device monitor        # check serial
```

Pure-logic modules **must** have native unit tests (NVS, rolling code, topic parsing, etc.). Hardware-bound modules (logger, wifi, RF emission) are validated by the HW test plan.

Validation = run the test plan from `plan.md` on the board. Tick the acceptance criteria in `spec.md` as you go.

If upload hangs: hold BOOT, press and release RESET, release BOOT, retry.

**Before Phase 5**: all acceptance criteria ticked, build clean, `pio check` and `pio test` both green, HW test plan passed.

---

## Phase 5 — Commit, push, PR

Conventional Commits, **no** `Co-Authored-By`.

```bash
git add <specific files>     # never git add -A
git commit -m "feat(rf): short description

Why this change (1-2 sentences if not obvious). Ref spec XXX."
git push -u origin <branch>
gh pr create --title "..." --body "..."
```

Scopes: `rf`, `mqtt`, `wifi`, `web`, `nvs`, `core`, `build`, `ci`, `docs`, `spec`, `test`.

PR body: summary + link to `specs/XXX-name/` + HW test checklist.

---

## Phase 6 — Merge

**Always wait for the user's explicit OK** ("yes", "merge", "go") before `gh pr merge`. Never auto-merge.

```bash
gh pr merge <num> --merge --delete-branch
git checkout main && git pull
```

Once merged: update the status in `spec.md` (all acceptance criteria ticked).

---

## Hardware quick reminders

- 3.3V max on the CC1101; never 5V
- Strapping pins to avoid: GPIO2, GPIO8, GPIO9
- Rolling code = critical state → NVS + MQTT retained backup
- 26 MHz crystal: `setMHZ(433.42)` directly, no custom calibration
