# CLAUDE.md — somfyrts2mqtt

Bridge Somfy RTS <-> MQTT sur ESP32-C3 + CC1101. Conçu pour s'intégrer avec [Sowel](https://github.com/mchacher/sowel) (plugin `sowel-plugin-somfy-rts`) sur le pattern Zigbee2MQTT : le firmware est un bridge "dumb" qui parle MQTT, toute la logique métier (groupes, scènes, recipes) reste côté Sowel.

## Hardware

- **MCU** : ESP32-C3 Super Mini (WiFi BLE, USB-C, 4 MB flash)
- **RF** : module CC1101 (433.42 MHz pour Somfy RTS), quartz 26 MHz
- **Antenne** : fil 17.3 cm (1/4 d'onde) au pad ANT du CC1101

### Pinout (CC1101 <-> ESP32-C3)

| CC1101 module | ESP32-C3 | Note |
|---|---|---|
| VCC (3.3V) | 3V3 | jamais 5V (max 3.6V) |
| GND | GND | |
| SCK | GPIO4 | |
| MISO (silkscreen "MOSI/GD01") | GPIO5 | label module erroné, c'est bien MISO |
| MOSI | GPIO6 | |
| CSN | GPIO7 | |
| GDO0 | GPIO10 | TX sync |
| GDO2 | GPIO3 | RX sniff (optionnel) |

Strapping pins ESP32-C3 à éviter : **GPIO2, 8, 9**.

## Build / flash / monitor

```bash
# pio n'est pas dans le PATH, utiliser le chemin complet ou un alias
~/.platformio/penv/bin/pio run -d /Users/mchacher/Documents/04_PlatformIO/somfyrts2mqtt
~/.platformio/penv/bin/pio run -d . -t upload
~/.platformio/penv/bin/pio device monitor
```

Si l'upload échoue (la Super Mini n'a pas d'auto-reset) : maintenir BOOT, presser/relâcher RESET, relâcher BOOT, relancer upload.

## Architecture

```
Sowel plugin somfy-rts <--MQTT--> mosquitto <--MQTT--> ESP32-C3 + CC1101 <--RF 433.42--> volets Somfy
```

Topics MQTT (préfixe `somfy2mqtt`) :
- `somfy2mqtt/<remote_id>/set` : commande (`up` / `down` / `stop` / `program`)
- `somfy2mqtt/<remote_id>/state` : retained, dernière commande envoyée (RTS unidirectionnel, pas de feedback réel)
- `somfy2mqtt/<remote_id>/rolling_code` : retained, compteur courant (backup pour restore après flash)

Pairing : web UI sur l'ESP (mode AP au premier boot ou via long-press d'un GPIO). Une "télécommande virtuelle" = `(remote_id sur 24 bits, rolling_code)` stockée en NVS.

## Libs

- [Legion2/Somfy_Remote_Lib](https://github.com/Legion2/Somfy_Remote_Lib) — frames Somfy RTS (fork maintenu de Nickduino)
- [LSatan/SmartRC-CC1101-Driver-Lib](https://github.com/LSatan/SmartRC-CC1101-Driver-Lib) — driver CC1101 SPI
- À ajouter quand on en aura besoin : `knolleary/PubSubClient`, `bblanchon/ArduinoJson`, `esp32async/AsyncTCP` + `esp32async/ESPAsyncWebServer`

## Gotchas

1. **Quartz CC1101 26 vs 27 MHz** — `ELECHOUSE_cc1101.setMHZ(433.42)` suppose 26 MHz. Ici on est OK (vérifié sur le module). Avec un 27 MHz, il faudrait `setClb()` pour recalibrer.
2. **Rolling code** — perdre le compteur = ré-appairer physiquement chaque moteur (bouton PROG derrière le store). Persister en NVS + backup MQTT côté Sowel.
3. **RTS unidirectionnel** — pas de feedback du moteur. La "position" sera toujours estimée par durée de course côté Sowel.
4. **Antenne obligatoire** — sans le fil 17.3 cm, portée < 1 m.

## Style de code — C++ minimaliste

C++17 sobre, jamais du C pur (l'écosystème Arduino/ESP est C++, faire du C oblige à wrapper chaque lib).

**Règles** :

- **Une feature = un module** : `include/<nom>.h` + `src/<nom>.cpp`, encapsulés dans un `namespace` du même nom (`rf`, `mqtt`, `wifi`, `nvs_store`, `web_ui`).
- **`.h` minimal** : juste l'API publique et les types nécessaires. L'état interne reste en `static` dans le `.cpp`, pas exposé.
- **Fonctions libres > classes**. Une classe seulement quand un objet a une vraie identité ou un cycle de vie (ex. `Remote` virtuelle Somfy, client MQTT). Pas d'héritage sauf imposé par une lib.
- **`enum class`** pour tous les états et commandes — jamais d'`int` ou de `#define` constants.
- **`constexpr`** pour toute valeur connue à la compilation (pins, timeouts, topics, longueurs).
- **Pas d'allocation dynamique en régime stationnaire**. Buffers `static` ou sur la pile. `new`/`delete` interdits dans `loop()`.
- **Pas d'exceptions** (désactivées par défaut sur arduino-esp32). Erreurs via `bool`, `std::optional<T>`, ou code de retour explicite.
- **Pas de surcharge d'opérateurs**, pas de templates métier, pas de polymorphisme dynamique.

Exemple type :

```cpp
// include/rf.h
#pragma once
#include <cstdint>

namespace rf {
  bool init();                                   // false si CC1101 ne répond pas
  bool send(uint32_t remote_id,
            uint16_t rolling_code,
            uint8_t  button);
  uint8_t cc1101_version();
}
```

```cpp
// src/rf.cpp
#include "rf.h"
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include "config.h"

namespace rf {
  static bool s_ready = false;                   // état module, pas dans le .h

  bool init() {
    ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO,
                               CC1101_MOSI, CC1101_CSN);
    if (!ELECHOUSE_cc1101.getCC1101()) return false;
    ELECHOUSE_cc1101.Init();
    ELECHOUSE_cc1101.setMHZ(SOMFY_FREQ_MHZ);
    s_ready = true;
    return true;
  }
}
```

## Convention de commits

Conventional commits. Scopes utiles : `rf`, `mqtt`, `wifi`, `web`, `nvs`, `core`, `build`.

## Skills

- `somfy-iterate` — workflow de dev (spec courte → branche → code → flash & test HW → PR). Voir [.claude/skills/somfy-iterate/SKILL.md](.claude/skills/somfy-iterate/SKILL.md).

## Specs

Toute feature ou fix non trivial passe par `specs/XXX-<name>/` (spec.md / architecture.md / plan.md, chacun court). Pas de commit sans spec.
