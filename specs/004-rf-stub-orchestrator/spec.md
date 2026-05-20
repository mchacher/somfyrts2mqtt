# 004 — rf (stub) + orchestrator

## But
Module `rf` avec `rf::send_somfy()` **stubbé** (log au lieu d'émettre). Câbler la chaîne complète : MQTT reçu → NVS lookup remote → `rf::send_somfy` → incrément `rolling_code` → re-persiste → publish state retained.

À la fin de cette iter, le firmware est **fonctionnellement complet** côté logique métier ; il manque juste la vraie émission RF (iter 006).

## Modules cibles
`include/rf.h`, `src/rf.cpp` (stub), `src/orchestrator.cpp`

## Statut
Backlog — sera détaillé au démarrage de l'iter (3 fichiers spec/architecture/plan).
