# 013 — UX hint for the Erase 2-remote workflow

## Goal
Prevent the user from silently falling into the self-erase trap with the 🗑 Erase button. Somfy receivers exclude the issuing remote from the deletion candidates, so erasing a remote requires emitting the 7 s PROG from a DIFFERENT already-paired remote. The current UI lets the user click Erase on the remote they want to delete with no feedback that the operation cannot succeed.

## Background
Discovered during iter 011 HW validation. The user clicked 🗑 Erase on remote `A1B2C3` then brief 🔗 Prog on the same `A1B2C3`, expecting it to be erased. The motor moved during the 7 s emission (normal long-press signaling) but `A1B2C3` stayed paired. Initial misdiagnosis (timing) led to iter 012, which was then closed without merging once the user verified that iter 011 actually works with the correct 2-remote workflow.

## Scope

**In scope:**
- Add a JavaScript `confirm()` modal triggered by the 🗑 Erase click, explaining the 2-remote requirement and showing which remote is the *source* (the one being clicked) vs the unknown *target* (to be picked next). The user must click OK to proceed; Cancel aborts the request.
- No change on Pair (➕): the workflow is intuitive (pair a new remote from an existing one).
- No change on Prog brief (🔗): it is the confirmation step of an already-initiated workflow.

**Out of scope:**
- Multi-step wizard (would be over-engineered for a single warning).
- Rich modal with screenshots / video. Native `confirm()` is enough.
- Changes to the backend or RF layer.

## Acceptance criteria
- [ ] Clicking 🗑 Erase on a remote opens a `confirm()` modal that names the clicked remote as the *source* and explains that the *target* (to be erased) is a different already-paired remote whose brief Prog must be clicked within ~10 s of the motor jog.
- [ ] Clicking OK in the modal triggers the existing POST flow (no behavior change beyond the gate).
- [ ] Clicking Cancel aborts: no POST, no row disable.
- [ ] No regression on the other buttons (Up / Stop / Down / Prog brief / Pair) -- their flows are untouched.
- [ ] Build clean on both envs, `pio check` zero defects, `pio test -e native` all green (46 cases, unchanged).
- [ ] CI green on the PR.

## Decisions
- **Native `confirm()` modal, not a custom one.** The codebase already uses `confirm()` for the delete-remote action (`web_ui.cpp` row 165-169). Keeping the same primitive avoids adding CSS / event handling for a single warning.
- **Source / target wording in the message.** "Source" is the remote being clicked, "target" is the remote to be erased afterwards. Explicit role labels make the workflow unambiguous.
- **No tooltip on Pair.** Pairing a NEW remote from an EXISTING one is intuitive and matches the Somfy mental model. Adding a modal there would just nag.
