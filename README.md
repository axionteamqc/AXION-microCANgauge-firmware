# AXION MicroCANgauge (firmware)

Small dual‑OLED CAN gauge for **MegaSquirt** ECUs.

This repository contains the **ESP32‑C3 firmware** that listens to MegaSquirt **Simplified Dash Broadcast (SDB)** frames and renders a compact gauge UI.

## Compatibility contract (non‑negotiable)
The firmware expects **Simplified Dash Broadcast** to be:

- **Enabled**
- **Mode: Automatic (standard)**
- **Base CAN ID: 1512 (0x5E8)**

If you use **Advanced** mode and change base IDs / layout, your frames will not match the included DBC and decoding will fail.

> User‑configurable items in this project are intentionally limited:
> - Sensor 1 / Sensor 2 scaling and selection (via Wi‑Fi portal)
> - UI / display preferences
> Everything else assumes “SDB Automatic”.

## What’s in here
- CAN/TWAI receiver + SDB decoder (MegaSquirt)
- Dual I2C OLED UI (U8g2)
- Wi‑Fi configuration portal (AP)
- NVS persistence for settings
- Optional OTA update path (if enabled in build/profile)


## Media / gallery
A few quick reference images (renders + PCB review photos). Full set in `media/README.md`.

<p float="left">
  <img src="media/renders/enclosure_front.png" width="260" />
  <img src="media/renders/enclosure_top_off.png" width="260" />
  <img src="hardware_review/5_CAN_V3_PCB_TOP_03.02.2026.png" width="260" />
</p>

<p float="left">
  <img src="media/renders/gauge_module_render.png" width="260" />
  <img src="media/renders/oled_128x32_front.png" width="260" />
  <img src="hardware_review/6_CAN_V3_PCB_BOTTOM_03.02.2026.png" width="260" />
</p>


## Build / flash
**Tooling:** PlatformIO.

Typical workflow (example):
- Open this folder in VSCode + PlatformIO
- Build: `pio run -e esp32c3_release`
- Upload: `pio run -e esp32c3_release -t upload`

> `platformio_local.ini` is ignored by git by design (local machine overrides).

## Security note
The Wi‑Fi portal is designed for **local bench / local vehicle setup**, not hostile networks.
It is **not** an authenticated admin interface. Treat it accordingly.

## Repository layout (high level)
- `src/` — firmware source
- `test/` — small unit tests
- `hardware_review/` — PCB photos + schematic (for review; no Gerbers)
- `docs/` — misc internal artifacts (diff reports, etc.)



## Hardware review package (no Gerbers)
See `hardware_review/` for PCB TOP/BOTTOM photos and schematic exports (SVG + PDF). Gerbers and full CAD projects are intentionally not published.


## License
- **Code:** GNU GPLv3 (see `LICENSE`)
- **Documentation / media / hardware review files:** **All rights reserved**, unless explicitly stated otherwise (see `NOTICE.md`)

## Credits
- Hardware schematic/PCB: **Petr**
- Enclosure 3D design: **Jean**
- Software: produced via iterative AI‑assisted development with human prompt orchestration and bench testing.
