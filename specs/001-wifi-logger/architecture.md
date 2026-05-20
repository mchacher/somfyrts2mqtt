# Architecture 001

## Modules touchés

| Fichier | Rôle |
|---|---|
| `include/logger.h` + `src/logger.cpp` | Namespace `logger`, fonctions `info` / `warn` / `err` avec préfixe tag |
| `include/wifi_manager.h` + `src/wifi_manager.cpp` | Namespace `wifi`, init + reconnect via events ESP32 |
| `include/secrets.h.example` | Template commité (placeholders `WIFI_SSID`, `WIFI_PASSWORD`) |
| `include/secrets.h` | Vrais creds (gitignored) |
| `.gitignore` | Ajouter `include/secrets.h` |
| `src/main.cpp` | Refactoré : init logger → init wifi, `loop()` vide |

Pas de modification de `platformio.ini` (WiFi.h est dans le core ESP32 Arduino).

## Décisions

- **`Serial.printf` direct dans le namespace `logger`, pas de framework de logging**. Le nom `logger` plutôt que `log` évite tout conflit avec `log()` de `<cmath>` inclus par Arduino.h. KISS : si on a besoin un jour de niveaux filtrables ou de logs vers fichier/réseau, on évoluera. Pour le moment c'est juste un préfixe + newline auto.
- **`wifi::init()` n'est pas bloquante**. On ne fait pas `while(!WiFi.isConnected()) delay()`. À la place, on s'abonne aux events ESP32 (`WiFi.onEvent`) pour réagir aux transitions, et `WiFi.begin()` retourne immédiatement.
- **Reconnect via `WiFi.setAutoReconnect(true)`** : pas besoin de réimplémenter une state machine, le core ESP32 gère le retry tout seul. On se contente de loguer les events.
- **Pas d'API publique pour `wifi::loop()`** : rien à faire dans le `loop()` principal pour cette iter, mais on garde la fonction (no-op) pour la convention.

## Flow

```
setup()
  ├─ Serial.begin(115200); delay(200)
  ├─ logger::info("boot", "hello somfyrts2mqtt")
  └─ wifi::init()
        ├─ WiFi.onEvent(on_wifi_event)
        ├─ WiFi.mode(WIFI_STA)
        ├─ WiFi.setAutoReconnect(true)
        ├─ logger::info("wifi", "connecting ssid=%s", WIFI_SSID)
        └─ WiFi.begin(WIFI_SSID, WIFI_PASSWORD)

on_wifi_event(event)
  ├─ SYSTEM_EVENT_STA_GOT_IP        → logger::info("wifi", "connected ip=%s", ip)
  ├─ SYSTEM_EVENT_STA_DISCONNECTED  → logger::warn("wifi", "disconnected reason=%d", r)
  └─ (auto-reconnect géré par le core)

loop()
  └─ wifi::loop()  (no-op pour l'instant)
```

## API publique

```cpp
// logger.h
namespace logger {
  void info(const char* tag, const char* fmt, ...);
  void warn(const char* tag, const char* fmt, ...);
  void err (const char* tag, const char* fmt, ...);
}

// wifi_manager.h
namespace wifi {
  void init();
  void loop();
  bool is_connected();
}
```
