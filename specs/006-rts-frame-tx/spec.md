# 006 — première trame Somfy RTS réelle

## But
Remplacer le stub `rf::send_somfy` (iter 004) par une vraie émission via `Legion2/Somfy_Remote_Lib` + `ELECHOUSE_cc1101`. Première trame Somfy RTS sur 433.42 MHz.

## Validation
- SDR (RTL-SDR + Universal Radio Hacker) pour décoder la trame émise
- **OU** mode PROG sur un vrai moteur Somfy : appui long bouton derrière le store → envoi PROG depuis l'ESP → le moteur fait un coup d'aller-retour de confirmation

## Modules cibles
`src/rf.cpp` (remplace le stub d'émission)

## Statut
Backlog — sera détaillé au démarrage de l'iter (3 fichiers spec/architecture/plan).
