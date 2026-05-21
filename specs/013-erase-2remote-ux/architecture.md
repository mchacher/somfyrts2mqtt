# Architecture 013

## Touched modules

| File | Change |
|---|---|
| `src/web_ui.cpp` | Inline JS: add a `confirm()` gate when `data-cmd == "program7s"` is clicked, with a source/target explanation message |

No other files. No C++ side change. No new endpoint, no NVS field, no protocol bump.

## JS handler diff (high level)

The existing click handler in `web_ui.cpp` is :

```js
body.querySelectorAll('.cmd-cell button').forEach(b => b.onclick = async () => {
  const id = b.parentElement.dataset.id;
  const cmd = b.dataset.cmd;
  const row = b.parentElement.querySelectorAll('button');
  row.forEach(x => x.disabled = true);
  try {
    await fetchJSON(`/api/remotes/${id}/${cmd}`, {method:'POST'});
    await new Promise(r => setTimeout(r, TX_MS[cmd] || 400));
    await loadRemotes();
  } catch (e) {
    show('#remotes-msg', 'err', e.message);
    row.forEach(x => x.disabled = false);
  }
});
```

Iter 013 inserts a confirm gate before the `row.disabled = true` line :

```js
if (cmd === 'program7s' && !confirm(
    `Erase workflow :\n\n` +
    `1. This emits a 7 s long-press from ${id} (the SOURCE).\n` +
    `2. The motor will jog after ~7 s, then waits ~10 s.\n` +
    `3. Within that window, click Prog briefly on the TARGET ` +
    `remote (different from ${id}) to erase it.\n\n` +
    `Note : Somfy forbids self-erase ; the target must be a ` +
    `different already-paired remote.\n\n` +
    `Proceed ?`)) {
  return;  // user cancelled
}
```

`confirm()` is synchronous and returns true/false ; the rest of the flow is untouched.

## Why a native confirm and not a styled modal

- Single warning, no follow-up dialog.
- Already-used primitive in the same file (delete-remote row, line 165-167).
- Browsers render `confirm()` consistently enough for our LAN-only context.
- Zero CSS / DOM event additions.

## Multi-line text in confirm()

Modern browsers (Chrome 90+, Firefox 90+, Safari 14+) render `\n` as line breaks in `confirm()`. We are LAN-only on a modern browser, so this is safe.
