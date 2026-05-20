# 005 — cc1101 init + ping

## But
Init SPI vers le CC1101, lecture des registres `PARTNUM` (attendu `0x00`) et `VERSION` (attendu `0x14`), calibration 433.42 MHz. Pas d'émission, juste valider que le module répond et que le câblage est correct.

## Modules cibles
`src/rf.cpp` (remplace le stub d'init de l'iter 004)

## Pré-requis
Hardware CC1101 reçu et câblé.

## Statut
Backlog — sera détaillé au démarrage de l'iter (3 fichiers spec/architecture/plan).
