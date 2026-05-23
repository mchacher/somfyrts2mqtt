# Enclosure

3D-printable housing for the bridge. Holds the ESP32 + CC1101 module + SMA antenna connector with the USB-C cable threaded out the side.

![Assembled device with housing open](IMG_2297.jpg)

## Files

| File | Purpose |
|---|---|
| `somfyrts2mqtt_housing.FCStd` | FreeCAD source -- modify here, re-export the STLs |
| `somfyrts2mqtt_housing-box_housing.stl` | Main box, print 1× |
| `somfyrts2mqtt_housing-box_cover.stl` | Lid, print 1× |
| `IMG_2297.jpg` | Photo of the assembled device with the lid removed, USB-C cable and SMA whip antenna attached |

## Mechanical bill of materials

In addition to the [electronic BoM](../docs/hardware.md#bill-of-materials) you need :

| Part | Qty | Notes |
|---|---|---|
| **M2 heat-set brass insert** | 4 | Pressed into the 4 corner posts of the housing with a soldering iron at ~200 °C. The cover then bolts onto these inserts |
| **M2 screw, ~6 mm long** | 4 | Holds the cover onto the housing. Cap-head looks cleaner ; flat-head is fine |
| **SMA-to-PCB pigtail or SMA bulkhead** | 1 | Soldered to the CC1101 `ANT` pad (the housing has a notch for the SMA flange) |
| **433 MHz whip antenna with SMA male** | 1 | Or a 17.3 cm wire if you prefer the bare-wire route -- but the SMA + antenna combo looks far cleaner |

## Print settings

Tested with PLA on a Bambu Lab printer ; defaults are forgiving :

| Setting | Value |
|---|---|
| Material | PLA or PETG (PETG if the bridge sits in direct sunlight / heat) |
| Layer height | 0.2 mm |
| Infill | 20 % gyroid or grid |
| Perimeters | 3 |
| Top / bottom layers | 4 |
| Supports | None required (the design is print-orientation-friendly) |
| Brim | 5 mm for the housing if you have warping issues ; not needed for the lid |
| Print orientation | Housing : opening up. Lid : flat |

## Modifying the design

Open `somfyrts2mqtt_housing.FCStd` in [FreeCAD](https://www.freecad.org/) ≥ 0.21. The model is parametric -- key dimensions live in a spreadsheet at the top of the tree. After editing, re-export both STLs from the `Mesh` workbench.
