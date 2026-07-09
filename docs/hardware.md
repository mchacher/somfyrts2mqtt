# Hardware

The bridge needs three things : an ESP32 MCU, a CC1101 sub-GHz transceiver, and a 433 MHz antenna. Total cost is around 5-10 €.

## Bill of materials

| Item | Notes |
|---|---|
| **ESP32-C3 Super Mini** _or_ **ESP32-S3 (PICYBI Radio Remote)** | Two supported boards. The C3 Super Mini is the reference DIY build (compact, native USB-CDC, 3.3 V LDO from USB). The PICYBI is an off-the-shelf ESP32-S3 board with the CC1101 already wired — nothing to solder. Each has its own pinout (below) and its own release binary |
| **CC1101 module** at 433 MHz, 26 MHz crystal | Any clone works ; some have flaky SPI -- see [troubleshooting](troubleshooting.md). Verify the crystal frequency on the module label : 27 MHz boards exist and need a code tweak |
| **17.3 cm of solid copper wire** | Quarter-wave at 433.42 MHz. Soldered to the CC1101 `ANT` pad. Skip if the board already has a spring antenna or an SMA connector |
| **Dupont jumper wires** | 7 wires : SCK, MISO, MOSI, CSN, GDO0, VCC, GND. Keep short (≤ 5 cm) to avoid SPI signal degradation |
| **(Optional) 100 nF + 100 µF decoupling capacitors** | Across the CC1101 VCC / GND, close to the module. Helps borderline modules ; see [troubleshooting](troubleshooting.md) |

## Wiring — ESP32-C3 Super Mini

| CC1101 pin | ESP32-C3 GPIO | Notes |
|---|---|---|
| VCC | 3V3 | Do **not** use 5V -- CC1101 is 3.3 V only |
| GND | GND | Shared ground |
| SCK | GPIO 4 | SPI clock |
| MISO | GPIO 5 | SPI data in |
| MOSI | GPIO 6 | SPI data out |
| CSN | GPIO 7 | Chip select |
| GDO0 | GPIO 10 | TX data line (modulation input) |
| GDO2 | GPIO 3 | Optional, RX / sniffing (not used in current firmware) |

Strapping pins to avoid on the C3 : GPIO 2, 8, 9.

```
ESP32-C3 Super Mini                 CC1101 module
                   ┌─────────────┐
              3V3  │ ●           │
              GND  │ ●           │
              ...                ▶ VCC  (3.3 V)
              GPIO 4   ────────────▶ SCK
              GPIO 5   ◀────────────  MISO
              GPIO 6   ────────────▶ MOSI
              GPIO 7   ────────────▶ CSN
              GPIO 10  ────────────▶ GDO0  (TX)
              GPIO 3   ◀────────────  GDO2  (RX, optional)
              GND      ────────────▶ GND
                   │             │
                   └─────────────┘
                          ANT  ───────  17.3 cm wire antenna
```

## Wiring — ESP32-S3 (PICYBI Radio Remote)

This board already has the CC1101 wired to the S3, so there is nothing to
solder — this table just documents the mapping the firmware expects
(`pio run -e esp32-s3-picybi`). Flash with the `esp32-s3-picybi` binary.

| CC1101 pin | ESP32-S3 GPIO | Notes |
|---|---|---|
| VCC | 3V3 | 3.3 V only |
| GND | GND | Shared ground |
| SCK | GPIO 12 | SPI clock |
| MISO | GPIO 13 | SPI data in |
| MOSI | GPIO 11 | SPI data out |
| CSN | GPIO 10 | Chip select |
| GDO0 | GPIO 8 | TX data line (modulation input) |
| GDO2 | GPIO 9 | Optional, RX / sniffing (not used in current firmware) |

Strapping pins on the S3 are GPIO 0, 3, 45, 46 — so GDO0/GDO2 on GPIO 8/9 are
fine here (they are *not* strapping on the S3, unlike the C3). The board uses
the native USB-Serial/JTAG for flashing and serial; the boot log over USB is
lost to CDC re-enumeration unless the firmware is built with `-DWAIT_FOR_SERIAL`
(dev-only, off by default).

## Antenna

A 17.3 cm length of solid copper wire (quarter-wave at 433.42 MHz) gives reliable whole-house coverage. Solder one end to the CC1101 `ANT` pad, leave the rest straight or in a vertical helix. Keep the wire away from the ESP32 metal can and any large metal surface.

If the board already has a spring antenna or an SMA connector, skip the wire. SMA + a real antenna gives the best range.

## Power supply

USB power from a 5 V / 2 A wall adapter is the recommended setup. Avoid :

- **Laptop USB hubs without external power** — they often sag below 4.6 V under load, which propagates to a sagging 3.3 V on the ESP32 LDO, which propagates to flaky WiFi and CC1101 SPI errors.
- **Long / thin USB cables** — voltage drops up to 0.5 V on the supply rail during WiFi TX bursts.

A 100 µF electrolytic cap across the CC1101 VCC / GND can rescue borderline setups. See [troubleshooting](troubleshooting.md).

## ESP32-C3 Super Mini specific note

The C3 Super Mini PA is miscalibrated above ~15 dBm on some boards. The firmware clamps `WiFi.setTxPower()` to 8.5 dBm explicitly to avoid AUTH_EXPIRE loops on certain APs (notably Freebox). This is automatic ; no user action required. See [troubleshooting](troubleshooting.md) for the full story.

## Enclosure (3D-printable)

A FreeCAD design + STLs for a small printed housing live in [`meca/`](../meca/). The box holds the ESP32 + CC1101 with an SMA bulkhead on top for an external whip antenna and a USB-C cutout on the side.

![Assembled device, lid open](../meca/IMG_2297.jpg)

Additional parts beyond the BoM above :

- **4 × M2 heat-set brass inserts** (pressed into the housing posts with a soldering iron)
- **4 × M2 screws** (~6 mm) to hold the lid onto the inserts

Full file inventory, print settings, and assembly notes : [`meca/README.md`](../meca/README.md).
