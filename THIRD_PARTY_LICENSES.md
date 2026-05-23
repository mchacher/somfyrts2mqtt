# Third-party licenses

The bridge firmware (this repository) is licensed under **GNU GPL v3.0** -- see [LICENSE](LICENSE).

At runtime, it links against the following third-party libraries (declared in [`platformio.ini`](platformio.ini) `lib_deps`). Each is redistributed unmodified by PlatformIO at build time ; full license text and source are available from each project's upstream repository.

| Library | Version constraint | License | Upstream |
|---|---|---|---|
| ArduinoJson | `^7.0.4` | MIT | <https://github.com/bblanchon/ArduinoJson> |
| PubSubClient | `^2.8` | MIT | <https://github.com/knolleary/pubsubclient> |
| SmartRC-CC1101-Driver-Lib | latest | MIT | <https://github.com/LSatan/SmartRC-CC1101-Driver-Lib> |
| Somfy_Remote_Lib | latest | Apache 2.0 | <https://github.com/Legion2/Somfy_Remote_Lib> |
| WiFiManager (tzapu) | `^2.0.17` | MIT | <https://github.com/tzapu/WiFiManager> |
| AsyncTCP (esp32async) | `^3.4.9` | LGPL-3.0 | <https://github.com/esp32async/AsyncTCP> |
| ESPAsyncWebServer (esp32async) | `^3.11.0` | LGPL-3.0 | <https://github.com/esp32async/ESPAsyncWebServer> |

The Arduino-ESP32 core (LGPL-2.1) and the ESP-IDF (Apache 2.0) are linked through the PlatformIO `espressif32` platform, also redistributed unmodified.

## License compatibility

GNU GPL v3.0 is compatible with all the licenses above :

- **MIT** → GPL : MIT is permissive, can be sublicensed under GPL.
- **Apache 2.0** → GPL : explicitly compatible since GPL-3.0 ([FSF list](https://www.gnu.org/licenses/license-list.html#apache2)).
- **LGPL-3.0** → GPL : the LGPL explicitly allows conversion to GPL.
- **LGPL-2.1** → GPL-3.0 : compatible via the LGPL's "any later version" clause.

LGPL-3.0 dependencies impose that the binary must remain relinkable against a different version of the library by the end user. Since this project is open-source and the `firmware.bin` published in [GitHub Releases](https://github.com/mchacher/somfyrts2mqtt/releases) is built from the source tagged in this same repository, anyone can rebuild and relink at will -- the LGPL obligation is satisfied by design.

## Acknowledgements

Special thanks to the maintainers of the libraries above. This bridge would not exist without their work, in particular :

- [@Legion2](https://github.com/Legion2) for the Somfy RTS protocol implementation that does the heavy lifting of frame building and bit-banging
- [@LSatan](https://github.com/LSatan) for the CC1101 driver
- [@tzapu](https://github.com/tzapu) for WiFiManager, which powers the captive portal commissioning flow
- [@esp32async](https://github.com/esp32async) for keeping AsyncTCP / ESPAsyncWebServer maintained
- [@knolleary](https://github.com/knolleary) and [@bblanchon](https://github.com/bblanchon) for the MQTT and JSON pillars of the embedded world
