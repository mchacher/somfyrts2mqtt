---
name: somfy-iterate
description: |
  Workflow de dev itératif pour somfyrts2mqtt (firmware ESP32-C3).
  Utiliser quand l'utilisateur demande "ajoute X", "implémente Y", "corrige Z" sur le firmware.
  Discipline simple mais rigoureuse : spec / architecture / plan courts, branche, HW test, PR.
disable-model-invocation: true
argument-hint: "[description de la tâche]"
---

# Dev loop somfyrts2mqtt

Tâche : $ARGUMENTS

Lis [CLAUDE.md](../../../CLAUDE.md) si tu n'as pas déjà le contexte (hardware, pinout, libs, gotchas).

---

## Phase 1 — Spec

Avant de coder, créer le dossier `specs/XXX-<kebab-name>/` (numéro séquentiel 3 chiffres) avec **3 fichiers courts** :

### `spec.md`
```markdown
# XXX — <nom>

## But
1-2 phrases : quoi et pourquoi.

## Périmètre
- Inclus : ...
- Exclu : ...

## Critères d'acceptation
- [ ] critère 1 vérifiable
- [ ] critère 2 vérifiable
```

### `architecture.md`
```markdown
# Architecture XXX

## Modules touchés
`src/...`, `include/config.h`, `platformio.ini` (libs)

## Décisions
- Choix retenu : ... (alternative écartée : ... car ...)
- Format MQTT / topic / NVS key impacté

## Flow
Schéma ASCII ou bullets : qui appelle quoi, dans quel ordre.
```

### `plan.md`
```markdown
# Plan XXX

## Étapes
1. ...
2. ...
3. ...

## Test plan (HW)
Comment vérifier sur la carte :
- Sortie série attendue : `[xxx] ...`
- Outil externe : sniffer RF / MQTT Explorer / navigateur
- Cas nominal + 1-2 cas limites
```

**Avant Phase 2** : présenter le résumé à l'utilisateur et attendre OK explicite ("oui", "go") avant de coder.

---

## Phase 2 — Branche

```bash
git checkout main && git pull
git checkout -b <prefix>/<short-name>
```

Préfixes : `feat/`, `fix/`, `refactor/`, `chore/`. Jamais de commit direct sur `main`. Vérifier la branche **juste avant** chaque commit (`git branch --show-current`).

---

## Phase 3 — Implémenter

- Code **simple**. Une feature = un module si possible (`.h` + `.cpp` dans `src/`).
- Pins / constantes / topics dans `include/config.h`, jamais en dur dans le code métier.
- Pas de `delay()` bloquant dans `loop()` quand MQTT/Web tournent — timers non bloquants (`millis()`).
- Logs `Serial.printf` préfixés par module : `[rf] frame sent`, `[mqtt] connected`, etc.
- Nouvelle lib → ajout à `platformio.ini` (`lib_deps`) + mention dans le commit.

Si une décision change pendant l'implémentation, **mettre à jour `architecture.md`** au lieu de perdre l'info dans la conversation.

---

## Phase 4 — Build, flash, test sur HW

```bash
~/.platformio/penv/bin/pio run -d .              # compile, zéro erreur
~/.platformio/penv/bin/pio run -d . -t upload    # flash
~/.platformio/penv/bin/pio device monitor        # vérifier série
```

Pas de tests unitaires sur ce projet. La validation = exécuter le test plan de `plan.md` sur la carte. Cocher les critères d'acceptation dans `spec.md` au fur et à mesure.

Upload qui bloque : maintenir BOOT, press/release RESET, release BOOT, relancer.

**Avant Phase 5** : tous les critères d'acceptation cochés, build clean, HW test passé.

---

## Phase 5 — Commit, push, PR

Conventional commits, **sans** `Co-Authored-By`.

```bash
git add <fichiers précis>     # jamais git add -A
git commit -m "feat(rf): description courte

Pourquoi (1-2 phrases si non évident). Ref spec XXX."
git push -u origin <branche>
gh pr create --title "..." --body "..."
```

Scopes : `rf`, `mqtt`, `wifi`, `web`, `nvs`, `core`, `build`, `docs`, `spec`.

PR body : résumé + lien vers `specs/XXX-name/` + checklist HW test.

---

## Phase 6 — Merge

**Toujours attendre OK explicite** de l'utilisateur ("oui", "merge", "go") avant `gh pr merge`. Jamais de merge auto.

```bash
gh pr merge <num> --merge --delete-branch
git checkout main && git pull
```

Une fois mergé : statut dans `spec.md` mis à jour (acceptance criteria tous `[x]`).

---

## Rappels HW

- 3.3V max sur le CC1101, jamais 5V
- Strapping pins à ne pas utiliser : GPIO2, GPIO8, GPIO9
- Rolling code = état critique → NVS + backup MQTT
- Quartz 26 MHz : `setMHZ(433.42)` direct
