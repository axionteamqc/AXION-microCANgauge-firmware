# CAN_GAUGE V51 – Baseline (Do Not Change Unless Requested)

Runtime invariants:
- ECU profile forced to Megasquirt; no AUTO detect in release builds.
- CAN auto-baud flow unchanged: listen-only candidate + normal-mode ACK validation; bitrate list and MS3 dash IDs untouched.
- Display topology: default SmallOnly (1xSmall) on fresh NVS; user can change via Display Mode menu; no forced first-boot picker.
- UI inversion gating: no invert during menu/edit/wizard; per-zone invert for large split topologies; small-only/dual-small use hardware invert; alert marker blink remains left of the big value.
- Per-page units and per-page alert enable masks persist in NVS; defaults metric and alerts enabled.
- Factory Reset wipes the entire `cangauge` NVS namespace and reboots.
- Data source switch: single ONE-LINE toggle in `AppConfig::kDataSource`; when set to CAN and lock present, CAN feeds datastore; mock otherwise.
- OLED boot flow: brand splash + two-page hello (“HELLO” then “DONOVAN”) on primary only; OLED2 stays idle during boot.
- Menus: Short=Next, Long=Prev, Double=OK throughout; Page Setup and Display Mode are submenus; About is a submenu with FW/ECU/Back and exits on double click.
- Alert engine and thresholds: alert invert and marker blinking active on warn/crit; thresholds per page with enable masks respected; editing via 4-click remains intact.

Build:
```sh
pio run -e esp32c3
```
