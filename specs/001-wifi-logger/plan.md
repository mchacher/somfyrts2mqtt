# Plan 001

## Étapes

1. Créer `include/secrets.h.example` avec deux `#define` placeholders + ajouter `include/secrets.h` à `.gitignore`
2. Créer `include/secrets.h` localement (rempli avec mes vrais creds, non commité)
3. Implémenter `include/logger.h` + `src/logger.cpp` (~30 lignes total)
4. Implémenter `include/wifi_manager.h` + `src/wifi_manager.cpp` (~50 lignes)
5. Refactorer `src/main.cpp` pour orchestrer logger + wifi
6. Build (zéro warning, zéro erreur) + flash + monitor série

## Test plan (HW)

| Cas | Action | Attendu en série |
|---|---|---|
| **Nominal** | Flash avec `secrets.h` correct, observer le boot | `[boot] hello somfyrts2mqtt` → `[wifi] connecting ssid=<x>` → `[wifi] connected ip=<x.x.x.x>` en < 10 s |
| **Reconnect** | Pendant que ça tourne, couper le routeur 30 s puis le rallumer | `[wifi] disconnected reason=<n>` puis `[wifi] connected ip=<x.x.x.x>` automatiquement |
| **Secrets manquant** | Renommer temporairement `secrets.h` et relancer build | Erreur de compilation claire (`fatal error: secrets.h: No such file or directory`), pas un crash runtime |
| **Mauvais SSID** | Mettre un SSID inexistant dans `secrets.h`, flash | `[wifi] connecting ssid=<bidon>` puis `[wifi] disconnected reason=...` répété toutes les ~5 s |
