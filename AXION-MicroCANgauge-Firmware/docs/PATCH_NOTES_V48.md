# Patch Notes — V48 (Summary)

Scope: Documentation checkpoint for recent UI/UX fixes. No behavioral changes are introduced by this file.

Included fixes (already in code):
- Boot hello message split into two pages ("HELLO" / "DONOVAN") to avoid clipping.
- OLED2 refresh during edits made responsive; alert inversion handled per topology.
- Settings/About menus now use consistent Short/Long/Double navigation; About is a proper submenu (FW/ECU/BACK).
- Display Mode and Page Setup moved to submenus; labels clarified (1xSMALL, 2xSMALL, 1xLARGE, S+L; CAN Setup).
- Default display topology is 1xSMALL; first-boot picker uses Short/Long/Double with double-click confirm.

Build:
```sh
pio run -e esp32c3
```
