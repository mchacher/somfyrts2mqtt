# somfyrts2mqtt

Somfy RTS to MQTT bridge running on ESP32-C3 + CC1101.

Designed as a companion device for [Sowel](https://github.com/mchacher/sowel) (plugin `sowel-plugin-somfy-rts`), following the same pattern as Zigbee2MQTT: the firmware is a "dumb" RF bridge that speaks MQTT; all home automation logic stays in Sowel.

## Hardware

- ESP32-C3 Super Mini
- CC1101 module (433.42 MHz, 26 MHz crystal)
- 17.3 cm wire antenna soldered to the CC1101 ANT pad

See [CLAUDE.md](CLAUDE.md) for the full pinout and design notes.

## Build

```bash
# Copy the secrets template and fill with your WiFi credentials
cp include/secrets.h.example include/secrets.h
$EDITOR include/secrets.h

# Build, check, test, flash
pio run
pio check
pio test -e native
pio run -t upload
pio device monitor
```

## Project layout

| Path | Role |
|---|---|
| `src/`, `include/` | Firmware source |
| `test/` | Native (host) unit tests (Unity) |
| `specs/` | Per-iteration specs (`XXX-name/`) — roadmap and design |
| `.github/workflows/` | CI (build + cppcheck + native tests), CodeQL, Dependabot |
| `CLAUDE.md` | Project conventions, code style, gotchas |

## Status

In active development. See [specs/](specs/) for the iteration roadmap.

## License

MIT — see [LICENSE](LICENSE).
