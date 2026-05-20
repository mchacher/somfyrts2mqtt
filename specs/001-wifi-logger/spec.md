# 001 — wifi + logger

## But
Mettre en place le squelette modulaire du firmware avec deux briques de base : un **logger série préfixé par module** et une **gestion WiFi non bloquante avec reconnect automatique**. Aucune fonctionnalité métier — juste valider la structure de code et démontrer la connexion réseau.

## Périmètre

**Inclus** :
- Module `logger` (namespace `logger`) : wrapper autour de `Serial.printf` avec préfixe `[<tag>]`
- Module `wifi` (namespace `wifi`) : connect + reconnect auto + état (connected / connecting / disconnected)
- Boot sequence dans `main.cpp` : `setup()` init logger puis wifi
- WiFi creds en `build_flags` via `include/secrets.h` (gitignored), template `include/secrets.h.example` commité

**Exclu** :
- Web UI / provisioning runtime (iter 007)
- mDNS, hostname custom, IPv6
- Watchdog / auto-restart
- Logs vers fichier ou réseau (Serial only)
- Niveaux de log filtrables (info/warn/err exposés, mais pas de filtre dynamique)

## Critères d'acceptation
- [ ] `include/secrets.h.example` présent dans le repo, `include/secrets.h` dans `.gitignore`
- [ ] Au boot, série affiche dans l'ordre : `[boot] hello somfyrts2mqtt`, `[wifi] connecting ssid=<x>`, `[wifi] connected ip=<x.x.x.x>` en moins de 10 secondes
- [ ] Si je coupe le WiFi (routeur off), série affiche `[wifi] disconnected`, puis au rétablissement `[wifi] reconnected ip=<x.x.x.x>` sans intervention manuelle
- [ ] API `logger::info(tag, fmt, ...)`, `logger::warn(tag, fmt, ...)`, `logger::err(tag, fmt, ...)` disponible
- [ ] Compile sans warning ni erreur, flash et tourne sur l'ESP32-C3 Super Mini

## Décisions à valider
- Choix retenu : **creds WiFi en `secrets.h` (gitignored)**. Alternative écartée : provisioning NVS via série au premier boot (sera couvert par iter 007 web UI, inutile d'en faire deux versions).
